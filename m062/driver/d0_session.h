// SPDX-License-Identifier: MIT
#pragma once
#include "cold_power.h"

namespace phaser360 { namespace windows {

class D0SessionOwner final {
public:
    D0SessionOwner() noexcept = default;
    D0SessionOwner(const D0SessionOwner&)=delete;
    D0SessionOwner& operator=(const D0SessionOwner&)=delete;

    NTSTATUS Begin(WDFDEVICE,IpcInterrupt&,HardwareAccessGate&,BootDma&) noexcept;
    GlkBoot* Boot() noexcept;
    ColdPower* Power() noexcept;
    bool Active() const noexcept { return session_!=nullptr; }
    ULONG Generation() const noexcept { return generation_; }

    bool ReleaseUnused() noexcept;
    bool ReleaseClean() noexcept;
    bool AbandonRemoved(HardwareAccessGate&) noexcept;

private:
    struct Session final {
        GlkBoot boot;
        ColdPower power;
        Session(IpcInterrupt& irq,HardwareAccessGate& gate) noexcept
            : boot(),power(boot,irq,gate) {}
    };

    WDFMEMORY memory_=nullptr;
    Session* session_=nullptr;
    ULONG generation_=0;

    bool Destroy() noexcept;
};

} }
