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
    ColdPower(GlkBoot& boot,IpcInterrupt& irq) noexcept : boot_(&boot),irq_(irq) {}
    ColdPower(const ColdPower&)=delete;
    ColdPower& operator=(const ColdPower&)=delete;
    // New constructed boot owner; same prepared WDF interrupt/resources.
    bool NextD0(GlkBoot& freshBoot,UCHAR* dsp,ULONG dspLength) noexcept;
    NTSTATUS Enter(WDFDEVICE,UCHAR* hda,ULONG hdaLength,UCHAR* dsp,ULONG dspLength,
                   const UCHAR* approvedPayload,SIZE_T bytes,
                   const UCHAR* approvedXman,SIZE_T xmanBytes,USHORT maxAbiMinor) noexcept;
    NTSTATUS AfterInterruptsEnabled() noexcept;
    bool BeforeInterruptsDisabled() noexcept;
    bool RetryEarlyCleanup() noexcept; // only before any framework Enable
    bool CanReleaseMappings() const noexcept { return state_==State::Closed; }
    TransferResult TransferEvidence() const noexcept { return transfer_; }
private:
    enum class State { Fresh, EarlyFailure, Booted, Active, StopFailure, Closed };
    State state_=State::Fresh;
    GlkBoot* boot_;
    IpcInterrupt& irq_;
    TransferResult transfer_;
};
}}
