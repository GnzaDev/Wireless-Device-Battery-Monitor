#include "incott_receiver.h"
#include "incott_protocol.h"
#include <algorithm>

void IncottReceiverMonitor::Sync(const std::vector<HidColl>& allowed) {
    std::vector<std::wstring> paths;
    for (const auto& c : allowed)
        if (c.vid == 0x093A && c.pid == 0x522C && c.page() == 0xFF05 && c.usage() == 1 &&
            c.featLen() >= 9 && c.inLen() >= 4) paths.push_back(c.path);
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    if (paths != paths_) { have_ = published_ = false; paths_ = paths; }
    readers_.erase(std::remove_if(readers_.begin(), readers_.end(), [&](const Reader& r) {
        return !std::binary_search(paths.begin(), paths.end(), r.path);
    }), readers_.end());
    for (const auto& c : allowed) {
        if (!std::binary_search(paths.begin(), paths.end(), c.path)) continue;
        // Recheck the collection as well as its path; callers cannot substitute an unrelated collection.
        if (c.vid != 0x093A || c.pid != 0x522C || c.page() != 0xFF05 || c.usage() != 1 ||
            c.featLen() < 9 || c.inLen() < 4) continue;
        if (std::any_of(readers_.begin(), readers_.end(), [&](const Reader& r) { return r.path == c.path; })) continue;
        auto dev = std::make_unique<HidDev>();
        if (dev->OpenReadOnly(c)) readers_.push_back({c.path, std::move(dev)});
    }
}

void IncottReceiverMonitor::Remember(BYTE battery) {
    if (paths_.empty() || (battery & 0x7F) > 100) return;
    cached_ = {};
    cached_.key = L"incott:522C";
    cached_.name = L"Incott mouse";
    cached_.kind = DeviceKind::Mouse;
    cached_.via = L"2.4 GHz receiver";
    cached_.vid = 0x093A;
    cached_.percent = battery & 0x7F;
    cached_.charging = (battery & 0x80) != 0;
    cached_.connected = true;
    cached_.graceSec = 1800;
    receivedAt_ = clock_();
    have_ = true;
}

bool IncottReceiverMonitor::Latest(DeviceStatus& out) const {
    if (!have_) return false;
    out = cached_;
    // Retain last known status while this receiver remains attached, but never present silence as fresh data.
    if (clock_() - receivedAt_ >= 90000) { out.asleep = true; out.percent = -1; }
    return true;
}

bool IncottReceiverMonitor::ReadPending(DeviceStatus& changed) {
    for (auto& reader : readers_) {
        std::vector<BYTE> r;
        // The handle stays open between calls, so status arriving outside a full poll remains queued.
        // Bound each drain to avoid starving refresh/shutdown under malformed or noisy input.
        for (int i = 0; i < 32; ++i) {
            if (reader.dev->Read(r, 1) < 0) break;
            BYTE battery = 0;
            if (incott_protocol::ParseStatus(r, battery)) Remember(battery);
        }
    }
    DeviceStatus current;
    if (!Latest(current)) return false;
    if (published_ && current.percent == lastPublished_.percent && current.charging == lastPublished_.charging &&
        current.asleep == lastPublished_.asleep) return false;
    changed = lastPublished_ = current;
    published_ = true;
    return true;
}
