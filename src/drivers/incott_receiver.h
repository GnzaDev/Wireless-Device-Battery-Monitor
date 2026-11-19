#pragma once
#include <memory>
#include "../hid.h"

// Owned and used exclusively by the Windows polling worker. No additional thread or settings writes.
// Sync receives ONLY collections already filtered by IgnoreDevices and Wraith model detection.
class IncottReceiverMonitor {
public:
    using Clock = unsigned long long (*)();
    explicit IncottReceiverMonitor(Clock clock = PlatNowMs) : clock_(clock) {}
    void Sync(const std::vector<HidColl>& allowed);
    void Reopen() { readers_.clear(); }  // device-change/resume/manual refresh; cache survives same-path reopen
    void Remember(BYTE battery);
    bool Latest(DeviceStatus& out) const;
    bool ReadPending(DeviceStatus& changed);

private:
    struct Reader { std::wstring path; std::unique_ptr<HidDev> dev; };
    std::vector<Reader> readers_;
    std::vector<std::wstring> paths_;
    Clock clock_;
    unsigned long long receivedAt_ = 0;
    bool have_ = false, published_ = false;
    DeviceStatus cached_, lastPublished_;
};
