// SPDX-License-Identifier: MIT
#include "pnp_resources.h"
#include "../../m051/driver/resource_contract.h"

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

NTSTATUS PnpResources::PrepareHardware(WDFDEVICE device,WDFCMRESLIST raw,WDFCMRESLIST translated) {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    auto* owner=GetPnpResourcesContext(device)->owner;
    if(!owner || owner->device_!=device) return STATUS_INVALID_DEVICE_STATE;
    return owner->Prepare(raw,translated);
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
    // Skeleton only: no MMIO, firmware, DMA or IRQ action.
    owner->phase_=PnpPowerPhase::D0Entered;
    // If surprise removal won after the first gate check, report failed entry.
    // KMDF will not call D0Exit for a failed D0Entry.
    if(!owner->gate_->Allowed()) {
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
    // Future ColdPower post-enable arming belongs here. This milestone only
    // records the framework ordering and performs no hardware operation.
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
    // Future guarded shutdown will be composed here before interrupt disable.
    owner->phase_=PnpPowerPhase::PreInterruptsDisabled;
    return STATUS_SUCCESS;
}

NTSTATUS PnpResources::D0Exit(WDFDEVICE device,WDF_POWER_DEVICE_STATE targetState) {
    UNREFERENCED_PARAMETER(targetState);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device) return STATUS_INVALID_DEVICE_STATE;
    auto* owner=GetPnpResourcesContext(device)->owner;
    if(!owner || owner->device_!=device ||
       owner->phase_!=PnpPowerPhase::PreInterruptsDisabled)
        return STATUS_INVALID_DEVICE_STATE;
    // Framework interrupt-disable occurs before this callback. No hardware work
    // is performed in this skeleton.
    owner->phase_=PnpPowerPhase::Prepared;
    return STATUS_SUCCESS;
}

void PnpResources::SurpriseRemoval(WDFDEVICE device) {
    // KMDF invokes this at PASSIVE_LEVEL but does not synchronize it with the
    // other PnP/power callbacks. Touch only the immutable owner/gate relation;
    // do not inspect or unmap resource pointers here.
    if(!device) return;
    auto* owner=GetPnpResourcesContext(device)->owner;
    if(owner && owner->gate_) owner->gate_->SurpriseRemove();
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
    if(!hda_) return STATUS_INSUFFICIENT_RESOURCES;
    // SurpriseRemoval can race this callback. Do not create another mapping
    // after the terminal gate has already been observed.
    if(gate_->Removed()) {
        (void)Release();
        return STATUS_INVALID_DEVICE_STATE;
    }
    dsp_=static_cast<UCHAR*>(MmMapIoSpaceEx(addresses[1],lengths[1],PAGE_READWRITE|PAGE_NOCACHE));
    if(!dsp_) {
        (void)Release();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

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
    return STATUS_SUCCESS;
}

NTSTATUS PnpResources::Release() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !gate_) return STATUS_INVALID_DEVICE_STATE;
    // Never unmap a resource bundle while the framework skeleton still says D0.
    // A failed D0Entry leaves phase Prepared and is therefore releasable without
    // a synthetic D0Exit, matching KMDF's documented failure semantics.
    if(phase_!=PnpPowerPhase::NoResources && phase_!=PnpPowerPhase::Prepared)
        return STATUS_INVALID_DEVICE_STATE;

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
