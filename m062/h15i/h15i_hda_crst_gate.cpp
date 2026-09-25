// SPDX-License-Identifier: MIT
#include "h15i_hda_crst_gate.h"

using namespace phaser360::windows;

inline void* operator new(SIZE_T,void* place) noexcept { return place; }
inline void operator delete(void*,void*) noexcept {}

const GUID phaser360::windows::kH15iInterfaceGuid={
    0x8c1b3150,0x6d12,0x4f88,{0x9d,0x36,0x15,0xf3,0x00,0x31,0x98,0x01}
};

namespace {
constexpr ULONG kHdaGcap=0x0000u,kHdaVmin=0x0002u,kHdaVmaj=0x0003u;
constexpr ULONG kHdaGctl=0x0008u,kIntelEm2=0x1030u,kGctlCrst=0x1u;
constexpr ULONG kDspAdspcs=0x0004u,kDspAdspis=0x000cu;
constexpr ULONG kDspHipci=0x0048u,kDspHipcie=0x004cu,kDspRom=0x80000u;

HardwareAccessGate* Gate(H15iDeviceContext* c) noexcept {
    return c && c->gateConstructed
        ? reinterpret_cast<HardwareAccessGate*>(c->gateStorage):nullptr;
}
bool DelayUs(unsigned us) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || us==0 || us>2000) return false;
    LARGE_INTEGER interval;
    interval.QuadPart=-static_cast<LONGLONG>(us)*10;
    return KeDelayExecutionThread(KernelMode,FALSE,&interval)==STATUS_SUCCESS;
}
bool ReadRegisters(H15iDeviceContext* c,H15iRegisterSet* r) noexcept {
    auto* g=Gate(c);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !c || !r || !g ||
       !g->Allowed() || g->Removed() || !c->hda || !c->dsp ||
       c->hdaLength!=kH15iHdaBytes || c->dspLength!=kH15iDspBytes)
        return false;
    r->hdaGcap=READ_REGISTER_USHORT(reinterpret_cast<volatile USHORT*>(c->hda+kHdaGcap));
    r->hdaVmin=READ_REGISTER_UCHAR(c->hda+kHdaVmin);
    r->hdaVmaj=READ_REGISTER_UCHAR(c->hda+kHdaVmaj);
    r->hdaGctl=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kHdaGctl));
    r->hdaIntelEm2=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kIntelEm2));
    r->dspAdspcs=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspAdspcs));
    r->dspAdspis=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspAdspis));
    r->dspHipci=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspHipci));
    r->dspHipcie=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspHipcie));
    r->dspRomStatus=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kDspRom));
    return r->hdaGcap!=0xffffu && r->hdaGctl!=0xffffffffu &&
           r->hdaIntelEm2!=0xffffffffu && r->dspAdspcs!=0xffffffffu &&
           r->dspAdspis!=0xffffffffu;
}
bool ExactKnownBaseline(const H15iRegisterSet& r) noexcept {
    return r.hdaGcap==0x6701u && r.hdaVmin==0u && r.hdaVmaj==1u &&
           r.hdaGctl==0x00000000u && r.hdaIntelEm2==0x04007000u &&
           r.dspAdspcs==0x001d003cu && r.dspAdspis==0u &&
           r.dspHipci==0u && r.dspHipcie==0x00420000u &&
           r.dspRomStatus==0x01006701u;
}
bool WriteGctlCrst(H15iDeviceContext* c,bool set) noexcept {
    auto* g=Gate(c);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !c || !g || !g->Allowed() ||
       g->Removed() || !c->hda || c->hdaLength!=kH15iHdaBytes)
        return false;
    auto* reg=reinterpret_cast<volatile ULONG*>(c->hda+kHdaGctl);
    const ULONG before=READ_REGISTER_ULONG(reg);
    if(before==0xffffffffu) return false;
    const ULONG next=set?(before|kGctlCrst):(before&~kGctlCrst);
    WRITE_REGISTER_ULONG(reg,next);
    return true;
}
bool PollGctlCrst(H15iDeviceContext* c,bool set) noexcept {
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
    WDF_DRIVER_CONFIG_INIT(&config,H15iEvtDeviceAdd);
    return WdfDriverCreate(driverObject,registryPath,WDF_NO_OBJECT_ATTRIBUTES,&config,WDF_NO_HANDLE);
}

