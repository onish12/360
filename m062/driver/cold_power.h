// SPDX-License-Identifier: MIT
#pragma once
#include "ipc_interrupt.h"
namespace phaser360 { namespace windows {
// Cold-start sessions. The PnP owner serializes methods and stops client
// admission before exit. Each boot owner lives until confirmed replacement;
// the interrupt owner outlives this object.
// Caller creates the interrupt in PrepareHardware and owns mappings/image trust.
class ColdPower final {
public:
    ColdPower(GlkBoot& boot,IpcInterrupt& irq,HardwareAccessGate& access) noexcept
        : boot_(&boot),irq_(irq),access_(&access),valid_(boot.BindAccessGate(&access)) {}
    ColdPower(GlkBoot&,IpcInterrupt&)=delete;
    ColdPower(const ColdPower&)=delete;
    ColdPower& operator=(const ColdPower&)=delete;
    // New constructed boot owner; same prepared WDF interrupt/resources.
    bool NextD0(GlkBoot& freshBoot,UCHAR* dsp,ULONG dspLength) noexcept;
    NTSTATUS Enter(WDFDEVICE,UCHAR* hda,ULONG hdaLength,UCHAR* dsp,ULONG dspLength,
                   const UCHAR* approvedPayload,SIZE_T bytes,
                   const UCHAR* approvedXman,SIZE_T xmanBytes,USHORT maxAbiMinor) noexcept;
    NTSTATUS AfterInterruptsEnabled() noexcept;
    bool BeforeInterruptsDisabled() noexcept;
    // Fallback for never-armed startup or an attempted pre-disable Stop. Only
    // in D0Exit after framework disconnect (Disable may be omitted), still D0,
    // hardware present/accessible. This is a caller obligation, not a status flag.
    // Not callable from ReleaseHardware or surprise-removal cleanup.
    bool AfterInterruptsDisconnected() noexcept;
    bool RetryEarlyCleanup() noexcept; // only before any framework Enable
    bool CanReleaseMappings() const noexcept { return state_==State::Closed; }
    TransferResult TransferEvidence() const noexcept { return transfer_; }
private:
    enum class State { Fresh, EarlyFailure, Booted, Active, StopFailure, Closed };
    State state_=State::Fresh;
    GlkBoot* boot_;
    IpcInterrupt& irq_;
    HardwareAccessGate* access_;
    bool valid_;
    TransferResult transfer_;
};
}}
