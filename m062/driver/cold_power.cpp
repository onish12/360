// SPDX-License-Identifier: MIT
#include "cold_power.h"
namespace phaser360 { namespace windows {
bool ColdPower::NextD0(GlkBoot& fresh,UCHAR* dsp,ULONG length) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !valid_ || !access_ || access_->Removed() ||
       state_!=State::Closed || &fresh==boot_ || !fresh.Fresh()) return false;
    auto* dma=boot_?boot_->DmaOwner():nullptr;
    if(!dma || !dma->HardwarePrepared() ||
       !fresh.BindDma(dma) || !fresh.BindAccessGate(access_)) return false;
    if(!irq_.RebindStopped(&fresh,dsp,length)) return false;
    boot_=&fresh; transfer_={}; state_=State::Fresh; return true;
}
NTSTATUS ColdPower::Enter(WDFDEVICE device,UCHAR* hda,ULONG hdaLength,UCHAR* dsp,ULONG dspLength,
                          const UCHAR* payload,SIZE_T bytes,const UCHAR* xman,SIZE_T xmanBytes,
                          USHORT maxAbiMinor) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !valid_ || !access_ || !access_->Allowed() ||
       state_!=State::Fresh || !irq_.CanStartBeforeEnable())
        return STATUS_INVALID_DEVICE_STATE;
    state_=State::EarlyFailure;
    const auto status=boot_->Prepare(device,hda,hdaLength,dsp,dspLength,payload,bytes,xman,xmanBytes,maxAbiMinor);
    if(!NT_SUCCESS(status)) { (void)RetryEarlyCleanup(); return status; }
    transfer_=boot_->Transfer();
    if(!transfer_.started || !transfer_.firmwareEntered || !transfer_.dmaReleased ||
       !transfer_.ipcReady || !transfer_.commandReady) {
        (void)RetryEarlyCleanup(); return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    state_=State::Booted; return STATUS_SUCCESS;
}
bool ColdPower::RetryEarlyCleanup() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return false;
    if(state_==State::Closed) return true;
    if(!valid_ || !access_ || !access_->Allowed()) return false;
    if(state_!=State::EarlyFailure) return false;
    if(!irq_.CancelBeforeEnable() || !boot_->Shutdown()) return false;
    state_=State::Closed; return true;
}
bool ColdPower::AbortBeforeInterruptsEnabled() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return false;
    if(state_==State::Closed) return true;
    if(!valid_ || !access_ || !access_->Allowed() || state_!=State::Booted)
        return false;
    if(!irq_.CancelBeforeEnable() || !boot_->Shutdown()) return false;
    state_=State::Closed; return true;
}
NTSTATUS ColdPower::AfterInterruptsEnabled() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !valid_ || !access_ || !access_->Allowed() ||
       state_!=State::Booted) return STATUS_INVALID_DEVICE_STATE;
    if(irq_.Arm()) { state_=State::Active; return STATUS_SUCCESS; }
    (void)BeforeInterruptsDisabled(); return STATUS_DEVICE_CONFIGURATION_ERROR;
}
bool ColdPower::AfterInterruptsDisconnected() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return false;
    if(state_==State::Closed) return true;
    if(!valid_ || !access_ || !access_->Allowed()) return false;
    if(state_!=State::Booted && state_!=State::StopFailure) return false;
    const bool masked=irq_.StopAfterDisconnect();
    const bool drained=irq_.DrainStopped();
    state_=State::StopFailure;
    if(!masked || !drained || !boot_->Shutdown()) return false;
    state_=State::Closed; return true;
}
bool ColdPower::BeforeInterruptsDisabled() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return false;
    if(state_==State::Closed) return true;
    if(!valid_ || !access_ || !access_->Allowed()) return false;
    if(state_!=State::Booted && state_!=State::Active && state_!=State::StopFailure) return false;
    state_=State::StopFailure;
    if(!irq_.Stop() || !irq_.DrainStopped() || !boot_->Shutdown()) return false;
    state_=State::Closed; return true;
}
}}
