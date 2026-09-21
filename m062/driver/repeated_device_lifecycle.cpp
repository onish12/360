// SPDX-License-Identifier: MIT
#include "repeated_device_lifecycle.h"

namespace phaser360 { namespace windows {

NTSTATUS RepeatedDeviceLifecycle::CreateInterruptShell(WDFDEVICE device) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || shellCreated_ || !device)
        return STATUS_INVALID_DEVICE_STATE;
    const auto status=irq_.CreateDormant(device);
    if(NT_SUCCESS(status)) shellCreated_=true;
    return status;
}

PnpLifecycleOps RepeatedDeviceLifecycle::Ops() noexcept {
    PnpLifecycleOps ops{};
    ops.context=this;
    ops.prepared=PreparedThunk;
    ops.d0Entry=D0EntryThunk;
    ops.postInterruptsEnabled=PostThunk;
    ops.preInterruptsDisabled=PreThunk;
    ops.d0Exit=ExitThunk;
    ops.release=ReleaseThunk;
    ops.surpriseRemoval=SurpriseThunk;
    return ops;
}

bool RepeatedDeviceLifecycle::SamePreparedView(const PnpResourceView& view) const noexcept {
    return prepared_ && view.hda && view.hdaLength==0x4000 &&
        view.dsp==binding_.dsp && view.dspLength==binding_.dspLength &&
        view.dspLength==0x100000;
}

NTSTATUS RepeatedDeviceLifecycle::Prepared(
    const PnpResourceView& view,const PnpDormantInterruptBinding& binding) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !shellCreated_ || prepared_ ||
       active_ || irqBound_ || sessions_.Active() || removed_ || !gate_.Allowed() ||
       binding.gate!=&gate_ || view.hdaLength!=0x4000 ||
       view.dsp!=binding.dsp || view.dspLength!=binding.dspLength ||
       view.dspLength!=0x100000 || !binding.raw || !binding.translated)
        return STATUS_INVALID_DEVICE_STATE;

    binding_=binding;
    prepared_=true;
    return STATUS_SUCCESS;
}

bool RepeatedDeviceLifecycle::CleanupFailedEntry() noexcept {
    if(!active_ || !sessions_.Active()) return false;

    auto* boot=sessions_.Boot();
    auto* power=sessions_.Power();
    if(!boot || !power) return false;

    if(power->CanReleaseMappings()) {
        if(!irq_.ResetDormantClosedSession() || !sessions_.ReleaseClean())
            return false;
        irqBound_=false; active_=false; ++failedD0_;
        return true;
    }

    if(boot->Fresh()) {
        if(!irq_.UnbindDormant() || !sessions_.ReleaseUnused())
            return false;
        irqBound_=false; active_=false; ++failedD0_;
        return true;
    }
    return false;
}

bool RepeatedDeviceLifecycle::AbandonRemovedBeforeEnable() noexcept {
    if(!active_ || !sessions_.Active()) return false;
    removed_=true;
    (void)irq_.FenceForSurpriseRemoval();
    if(!irq_.ResetDormantRemovedSession() || !sessions_.AbandonRemoved(gate_))
        return false;
    irqBound_=false; active_=false; ++failedD0_;
    return true;
}

