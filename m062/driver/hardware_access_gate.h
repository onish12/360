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
        return old==kOpen || old==kClosed;
    }
    void SurpriseRemove() noexcept {
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
