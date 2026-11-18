// Includes the driver to exercise its internal Incott poll with a fake HID backend.
// Link src/drivers/xenta_protocol.cpp for the unrelated Xenta parser.
#include <cassert>
#include <cstdio>
#include "../src/drivers/misc_mice.cpp"

namespace {
bool openOk, sendOk, getOk;
int reads, sends;
std::vector<BYTE> reply;

PollContext Context() {
    openOk = sendOk = getOk = true;
    reads = sends = 0;
    reply = {0x09, 0x89, 42, 1, 0, 0, 0, 0, 0};
    PollContext ctx;
    HidColl c;
    c.vid = 0x093A;
    c.pid = 0x522C;
    c.iface = 3;
    c.caps.UsagePage = 0xFF05;
    c.caps.Usage = 1;
    c.caps.FeatureReportByteLength = 9;
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
bool HidDev::OpenReadOnly(const HidColl&) { assert(false); return false; }
void HidDev::Flush() { assert(false); }
int HidDev::Read(std::vector<BYTE>&, int) { assert(false); return -1; }
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
    Contains(trace, L"Rejected: no non-stale");

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
    puts("Incott diagnostic: 9 scenarios passed");
}