NTSTATUS RepeatedDeviceLifecycle::D0Entry(
    WDFDEVICE device,const PnpResourceView& view) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !shellCreated_ || !prepared_ ||
       active_ || irqBound_ || sessions_.Active() || removed_ || !device ||
       !gate_.Allowed() || !SamePreparedView(view))
        return STATUS_INVALID_DEVICE_STATE;

    auto status=sessions_.Begin(device,irq_,gate_);
    if(!NT_SUCCESS(status)) return status;

    active_=true;
    auto* boot=sessions_.Boot();
    auto* power=sessions_.Power();
    if(!boot || !power || !irq_.BindDormant(binding_,boot)) {
        if(sessions_.ReleaseUnused()) active_=false;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    irqBound_=true;

    if(!irq_.GrantBootStart()) {
        return CleanupFailedEntry()?STATUS_DEVICE_CONFIGURATION_ERROR
                                   :STATUS_INVALID_DEVICE_STATE;
    }

    status=firmware_.Enter(
        *power,device,view.hda,view.hdaLength,view.dsp,view.dspLength);
    if(!NT_SUCCESS(status)) {
        if(gate_.Removed()) {
            return AbandonRemovedBeforeEnable()?status:STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        return CleanupFailedEntry()?status:STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if(!gate_.Allowed()) {
        return AbandonRemovedBeforeEnable()
            ? STATUS_INVALID_DEVICE_STATE : STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if(!irq_.GrantFrameworkEnableAfterBoot()) {
        if(gate_.Removed())
            return AbandonRemovedBeforeEnable()
                ? STATUS_INVALID_DEVICE_STATE : STATUS_DEVICE_CONFIGURATION_ERROR;
        if(power->AbortBeforeInterruptsEnabled())
            return CleanupFailedEntry()
                ? STATUS_DEVICE_CONFIGURATION_ERROR : STATUS_INVALID_DEVICE_STATE;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if(!gate_.Allowed()) {
        return AbandonRemovedBeforeEnable()
            ? STATUS_INVALID_DEVICE_STATE : STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

NTSTATUS RepeatedDeviceLifecycle::PostInterruptsEnabled() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !active_ || !sessions_.Active())
        return STATUS_INVALID_DEVICE_STATE;
    if(removed_ || !gate_.Allowed()) {
        removed_=true; (void)irq_.FenceForSurpriseRemoval();
        return STATUS_INVALID_DEVICE_STATE;
    }
    auto* power=sessions_.Power();
    return power?power->AfterInterruptsEnabled():STATUS_INVALID_DEVICE_STATE;
}

NTSTATUS RepeatedDeviceLifecycle::PreInterruptsDisabled() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !active_ || !sessions_.Active())
        return STATUS_INVALID_DEVICE_STATE;
    if(removed_ || gate_.Removed()) {
        removed_=true;
        return irq_.FenceForSurpriseRemoval()?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;
    }
    auto* power=sessions_.Power();
    return power && power->BeforeInterruptsDisabled()
        ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

bool RepeatedDeviceLifecycle::FinishCleanSession() noexcept {
    auto* power=sessions_.Power();
    if(!power || !power->CanReleaseMappings()) return false;
    if(!irq_.ResetDormantClosedSession() || !sessions_.ReleaseClean())
        return false;
    irqBound_=false; active_=false; ++completedD0_;
    return true;
}

bool RepeatedDeviceLifecycle::FinishRemovedSession() noexcept {
    if(!gate_.Removed() || !sessions_.Active()) return false;
    (void)irq_.FenceForSurpriseRemoval();
    if(!irq_.ResetDormantRemovedSession()) {
        if(!irq_.DrainStopped() || !irq_.ResetDormantRemovedSession())
            return false;
    }
    if(!sessions_.AbandonRemoved(gate_)) return false;
    irqBound_=false; active_=false;
    return true;
}

NTSTATUS RepeatedDeviceLifecycle::D0Exit() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !active_ || !sessions_.Active())
        return STATUS_INVALID_DEVICE_STATE;

    if(removed_ || gate_.Removed()) {
        removed_=true;
        return FinishRemovedSession()?STATUS_SUCCESS:STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    auto* power=sessions_.Power();
    if(!power) return STATUS_INVALID_DEVICE_STATE;
    if(!power->CanReleaseMappings() && !power->AfterInterruptsDisconnected())
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    return FinishCleanSession()?STATUS_SUCCESS:STATUS_DEVICE_CONFIGURATION_ERROR;
}

NTSTATUS RepeatedDeviceLifecycle::Release() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || active_ || irqBound_ ||
       sessions_.Active())
        return STATUS_INVALID_DEVICE_STATE;
    binding_=PnpDormantInterruptBinding{};
    prepared_=false;
    return STATUS_SUCCESS;
}

void RepeatedDeviceLifecycle::SurpriseRemoval() noexcept {
    removed_=true;
    if(irqBound_) (void)irq_.FenceForSurpriseRemoval();
}

NTSTATUS RepeatedDeviceLifecycle::PreparedThunk(
    void* context,const PnpResourceView& view,
    const PnpDormantInterruptBinding& binding) noexcept {
    return context
        ? static_cast<RepeatedDeviceLifecycle*>(context)->Prepared(view,binding)
        : STATUS_INVALID_DEVICE_STATE;
}
NTSTATUS RepeatedDeviceLifecycle::D0EntryThunk(
    void* context,WDFDEVICE device,const PnpResourceView& view) noexcept {
    return context
        ? static_cast<RepeatedDeviceLifecycle*>(context)->D0Entry(device,view)
        : STATUS_INVALID_DEVICE_STATE;
}
NTSTATUS RepeatedDeviceLifecycle::PostThunk(void* context) noexcept {
    return context
        ? static_cast<RepeatedDeviceLifecycle*>(context)->PostInterruptsEnabled()
        : STATUS_INVALID_DEVICE_STATE;
}
NTSTATUS RepeatedDeviceLifecycle::PreThunk(void* context) noexcept {
    return context
        ? static_cast<RepeatedDeviceLifecycle*>(context)->PreInterruptsDisabled()
        : STATUS_INVALID_DEVICE_STATE;
}
NTSTATUS RepeatedDeviceLifecycle::ExitThunk(void* context) noexcept {
    return context
        ? static_cast<RepeatedDeviceLifecycle*>(context)->D0Exit()
        : STATUS_INVALID_DEVICE_STATE;
}
NTSTATUS RepeatedDeviceLifecycle::ReleaseThunk(void* context) noexcept {
    return context
        ? static_cast<RepeatedDeviceLifecycle*>(context)->Release()
        : STATUS_INVALID_DEVICE_STATE;
}
void RepeatedDeviceLifecycle::SurpriseThunk(void* context) noexcept {
    if(context) static_cast<RepeatedDeviceLifecycle*>(context)->SurpriseRemoval();
}

} }
