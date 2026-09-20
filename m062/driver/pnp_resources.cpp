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
    UNREFERENCED_PARAMETER(raw);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    auto* owner=GetPnpResourcesContext(device)->owner;
    if(!owner || owner->device_!=device) return STATUS_INVALID_DEVICE_STATE;
    return owner->Prepare(translated);
}
NTSTATUS PnpResources::ReleaseHardware(WDFDEVICE device,WDFCMRESLIST translated) {
    UNREFERENCED_PARAMETER(translated);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    auto* owner=GetPnpResourcesContext(device)->owner;
    // Release also follows failed Prepare, including an unattached owner.
    if(!owner) return STATUS_SUCCESS;
    if(owner->device_!=device) return STATUS_INVALID_DEVICE_STATE;
    owner->Release(); return STATUS_SUCCESS;
}
NTSTATUS PnpResources::Prepare(WDFCMRESLIST translated) noexcept {
    if(hda_ || dsp_) return STATUS_INVALID_DEVICE_STATE;
    if(!translated) return STATUS_DEVICE_CONFIGURATION_ERROR;
    PHYSICAL_ADDRESS addresses[2]={}; ULONG lengths[2]={}; ULONG memoryCount=0;
    const ULONG count=WdfCmResourceListGetCount(translated);
    if(count==0 || count>64) return STATUS_DEVICE_CONFIGURATION_ERROR;
    for(ULONG i=0;i<count;++i) {
        const auto* descriptor=WdfCmResourceListGetDescriptor(translated,i);
        if(!descriptor) return STATUS_DEVICE_CONFIGURATION_ERROR;
        // Large-memory encoding is outside the measured GLK BAR contract.
        if(descriptor->Type==CmResourceTypeMemoryLarge) return STATUS_DEVICE_CONFIGURATION_ERROR;
        if(descriptor->Type!=CmResourceTypeMemory) continue;
        if(memoryCount==2 || descriptor->u.Memory.Start.QuadPart<=0 ||
           (descriptor->Flags & CM_RESOURCE_MEMORY_READ_ONLY)!=0 ||
           (descriptor->Flags & CM_RESOURCE_MEMORY_WRITE_ONLY)!=0)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        addresses[memoryCount]=descriptor->u.Memory.Start;
        lengths[memoryCount]=descriptor->u.Memory.Length; ++memoryCount;
    }
    // Translated list order, not physical-address sorting: BAR0 then BAR4.
    if(memoryCount!=2 || !valid_resources(static_cast<resource_address>(addresses[0].QuadPart),lengths[0],
                                          static_cast<resource_address>(addresses[1].QuadPart),lengths[1]))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    hda_=MmMapIoSpaceEx(addresses[0],lengths[0],PAGE_READWRITE|PAGE_NOCACHE);
    if(!hda_) return STATUS_INSUFFICIENT_RESOURCES;
    dsp_=MmMapIoSpaceEx(addresses[1],lengths[1],PAGE_READWRITE|PAGE_NOCACHE);
    if(!dsp_) { Release(); return STATUS_INSUFFICIENT_RESOURCES; }
    return STATUS_SUCCESS;
}
void PnpResources::Release() noexcept {
    // Never touch registers here: ReleaseHardware can run after power removal.
    // This owner has no DMA/interrupt consumers and does not export pointers.
    if(dsp_) { MmUnmapIoSpace(dsp_,0x100000); dsp_=nullptr; }
    if(hda_) { MmUnmapIoSpace(hda_,0x4000); hda_=nullptr; }
}
}}
