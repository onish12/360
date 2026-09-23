// SPDX-License-Identifier: MIT
#include "h15j_hda_quiescence_gate.h"

using namespace phaser360::windows;

inline void* operator new(SIZE_T,void* place) noexcept { return place; }
inline void operator delete(void*,void*) noexcept {}

const GUID phaser360::windows::kH15jInterfaceGuid={
    0x8c1b3150,0x6d12,0x4f88,{0x9d,0x36,0x15,0xf3,0x00,0x31,0x98,0x01}
};

namespace {
constexpr ULONG kHdaGcap=0x0000u,kHdaVmin=0x0002u,kHdaVmaj=0x0003u;
constexpr ULONG kHdaGctl=0x0008u,kHdaCorbctl=0x004cu,kHdaRirbctl=0x005cu;
constexpr ULONG kHdaSdBase=0x0080u,kHdaSdStride=0x20u,kHdaRun=0x02u;
constexpr ULONG kIntelEm2=0x1030u,kGctlCrst=0x1u;
constexpr ULONG kDspAdspcs=0x0004u,kDspAdspic=0x0008u,kDspAdspis=0x000cu;
constexpr ULONG kDspHipci=0x0048u,kDspHipcie=0x004cu,kDspHipcctl=0x0050u,kDspRom=0x80000u;

HardwareAccessGate* Gate(H15jDeviceContext* c) noexcept {
    return c && c->gateConstructed
        ? reinterpret_cast<HardwareAccessGate*>(c->gateStorage):nullptr;
}
bool DelayUs(unsigned us) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || us==0 || us>2000) return false;
    LARGE_INTEGER interval;
    interval.QuadPart=-static_cast<LONGLONG>(us)*10;
    return KeDelayExecutionThread(KernelMode,FALSE,&interval)==STATUS_SUCCESS;
}
bool ReadRegisters(H15jDeviceContext* c,H15jRegisterSet* r) noexcept {
    auto* g=Gate(c);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !c || !r || !g ||
       !g->Allowed() || g->Removed() || !c->hda || !c->dsp ||
       c->hdaLength!=kH15jHdaBytes || c->dspLength!=kH15jDspBytes)
        return false;
    RtlZeroMemory(r,sizeof(*r));
    r->hdaGcap=READ_REGISTER_USHORT(reinterpret_cast<volatile USHORT*>(c->hda+kHdaGcap));
    r->hdaVmin=READ_REGISTER_UCHAR(c->hda+kHdaVmin);
    r->hdaVmaj=READ_REGISTER_UCHAR(c->hda+kHdaVmaj);
    r->hdaGctl=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kHdaGctl));
    r->hdaIntelEm2=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kIntelEm2));
    r->hdaCorbctl=READ_REGISTER_UCHAR(c->hda+kHdaCorbctl);
    r->hdaRirbctl=READ_REGISTER_UCHAR(c->hda+kHdaRirbctl);
    const ULONG iss=(r->hdaGcap>>8)&0xfu;
    const ULONG oss=(r->hdaGcap>>12)&0xfu;
    const ULONG bss=(r->hdaGcap>>3)&0x1fu;
    const ULONG streams=iss+oss+bss;
    if(streams==0 || streams>32 || kHdaSdBase+streams*kHdaSdStride>c->hdaLength)
        return false;
    r->hdaStreamCount=static_cast<USHORT>(streams);
    for(ULONG i=0;i<streams;++i) {
        const ULONG ctl=READ_REGISTER_ULONG(
            reinterpret_cast<volatile ULONG*>(c->hda+kHdaSdBase+i*kHdaSdStride));
        if(ctl==0xffffffffu) return false;
        if(ctl&kHdaRun) r->hdaStreamRunMask|=(1u<<i);
    }
    r->dspAdspcs=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspAdspcs));
    r->dspAdspic=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspAdspic));
    r->dspAdspis=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspAdspis));
    r->dspHipci=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspHipci));
    r->dspHipcie=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspHipcie));
    r->dspHipcctl=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspHipcctl));
    r->dspRomStatus=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspRom));
    return r->hdaGcap!=0xffffu && r->hdaGctl!=0xffffffffu &&
           r->hdaIntelEm2!=0xffffffffu && r->dspAdspcs!=0xffffffffu &&
           r->dspAdspic!=0xffffffffu && r->dspAdspis!=0xffffffffu &&
           r->dspHipcctl!=0xffffffffu;
}
bool HdaQuiescent(const H15jRegisterSet& r) noexcept {
    return (r.hdaCorbctl&kHdaRun)==0 && (r.hdaRirbctl&kHdaRun)==0 &&
           r.hdaStreamCount==13u && r.hdaStreamRunMask==0u;
}
bool ExactKnownBaseline(const H15jRegisterSet& r) noexcept {
    return r.hdaGcap==0x6701u && r.hdaVmin==0u && r.hdaVmaj==1u &&
           r.hdaGctl==0x00000000u && r.hdaIntelEm2==0x04007000u &&
           r.dspAdspcs==0x001d003cu && r.dspAdspis==0u &&
           r.dspHipci==0u && r.dspHipcie==0x00420000u &&
           r.dspRomStatus==0x01006701u;
}
bool WriteGctlCrst(H15jDeviceContext* c,bool set) noexcept {
    auto* g=Gate(c);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !c || !g || !g->Allowed() ||
       g->Removed() || !c->hda || c->hdaLength!=kH15jHdaBytes)
        return false;
    auto* reg=reinterpret_cast<volatile ULONG*>(c->hda+kHdaGctl);
    const ULONG before=READ_REGISTER_ULONG(reg);
    if(before==0xffffffffu) return false;
    const ULONG next=set?(before|kGctlCrst):(before&~kGctlCrst);
    WRITE_REGISTER_ULONG(reg,next);
    return true;
}
bool PollGctlCrst(H15jDeviceContext* c,bool set) noexcept {
    auto* g=Gate(c);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !c || !g || !g->Allowed() ||
       g->Removed() || !c->hda) return false;
    auto* reg=reinterpret_cast<volatile ULONG*>(c->hda+kHdaGctl);
    const ULONG want=set?kGctlCrst:0u;
    for(unsigned i=0;i<1000;++i) {
        const ULONG v=READ_REGISTER_ULONG(reg);
        if(v==0xffffffffu) return false;
        if((v&kGctlCrst)==want) return true;
        KeStallExecutionProcessor(10);
    }
    return false;
}
}

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject,PUNICODE_STRING registryPath) {
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config,H15jEvtDeviceAdd);
    return WdfDriverCreate(driverObject,registryPath,WDF_NO_OBJECT_ATTRIBUTES,&config,WDF_NO_HANDLE);
}

