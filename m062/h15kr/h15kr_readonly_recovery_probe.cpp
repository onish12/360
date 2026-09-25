// SPDX-License-Identifier: MIT
#include "h15kr_readonly_recovery_probe.h"

using namespace phaser360::windows;

const GUID phaser360::windows::kH15krInterfaceGuid={
    0x8c1b3150,0x6d12,0x4f88,{0x9d,0x36,0x15,0xf5,0x00,0x31,0x98,0x02}
};

namespace {
constexpr ULONG kHdaGcap=0x0000u,kHdaVmin=0x0002u,kHdaVmaj=0x0003u;
constexpr ULONG kHdaGctl=0x0008u,kCorbctl=0x004cu,kRirbctl=0x005cu;
constexpr ULONG kStreamBase=0x0080u,kStreamStride=0x20u,kRunBit=0x2u;
constexpr ULONG kIntelEm2=0x1030u;
constexpr ULONG kDspAdspcs=0x0004u,kDspAdspic=0x0008u,kDspAdspis=0x000cu;
constexpr ULONG kDspHipci=0x0048u,kDspHipcie=0x004cu,kDspHipcctl=0x0050u,kDspRom=0x80000u;

bool RangeOk(ULONG length,ULONG offset,ULONG bytes) noexcept {
    return offset<=length && bytes<=length-offset;
}
}

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject,PUNICODE_STRING registryPath) {
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config,H15krEvtDeviceAdd);
    return WdfDriverCreate(driverObject,registryPath,WDF_NO_OBJECT_ATTRIBUTES,&config,WDF_NO_HANDLE);
}

NTSTATUS phaser360::windows::H15krEvtDeviceAdd(WDFDRIVER driver,PWDFDEVICE_INIT init) {
    UNREFERENCED_PARAMETER(driver);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !init) return STATUS_INVALID_DEVICE_STATE;
    WdfDeviceInitSetDeviceType(init,kH15krDeviceType);

    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware=H15krEvtPrepareHardware;
    pnp.EvtDeviceReleaseHardware=H15krEvtReleaseHardware;
    pnp.EvtDeviceD0Entry=H15krEvtD0Entry;
    pnp.EvtDeviceD0Exit=H15krEvtD0Exit;
    pnp.EvtDeviceSurpriseRemoval=H15krEvtSurpriseRemoval;
    WdfDeviceInitSetPnpPowerEventCallbacks(init,&pnp);

    WDF_OBJECT_ATTRIBUTES attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr,H15krDeviceContext);
    WDFDEVICE device=nullptr;
    auto status=WdfDeviceCreate(&init,&attr,&device);
    if(!NT_SUCCESS(status)) return status;

    auto* c=H15krGetContext(device);
    if(!c) return STATUS_INVALID_DEVICE_STATE;
    RtlZeroMemory(c,sizeof(*c));

    WDF_OBJECT_ATTRIBUTES la;
    WDF_OBJECT_ATTRIBUTES_INIT(&la);
    la.ParentObject=device;
    status=WdfSpinLockCreate(&la,&c->lock);
    if(!NT_SUCCESS(status)) return status;

    c->snapshot.version=1u;
    c->snapshot.size=sizeof(H15krSnapshotV1);
    c->snapshot.flags=H15krDeviceAdded|H15krNoMmioWrite|H15krNoPciWrite|
        H15krNoDma|H15krNoIrqOwnership|H15krNoFirmware|H15krNoPlayback|H15krMappingsReadOnly;
    c->snapshot.lastStatus=STATUS_SUCCESS;
    c->snapshot.generation=1u;

    WDF_IO_QUEUE_CONFIG q;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&q,WdfIoQueueDispatchParallel);
    q.PowerManaged=WdfFalse;
    q.EvtIoDeviceControl=H15krEvtIoDeviceControl;
    status=WdfIoQueueCreate(device,&q,WDF_NO_OBJECT_ATTRIBUTES,WDF_NO_HANDLE);
    if(!NT_SUCCESS(status)) return status;

    return WdfDeviceCreateDeviceInterface(device,&kH15krInterfaceGuid,nullptr);
}

