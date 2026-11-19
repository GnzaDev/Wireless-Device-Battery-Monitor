// Includes the driver to exercise its internal Incott poll with a fake HID backend.
// Link src/drivers/xenta_protocol.cpp for the unrelated Xenta parser.
#include <cassert>
#include <cstdio>
#include "../src/drivers/misc_mice.cpp"

namespace {
bool openOk, sendOk, getOk;
int reads, sends, inputOpens, inputReads;
bool inputOk;
int readDelay, lastTimeout;
std::vector<BYTE> reply;
std::vector<std::vector<BYTE>> pushes;

PollContext Context() {
    openOk = sendOk = getOk = true;
    reads = sends = inputOpens = inputReads = 0;
    inputOk = true;
    readDelay = 0;
    lastTimeout = 901;
    pushes.clear();
    reply = {0x09, 0x89, 42, 1, 0, 0, 0, 0, 0};
    PollContext ctx;
    HidColl c;
    c.vid = 0x093A;
    c.pid = 0x522C;
    c.iface = 3;
    c.caps.UsagePage = 0xFF05;
    c.caps.Usage = 1;
    c.caps.FeatureReportByteLength = 9;
    c.caps.InputReportByteLength = 33;
    ctx.colls.push_back(c);
    return ctx;
}
void Contains(const std::wstring& text, const wchar_t* part) {
    assert(text.find(part) != std::wstring::npos);
}
}

bool HidDev::OpenFeatureOnly(const HidColl& c) {
    c_ = c;
    if (!openOk) SetLastError(ERROR_ACCESS_DENIED);
    return openOk;
}
void HidDev::Close() {}
bool HidDev::Open(const HidColl&) { assert(false); return false; }
bool HidDev::OpenReadOnly(const HidColl& c) {
    // The listener must be opened before the feature query, on the known vendor collection only.
    assert(sends == 0 && c.page() == 0xFF05 && c.usage() == 1);
    ++inputOpens;
    return inputOk;
}
void HidDev::Flush() { assert(false); }
int HidDev::Read(std::vector<BYTE>& out, int timeout) {
    assert(timeout > 0 && timeout <= 900 && timeout <= lastTimeout);
    lastTimeout = timeout;
    if (readDelay) Sleep(readDelay);
    ++inputReads;
    if (pushes.empty()) return -1;
    out = pushes.front();
    pushes.erase(pushes.begin());
    return (int)out.size();
}
bool HidDev::Write(const std::vector<BYTE>&, int) { assert(false); return false; }
std::vector<HidColl> EnumHid() { return {}; }
int Clamp100(int n) { return n < 0 ? 0 : n > 100 ? 100 : n; }
bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    assert(data == std::vector<BYTE>({0x09, 0x89}));
    ++sends;
    if (!sendOk) SetLastError(ERROR_GEN_FAILURE);
    return sendOk;
}
bool HidDev::GetFeature(BYTE id, std::vector<BYTE>& out) {
    assert(id == 9);
    ++reads;
    out = reply;
    if (!getOk) SetLastError(ERROR_GEN_FAILURE);
    return getOk;
}
std::wstring Hex4(USHORT n) {
    wchar_t b[5];
    swprintf(b, 5, L"%04X", n);
    return b;
}