NTSTATUS phaser360::windows::H15jEvtDeviceAdd(WDFDRIVER driver,PWDFDEVICE_INIT init) {
    UNREFERENCED_PARAMETER(driver);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !init) return STATUS_INVALID_DEVICE_STATE;
    WdfDeviceInitSetDeviceType(init,kH15jDeviceType);

    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware=H15jEvtPrepareHardware;
    pnp.EvtDeviceReleaseHardware=H15jEvtReleaseHardware;
    pnp.EvtDeviceD0Entry=H15jEvtD0Entry;
    pnp.EvtDeviceD0Exit=H15jEvtD0Exit;
    pnp.EvtDeviceSurpriseRemoval=H15jEvtSurpriseRemoval;
    WdfDeviceInitSetPnpPowerEventCallbacks(init,&pnp);

    WDF_OBJECT_ATTRIBUTES attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr,H15jDeviceContext);
    attr.EvtCleanupCallback=H15jEvtCleanup;
    attr.ExecutionLevel=WdfExecutionLevelPassive;
    attr.SynchronizationScope=WdfSynchronizationScopeDevice;
    WDFDEVICE device=nullptr;
    auto status=WdfDeviceCreate(&init,&attr,&device);
    if(!NT_SUCCESS(status)) return status;

    auto* c=H15jGetContext(device);
    if(!c) return STATUS_INVALID_DEVICE_STATE;
    RtlZeroMemory(c,sizeof(*c));
    (void)::new(c->gateStorage) HardwareAccessGate();
    c->gateConstructed=TRUE;

    WDF_IO_QUEUE_CONFIG q;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&q,WdfIoQueueDispatchSequential);
    q.PowerManaged=WdfFalse;
    q.EvtIoDeviceControl=H15jEvtIoDeviceControl;
    status=WdfIoQueueCreate(device,&q,WDF_NO_OBJECT_ATTRIBUTES,WDF_NO_HANDLE);
    if(!NT_SUCCESS(status)) return status;
    return WdfDeviceCreateDeviceInterface(device,&kH15jInterfaceGuid,nullptr);
}

void phaser360::windows::H15jEvtCleanup(WDFOBJECT object) {
    auto* c=H15jGetContext(object);auto* g=Gate(c);
    if(g){g->~HardwareAccessGate();c->gateConstructed=FALSE;}
}