NTSTATUS phaser360::windows::H15krEvtPrepareHardware(
    WDFDEVICE device,WDFCMRESLIST raw,WDFCMRESLIST translated) {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device || !raw || !translated)
        return STATUS_INVALID_DEVICE_STATE;
    auto* c=H15krGetContext(device);
    if(!c || !c->lock || c->hda || c->dsp) return STATUS_INVALID_DEVICE_STATE;

    const ULONG rc=WdfCmResourceListGetCount(raw),tc=WdfCmResourceListGetCount(translated);
    if(!rc || rc!=tc || rc>64) return STATUS_DEVICE_CONFIGURATION_ERROR;
    PHYSICAL_ADDRESS a[2]{}; ULONG l[2]{}; ULONG mem=0,irq=0;
    for(ULONG i=0;i<tc;++i){
        auto* rd=WdfCmResourceListGetDescriptor(raw,i);
        auto* td=WdfCmResourceListGetDescriptor(translated,i);
        if(!rd || !td || rd->Type!=td->Type) return STATUS_DEVICE_CONFIGURATION_ERROR;
        if(td->Type==CmResourceTypeMemory){
            if(mem>=2 || td->u.Memory.Start.QuadPart<=0) return STATUS_DEVICE_CONFIGURATION_ERROR;
            a[mem]=td->u.Memory.Start;l[mem]=td->u.Memory.Length;++mem;
        }else if(td->Type==CmResourceTypeInterrupt){++irq;}
        else if(td->Type==CmResourceTypeMemoryLarge) return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if(mem!=2 || irq!=1 || l[0]!=kH15krHdaBytes || l[1]!=kH15krDspBytes)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    auto* hda=static_cast<UCHAR*>(MmMapIoSpaceEx(a[0],l[0],PAGE_READONLY|PAGE_NOCACHE));
    if(!hda) return STATUS_INSUFFICIENT_RESOURCES;
    auto* dsp=static_cast<UCHAR*>(MmMapIoSpaceEx(a[1],l[1],PAGE_READONLY|PAGE_NOCACHE));
    if(!dsp){MmUnmapIoSpace(hda,l[0]);return STATUS_INSUFFICIENT_RESOURCES;}

    c->hda=hda;c->dsp=dsp;c->hdaLength=l[0];c->dspLength=l[1];
    WdfSpinLockAcquire(c->lock);
    ++c->snapshot.generation;++c->snapshot.prepareCount;
    c->snapshot.rawResourceCount=rc;c->snapshot.translatedResourceCount=tc;
    c->snapshot.memoryCount=mem;c->snapshot.interruptCount=irq;
    c->snapshot.hdaPhysical=static_cast<ULONGLONG>(a[0].QuadPart);
    c->snapshot.dspPhysical=static_cast<ULONGLONG>(a[1].QuadPart);
    c->snapshot.hdaLength=l[0];c->snapshot.dspLength=l[1];
    c->snapshot.flags|=H15krPrepared|H15krResourcesExact|H15krMappingsLive;
    WdfSpinLockRelease(c->lock);
    return STATUS_SUCCESS;
}

NTSTATUS phaser360::windows::H15krEvtD0Entry(WDFDEVICE device,WDF_POWER_DEVICE_STATE previousState) {
    UNREFERENCED_PARAMETER(previousState);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device) return STATUS_INVALID_DEVICE_STATE;
    auto* c=H15krGetContext(device);
    if(!c || !c->lock || !c->hda || !c->dsp ||
       c->hdaLength!=kH15krHdaBytes || c->dspLength!=kH15krDspBytes)
        return STATUS_INVALID_DEVICE_STATE;

    if(!RangeOk(c->hdaLength,kIntelEm2,sizeof(ULONG)) ||
       !RangeOk(c->dspLength,kDspRom,sizeof(ULONG)))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    H15krSnapshotV1 n{};
    WdfSpinLockAcquire(c->lock); n=c->snapshot; WdfSpinLockRelease(c->lock);

    n.hdaGcap=READ_REGISTER_USHORT(reinterpret_cast<volatile USHORT*>(c->hda+kHdaGcap));
    n.hdaVmin=READ_REGISTER_UCHAR(c->hda+kHdaVmin);
    n.hdaVmaj=READ_REGISTER_UCHAR(c->hda+kHdaVmaj);
    n.hdaGctl=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kHdaGctl));
    n.hdaCorbctl=READ_REGISTER_UCHAR(c->hda+kCorbctl);
    n.hdaRirbctl=READ_REGISTER_UCHAR(c->hda+kRirbctl);
    const ULONG total=((n.hdaGcap>>8)&0xfu)+((n.hdaGcap>>12)&0xfu)+((n.hdaGcap>>3)&0x1fu);
    if(total==0 || total>30) return STATUS_DEVICE_CONFIGURATION_ERROR;
    n.totalStreams=static_cast<UCHAR>(total); n.streamRunMask=0;
    for(ULONG i=0;i<total;++i){
        const UCHAR ctl=READ_REGISTER_UCHAR(c->hda+kStreamBase+i*kStreamStride);
        if(ctl&kRunBit)n.streamRunMask|=(1u<<i);
    }
    n.hdaIntelEm2=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kIntelEm2));
    n.dspAdspcs=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspAdspcs));
    n.dspAdspic=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspAdspic));
    n.dspAdspis=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspAdspis));
    n.dspHipci=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspHipci));
    n.dspHipcie=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspHipcie));
    n.dspHipcctl=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspHipcctl));
    n.dspRomStatus=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspRom));

    ++n.d0EntryCount; n.flags|=H15krD0Entered;
    const bool valid=n.hdaGcap!=0xffffu && n.hdaGctl!=0xffffffffu &&
        n.hdaIntelEm2!=0xffffffffu && n.dspAdspcs!=0xffffffffu &&
        n.dspAdspic!=0xffffffffu && n.dspAdspis!=0xffffffffu &&
        n.dspHipcctl!=0xffffffffu;
    n.lastStatus=valid?STATUS_SUCCESS:STATUS_DEVICE_CONFIGURATION_ERROR;
    if(valid)n.flags|=H15krSnapshotValid;

    WdfSpinLockAcquire(c->lock); c->snapshot=n; WdfSpinLockRelease(c->lock);
    return valid?STATUS_SUCCESS:STATUS_DEVICE_CONFIGURATION_ERROR;
}

