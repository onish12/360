// SPDX-License-Identifier: MIT
#include "pnp_resources.h"
#include "../../m051/driver/resource_contract.h"
#include "stage_trace.h"

namespace phaser360 { namespace windows {

struct PnpResourcesContext { PnpResources* owner; };
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PnpResourcesContext,GetPnpResourcesContext)

NTSTATUS PnpResources::Configure(PWDFDEVICE_INIT init,WDF_OBJECT_ATTRIBUTES* attributes) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !init || !attributes) return STATUS_INVALID_PARAMETER;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(attributes,PnpResourcesContext);
    WDF_PNPPOWER_EVENT_CALLBACKS callbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&callbacks);
    callbacks.EvtDevicePrepareHardware=PrepareHardware;
    callbacks.EvtDeviceReleaseHardware=ReleaseHardware;
    callbacks.EvtDeviceSurpriseRemoval=SurpriseRemoval;
    callbacks.EvtDeviceD0Entry=D0Entry;
    callbacks.EvtDeviceD0EntryPostInterruptsEnabled=D0EntryPostInterruptsEnabled;
    callbacks.EvtDeviceD0ExitPreInterruptsDisabled=D0ExitPreInterruptsDisabled;
    callbacks.EvtDeviceD0Exit=D0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(init,&callbacks);
    return STATUS_SUCCESS;
}

NTSTATUS PnpResources::Attach(WDFDEVICE device) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device || device_) return STATUS_INVALID_DEVICE_STATE;
    auto* context=GetPnpResourcesContext(device);
    if(context->owner) return STATUS_INVALID_DEVICE_STATE;
    device_=device; context->owner=this; return STATUS_SUCCESS;
}

bool PnpResources::InstallLifecycle(const PnpLifecycleOps& ops) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || phase_!=PnpPowerPhase::NoResources ||
       hda_ || dsp_ || lifecycle_.context || !ops.context)
        return false;
    lifecycle_=ops;
    return true;
}

NTSTATUS PnpResources::PrepareHardware(WDFDEVICE device,WDFCMRESLIST raw,WDFCMRESLIST translated) {
    StageTrace(L"P10_PREPARE_HARDWARE_ENTER");
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    auto* owner=GetPnpResourcesContext(device)->owner;
    if(!owner || owner->device_!=device) return STATUS_INVALID_DEVICE_STATE;
    const auto status=owner->Prepare(raw,translated);
    StageTraceStatus(NT_SUCCESS(status)
        ? L"P40_PREPARE_HARDWARE_OK" : L"P40_PREPARE_HARDWARE_FAIL",status);
    return status;
}

NTSTATUS PnpResources::ReleaseHardware(WDFDEVICE device,WDFCMRESLIST translated) {
    UNREFERENCED_PARAMETER(translated);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    auto* owner=GetPnpResourcesContext(device)->owner;
    // Release follows failed Prepare too, including an unattached owner.
    if(!owner) return STATUS_SUCCESS;
    if(owner->device_!=device) return STATUS_INVALID_DEVICE_STATE;
    return owner->Release();
}

NTSTATUS PnpResources::D0Entry(WDFDEVICE device,WDF_POWER_DEVICE_STATE previousState) {
    UNREFERENCED_PARAMETER(previousState);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device) return STATUS_INVALID_DEVICE_STATE;
    auto* owner=GetPnpResourcesContext(device)->owner;
    if(!owner || owner->device_!=device || owner->phase_!=PnpPowerPhase::Prepared ||
       !owner->gate_ || !owner->gate_->Allowed() || !owner->hda_ || !owner->dsp_)
        return STATUS_INVALID_DEVICE_STATE;
    if(owner->lifecycle_.d0Entry) {
        PnpResourceView snapshot{};
        if(!owner->CopyPreparedView(&snapshot)) return STATUS_INVALID_DEVICE_STATE;
        const auto status=owner->lifecycle_.d0Entry(owner->lifecycle_.context,device,snapshot);
        if(!NT_SUCCESS(status)) return status; // failed entry gets no D0Exit
    }
    owner->phase_=PnpPowerPhase::D0Entered;
    if(!owner->gate_->Allowed()) {
        // SurpriseRemoval can race the tail of D0Entry. A failed D0Entry gets
        // no framework D0Exit, so compensate here after closing the terminal
        // software fence. No interrupt Enable has occurred yet.
        if(owner->lifecycle_.surpriseRemoval)
            owner->lifecycle_.surpriseRemoval(owner->lifecycle_.context);
        if(owner->lifecycle_.d0Exit &&
           !NT_SUCCESS(owner->lifecycle_.d0Exit(owner->lifecycle_.context))) {
            owner->phase_=PnpPowerPhase::Prepared;
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        owner->phase_=PnpPowerPhase::Prepared;
        return STATUS_INVALID_DEVICE_STATE;
    }
    return STATUS_SUCCESS;
}

