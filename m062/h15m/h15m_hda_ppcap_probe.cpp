// SPDX-License-Identifier: MIT
#include "h15m_hda_ppcap_probe.h"
using namespace phaser360::windows;
inline void* operator new(SIZE_T,void* place) noexcept { return place; }
inline void operator delete(void*,void*) noexcept {}

const GUID phaser360::windows::kH15mInterfaceGuid={
  0x8c1b3150,0x6d12,0x4f88,{0x9d,0x36,0x15,0xf7,0x00,0x31,0x98,0x03}
};

namespace {
constexpr ULONG kHdaGcap=0x0000u,kHdaVmin=0x0002u,kHdaVmaj=0x0003u,kHdaGctl=0x0008u;
constexpr ULONG kHdaLlch=0x0014u,kCorbctl=0x004cu,kRirbctl=0x005cu;
constexpr ULONG kStreamBase=0x0080u,kStreamStride=0x20u,kRunBit=0x2u,kIntelEm2=0x1030u,kGctlCrst=0x1u;
constexpr ULONG kAdspcs=0x0004u,kAdspic=0x0008u,kAdspis=0x000cu,kHipci=0x0048u,kHipcie=0x004cu,kHipcctl=0x0050u,kRom=0x80000u;
constexpr ULONG kPpCapId=3u,kPpctlOffset=0x04u,kPpstsOffset=0x08u;

HardwareAccessGate* Gate(H15mDeviceContext* c) noexcept {
  return c&&c->gateConstructed?reinterpret_cast<HardwareAccessGate*>(c->gateStorage):nullptr;
}
bool DelayUs(unsigned us) noexcept {
  if(KeGetCurrentIrql()!=PASSIVE_LEVEL||!us||us>2000)return false;
  LARGE_INTEGER x{};x.QuadPart=-static_cast<LONGLONG>(us)*10;
  return KeDelayExecutionThread(KernelMode,FALSE,&x)==STATUS_SUCCESS;
}
bool ReadObs(H15mDeviceContext* c,H15mObservation* r) noexcept {
  auto*g=Gate(c);
  if(KeGetCurrentIrql()!=PASSIVE_LEVEL||!c||!r||!g||!g->Allowed()||g->Removed()||
     !c->hda||!c->dsp||c->hdaLength!=kH15mHdaBytes||c->dspLength!=kH15mDspBytes)return false;
  RtlZeroMemory(r,sizeof(*r));
  r->hdaGcap=READ_REGISTER_USHORT(reinterpret_cast<volatile USHORT*>(c->hda+kHdaGcap));
  r->hdaVmin=READ_REGISTER_UCHAR(c->hda+kHdaVmin);r->hdaVmaj=READ_REGISTER_UCHAR(c->hda+kHdaVmaj);
  r->hdaGctl=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kHdaGctl));
  r->hdaCorbctl=READ_REGISTER_UCHAR(c->hda+kCorbctl);r->hdaRirbctl=READ_REGISTER_UCHAR(c->hda+kRirbctl);
  const ULONG total=((r->hdaGcap>>8)&0xfu)+((r->hdaGcap>>12)&0xfu)+((r->hdaGcap>>3)&0x1fu);
  if(total!=13u)return false;r->totalStreams=static_cast<UCHAR>(total);
  for(ULONG i=0;i<total;++i){if(READ_REGISTER_UCHAR(c->hda+kStreamBase+i*kStreamStride)&kRunBit)r->streamRunMask|=1u<<i;}
  r->hdaIntelEm2=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kIntelEm2));
  r->dspAdspcs=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kAdspcs));
  r->dspAdspic=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kAdspic));
  r->dspAdspis=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kAdspis));
  r->dspHipci=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kHipci));
  r->dspHipcie=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kHipcie));
  r->dspHipcctl=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kHipcctl));
  r->dspRomStatus=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kRom));
  return r->hdaGcap!=0xffffu&&r->hdaGctl!=0xffffffffu&&r->dspAdspcs!=0xffffffffu;
}
bool Idle(const H15mObservation&r) noexcept {
  return (r.hdaCorbctl&kRunBit)==0&&(r.hdaRirbctl&kRunBit)==0&&r.totalStreams==13u&&r.streamRunMask==0u;
}
bool Baseline(const H15mObservation&r) noexcept {
  return r.hdaGcap==0x6701u&&r.hdaVmin==0&&r.hdaVmaj==1&&r.hdaGctl==0&&Idle(r)&&
    r.hdaIntelEm2==0x04007000u&&r.dspAdspcs==0x001d003cu&&r.dspAdspic==0&&r.dspAdspis==0&&
    r.dspHipci==0&&r.dspHipcie==0x00420000u&&r.dspHipcctl==0&&r.dspRomStatus==0x01006701u;
}
bool WriteGctlCrst(H15mDeviceContext*c,bool set) noexcept {
  auto*g=Gate(c);if(!c||!g||!g->Allowed()||g->Removed()||!c->hda||KeGetCurrentIrql()!=PASSIVE_LEVEL)return false;
  auto*reg=reinterpret_cast<volatile ULONG*>(c->hda+kHdaGctl);
  const ULONG before=READ_REGISTER_ULONG(reg);if(before==0xffffffffu)return false;
  WRITE_REGISTER_ULONG(reg,set?(before|kGctlCrst):(before&~kGctlCrst));return true;
}
bool PollCrst(H15mDeviceContext*c,bool set) noexcept {
  auto*g=Gate(c);if(!c||!g||!g->Allowed()||g->Removed()||!c->hda)return false;
  auto*reg=reinterpret_cast<volatile ULONG*>(c->hda+kHdaGctl);const ULONG want=set?kGctlCrst:0u;
  for(unsigned i=0;i<1000;++i){const ULONG v=READ_REGISTER_ULONG(reg);if(v==0xffffffffu)return false;if((v&kGctlCrst)==want)return true;KeStallExecutionProcessor(10);}
  return false;
}
bool WalkCaps(H15mDeviceContext*c,H15mResultV1*r) noexcept {
  if(!c||!r||!c->hda||KeGetCurrentIrql()!=PASSIVE_LEVEL)return false;
  r->llch=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kHdaLlch));
  ULONG off=r->llch&0xffffu;if(!off)return false;
  for(ULONG n=0;n<kH15mMaxCaps;++n){
    if((off&3u)!=0||off>c->hdaLength-sizeof(ULONG))return false;
    for(ULONG j=0;j<n;++j)if(r->caps[j].offset==off)return false;
    const ULONG h=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+off));
    if(h==0xffffffffu)return false;
    r->caps[n].offset=off;r->caps[n].header=h;r->hdaCapCount=n+1;
    const ULONG id=(h>>16)&0xfffu;
    if(id==kPpCapId&&r->ppOffset==0){
      if(off>c->hdaLength-(kPpstsOffset+sizeof(ULONG)))return false;
      r->ppOffset=off;r->ppHeader=h;
      r->ppctl=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+off+kPpctlOffset));
      r->ppsts=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+off+kPpstsOffset));
      r->flags|=H15mPpCapabilityFound|H15mPpRegistersCaptured;
    }
    const ULONG next=h&0xffffu;
    if(!next)return true;
    off=next;
  }
  return false;
}
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT o,PUNICODE_STRING r){WDF_DRIVER_CONFIG c;WDF_DRIVER_CONFIG_INIT(&c,H15mEvtDeviceAdd);return WdfDriverCreate(o,r,WDF_NO_OBJECT_ATTRIBUTES,&c,WDF_NO_HANDLE);}
NTSTATUS phaser360::windows::H15mEvtDeviceAdd(WDFDRIVER d,PWDFDEVICE_INIT i){
  UNREFERENCED_PARAMETER(d);if(!i||KeGetCurrentIrql()!=PASSIVE_LEVEL)return STATUS_INVALID_DEVICE_STATE;WdfDeviceInitSetDeviceType(i,kH15mDeviceType);
  WDF_PNPPOWER_EVENT_CALLBACKS p;WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&p);p.EvtDevicePrepareHardware=H15mEvtPrepareHardware;p.EvtDeviceReleaseHardware=H15mEvtReleaseHardware;p.EvtDeviceD0Entry=H15mEvtD0Entry;p.EvtDeviceD0Exit=H15mEvtD0Exit;p.EvtDeviceSurpriseRemoval=H15mEvtSurpriseRemoval;WdfDeviceInitSetPnpPowerEventCallbacks(i,&p);
  WDF_OBJECT_ATTRIBUTES a;WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&a,H15mDeviceContext);a.EvtCleanupCallback=H15mEvtCleanup;a.ExecutionLevel=WdfExecutionLevelPassive;a.SynchronizationScope=WdfSynchronizationScopeDevice;
  WDFDEVICE dev=nullptr;auto s=WdfDeviceCreate(&i,&a,&dev);if(!NT_SUCCESS(s))return s;auto*c=H15mGetContext(dev);RtlZeroMemory(c,sizeof(*c));(void)::new(c->gateStorage)HardwareAccessGate();c->gateConstructed=TRUE;
  WDF_IO_QUEUE_CONFIG q;WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&q,WdfIoQueueDispatchSequential);q.PowerManaged=WdfFalse;q.EvtIoDeviceControl=H15mEvtIoDeviceControl;
  s=WdfIoQueueCreate(dev,&q,WDF_NO_OBJECT_ATTRIBUTES,WDF_NO_HANDLE);if(!NT_SUCCESS(s))return s;return WdfDeviceCreateDeviceInterface(dev,&kH15mInterfaceGuid,nullptr);
}
void phaser360::windows::H15mEvtCleanup(WDFOBJECT o){auto*c=H15mGetContext(o);auto*g=Gate(c);if(g){g->~HardwareAccessGate();c->gateConstructed=FALSE;}}
NTSTATUS phaser360::windows::H15mEvtPrepareHardware(WDFDEVICE dev,WDFCMRESLIST raw,WDFCMRESLIST tr){
  auto*c=H15mGetContext(dev);auto*g=Gate(c);if(!c||!g||KeGetCurrentIrql()!=PASSIVE_LEVEL)return STATUS_INVALID_DEVICE_STATE;
  const ULONG rc=WdfCmResourceListGetCount(raw),tc=WdfCmResourceListGetCount(tr);if(!rc||rc!=tc)return STATUS_DEVICE_CONFIGURATION_ERROR;
  PHYSICAL_ADDRESS pa[2]{};ULONG ln[2]{},m=0,irq=0;
  for(ULONG x=0;x<tc;++x){auto*td=WdfCmResourceListGetDescriptor(tr,x);auto*rd=WdfCmResourceListGetDescriptor(raw,x);if(!td||!rd||td->Type!=rd->Type)return STATUS_DEVICE_CONFIGURATION_ERROR;
    if(td->Type==CmResourceTypeMemory){if(m>=2)return STATUS_DEVICE_CONFIGURATION_ERROR;pa[m]=td->u.Memory.Start;ln[m]=td->u.Memory.Length;++m;}else if(td->Type==CmResourceTypeInterrupt)++irq;}
  if(m!=2||irq!=1||ln[0]!=kH15mHdaBytes||ln[1]!=kH15mDspBytes)return STATUS_DEVICE_CONFIGURATION_ERROR;
  auto*h=static_cast<UCHAR*>(MmMapIoSpaceEx(pa[0],ln[0],PAGE_READWRITE|PAGE_NOCACHE));if(!h)return STATUS_INSUFFICIENT_RESOURCES;
  auto*d=static_cast<UCHAR*>(MmMapIoSpaceEx(pa[1],ln[1],PAGE_READONLY|PAGE_NOCACHE));if(!d){MmUnmapIoSpace(h,ln[0]);return STATUS_INSUFFICIENT_RESOURCES;}
  if(!g->OpenForPrepare()){MmUnmapIoSpace(d,ln[1]);MmUnmapIoSpace(h,ln[0]);return STATUS_INVALID_DEVICE_STATE;}
  c->hda=h;c->dsp=d;c->hdaLength=ln[0];c->dspLength=ln[1];c->hdaPhysical=pa[0].QuadPart;c->dspPhysical=pa[1].QuadPart;InterlockedIncrement(&c->generation);InterlockedExchange(&c->ready,1);return STATUS_SUCCESS;
}
NTSTATUS phaser360::windows::H15mEvtD0Entry(WDFDEVICE dev,WDF_POWER_DEVICE_STATE){auto*c=H15mGetContext(dev);auto*g=Gate(c);const bool ok=c&&g&&g->Allowed()&&!g->Removed()&&c->hda&&c->dsp&&InterlockedCompareExchange(&c->ready,0,0);if(c)InterlockedExchange(&c->d0,ok?1:0);return ok?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;}
NTSTATUS phaser360::windows::H15mEvtD0Exit(WDFDEVICE dev,WDF_POWER_DEVICE_STATE){if(dev){auto*c=H15mGetContext(dev);if(c)InterlockedExchange(&c->d0,0);}return STATUS_SUCCESS;}
void phaser360::windows::H15mEvtSurpriseRemoval(WDFDEVICE dev){if(!dev)return;auto*c=H15mGetContext(dev);auto*g=Gate(c);if(c){InterlockedExchange(&c->d0,0);InterlockedExchange(&c->ready,0);}if(g)g->SurpriseRemove();}
NTSTATUS phaser360::windows::H15mEvtReleaseHardware(WDFDEVICE dev,WDFCMRESLIST){auto*c=H15mGetContext(dev);auto*g=Gate(c);if(!c)return STATUS_INVALID_DEVICE_STATE;InterlockedExchange(&c->d0,0);InterlockedExchange(&c->ready,0);if(g)(void)g->CloseForRelease();auto*d=c->dsp;auto dl=c->dspLength;auto*h=c->hda;auto hl=c->hdaLength;c->dsp=nullptr;c->hda=nullptr;if(d)MmUnmapIoSpace(d,dl);if(h)MmUnmapIoSpace(h,hl);return STATUS_SUCCESS;}

