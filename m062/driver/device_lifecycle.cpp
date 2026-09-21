// SPDX-License-Identifier: MIT
#include "device_lifecycle.h"

namespace phaser360 { namespace windows {

NTSTATUS DeviceLifecycle::CreateInterruptShell(WDFDEVICE device) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || shellCreated_ || !device)
        return STATUS_INVALID_DEVICE_STATE;
    const auto status=irq_.CreateDormant(device);
    if(NT_SUCCESS(status)) shellCreated_=true;
    return status;
}

PnpLifecycleOps DeviceLifecycle::Ops() noexcept {
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

NTSTATUS DeviceLifecycle::Prepared(
    const PnpResourceView& view,const PnpDormantInterruptBinding& binding) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !shellCreated_ || bound_ ||
       d0Attempted_ || d0Consumed_ || removed_ || !gate_.Allowed() ||
       view.dsp!=binding.dsp || view.dspLength!=binding.dspLength ||
       view.dspLength!=0x100000 || view.hdaLength!=0x4000)
        return STATUS_INVALID_DEVICE_STATE;
    if(!irq_.BindDormant(binding,&boot_))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    bound_=true;
    return STATUS_SUCCESS;
}

bool DeviceLifecycle::CleanupFailedEntryWithoutFramework() noexcept {
    // If ColdPower reached its confirmed Closed state, CancelBeforeEnable has
    // already detached the DSP pointer and ResetDormantClosedSession is the
    // only valid reset. If firmware admission failed before ColdPower entered,
    // the still-dormant binding can simply be unbound.
    if(power_.CanReleaseMappings()) {
        if(!irq_.ResetDormantClosedSession()) return false;
        bound_=false; d0Consumed_=true;
        return true;
    }
    if(irq_.UnbindDormant()) {
        bound_=false; d0Consumed_=true;
        return true;
    }
    return false;
}

bool DeviceLifecycle::AbandonRemovedBeforeEnable() noexcept {
    removed_=true;
    (void)irq_.FenceForSurpriseRemoval();
    // Framework has not called Enable, so no ISR/DPC/work item can exist.
    if(!irq_.ResetDormantRemovedSession()) return false;
    bound_=false; d0Consumed_=true;
    return true;
}

