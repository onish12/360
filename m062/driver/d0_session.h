// SPDX-License-Identifier: MIT
#pragma once
#include "cold_power.h"

namespace phaser360 { namespace windows {

// One WDF-owned, nonpaged boot/power object pair per D0 attempt.
// The memory object is parented to the WDFDEVICE but is explicitly deleted
// after confirmed clean shutdown. Terminal surprise removal has a separate
// abandon path that performs no hardware access and makes no quiescence claim.
class D0SessionOwner final {
public:
    D0SessionOwner() noexcept = default;
    D0SessionOwner(const D0SessionOwner&)=delete;
    D0SessionOwner& operator=(const D0SessionOwner&)=delete;

    NTSTATUS Begin(WDFDEVICE,IpcInterrupt&,HardwareAccessGate&) noexcept;
    GlkBoot* Boot() noexcept;
    ColdPower* Power() noexcept;
    bool Active() const noexcept { return session_!=nullptr; }
    ULONG Generation() const noexcept { return generation_; }

    // Fresh boot was never attempted; IRQ binding must already be removed.
    bool ReleaseUnused() noexcept;
    // Normal path: ColdPower proved shutdown and command transport is closed.
    bool ReleaseClean() noexcept;
    // Terminal Removed only. No MMIO, DMA stop or hardware-quiescence claim.
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