int main() {
    auto ctx = Context();
    std::wstring trace;
    PollIncott(ctx, &trace);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 42);
    assert(sends == 1 && reads == 1);
    Contains(trace, L"09 89 2A 01 00 00 00 00 00");
    Contains(trace, L"Accepted: 42%");

    ctx = Context(); trace.clear(); openOk = false;
    PollIncott(ctx, &trace);
    assert(ctx.out.empty() && sends == 0 && reads == 0);
    Contains(trace, L"OpenFeatureOnly: FAILED, Win32 error=5");

    ctx = Context(); trace.clear(); sendOk = false;
    PollIncott(ctx, &trace);
    assert(ctx.out.empty() && sends == 1 && reads == 0);
    Contains(trace, L"SET_FEATURE 09 89 (zero padded): FAILED");

    ctx = Context(); trace.clear(); getOk = false;
    PollIncott(ctx, &trace);
    assert(ctx.out.empty() && reads == 1);
    Contains(trace, L"FAILED, Win32 error=31");

    ctx = Context(); trace.clear(); reply = {9, 0x89, 0, 0};
    PollIncott(ctx, &trace);
    assert(ctx.out.empty() && reads == 10);
    Contains(trace, L"No valid Incott battery");

    ctx = Context(); trace.clear(); reply[2] = 127;
    PollIncott(ctx, &trace);
    assert(ctx.out.empty());
    Contains(trace, L"Rejected: battery exceeds 100");

    ctx = Context(); trace.clear(); ctx.Claim(0x093A, 0x522C);
    PollIncott(ctx, &trace);
    assert(sends == 0 && reads == 0);
    Contains(trace, L"Skipped: claimed");

    ctx = Context(); trace.clear(); ctx.colls[0].caps.UsagePage = 0xFF00;
    PollIncott(ctx, &trace);
    assert(sends == 0);
    Contains(trace, L"Matching FF05 feature collections: 0");

    ctx = Context();
    PollIncott(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 42);
    assert(inputReads == 0);  // successful feature reply does not wait for a push

    ctx = Context(); trace.clear(); reply = {9, 0x89, 0, 0};
    pushes = {{9, 40, 0x24, 0}};
    PollIncott(ctx, &trace);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 40 && !ctx.out[0].charging);
    assert(inputOpens == 1 && inputReads == 1);
    Contains(trace, L"Using passive status push");

    ctx = Context(); trace.clear(); getOk = false;
    pushes = {{9, (BYTE)(128 | 42), 0x24, 0}};
    PollIncott(ctx, &trace);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 42 && ctx.out[0].charging);

    ctx = Context(); trace.clear(); getOk = false;
    pushes = {{8, 40, 0x24, 0}, {9, 127, 0x24, 0}, {9, 40, 0x27, 0},
              {9, 40, 0x64, 0}, {9, 40, 0x24, 0x80}, {9, 0, 0, 0}, {9}, {9, 41, 0x24, 0}};
    PollIncott(ctx, &trace);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 41 && inputReads == 8);
    Contains(trace, L"Ignored: invalid input status");

    ctx = Context(); trace.clear(); getOk = false; inputOk = false;
    PollIncott(ctx, &trace);
    assert(ctx.out.empty() && inputReads == 0);

    ctx = Context(); trace.clear(); getOk = false;
    ctx.colls[0].pid = 0x622C;
    PollIncott(ctx, &trace);
    assert(ctx.out.empty() && inputOpens == 0);  // wired behavior is unchanged

    ctx = Context(); trace.clear(); getOk = false;
    pushes = {{9, 0x80, 0x24, 0}};
    PollIncott(ctx, &trace);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 0 && ctx.out[0].charging && ctx.out[0].connected);

    ctx = Context(); trace.clear(); getOk = false;
    ctx.colls[0].caps.Usage = 2;
    PollIncott(ctx, &trace);
    assert(ctx.out.empty() && inputOpens == 0 && sends == 0);

    ctx = Context(); trace.clear(); getOk = false;
    pushes.assign(100, {8, 40, 0x24, 0});
    readDelay = 10;
    PollIncott(ctx, &trace);
    assert(ctx.out.empty() && inputReads < 100 && !pushes.empty());
    Contains(trace, L"900 ms collection budget");  // unrelated packets cannot restart the deadline

    BYTE battery = 0;
    assert(!ParseIncottPush({9, 40, 0x24, 0x80}, battery));  // web driver's profile-switch event

    ctx = Context(); trace.clear(); getOk = false;
    pushes = {{9, 0, 0x24, 0}};
    PollIncott(ctx, &trace);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 0 && !ctx.out[0].charging && ctx.out[0].connected);

    // Exact owner's G23 V2 Pro 8K push: low flag bits and later telemetry bytes are not reserved zeros.
    ctx = Context(); trace.clear(); reply = {9, 0x89, 0, 0, 0, 0, 0, 0, 0x5A};
    std::vector<BYTE> captured(33, 0);
    captured[0] = 0x09; captured[1] = 0x2A; captured[2] = 0x20; captured[3] = 0x08;
    captured[5] = 0x1F; captured[6] = 0x1F; captured[7] = 0x01;
    pushes = {captured};
    PollIncott(ctx, &trace);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 42 && !ctx.out[0].charging);
    assert(ctx.out[0].connected && ctx.out[0].via == L"2.4 GHz receiver");
    Contains(trace, L"Using passive status push");
    Contains(trace, L"Accepted: 42%");

    puts("Incott diagnostic and passive fallback: 20 scenarios passed");
}
