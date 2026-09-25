// SPDX-License-Identifier: MIT
#include "repeated_device_lifecycle.h"
#include "stage_trace.h"

namespace phaser360 { namespace windows {

NTSTATUS RepeatedDeviceLifecycle::RecordD0Status(NTSTATUS status) noexcept {
    if(telemetry_) telemetry_->SetLastD0Status(status);
    return status;
}

NTSTATUS RepeatedDeviceLifecycle::CreateInterruptShell(WDFDEVICE device) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || shellCreated_ || device_ || !device)
        return STATUS_INVALID_DEVICE_STATE;
    const auto status=irq_.CreateDormant(device);
    if(NT_SUCCESS(status)) {
        device_=device;
        shellCreated_=true;
    }
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
    return prepared_ && dma_.HardwarePrepared() &&
        view.hda && view.hdaLength==0x4000 &&
        view.dsp==binding_.dsp && view.dspLength==binding_.dspLength &&
        view.dspLength==0x100000;
}

NTSTATUS RepeatedDeviceLifecycle::Prepared(
    const PnpResourceView& view,const PnpDormantInterruptBinding& binding) noexcept {
    StageTrace(L"P30_LIFECYCLE_PREPARED_ENTER");
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !shellCreated_ || !device_ || prepared_ ||
       active_ || irqBound_ || sessions_.Active() || dma_.HardwarePrepared() ||
       gate_.Removed() || !gate_.Allowed() ||
       binding.gate!=&gate_ || view.hdaLength!=0x4000 ||
       view.dsp!=binding.dsp || view.dspLength!=binding.dspLength ||
       view.dspLength!=0x100000 || !binding.raw || !binding.translated)
        return STATUS_INVALID_DEVICE_STATE;

    // R3: DMA framework objects are created while the framework is still in
    // PrepareHardware. D0 never creates a DMA enabler or a common buffer.
    const auto dmaStatus=dma_.PrepareHardware(device_,kPinnedPayloadBytes);
    if(!NT_SUCCESS(dmaStatus)) {
        StageTraceStatus(L"P31_LIFECYCLE_DMA_FAIL",dmaStatus);
        return dmaStatus;
    }

    binding_=binding;
    prepared_=true;
    if(telemetry_) telemetry_->SetFlag(TelemetryResourcesPrepared,true);
    StageTrace(L"P31_LIFECYCLE_PREPARED_OK");
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
        if(telemetry_) {
            telemetry_->SetFlag(TelemetryD0Active,false);
            telemetry_->IncrementFailed();
        }
        return true;
    }

    if(boot->Fresh()) {
        if(!irq_.UnbindDormant() || !sessions_.ReleaseUnused())
            return false;
        irqBound_=false; active_=false; ++failedD0_;
        if(telemetry_) {
            telemetry_->SetFlag(TelemetryD0Active,false);
            telemetry_->IncrementFailed();
        }
        return true;
    }
    return false;
}

bool RepeatedDeviceLifecycle::AbandonRemovedBeforeEnable() noexcept {
    if(!active_ || !sessions_.Active() || !gate_.Removed()) return false;
    if(telemetry_) telemetry_->SetFlag(TelemetryRemoved,true);
    (void)irq_.FenceForSurpriseRemoval();
    if(!irq_.ResetDormantRemovedSession() || !sessions_.AbandonRemoved(gate_))
        return false;
    irqBound_=false; active_=false; ++failedD0_;
    if(telemetry_) {
        telemetry_->SetFlag(TelemetryD0Active,false);
        telemetry_->IncrementFailed();
    }
    return true;
}