NTSTATUS phaser360::windows::H15jEvtPrepareHardware(
    WDFDEVICE device,WDFCMRESLIST raw,WDFCMRESLIST translated) {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device || !raw || !translated)
        return STATUS_INVALID_DEVICE_STATE;
    auto* c=H15jGetContext(device);auto* g=Gate(c);
    if(!c || !g || c->hda || c->dsp || g->Removed())
        return STATUS_INVALID_DEVICE_STATE;

    const ULONG rc=WdfCmResourceListGetCount(raw),tc=WdfCmResourceListGetCount(translated);
    if(!rc || rc!=tc || rc>64) return STATUS_DEVICE_CONFIGURATION_ERROR;
    PHYSICAL_ADDRESS a[2]{};ULONG l[2]{};ULONG mem=0,irq=0;
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
    if(mem!=2 || irq!=1 || l[0]!=kH15jHdaBytes || l[1]!=kH15jDspBytes)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    auto* hda=static_cast<UCHAR*>(MmMapIoSpaceEx(a[0],l[0],PAGE_READWRITE|PAGE_NOCACHE));
    if(!hda) return STATUS_INSUFFICIENT_RESOURCES;
    auto* dsp=static_cast<UCHAR*>(MmMapIoSpaceEx(a[1],l[1],PAGE_READONLY|PAGE_NOCACHE));
    if(!dsp){MmUnmapIoSpace(hda,l[0]);return STATUS_INSUFFICIENT_RESOURCES;}
    if(!g->OpenForPrepare()){
        MmUnmapIoSpace(dsp,l[1]);MmUnmapIoSpace(hda,l[0]);
        return STATUS_INVALID_DEVICE_STATE;
    }

    c->hda=hda;c->dsp=dsp;c->hdaLength=l[0];c->dspLength=l[1];
    c->hdaPhysical=static_cast<ULONGLONG>(a[0].QuadPart);
    c->dspPhysical=static_cast<ULONGLONG>(a[1].QuadPart);
    InterlockedIncrement(&c->generation);InterlockedExchange(&c->ready,1);
    return STATUS_SUCCESS;
}

NTSTATUS phaser360::windows::H15jEvtD0Entry(
    WDFDEVICE device,WDF_POWER_DEVICE_STATE previousState) {
    UNREFERENCED_PARAMETER(previousState);
    if(!device) return STATUS_INVALID_DEVICE_STATE;
    auto* c=H15jGetContext(device);auto* g=Gate(c);
    const bool ok=c&&g&&g->Allowed()&&!g->Removed()&&c->hda&&c->dsp&&
        InterlockedCompareExchange(&c->ready,0,0)!=0;
    if(c)InterlockedExchange(&c->d0,ok?1:0);
    return ok?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;
}

NTSTATUS phaser360::windows::H15jEvtD0Exit(
    WDFDEVICE device,WDF_POWER_DEVICE_STATE targetState) {
    UNREFERENCED_PARAMETER(targetState);
    if(device){auto* c=H15jGetContext(device);if(c)InterlockedExchange(&c->d0,0);}
    return STATUS_SUCCESS;
}

void phaser360::windows::H15jEvtSurpriseRemoval(WDFDEVICE device) {
    if(!device)return;
    auto* c=H15jGetContext(device);auto* g=Gate(c);
    if(c){InterlockedExchange(&c->d0,0);InterlockedExchange(&c->ready,0);}
    if(g)g->SurpriseRemove();
}

NTSTATUS phaser360::windows::H15jEvtReleaseHardware(
    WDFDEVICE device,WDFCMRESLIST translated) {
    UNREFERENCED_PARAMETER(translated);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device) return STATUS_INVALID_DEVICE_STATE;
    auto* c=H15jGetContext(device);auto* g=Gate(c);
    if(!c) return STATUS_INVALID_DEVICE_STATE;
    InterlockedExchange(&c->d0,0);InterlockedExchange(&c->ready,0);
    if(g)(void)g->CloseForRelease();
    auto* dsp=c->dsp;auto dl=c->dspLength;auto* hda=c->hda;auto hl=c->hdaLength;
    c->dsp=nullptr;c->hda=nullptr;c->dspLength=0;c->hdaLength=0;
    if(dsp)MmUnmapIoSpace(dsp,dl);
    if(hda)MmUnmapIoSpace(hda,hl);
    return STATUS_SUCCESS;
}