NTSTATUS PnpResources::D0EntryPostInterruptsEnabled(
    WDFDEVICE device,WDF_POWER_DEVICE_STATE previousState) {
    UNREFERENCED_PARAMETER(previousState);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device) return STATUS_INVALID_DEVICE_STATE;
    auto* owner=GetPnpResourcesContext(device)->owner;
    if(!owner || owner->device_!=device || owner->phase_!=PnpPowerPhase::D0Entered ||
       !owner->gate_ || !owner->gate_->Allowed())
        return STATUS_INVALID_DEVICE_STATE;
    if(owner->lifecycle_.postInterruptsEnabled) {
        const auto status=owner->lifecycle_.postInterruptsEnabled(owner->lifecycle_.context);
        if(!NT_SUCCESS(status)) return status;
    }
    owner->phase_=PnpPowerPhase::Operational;
    return STATUS_SUCCESS;
}

NTSTATUS PnpResources::D0ExitPreInterruptsDisabled(
    WDFDEVICE device,WDF_POWER_DEVICE_STATE targetState) {
    UNREFERENCED_PARAMETER(targetState);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device) return STATUS_INVALID_DEVICE_STATE;
    auto* owner=GetPnpResourcesContext(device)->owner;
    if(!owner || owner->device_!=device ||
       (owner->phase_!=PnpPowerPhase::Operational &&
        owner->phase_!=PnpPowerPhase::D0Entered))
        return STATUS_INVALID_DEVICE_STATE;
    // Must remain callable after SurpriseRemoval; therefore no Allowed() check.
    const auto status=owner->lifecycle_.preInterruptsDisabled
        ? owner->lifecycle_.preInterruptsDisabled(owner->lifecycle_.context)
        : STATUS_SUCCESS;
    // Even failure must permit the post-framework-disconnect D0Exit fallback.
    owner->phase_=PnpPowerPhase::PreInterruptsDisabled;
    return status;
}

NTSTATUS PnpResources::D0Exit(WDFDEVICE device,WDF_POWER_DEVICE_STATE targetState) {
    UNREFERENCED_PARAMETER(targetState);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device) return STATUS_INVALID_DEVICE_STATE;
    auto* owner=GetPnpResourcesContext(device)->owner;
    if(!owner || owner->device_!=device ||
       owner->phase_!=PnpPowerPhase::PreInterruptsDisabled)
        return STATUS_INVALID_DEVICE_STATE;
    // Framework interrupt-disable/disconnect occurs before this callback.
    if(owner->lifecycle_.d0Exit) {
        const auto status=owner->lifecycle_.d0Exit(owner->lifecycle_.context);
        if(!NT_SUCCESS(status)) return status;
    }
    owner->phase_=PnpPowerPhase::Prepared;
    return STATUS_SUCCESS;
}

void PnpResources::SurpriseRemoval(WDFDEVICE device) {
    // KMDF invokes this at PASSIVE_LEVEL but does not synchronize it with the
    // other PnP/power callbacks. Touch only the immutable owner/gate relation;
    // do not inspect or unmap resource pointers here.
    if(!device) return;
    auto* owner=GetPnpResourcesContext(device)->owner;
    if(owner && owner->gate_) {
        owner->gate_->SurpriseRemove();
        if(owner->lifecycle_.surpriseRemoval)
            owner->lifecycle_.surpriseRemoval(owner->lifecycle_.context);
    }
}