NTSTATUS RepeatedDeviceLifecycle::D0Entry(
    WDFDEVICE device,const PnpResourceView& view) noexcept {
    StageTrace(L"D20_LIFECYCLE_D0_ENTER");
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !shellCreated_ || !prepared_ ||
       !dma_.HardwarePrepared() || active_ || irqBound_ || sessions_.Active() ||
       gate_.Removed() || !device || device!=device_ ||
       !gate_.Allowed() || !SamePreparedView(view))
        return RecordD0Status(STATUS_INVALID_DEVICE_STATE);

    auto status=sessions_.Begin(device,irq_,gate_,dma_);
    if(!NT_SUCCESS(status)) {
        StageTraceStatus(L"D21_SESSION_BEGIN_FAIL",status);
        return RecordD0Status(status);
    }
    StageTrace(L"D21_SESSION_BEGIN_OK");

    active_=true;
    if(telemetry_) {
        telemetry_->SetSessionGeneration(sessions_.Generation());
        telemetry_->SetFlag(TelemetryD0Active,true);
    }

    auto* boot=sessions_.Boot();
    auto* power=sessions_.Power();
    if(!boot || !power || !irq_.BindDormant(binding_,boot)) {
        StageTraceStatus(L"D22_IRQ_BIND_FAIL",STATUS_DEVICE_CONFIGURATION_ERROR);
        if(sessions_.ReleaseUnused()) {
            active_=false;
            if(telemetry_) telemetry_->SetFlag(TelemetryD0Active,false);
        }
        return RecordD0Status(STATUS_DEVICE_CONFIGURATION_ERROR);
    }
    irqBound_=true;
    StageTrace(L"D22_IRQ_BIND_OK");

    if(!irq_.GrantBootStart()) {
        StageTraceStatus(L"D23_BOOT_GRANT_FAIL",STATUS_DEVICE_CONFIGURATION_ERROR);
        const auto result=CleanupFailedEntry()
            ? STATUS_DEVICE_CONFIGURATION_ERROR : STATUS_INVALID_DEVICE_STATE;
        return RecordD0Status(result);
    }

    StageTrace(L"D23_BOOT_GRANT_OK");
    StageTrace(L"D30_FIRMWARE_ENTER");
    status=firmware_.Enter(
        *power,device,view.hda,view.hdaLength,view.dsp,view.dspLength);
    if(!NT_SUCCESS(status)) {
        StageTraceStatus(L"D40_FIRMWARE_ENTER_FAIL",status);
        if(gate_.Removed()) {
            const auto result=AbandonRemovedBeforeEnable()
                ? status : STATUS_DEVICE_CONFIGURATION_ERROR;
            return RecordD0Status(result);
        }
        const auto result=CleanupFailedEntry()
            ? status : STATUS_DEVICE_CONFIGURATION_ERROR;
        return RecordD0Status(result);
    }

    StageTrace(L"D40_FIRMWARE_COMMAND_READY");
    if(!gate_.Allowed()) {
        const auto result=AbandonRemovedBeforeEnable()
            ? STATUS_INVALID_DEVICE_STATE : STATUS_DEVICE_CONFIGURATION_ERROR;
        return RecordD0Status(result);
    }

    if(!irq_.GrantFrameworkEnableAfterBoot()) {
        StageTraceStatus(L"D50_FRAMEWORK_ENABLE_GRANT_FAIL",STATUS_DEVICE_CONFIGURATION_ERROR);
        if(gate_.Removed()) {
            const auto result=AbandonRemovedBeforeEnable()
                ? STATUS_INVALID_DEVICE_STATE : STATUS_DEVICE_CONFIGURATION_ERROR;
            return RecordD0Status(result);
        }
        if(power->AbortBeforeInterruptsEnabled()) {
            const auto result=CleanupFailedEntry()
                ? STATUS_DEVICE_CONFIGURATION_ERROR : STATUS_INVALID_DEVICE_STATE;
            return RecordD0Status(result);
        }
        return RecordD0Status(STATUS_DEVICE_CONFIGURATION_ERROR);
    }
    StageTrace(L"D50_FRAMEWORK_ENABLE_GRANT_OK");

    if(!gate_.Allowed()) {
        const auto result=AbandonRemovedBeforeEnable()
            ? STATUS_INVALID_DEVICE_STATE : STATUS_DEVICE_CONFIGURATION_ERROR;
        return RecordD0Status(result);
    }
    return RecordD0Status(STATUS_SUCCESS);
}

NTSTATUS RepeatedDeviceLifecycle::PostInterruptsEnabled() noexcept {
    StageTrace(L"POST10_ENTER");
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !active_ || !sessions_.Active())
        return STATUS_INVALID_DEVICE_STATE;
    if(gate_.Removed() || !gate_.Allowed()) {
        if(telemetry_) telemetry_->SetFlag(TelemetryRemoved,true);
        (void)irq_.FenceForSurpriseRemoval();
        return STATUS_INVALID_DEVICE_STATE;
    }
    auto* power=sessions_.Power();
    const auto status=power?power->AfterInterruptsEnabled():STATUS_INVALID_DEVICE_STATE;
    StageTraceStatus(NT_SUCCESS(status)?L"POST20_OK":L"POST20_FAIL",status);
    return status;
}

NTSTATUS RepeatedDeviceLifecycle::PreInterruptsDisabled() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !active_ || !sessions_.Active())
        return STATUS_INVALID_DEVICE_STATE;
    if(gate_.Removed()) {
        if(telemetry_) telemetry_->SetFlag(TelemetryRemoved,true);
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
    if(telemetry_) {
        telemetry_->SetFlag(TelemetryD0Active,false);
        telemetry_->IncrementCompleted();
    }
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
    if(telemetry_) {
        telemetry_->SetFlag(TelemetryD0Active,false);
        telemetry_->SetFlag(TelemetryRemoved,true);
    }
    return true;
}

NTSTATUS RepeatedDeviceLifecycle::D0Exit() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !active_ || !sessions_.Active())
        return STATUS_INVALID_DEVICE_STATE;

    if(gate_.Removed()) {
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

    if(gate_.Removed()) {
        // No hardware/DMA quiescence is asserted after terminal removal.
        // Discard local handles; WDF device-parent teardown owns the objects.
        if(!dma_.AbandonForRemoval())
            return STATUS_DEVICE_CONFIGURATION_ERROR;
    } else {
        const auto dmaStatus=dma_.ReleaseHardware();
        if(!NT_SUCCESS(dmaStatus)) return dmaStatus;
    }

    binding_=PnpDormantInterruptBinding{};
    prepared_=false;
    if(telemetry_) telemetry_->SetFlag(TelemetryResourcesPrepared,false);
    return STATUS_SUCCESS;
}

void RepeatedDeviceLifecycle::SurpriseRemoval() noexcept {
    if(telemetry_) telemetry_->SetFlag(TelemetryRemoved,true);
    (void)irq_.FenceForSurpriseRemoval();
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
