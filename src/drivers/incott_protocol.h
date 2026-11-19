#pragma once
#include "../hid.h"

namespace incott_protocol {
// romkazor/IncottHIDApp device.go and incottwebdriver.netlify.app/js/chunk-v1013.js (irServer).
// Input report: [09][battery|charge][dpi<<4|rate][flags]. Unlike feature replies, no 89 command echo.
inline bool ParseStatus(const std::vector<BYTE>& r, BYTE& battery) {
    if (r.size() < 4 || r[0] != 0x09 || (r[1] & 0x7F) > 100) return false;
    // Only bit 7 denotes a profile-switch event. Low bits are telemetry (G23 V2 Pro reports 08).
    if ((r[2] & 0x0F) > 6 || (r[2] >> 4) > 5 || (r[3] & 0x80) != 0) return false;
    if ((r[1] | r[2] | r[3]) == 0) return false;
    battery = r[1];
    return true;
}
}
