// SPDX-License-Identifier: MIT
#pragma once
#include "ipc_interrupt.h"
namespace phaser360 { namespace windows {
// Single cold-start session. The PnP owner serializes all these methods and
// stops client admission before exit. Both referenced owners outlive this object.
// Caller creates the interrupt in PrepareHardware and owns mappings/image trust.
class ColdPower final {
public:
    ColdPower(GlkBoot& boot,IpcInterrupt& irq) noexcept : boot_(boot),irq_(irq) {}
    ColdPower(const ColdPower&)=delete;
    ColdPower& operator=(const ColdPower&)=delete;
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
    GlkBoot& boot_;
    IpcInterrupt& irq_;
    TransferResult transfer_;
};
}}