void phaser360::windows::H15mEvtIoDeviceControl(WDFQUEUE q,WDFREQUEST req,SIZE_T,SIZE_T inLen,ULONG code){
  if(code!=IOCTL_PHASER360_H15M_TRANSACTION){WdfRequestComplete(req,STATUS_INVALID_DEVICE_REQUEST);return;}
  auto*c=H15mGetContext(WdfIoQueueGetDevice(q));auto*g=Gate(c);
  if(!c||!g||!g->Allowed()||g->Removed()||!c->d0||inLen!=sizeof(H15mRequestV1)){WdfRequestComplete(req,STATUS_DEVICE_NOT_READY);return;}
  H15mRequestV1*in=nullptr;H15mResultV1*out=nullptr;SIZE_T n=0;
  if(!NT_SUCCESS(WdfRequestRetrieveInputBuffer(req,sizeof(*in),reinterpret_cast<void**>(&in),&n))||
     !NT_SUCCESS(WdfRequestRetrieveOutputBuffer(req,sizeof(*out),reinterpret_cast<void**>(&out),&n))){WdfRequestComplete(req,STATUS_BUFFER_TOO_SMALL);return;}
  H15mResultV1 r{};r.version=1u;r.size=sizeof(r);
  r.flags=H15mNoPciWrite|H15mNoDspMmioWrite|H15mNoPpctlWrite|H15mNoEm2Write|H15mNoDma|H15mNoIrqOwnership|H15mNoFirmware|H15mNoDspBoot|H15mNoPlayback|H15mOneShot|H15mSplitMappings|H15mCapabilityWalkBounded|H15mCore0StateReadOnly;
  r.generation=InterlockedCompareExchange(&c->generation,0,0);r.hdaPhysical=c->hdaPhysical;r.dspPhysical=c->dspPhysical;r.hdaLength=c->hdaLength;r.dspLength=c->dspLength;
  if(in->version!=1u||in->size!=sizeof(*in)||in->expectedPgctl!=kH15mExpectedPgctl||in->expectedCgctl!=kH15mExpectedCgctl||InterlockedCompareExchange(&c->consumed,1,0)){
    r.transactionStatus=STATUS_INVALID_PARAMETER;*out=r;WdfRequestCompleteWithInformation(req,STATUS_SUCCESS,sizeof(r));return;
  }
  bool crst=false;
  PciConfigAttestation pci;auto st=pci.Capture(WdfIoQueueGetDevice(q));r.transactionStatus=st;
  if(NT_SUCCESS(st)&&pci.Valid()){
    const auto&p=pci.Snapshot();r.flags|=H15mAttestationValid;r.vendorId=p.vendorId;r.deviceId=p.deviceId;r.headerType=p.headerType;r.firstCapability=p.firstCapability;r.capabilityCount=p.capabilityCount;r.pgctl=p.pgctl;r.cgctl=p.cgctl;
    if(p.pgctl==kH15mExpectedPgctl&&p.cgctl==kH15mExpectedCgctl&&ReadObs(c,&r.before)&&Baseline(r.before)){
      r.flags|=H15mExpectedBaselineMatch|H15mBeforeCaptured|H15mHdaTransportIdle;
      if(DelayUs(500)&&WriteGctlCrst(c,true)){crst=true;r.flags|=H15mCrstSetWritten;
        if(PollCrst(c,true)){r.flags|=H15mCrstReadyObserved;
          if(DelayUs(1000)&&ReadObs(c,&r.ready)){r.flags|=H15mReadyCaptured;
            if(WalkCaps(c,&r))r.flags|=H15mCapabilityChainCaptured;
          }
        }
      }
    }else r.transactionStatus=STATUS_DEVICE_CONFIGURATION_ERROR;
  }
  if(crst){
    if(WriteGctlCrst(c,false)){r.flags|=H15mCrstClearWritten;
      if(PollCrst(c,false)){r.flags|=H15mCrstRestoredObserved;
        if(ReadObs(c,&r.restored)){r.flags|=H15mRestoredCaptured;if(r.restored.hdaGctl==r.before.hdaGctl)r.flags|=H15mGctlRestoredExact;}
      }
    }
  }
  if((r.flags&kH15mRequiredFlags)==kH15mRequiredFlags)r.transactionStatus=STATUS_SUCCESS;
  else if(NT_SUCCESS(r.transactionStatus))r.transactionStatus=STATUS_DEVICE_CONFIGURATION_ERROR;
  *out=r;WdfRequestCompleteWithInformation(req,STATUS_SUCCESS,sizeof(r));
}