NTSTATUS PnpResources::Prepare(WDFCMRESLIST raw,WDFCMRESLIST translated) noexcept {
    if(!gate_ || gate_->Removed() || hda_ || dsp_ ||
       phase_!=PnpPowerPhase::NoResources) return STATUS_INVALID_DEVICE_STATE;
    if(!raw || !translated) return STATUS_DEVICE_CONFIGURATION_ERROR;

    const ULONG rawCount=WdfCmResourceListGetCount(raw);
    const ULONG translatedCount=WdfCmResourceListGetCount(translated);
    if(rawCount==0 || rawCount!=translatedCount || rawCount>64)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    PHYSICAL_ADDRESS addresses[2]={};
    ULONG lengths[2]={};
    ULONG memoryCount=0;
    PnpResourceView candidate={};

    for(ULONG i=0;i<translatedCount;++i) {
        auto* rawDescriptor=WdfCmResourceListGetDescriptor(raw,i);
        auto* descriptor=WdfCmResourceListGetDescriptor(translated,i);
        if(!rawDescriptor || !descriptor || rawDescriptor->Type!=descriptor->Type)
            return STATUS_DEVICE_CONFIGURATION_ERROR;

        if(descriptor->Type==CmResourceTypeMemoryLarge)
            return STATUS_DEVICE_CONFIGURATION_ERROR;

        if(descriptor->Type==CmResourceTypeInterrupt) {
            if(candidate.interruptCount==PnpResourceView::kMaxInterrupts)
                return STATUS_DEVICE_CONFIGURATION_ERROR;

            const bool rawMessage=(rawDescriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)!=0;
            const bool translatedMessage=(descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)!=0;
            // Raw and translated entries describe the same interrupt. A
            // disagreement about which union member is valid is structurally
            // ambiguous and must never reach WdfInterruptCreate.
            if(rawMessage!=translatedMessage)
                return STATUS_DEVICE_CONFIGURATION_ERROR;

            auto& irq=candidate.interrupts[candidate.interruptCount++];
            irq.raw=rawDescriptor;
            irq.translated=descriptor;
            irq.kind=rawMessage ? PnpInterruptKind::MessageSignaled
                                : PnpInterruptKind::LineBased;
            irq.rawShareDisposition=rawDescriptor->ShareDisposition;
            irq.translatedShareDisposition=descriptor->ShareDisposition;
            irq.rawFlags=rawDescriptor->Flags;
            irq.translatedFlags=descriptor->Flags;

            if(rawMessage) {
                irq.messageCount=rawDescriptor->u.MessageInterrupt.Raw.MessageCount;
                // MessageCount identifies how many MSI messages the raw
                // resource represents. Zero would provide no usable message.
                if(irq.messageCount==0) return STATUS_DEVICE_CONFIGURATION_ERROR;
                irq.rawVector=rawDescriptor->u.MessageInterrupt.Raw.Vector;
                irq.rawAffinity=static_cast<ULONG_PTR>(
                    rawDescriptor->u.MessageInterrupt.Raw.Affinity);
                irq.translatedLevel=descriptor->u.MessageInterrupt.Translated.Level;
                irq.translatedVector=descriptor->u.MessageInterrupt.Translated.Vector;
                irq.translatedAffinity=static_cast<ULONG_PTR>(
                    descriptor->u.MessageInterrupt.Translated.Affinity);
            } else {
                irq.rawLevel=rawDescriptor->u.Interrupt.Level;
                irq.rawVector=rawDescriptor->u.Interrupt.Vector;
                irq.rawAffinity=static_cast<ULONG_PTR>(rawDescriptor->u.Interrupt.Affinity);
                irq.translatedLevel=descriptor->u.Interrupt.Level;
                irq.translatedVector=descriptor->u.Interrupt.Vector;
                irq.translatedAffinity=static_cast<ULONG_PTR>(descriptor->u.Interrupt.Affinity);
            }
            continue;
        }

        if(descriptor->Type!=CmResourceTypeMemory) continue;
        if(memoryCount==2 || descriptor->u.Memory.Start.QuadPart<=0 ||
           (descriptor->Flags & CM_RESOURCE_MEMORY_READ_ONLY)!=0 ||
           (descriptor->Flags & CM_RESOURCE_MEMORY_WRITE_ONLY)!=0)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        addresses[memoryCount]=descriptor->u.Memory.Start;
        lengths[memoryCount]=descriptor->u.Memory.Length;
        ++memoryCount;
    }

    // Memory occurrence order follows the translated PCI resource list:
    // reviewed BAR0 HDA first, reviewed BAR4 DSP second. Other resource types
    // may be interleaved and do not change memory occurrence order.
    if(memoryCount!=2 ||
       !valid_resources(static_cast<resource_address>(addresses[0].QuadPart),lengths[0],
                        static_cast<resource_address>(addresses[1].QuadPart),lengths[1]))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    hda_=static_cast<UCHAR*>(MmMapIoSpaceEx(addresses[0],lengths[0],PAGE_READWRITE|PAGE_NOCACHE));
    if(!hda_) {
        StageTraceStatus(L"P11_MAP_HDA_FAIL",STATUS_INSUFFICIENT_RESOURCES);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    StageTrace(L"P11_MAP_HDA_OK");
    // SurpriseRemoval can race this callback. Do not create another mapping
    // after the terminal gate has already been observed.
    if(gate_->Removed()) {
        (void)Release();
        return STATUS_INVALID_DEVICE_STATE;
    }
    dsp_=static_cast<UCHAR*>(MmMapIoSpaceEx(addresses[1],lengths[1],PAGE_READWRITE|PAGE_NOCACHE));
    if(!dsp_) {
        StageTraceStatus(L"P12_MAP_DSP_FAIL",STATUS_INSUFFICIENT_RESOURCES);
        (void)Release();
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    StageTrace(L"P12_MAP_DSP_OK");

    candidate.hda=hda_;
    candidate.hdaLength=lengths[0];
    candidate.dsp=dsp_;
    candidate.dspLength=lengths[1];

    // Consumers cannot touch either mapping until every resource has been
    // validated/mapped. Removed is terminal and cannot reopen.
    if(!gate_->OpenForPrepare()) {
        (void)Release();
        return STATUS_INVALID_DEVICE_STATE;
    }
    view_=candidate;
    // Linearize successful preparation only while access is still open. If
    // surprise removal won the race after OpenForPrepare, unwind resource-only
    // mappings and report a failed start; Removed remains terminal.
    if(!gate_->Allowed()) {
        view_=PnpResourceView{};
        (void)Release();
        return STATUS_INVALID_DEVICE_STATE;
    }
    phase_=PnpPowerPhase::Prepared;
    if(lifecycle_.prepared) {
        PnpDormantInterruptBinding binding{};
        if(!CopyDormantInterruptBinding(&binding)) {
            phase_=PnpPowerPhase::Prepared;
            (void)Release();
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        const auto status=lifecycle_.prepared(lifecycle_.context,view_,binding);
        if(!NT_SUCCESS(status)) {
            phase_=PnpPowerPhase::Prepared;
            (void)Release();
            return status;
        }
        lifecyclePrepared_=true;
    }

    // H15A final PrepareHardware linearization point. SurpriseRemoval is not
    // synchronized with this callback; if it won while lifecycle_.prepared was
    // running, unwind the fully prepared software/resource bundle now. A remove
    // that wins after this atomic gate observation is ordered after successful
    // preparation and will follow the normal surprise-removal teardown path.
    if(!gate_->Allowed()) {
        const auto releaseStatus=Release();
        return NT_SUCCESS(releaseStatus)
            ? STATUS_INVALID_DEVICE_STATE : STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

NTSTATUS PnpResources::Release() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !gate_) return STATUS_INVALID_DEVICE_STATE;
    // Never unmap a resource bundle while the framework skeleton still says D0.
    // A failed D0Entry leaves phase Prepared and is therefore releasable without
    // a synthetic D0Exit, matching KMDF's documented failure semantics.
    if(phase_!=PnpPowerPhase::NoResources && phase_!=PnpPowerPhase::Prepared)
        return STATUS_INVALID_DEVICE_STATE;

    if(lifecyclePrepared_ && lifecycle_.release) {
        const auto status=lifecycle_.release(lifecycle_.context);
        if(!NT_SUCCESS(status)) return status;
        lifecyclePrepared_=false;
    }
    // Atomic with respect to SurpriseRemove: Open becomes Closed, Closed stays
    // Closed, and terminal Removed is accepted without being rewritten.
    if(!gate_->CloseForRelease()) return STATUS_INVALID_DEVICE_STATE;

    view_=PnpResourceView{};
    if(dsp_) { MmUnmapIoSpace(dsp_,0x100000); dsp_=nullptr; }
    if(hda_) { MmUnmapIoSpace(hda_,0x4000); hda_=nullptr; }
    phase_=PnpPowerPhase::NoResources;
    return STATUS_SUCCESS;
}

bool PnpResources::CopyPreparedView(PnpResourceView* out) const noexcept {
    if(!out) return false;
    *out=PnpResourceView{};
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !Prepared()) return false;
    const auto snapshot=view_;
    if(!gate_->Allowed()) return false;
    *out=snapshot;
    return true;
}

} }