NTSTATUS phaser360::windows::H15iEvtDeviceAdd(WDFDRIVER driver,PWDFDEVICE_INIT init) {
    UNREFERENCED_PARAMETER(driver);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !init) return STATUS_INVALID_DEVICE_STATE;
    WdfDeviceInitSetDeviceType(init,kH15iDeviceType);

    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware=H15iEvtPrepareHardware;
    pnp.EvtDeviceReleaseHardware=H15iEvtReleaseHardware;
    pnp.EvtDeviceD0Entry=H15iEvtD0Entry;
    pnp.EvtDeviceD0Exit=H15iEvtD0Exit;
    pnp.EvtDeviceSurpriseRemoval=H15iEvtSurpriseRemoval;
    WdfDeviceInitSetPnpPowerEventCallbacks(init,&pnp);

    WDF_OBJECT_ATTRIBUTES attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr,H15iDeviceContext);
    attr.EvtCleanupCallback=H15iEvtCleanup;
    attr.ExecutionLevel=WdfExecutionLevelPassive;
    attr.SynchronizationScope=WdfSynchronizationScopeDevice;
    WDFDEVICE device=nullptr;
    auto status=WdfDeviceCreate(&init,&attr,&device);
    if(!NT_SUCCESS(status)) return status;

    auto* c=H15iGetContext(device);
    if(!c) return STATUS_INVALID_DEVICE_STATE;
    RtlZeroMemory(c,sizeof(*c));
    (void)::new(c->gateStorage) HardwareAccessGate();
    c->gateConstructed=TRUE;

    WDF_IO_QUEUE_CONFIG q;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&q,WdfIoQueueDispatchSequential);
    q.PowerManaged=WdfFalse;
    q.EvtIoDeviceControl=H15iEvtIoDeviceControl;
    status=WdfIoQueueCreate(device,&q,WDF_NO_OBJECT_ATTRIBUTES,WDF_NO_HANDLE);
    if(!NT_SUCCESS(status)) return status;
    return WdfDeviceCreateDeviceInterface(device,&kH15iInterfaceGuid,nullptr);
}

void phaser360::windows::H15iEvtCleanup(WDFOBJECT object) {
    auto* c=H15iGetContext(object);auto* g=Gate(c);
    if(g){g->~HardwareAccessGate();c->gateConstructed=FALSE;}
}

NTSTATUS phaser360::windows::H15iEvtPrepareHardware(
    WDFDEVICE device,WDFCMRESLIST raw,WDFCMRESLIST translated) {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device || !raw || !translated)
        return STATUS_INVALID_DEVICE_STATE;
    auto* c=H15iGetContext(device);auto* g=Gate(c);
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
    if(mem!=2 || irq!=1 || l[0]!=kH15iHdaBytes || l[1]!=kH15iDspBytes)
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

NTSTATUS phaser360::windows::H15iEvtD0Entry(
    WDFDEVICE device,WDF_POWER_DEVICE_STATE previousState) {
    UNREFERENCED_PARAMETER(previousState);
    if(!device) return STATUS_INVALID_DEVICE_STATE;
    auto* c=H15iGetContext(device);auto* g=Gate(c);
    const bool ok=c&&g&&g->Allowed()&&!g->Removed()&&c->hda&&c->dsp&&
        InterlockedCompareExchange(&c->ready,0,0)!=0;
    if(c)InterlockedExchange(&c->d0,ok?1:0);
    return ok?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;
}

NTSTATUS phaser360::windows::H15iEvtD0Exit(
    WDFDEVICE device,WDF_POWER_DEVICE_STATE targetState) {
    UNREFERENCED_PARAMETER(targetState);
    if(device){auto* c=H15iGetContext(device);if(c)InterlockedExchange(&c->d0,0);}
    return STATUS_SUCCESS;
}

void phaser360::windows::H15iEvtSurpriseRemoval(WDFDEVICE device) {
    if(!device)return;
    auto* c=H15iGetContext(device);auto* g=Gate(c);
    if(c){InterlockedExchange(&c->d0,0);InterlockedExchange(&c->ready,0);}
    if(g)g->SurpriseRemove();
}

NTSTATUS phaser360::windows::H15iEvtReleaseHardware(
    WDFDEVICE device,WDFCMRESLIST translated) {
    UNREFERENCED_PARAMETER(translated);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device) return STATUS_INVALID_DEVICE_STATE;
    auto* c=H15iGetContext(device);auto* g=Gate(c);
    if(!c) return STATUS_INVALID_DEVICE_STATE;
    InterlockedExchange(&c->d0,0);InterlockedExchange(&c->ready,0);
    if(g)(void)g->CloseForRelease();
    auto* dsp=c->dsp;auto dl=c->dspLength;auto* hda=c->hda;auto hl=c->hdaLength;
    c->dsp=nullptr;c->hda=nullptr;c->dspLength=0;c->hdaLength=0;
    if(dsp)MmUnmapIoSpace(dsp,dl);
    if(hda)MmUnmapIoSpace(hda,hl);
    return STATUS_SUCCESS;
}