NTSTATUS phaser360::windows::H15krEvtD0Exit(WDFDEVICE device,WDF_POWER_DEVICE_STATE targetState) {
    UNREFERENCED_PARAMETER(targetState);
    if(!device)return STATUS_INVALID_DEVICE_STATE;
    auto* c=H15krGetContext(device);
    if(!c || !c->lock)return STATUS_INVALID_DEVICE_STATE;
    WdfSpinLockAcquire(c->lock);
    ++c->snapshot.d0ExitCount;c->snapshot.flags&=~H15krD0Entered;
    WdfSpinLockRelease(c->lock);
    return STATUS_SUCCESS;
}

void phaser360::windows::H15krEvtSurpriseRemoval(WDFDEVICE device) {
    if(!device)return;
    auto* c=H15krGetContext(device);
    if(c)InterlockedExchange(&c->removed,1);
}

NTSTATUS phaser360::windows::H15krEvtReleaseHardware(WDFDEVICE device,WDFCMRESLIST translated) {
    UNREFERENCED_PARAMETER(translated);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device)return STATUS_INVALID_DEVICE_STATE;
    auto* c=H15krGetContext(device);
    if(!c || !c->lock)return STATUS_INVALID_DEVICE_STATE;
    auto* dsp=c->dsp;auto dl=c->dspLength;auto* hda=c->hda;auto hl=c->hdaLength;
    c->dsp=nullptr;c->hda=nullptr;c->dspLength=0;c->hdaLength=0;
    if(dsp)MmUnmapIoSpace(dsp,dl);if(hda)MmUnmapIoSpace(hda,hl);
    WdfSpinLockAcquire(c->lock);
    ++c->snapshot.releaseCount;
    c->snapshot.flags&=~(H15krPrepared|H15krResourcesExact|H15krMappingsLive|H15krD0Entered);
    WdfSpinLockRelease(c->lock);
    return STATUS_SUCCESS;
}

void phaser360::windows::H15krEvtIoDeviceControl(
    WDFQUEUE queue,WDFREQUEST request,SIZE_T outputLength,SIZE_T inputLength,ULONG code) {
    UNREFERENCED_PARAMETER(outputLength);
    if(!queue || !request)return;
    if(code!=IOCTL_PHASER360_H15KR_SNAPSHOT){
        WdfRequestComplete(request,STATUS_INVALID_DEVICE_REQUEST);return;
    }
    if(inputLength!=0){WdfRequestComplete(request,STATUS_INVALID_PARAMETER);return;}
    H15krSnapshotV1* out=nullptr;SIZE_T avail=0;
    auto status=WdfRequestRetrieveOutputBuffer(request,sizeof(*out),reinterpret_cast<void**>(&out),&avail);
    if(!NT_SUCCESS(status)){WdfRequestComplete(request,status);return;}
    auto* c=H15krGetContext(WdfIoQueueGetDevice(queue));
    if(!c || !c->lock){WdfRequestComplete(request,STATUS_DEVICE_NOT_READY);return;}
    WdfSpinLockAcquire(c->lock);*out=c->snapshot;WdfSpinLockRelease(c->lock);
    WdfRequestCompleteWithInformation(request,STATUS_SUCCESS,sizeof(*out));
}