bool phaser360::windows::PnpResources::CopySingleInterruptForCreate(
    PnpInterruptResource* out) const noexcept {
    if(!out) return false;
    *out=PnpInterruptResource{};

    PnpResourceView snapshot{};
    if(!CopyPreparedView(&snapshot) || snapshot.interruptCount!=1) return false;

    const auto& irq=snapshot.interrupts[0];
    if(!irq.raw || !irq.translated ||
       irq.raw->Type!=CmResourceTypeInterrupt ||
       irq.translated->Type!=CmResourceTypeInterrupt)
        return false;

    const bool rawMessage=(irq.rawFlags & CM_RESOURCE_INTERRUPT_MESSAGE)!=0;
    const bool translatedMessage=(irq.translatedFlags & CM_RESOURCE_INTERRUPT_MESSAGE)!=0;
    if(rawMessage!=translatedMessage) return false;

    if(irq.kind==PnpInterruptKind::MessageSignaled) {
        // Live PCI capability evidence reports InterruptMessageMaximum=1.
        // Do not silently accept a multi-message MSI/MSI-X shape.
        if(!rawMessage || irq.messageCount!=1) return false;
    } else {
        if(rawMessage || irq.messageCount!=0) return false;
    }

    *out=irq;
    // The descriptors are borrowed from PrepareHardware. Refuse to return a
    // usable pair if surprise removal closed the resource lifetime meanwhile.
    if(!gate_ || !gate_->Allowed()) {
        *out=PnpInterruptResource{};
        return false;
    }
    return true;
}

