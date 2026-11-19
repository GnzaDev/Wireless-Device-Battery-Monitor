#include <cassert>
#include <cstdio>
#include <deque>
#include <map>
#include "../src/drivers/incott_receiver.h"

namespace {
unsigned long long now = 1000;
int opens = 0, closes = 0, reads = 0;
bool failOpen = false, flood = false;
std::map<std::wstring, std::deque<std::vector<BYTE>>> packets;
unsigned long long Clock() { return now; }
HidColl Receiver(const wchar_t* path = L"receiver") {
    HidColl c;
    c.path = path; c.vid = 0x093A; c.pid = 0x522C;
    c.caps.UsagePage = 0xFF05; c.caps.Usage = 1;
    c.caps.InputReportByteLength = 33; c.caps.FeatureReportByteLength = 9;
    return c;
}
void Push(BYTE battery = 42) { packets[L"receiver"].push_back({9, battery, 0x20, 8, 0, 0x1F, 0x1F, 1}); }
}

bool HidDev::OpenReadOnly(const HidColl& c) {
    assert(c.vid == 0x093A && c.pid == 0x522C && c.page() == 0xFF05 && c.usage() == 1);
    ++opens;
    if (failOpen) return false;
    c_ = c;
    h_ = reinterpret_cast<HANDLE>(1);
    return true;
}
void HidDev::Close() {
    if (h_ != INVALID_HANDLE_VALUE) { ++closes; h_ = INVALID_HANDLE_VALUE; }
}
int HidDev::Read(std::vector<BYTE>& out, int timeout) {
    assert(timeout == 1);
    ++reads;
    if (flood) { out = {8, 0, 0, 0}; return 4; }
    auto& q = packets[c_.path];
    if (q.empty()) return -1;
    out = q.front(); q.pop_front();
    return (int)out.size();
}

int main() {
    DeviceStatus s;
    {
        IncottReceiverMonitor monitor(Clock);
        auto c = Receiver();
        monitor.Sync({c});
        assert(opens == 1 && !monitor.Latest(s));
        assert(!monitor.ReadPending(s));
        // Critical regression: a push arrives well AFTER the old 900 ms polling window.
        now += 5000;
        Push();
        assert(monitor.ReadPending(s) && s.percent == 42 && s.connected && !s.charging);
        monitor.Sync({c});
        assert(opens == 1 && closes == 0);  // full polls keep the same handle open
        now += 60000;
        assert(monitor.Latest(s) && s.percent == 42 && !s.asleep);
        assert(!monitor.ReadPending(s));  // no duplicate UI event or fabricated fresh reading
        now += 30000;
        assert(monitor.ReadPending(s) && s.asleep && s.percent == -1);
        assert(!monitor.ReadPending(s));
        Push();
        assert(monitor.ReadPending(s) && s.percent == 42 && !s.asleep);
        Push(128 | 41);
        assert(monitor.ReadPending(s) && s.percent == 41 && s.charging);
        packets[L"receiver"].push_back({9, 99, 0x20, 0x80});  // profile event, not a battery update
        packets[L"receiver"].push_back({9, 127, 0x20, 8});
        assert(!monitor.ReadPending(s));
        assert(monitor.Latest(s) && s.percent == 41 && s.charging);
        int before = reads;
        flood = true;
        assert(!monitor.ReadPending(s) && reads - before == 32);
        flood = false;
        monitor.Reopen();  // hotplug/resume can reuse the identical HID path
        assert(closes == 1);
        monitor.Sync({c});
        assert(opens == 2 && monitor.Latest(s));
        monitor.Sync({});  // unplug, IgnoreDevices or Wraith claim removes permission immediately
        assert(closes == 2 && !monitor.Latest(s));
        monitor.Sync({Receiver(L"replacement")});
        assert(opens == 3 && !monitor.Latest(s));  // never reuse another receiver's cached battery
    }
    assert(closes == 3);  // worker shutdown releases its last read handle
    {
        IncottReceiverMonitor monitor(Clock);
        auto c = Receiver();
        c.pid = 0x622C; monitor.Sync({c});
        c = Receiver(); c.caps.UsagePage = 1; monitor.Sync({c});
        c = Receiver(); c.caps.Usage = 2; monitor.Sync({c});
        assert(opens == 3);  // no cable/keyboard/unrecognized collection opened
        failOpen = true; monitor.Sync({Receiver()});
        assert(opens == 4 && !monitor.Latest(s));
        failOpen = false; monitor.Sync({Receiver()});
        assert(opens == 5);  // retry failed open on the next sync
        monitor.Remember(128 | 42);  // a successful feature query can seed the same cache
        assert(monitor.ReadPending(s) && s.percent == 42 && s.charging);
        monitor.Remember(127);
        assert(!monitor.ReadPending(s));
    }
    assert(closes == 4);
    puts("Incott persistent receiver: delayed push, cache, stale/recovery, bounds, reconnect, filtering and shutdown passed");
}
