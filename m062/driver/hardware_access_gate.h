// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>

namespace phaser360 { namespace windows {

class HardwareAccessGate final {
public:
    HardwareAccessGate() noexcept = default;
    HardwareAccessGate(const HardwareAccessGate&)=delete;
    HardwareAccessGate& operator=(const HardwareAccessGate&)=delete;

    bool OpenForPrepare() noexcept {
        return InterlockedCompareExchange(&state_,kOpen,kClosed)==kClosed;
    }
    bool CloseForRelease() noexcept {
        const LONG old=InterlockedCompareExchange(&state_,kClosed,kOpen);
        // Removed is already terminal/closed to consumers. Do not rewrite it.
        // Treat it as release-safe so SurpriseRemoval racing with ReleaseHardware
        // cannot strand resource cleanup merely because the state changed first.
        return old==kOpen || old==kClosed || old==kRemoved;
    }
    void SurpriseRemove() noexcept {
        CloseTerminally();
    }
    // Also used at final framework resource release after failed cleanup.
    // This closes software access; it is not a hardware-stop assertion.
    void CloseTerminally() noexcept {
        (void)InterlockedExchange(&state_,kRemoved);
    }
    bool Allowed() const noexcept {
        return InterlockedCompareExchange(&state_,kOpen,kOpen)==kOpen;
    }
    bool Removed() const noexcept {
        return InterlockedCompareExchange(&state_,kRemoved,kRemoved)==kRemoved;
    }

private:
    static constexpr LONG kClosed=0;
    static constexpr LONG kOpen=1;
    static constexpr LONG kRemoved=2;
    mutable volatile LONG state_=kClosed;
};

} }