bool phaser360::windows::PnpResources::CopyDormantInterruptBinding(
    PnpDormantInterruptBinding* out) const noexcept {
    if(!out) return false;
    *out=PnpDormantInterruptBinding{};

    PnpResourceView snapshot{};
    PnpInterruptResource selected{};
    if(!CopyPreparedView(&snapshot) || snapshot.interruptCount!=1 ||
       !CopySingleInterruptForCreate(&selected))
        return false;

    const auto& only=snapshot.interrupts[0];
    if(!snapshot.dsp || snapshot.dspLength!=0x100000 ||
       selected.raw!=only.raw || selected.translated!=only.translated ||
       selected.kind!=only.kind || selected.messageCount!=only.messageCount ||
       !gate_ || !gate_->Allowed())
        return false;

    PnpDormantInterruptBinding candidate{};
    candidate.gate=gate_;
    candidate.dsp=snapshot.dsp;
    candidate.dspLength=snapshot.dspLength;
    candidate.raw=selected.raw;
    candidate.translated=selected.translated;
    candidate.kind=selected.kind;
    candidate.messageCount=selected.messageCount;

    // SurpriseRemoval is terminal and may race this serialized PnP reader.
    // Never publish a binding after the gate has closed.
    if(!candidate.gate->Allowed()) return false;
    *out=candidate;
    return true;
}