void phaser360::windows::H15iEvtIoDeviceControl(
    WDFQUEUE queue,WDFREQUEST request,SIZE_T outputLength,SIZE_T inputLength,ULONG code) {
    UNREFERENCED_PARAMETER(outputLength);
    if(!queue || !request)return;
    if(code!=IOCTL_PHASER360_H15I_TRANSACTION){
        WdfRequestComplete(request,STATUS_INVALID_DEVICE_REQUEST);return;
    }
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || inputLength!=sizeof(H15iRequestV1)){
        WdfRequestComplete(request,STATUS_INVALID_PARAMETER);return;
    }

    auto device=WdfIoQueueGetDevice(queue);
    auto* c=H15iGetContext(device);auto* g=Gate(c);
    if(!c || !g || !g->Allowed() || g->Removed() ||
       InterlockedCompareExchange(&c->ready,0,0)==0 ||
       InterlockedCompareExchange(&c->d0,0,0)==0){
        WdfRequestComplete(request,STATUS_DEVICE_NOT_READY);return;
    }

    H15iRequestV1* in=nullptr;H15iResultV1* out=nullptr;SIZE_T avail=0;
    auto status=WdfRequestRetrieveInputBuffer(request,sizeof(*in),reinterpret_cast<void**>(&in),&avail);
    if(!NT_SUCCESS(status)){WdfRequestComplete(request,status);return;}
    status=WdfRequestRetrieveOutputBuffer(request,sizeof(*out),reinterpret_cast<void**>(&out),&avail);
    if(!NT_SUCCESS(status)){WdfRequestComplete(request,status);return;}

    H15iResultV1 result{};
    result.version=1u;result.size=sizeof(result);
    result.flags=H15iNoPciWrite|H15iNoDspMmioWrite|H15iNoDma|
        H15iNoIrqOwnership|H15iNoFirmware|H15iNoDspBoot|H15iOneShot|H15iSplitMappings;
    result.generation=static_cast<ULONG>(InterlockedCompareExchange(&c->generation,0,0));
    result.hdaPhysical=c->hdaPhysical;result.dspPhysical=c->dspPhysical;
    result.hdaLength=c->hdaLength;result.dspLength=c->dspLength;

    if(in->version!=1u || in->size!=sizeof(*in) ||
       in->expectedPgctl!=kH15iExpectedPgctl || in->expectedCgctl!=kH15iExpectedCgctl){
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
        result.flags|=H15iAttestationValid;
        result.vendorId=p.vendorId;result.deviceId=p.deviceId;
        result.headerType=p.headerType;result.firstCapability=p.firstCapability;
        result.capabilityCount=p.capabilityCount;result.pgctl=p.pgctl;result.cgctl=p.cgctl;

        if(p.pgctl==kH15iExpectedPgctl && p.cgctl==kH15iExpectedCgctl &&
           ReadRegisters(c,&result.before) && ExactKnownBaseline(result.before)){
            result.flags|=H15iExpectedBaselineMatch|H15iBeforeCaptured;

            if(DelayUs(500) && WriteGctlCrst(c,true)){
                crstSetWritten=true;
                result.flags|=H15iCrstSetWritten;
                if(PollGctlCrst(c,true)){
                    result.flags|=H15iCrstReadyObserved;
                    if(DelayUs(1000) && ReadRegisters(c,&result.ready))
                        result.flags|=H15iReadyCaptured;
                }
            }
        }else result.transactionStatus=STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if(crstSetWritten){
        if(WriteGctlCrst(c,false)){
            result.flags|=H15iCrstClearWritten;
            if(PollGctlCrst(c,false)){
                result.flags|=H15iCrstRestoredObserved;
                if(ReadRegisters(c,&result.restored)){
                    result.flags|=H15iRestoredCaptured;
                    if(result.restored.hdaGctl==result.before.hdaGctl)
                        result.flags|=H15iGctlRestoredExact;
                }
            }
        }
    }

    if((result.flags&kH15iRequiredFlags)==kH15iRequiredFlags)
        result.transactionStatus=STATUS_SUCCESS;
    else if(NT_SUCCESS(result.transactionStatus))
        result.transactionStatus=STATUS_DEVICE_CONFIGURATION_ERROR;

    *out=result;
    WdfRequestCompleteWithInformation(request,STATUS_SUCCESS,sizeof(result));
}