NTSTATUS DeviceLifecycle::D0Entry(
    WDFDEVICE device,const PnpResourceView& view) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !shellCreated_ || !bound_ ||
       d0Attempted_ || d0Consumed_ || removed_ || !device || !gate_.Allowed() ||
       view.hdaLength!=0x4000 || view.dspLength!=0x100000 ||
       !view.hda || !view.dsp)
        return STATUS_INVALID_DEVICE_STATE;

    d0Attempted_=true;
    if(!irq_.GrantBootStart()) {
        (void)CleanupFailedEntryWithoutFramework();
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    const auto status=firmware_.Enter(
        power_,device,view.hda,view.hdaLength,view.dsp,view.dspLength);
    if(!NT_SUCCESS(status)) {
        if(gate_.Removed()) (void)AbandonRemovedBeforeEnable();
        else (void)CleanupFailedEntryWithoutFramework();
        return status;
    }
    if(!gate_.Allowed()) {
        return AbandonRemovedBeforeEnable()
            ? STATUS_INVALID_DEVICE_STATE : STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    // D0Entry is the only place that can open this second permission. The
    // framework has not invoked EvtInterruptEnable yet.
    if(!irq_.GrantFrameworkEnableAfterBoot()) {
        if(gate_.Removed()) (void)AbandonRemovedBeforeEnable();
        else if(power_.AbortBeforeInterruptsEnabled())
            (void)CleanupFailedEntryWithoutFramework();
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if(!gate_.Allowed()) {
        return AbandonRemovedBeforeEnable()
            ? STATUS_INVALID_DEVICE_STATE : STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

NTSTATUS DeviceLifecycle::PostInterruptsEnabled() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !d0Attempted_ || d0Consumed_)
        return STATUS_INVALID_DEVICE_STATE;
    if(removed_ || !gate_.Allowed()) {
        removed_=true;
        (void)irq_.FenceForSurpriseRemoval();
        return STATUS_INVALID_DEVICE_STATE;
    }
    return power_.AfterInterruptsEnabled();
}

NTSTATUS DeviceLifecycle::PreInterruptsDisabled() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !d0Attempted_ || d0Consumed_)
        return STATUS_INVALID_DEVICE_STATE;
    if(removed_ || gate_.Removed()) {
        removed_=true;
        return irq_.FenceForSurpriseRemoval()?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;
    }
    return power_.BeforeInterruptsDisabled()
        ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

NTSTATUS DeviceLifecycle::D0Exit() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !d0Attempted_ || d0Consumed_)
        return STATUS_INVALID_DEVICE_STATE;

    if(removed_ || gate_.Removed()) {
        removed_=true;
        (void)irq_.FenceForSurpriseRemoval();
        // If framework Enable never happened (including a SurpriseRemoval race
        // at the tail of D0Entry), there can be no ISR/deferred work to drain.
        // Otherwise framework has now disconnected/disabled and we drain first.
        if(!irq_.ResetDormantRemovedSession()) {
            if(!irq_.DrainStopped() || !irq_.ResetDormantRemovedSession())
                return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        bound_=false; d0Consumed_=true;
        return STATUS_SUCCESS;
    }

    // Normal pre-disable may have failed. After framework disconnect this is
    // the reviewed M0.6.14 fallback, still while D0 hardware is accessible.
    if(!power_.CanReleaseMappings() && !power_.AfterInterruptsDisconnected())
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if(!power_.CanReleaseMappings() || !irq_.ResetDormantClosedSession())
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    bound_=false; d0Consumed_=true;
    return STATUS_SUCCESS;
}

NTSTATUS DeviceLifecycle::Release() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if(bound_) {
        // Resource stop before any D0 entry: retire only the dormant binding.
        if(d0Attempted_ || !irq_.UnbindDormant())
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        bound_=false;
    }
    return STATUS_SUCCESS;
}

void DeviceLifecycle::SurpriseRemoval() noexcept {
    removed_=true;
    if(bound_) (void)irq_.FenceForSurpriseRemoval();
}

NTSTATUS DeviceLifecycle::PreparedThunk(
    void* context,const PnpResourceView& view,
    const PnpDormantInterruptBinding& binding) noexcept {
    return context
        ? static_cast<DeviceLifecycle*>(context)->Prepared(view,binding)
        : STATUS_INVALID_DEVICE_STATE;
}
NTSTATUS DeviceLifecycle::D0EntryThunk(
    void* context,WDFDEVICE device,const PnpResourceView& view) noexcept {
    return context
        ? static_cast<DeviceLifecycle*>(context)->D0Entry(device,view)
        : STATUS_INVALID_DEVICE_STATE;
}
NTSTATUS DeviceLifecycle::PostThunk(void* context) noexcept {
    return context
        ? static_cast<DeviceLifecycle*>(context)->PostInterruptsEnabled()
        : STATUS_INVALID_DEVICE_STATE;
}
NTSTATUS DeviceLifecycle::PreThunk(void* context) noexcept {
    return context
        ? static_cast<DeviceLifecycle*>(context)->PreInterruptsDisabled()
        : STATUS_INVALID_DEVICE_STATE;
}
NTSTATUS DeviceLifecycle::ExitThunk(void* context) noexcept {
    return context
        ? static_cast<DeviceLifecycle*>(context)->D0Exit()
        : STATUS_INVALID_DEVICE_STATE;
}
NTSTATUS DeviceLifecycle::ReleaseThunk(void* context) noexcept {
    return context
        ? static_cast<DeviceLifecycle*>(context)->Release()
        : STATUS_INVALID_DEVICE_STATE;
}
void DeviceLifecycle::SurpriseThunk(void* context) noexcept {
    if(context) static_cast<DeviceLifecycle*>(context)->SurpriseRemoval();
}

} }