void phaser360::windows::H15jEvtIoDeviceControl(
    WDFQUEUE queue,WDFREQUEST request,SIZE_T outputLength,SIZE_T inputLength,ULONG code) {
    UNREFERENCED_PARAMETER(outputLength);
    if(!queue || !request)return;
    if(code!=IOCTL_PHASER360_H15J_TRANSACTION){
        WdfRequestComplete(request,STATUS_INVALID_DEVICE_REQUEST);return;
    }
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || inputLength!=sizeof(H15jRequestV1)){
        WdfRequestComplete(request,STATUS_INVALID_PARAMETER);return;
    }

    auto device=WdfIoQueueGetDevice(queue);
    auto* c=H15jGetContext(device);auto* g=Gate(c);
    if(!c || !g || !g->Allowed() || g->Removed() ||
       InterlockedCompareExchange(&c->ready,0,0)==0 ||
       InterlockedCompareExchange(&c->d0,0,0)==0){
        WdfRequestComplete(request,STATUS_DEVICE_NOT_READY);return;
    }

    H15jRequestV1* in=nullptr;H15jResultV1* out=nullptr;SIZE_T avail=0;
    auto status=WdfRequestRetrieveInputBuffer(request,sizeof(*in),reinterpret_cast<void**>(&in),&avail);
    if(!NT_SUCCESS(status)){WdfRequestComplete(request,status);return;}
    status=WdfRequestRetrieveOutputBuffer(request,sizeof(*out),reinterpret_cast<void**>(&out),&avail);
    if(!NT_SUCCESS(status)){WdfRequestComplete(request,status);return;}

    H15jResultV1 result{};
    result.version=1u;result.size=sizeof(result);
    result.flags=H15jNoPciWrite|H15jNoDspMmioWrite|H15jNoDma|
        H15jNoIrqOwnership|H15jNoFirmware|H15jNoDspBoot|H15jOneShot|H15jSplitMappings;
    result.generation=static_cast<ULONG>(InterlockedCompareExchange(&c->generation,0,0));
    result.hdaPhysical=c->hdaPhysical;result.dspPhysical=c->dspPhysical;
    result.hdaLength=c->hdaLength;result.dspLength=c->dspLength;

    if(in->version!=1u || in->size!=sizeof(*in) ||
       in->expectedPgctl!=kH15jExpectedPgctl || in->expectedCgctl!=kH15jExpectedCgctl){
        result.transactionStatus=STATUS_INVALID_PARAMETER;*out=result;
        WdfRequestCompleteWithInformation(request,STATUS_SUCCESS,sizeof(result));return;
    }
    if(InterlockedCompareExchange(&c->consumed,1,0)!=0){
        WdfRequestComplete(request,STATUS_INVALID_DEVICE_STATE);return;
    }

    bool crstSetWritten=false;
    PciConfigAttestation pci;
    status=pci.Capture(device);
    result.transactionStatus=status;
    if(NT_SUCCESS(status)&&pci.Valid()){
        const auto& p=pci.Snapshot();
        result.flags|=H15jAttestationValid;
        result.vendorId=p.vendorId;result.deviceId=p.deviceId;
        result.headerType=p.headerType;result.firstCapability=p.firstCapability;
        result.capabilityCount=p.capabilityCount;result.pgctl=p.pgctl;result.cgctl=p.cgctl;

        if(p.pgctl==kH15jExpectedPgctl && p.cgctl==kH15jExpectedCgctl &&
           ReadRegisters(c,&result.before) && ExactKnownBaseline(result.before) &&
           HdaQuiescent(result.before)){
            result.flags|=H15jExpectedBaselineMatch|H15jBeforeCaptured|H15jBeforeQuiescent;

            if(DelayUs(500) && WriteGctlCrst(c,true)){
                crstSetWritten=true;
                result.flags|=H15jCrstSetWritten;
                if(PollGctlCrst(c,true)){
                    result.flags|=H15jCrstReadyObserved;
                    if(DelayUs(1000) && ReadRegisters(c,&result.ready)){
                        result.flags|=H15jReadyCaptured;
                        if(HdaQuiescent(result.ready))
                            result.flags|=H15jReadyQuiescent;
                    }
                }
            }
        }else result.transactionStatus=STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if(crstSetWritten && (result.flags&H15jReadyQuiescent)){
        if(WriteGctlCrst(c,false)){
            result.flags|=H15jCrstClearWritten;
            if(PollGctlCrst(c,false)){
                result.flags|=H15jCrstRestoredObserved;
                if(ReadRegisters(c,&result.restored)){
                    result.flags|=H15jRestoredCaptured;
                    if(result.restored.hdaGctl==result.before.hdaGctl)
                        result.flags|=H15jGctlRestoredExact;
                }
            }
        }
    }

    if((result.flags&kH15jRequiredFlags)==kH15jRequiredFlags)
        result.transactionStatus=STATUS_SUCCESS;
    else if(NT_SUCCESS(result.transactionStatus))
        result.transactionStatus=STATUS_DEVICE_CONFIGURATION_ERROR;

    *out=result;
    WdfRequestCompleteWithInformation(request,STATUS_SUCCESS,sizeof(result));
}
