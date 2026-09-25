// SPDX-License-Identifier: MIT
#include "h15n_ppctl_gprocen_gate.h"
using namespace phaser360::windows;
inline void* operator new(SIZE_T,void* place) noexcept { return place; }
inline void operator delete(void*,void*) noexcept {}

const GUID phaser360::windows::kH15nInterfaceGuid={
  0x8c1b3150,0x6d12,0x4f88,{0x9d,0x36,0x15,0xf8,0x00,0x31,0x98,0x04}
};

namespace {
constexpr ULONG kGcap=0x0000u,kVmin=0x0002u,kVmaj=0x0003u,kGctl=0x0008u,kLlch=0x0014u;
constexpr ULONG kCorbctl=0x004cu,kRirbctl=0x005cu,kStreamBase=0x0080u,kStreamStride=0x20u,kRunBit=0x2u;
constexpr ULONG kEm2=0x1030u,kGctlCrst=0x1u;
constexpr ULONG kAdspcs=0x0004u,kAdspic=0x0008u,kAdspis=0x000cu,kHipci=0x0048u,kHipcie=0x004cu,kHipcctl=0x0050u,kRom=0x80000u;
constexpr ULONG kPpOffset=0x0800u,kPpHeader=0x00030500u,kPpctlOff=0x04u,kPpstsOff=0x08u,kGprocen=0x40000000u,kCpa0=0x01000000u;
constexpr ULONG kCapOffsets[5]={0x0c00u,0x0800u,0x0500u,0x1f00u,0x0700u};
constexpr ULONG kCapHeaders[5]={0x00020800u,0x00030500u,0x00011f00u,0x00050700u,0x00040000u};

HardwareAccessGate* Gate(H15nDeviceContext*c) noexcept {return c&&c->gateConstructed?reinterpret_cast<HardwareAccessGate*>(c->gateStorage):nullptr;}
bool DelayUs(unsigned us) noexcept {
  if(KeGetCurrentIrql()!=PASSIVE_LEVEL||!us||us>1000)return false;
  LARGE_INTEGER x{};x.QuadPart=-static_cast<LONGLONG>(us)*10;
  return KeDelayExecutionThread(KernelMode,FALSE,&x)==STATUS_SUCCESS;
}
bool ReadObs(H15nDeviceContext*c,H15nObservation*r) noexcept {
  auto*g=Gate(c);if(!c||!r||!g||!g->Allowed()||g->Removed()||!c->hda||!c->dsp||KeGetCurrentIrql()!=PASSIVE_LEVEL)return false;
  RtlZeroMemory(r,sizeof(*r));
  r->hdaGcap=READ_REGISTER_USHORT(reinterpret_cast<volatile USHORT*>(c->hda+kGcap));
  r->hdaVmin=READ_REGISTER_UCHAR(c->hda+kVmin);r->hdaVmaj=READ_REGISTER_UCHAR(c->hda+kVmaj);
  r->hdaGctl=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kGctl));
  r->hdaCorbctl=READ_REGISTER_UCHAR(c->hda+kCorbctl);r->hdaRirbctl=READ_REGISTER_UCHAR(c->hda+kRirbctl);
  const ULONG total=((r->hdaGcap>>8)&0xfu)+((r->hdaGcap>>12)&0xfu)+((r->hdaGcap>>3)&0x1fu);
  if(total!=13u)return false;r->totalStreams=static_cast<UCHAR>(total);
  for(ULONG i=0;i<total;++i){if(READ_REGISTER_UCHAR(c->hda+kStreamBase+i*kStreamStride)&kRunBit)r->streamRunMask|=1u<<i;}
  r->hdaIntelEm2=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kEm2));
  r->dspAdspcs=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kAdspcs));
  r->dspAdspic=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kAdspic));
  r->dspAdspis=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kAdspis));
  r->dspHipci=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kHipci));
  r->dspHipcie=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kHipcie));
  r->dspHipcctl=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kHipcctl));
  r->dspRomStatus=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->dsp+kRom));
  return r->hdaGcap!=0xffffu&&r->hdaGctl!=0xffffffffu&&r->dspAdspcs!=0xffffffffu;
}
bool Idle(const H15nObservation&r) noexcept {return (r.hdaCorbctl&kRunBit)==0&&(r.hdaRirbctl&kRunBit)==0&&r.totalStreams==13u&&r.streamRunMask==0;}
bool Baseline(const H15nObservation&r) noexcept {
  return r.hdaGcap==0x6701u&&r.hdaVmin==0&&r.hdaVmaj==1&&r.hdaGctl==0&&Idle(r)&&
    r.hdaIntelEm2==0x04007000u&&r.dspAdspcs==0x001d003cu&&r.dspAdspic==0&&r.dspAdspis==0&&
    r.dspHipci==0&&r.dspHipcie==0x00420000u&&r.dspHipcctl==0&&r.dspRomStatus==0x01006701u;
}
bool WriteGctlCrst(H15nDeviceContext*c,bool set) noexcept {
  auto*g=Gate(c);if(!c||!g||!g->Allowed()||g->Removed()||!c->hda||KeGetCurrentIrql()!=PASSIVE_LEVEL)return false;
  auto*reg=reinterpret_cast<volatile ULONG*>(c->hda+kGctl);const ULONG v=READ_REGISTER_ULONG(reg);if(v==0xffffffffu)return false;
  WRITE_REGISTER_ULONG(reg,set?(v|kGctlCrst):(v&~kGctlCrst));return true;
}
bool PollGctlCrst(H15nDeviceContext*c,bool set) noexcept {
  auto*reg=reinterpret_cast<volatile ULONG*>(c->hda+kGctl);const ULONG want=set?kGctlCrst:0u;
  for(unsigned i=0;i<1000;++i){const ULONG v=READ_REGISTER_ULONG(reg);if(v==0xffffffffu)return false;if((v&kGctlCrst)==want)return true;KeStallExecutionProcessor(10);}return false;
}
bool DiscoverExactPp(H15nDeviceContext*c,H15nResultV1*r) noexcept {
  if(!c||!r||!c->hda)return false;r->llch=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kLlch));
  ULONG off=r->llch&0xffffu;if(off!=kCapOffsets[0])return false;
  for(ULONG i=0;i<5;++i){
    const ULONG h=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+off));
    if(off!=kCapOffsets[i]||h!=kCapHeaders[i])return false;
    const ULONG next=h&0xffffu;
    if(i<4){if(next!=kCapOffsets[i+1])return false;off=next;}else if(next!=0)return false;
  }
  r->ppOffset=kPpOffset;r->ppHeader=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset));
  r->ppctlBefore=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpctlOff));
  r->ppstsBefore=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpstsOff));
  return r->ppHeader==kPpHeader;
}
bool WriteGprocen(H15nDeviceContext*c,bool set) noexcept {
  auto*g=Gate(c);if(!c||!g||!g->Allowed()||g->Removed()||!c->hda||KeGetCurrentIrql()!=PASSIVE_LEVEL)return false;
  auto*reg=reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpctlOff);const ULONG v=READ_REGISTER_ULONG(reg);if(v==0xffffffffu)return false;
  const ULONG next=set?(v|kGprocen):(v&~kGprocen);if(((v^next)&~kGprocen)!=0)return false;
  WRITE_REGISTER_ULONG(reg,next);return true;
}
bool PollGprocen(H15nDeviceContext*c,bool set) noexcept {
  auto*reg=reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpctlOff);const ULONG want=set?kGprocen:0u;
  for(unsigned i=0;i<1000;++i){const ULONG v=READ_REGISTER_ULONG(reg);if(v==0xffffffffu)return false;if((v&kGprocen)==want)return true;KeStallExecutionProcessor(10);}return false;
}
bool PollCpa0(H15nDeviceContext*c) noexcept {
  auto*reg=reinterpret_cast<volatile ULONG*>(c->dsp+kAdspcs);
  for(unsigned i=0;i<100;++i){const ULONG v=READ_REGISTER_ULONG(reg);if(v==0xffffffffu)return false;if(v&kCpa0)return true;if(!DelayUs(500))return false;}return false;
}
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT o,PUNICODE_STRING r){WDF_DRIVER_CONFIG c;WDF_DRIVER_CONFIG_INIT(&c,H15nEvtDeviceAdd);return WdfDriverCreate(o,r,WDF_NO_OBJECT_ATTRIBUTES,&c,WDF_NO_HANDLE);}
NTSTATUS phaser360::windows::H15nEvtDeviceAdd(WDFDRIVER d,PWDFDEVICE_INIT i){
  UNREFERENCED_PARAMETER(d);if(!i||KeGetCurrentIrql()!=PASSIVE_LEVEL)return STATUS_INVALID_DEVICE_STATE;WdfDeviceInitSetDeviceType(i,kH15nDeviceType);
  WDF_PNPPOWER_EVENT_CALLBACKS p;WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&p);p.EvtDevicePrepareHardware=H15nEvtPrepareHardware;p.EvtDeviceReleaseHardware=H15nEvtReleaseHardware;p.EvtDeviceD0Entry=H15nEvtD0Entry;p.EvtDeviceD0Exit=H15nEvtD0Exit;p.EvtDeviceSurpriseRemoval=H15nEvtSurpriseRemoval;WdfDeviceInitSetPnpPowerEventCallbacks(i,&p);
  WDF_OBJECT_ATTRIBUTES a;WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&a,H15nDeviceContext);a.EvtCleanupCallback=H15nEvtCleanup;a.ExecutionLevel=WdfExecutionLevelPassive;a.SynchronizationScope=WdfSynchronizationScopeDevice;
  WDFDEVICE dev=nullptr;auto s=WdfDeviceCreate(&i,&a,&dev);if(!NT_SUCCESS(s))return s;auto*c=H15nGetContext(dev);RtlZeroMemory(c,sizeof(*c));(void)::new(c->gateStorage)HardwareAccessGate();c->gateConstructed=TRUE;
  WDF_IO_QUEUE_CONFIG q;WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&q,WdfIoQueueDispatchSequential);q.PowerManaged=WdfFalse;q.EvtIoDeviceControl=H15nEvtIoDeviceControl;
  s=WdfIoQueueCreate(dev,&q,WDF_NO_OBJECT_ATTRIBUTES,WDF_NO_HANDLE);if(!NT_SUCCESS(s))return s;return WdfDeviceCreateDeviceInterface(dev,&kH15nInterfaceGuid,nullptr);
}
void phaser360::windows::H15nEvtCleanup(WDFOBJECT o){auto*c=H15nGetContext(o);auto*g=Gate(c);if(g){g->~HardwareAccessGate();c->gateConstructed=FALSE;}}
NTSTATUS phaser360::windows::H15nEvtPrepareHardware(WDFDEVICE dev,WDFCMRESLIST raw,WDFCMRESLIST tr){
  auto*c=H15nGetContext(dev);auto*g=Gate(c);if(!c||!g||KeGetCurrentIrql()!=PASSIVE_LEVEL)return STATUS_INVALID_DEVICE_STATE;
  const ULONG rc=WdfCmResourceListGetCount(raw),tc=WdfCmResourceListGetCount(tr);if(!rc||rc!=tc)return STATUS_DEVICE_CONFIGURATION_ERROR;
  PHYSICAL_ADDRESS pa[2]{};ULONG ln[2]{},m=0,irq=0;
  for(ULONG x=0;x<tc;++x){auto*td=WdfCmResourceListGetDescriptor(tr,x);auto*rd=WdfCmResourceListGetDescriptor(raw,x);if(!td||!rd||td->Type!=rd->Type)return STATUS_DEVICE_CONFIGURATION_ERROR;
    if(td->Type==CmResourceTypeMemory){if(m>=2)return STATUS_DEVICE_CONFIGURATION_ERROR;pa[m]=td->u.Memory.Start;ln[m]=td->u.Memory.Length;++m;}else if(td->Type==CmResourceTypeInterrupt)++irq;}
  if(m!=2||irq!=1||ln[0]!=kH15nHdaBytes||ln[1]!=kH15nDspBytes)return STATUS_DEVICE_CONFIGURATION_ERROR;
  auto*h=static_cast<UCHAR*>(MmMapIoSpaceEx(pa[0],ln[0],PAGE_READWRITE|PAGE_NOCACHE));if(!h)return STATUS_INSUFFICIENT_RESOURCES;
  auto*d=static_cast<UCHAR*>(MmMapIoSpaceEx(pa[1],ln[1],PAGE_READONLY|PAGE_NOCACHE));if(!d){MmUnmapIoSpace(h,ln[0]);return STATUS_INSUFFICIENT_RESOURCES;}
  if(!g->OpenForPrepare()){MmUnmapIoSpace(d,ln[1]);MmUnmapIoSpace(h,ln[0]);return STATUS_INVALID_DEVICE_STATE;}
  c->hda=h;c->dsp=d;c->hdaLength=ln[0];c->dspLength=ln[1];c->hdaPhysical=pa[0].QuadPart;c->dspPhysical=pa[1].QuadPart;InterlockedIncrement(&c->generation);InterlockedExchange(&c->ready,1);return STATUS_SUCCESS;
}
NTSTATUS phaser360::windows::H15nEvtD0Entry(WDFDEVICE dev,WDF_POWER_DEVICE_STATE){auto*c=H15nGetContext(dev);auto*g=Gate(c);const bool ok=c&&g&&g->Allowed()&&!g->Removed()&&c->hda&&c->dsp&&InterlockedCompareExchange(&c->ready,0,0);if(c)InterlockedExchange(&c->d0,ok?1:0);return ok?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;}
NTSTATUS phaser360::windows::H15nEvtD0Exit(WDFDEVICE dev,WDF_POWER_DEVICE_STATE){if(dev){auto*c=H15nGetContext(dev);if(c)InterlockedExchange(&c->d0,0);}return STATUS_SUCCESS;}
void phaser360::windows::H15nEvtSurpriseRemoval(WDFDEVICE dev){if(!dev)return;auto*c=H15nGetContext(dev);auto*g=Gate(c);if(c){InterlockedExchange(&c->d0,0);InterlockedExchange(&c->ready,0);}if(g)g->SurpriseRemove();}
NTSTATUS phaser360::windows::H15nEvtReleaseHardware(WDFDEVICE dev,WDFCMRESLIST){auto*c=H15nGetContext(dev);auto*g=Gate(c);if(!c)return STATUS_INVALID_DEVICE_STATE;InterlockedExchange(&c->d0,0);InterlockedExchange(&c->ready,0);if(g)(void)g->CloseForRelease();auto*d=c->dsp;auto dl=c->dspLength;auto*h=c->hda;auto hl=c->hdaLength;c->dsp=nullptr;c->hda=nullptr;if(d)MmUnmapIoSpace(d,dl);if(h)MmUnmapIoSpace(h,hl);return STATUS_SUCCESS;}

void phaser360::windows::H15nEvtIoDeviceControl(WDFQUEUE q,WDFREQUEST req,SIZE_T,SIZE_T inLen,ULONG code){
  if(code!=IOCTL_PHASER360_H15N_TRANSACTION){WdfRequestComplete(req,STATUS_INVALID_DEVICE_REQUEST);return;}
  auto*c=H15nGetContext(WdfIoQueueGetDevice(q));auto*g=Gate(c);
  if(!c||!g||!g->Allowed()||g->Removed()||!c->d0||inLen!=sizeof(H15nRequestV1)){WdfRequestComplete(req,STATUS_DEVICE_NOT_READY);return;}
  H15nRequestV1*in=nullptr;H15nResultV1*out=nullptr;SIZE_T n=0;
  if(!NT_SUCCESS(WdfRequestRetrieveInputBuffer(req,sizeof(*in),reinterpret_cast<void**>(&in),&n))||
     !NT_SUCCESS(WdfRequestRetrieveOutputBuffer(req,sizeof(*out),reinterpret_cast<void**>(&out),&n))){WdfRequestComplete(req,STATUS_BUFFER_TOO_SMALL);return;}
  H15nResultV1 r{};r.version=1u;r.size=sizeof(r);
  r.flags=H15nNoPciWrite|H15nNoDspMmioWrite|H15nNoEm2Write|H15nNoDma|H15nNoIrqOwnership|H15nNoFirmware|H15nNoDspBoot|H15nNoPlayback|H15nOneShot|H15nOnlyGprocenPpctlWrite|H15nCore0StateReadOnly;
  r.generation=InterlockedCompareExchange(&c->generation,0,0);r.hdaPhysical=c->hdaPhysical;r.dspPhysical=c->dspPhysical;r.hdaLength=c->hdaLength;r.dspLength=c->dspLength;
  if(in->version!=1u||in->size!=sizeof(*in)||in->expectedPgctl!=kH15nExpectedPgctl||in->expectedCgctl!=kH15nExpectedCgctl||InterlockedCompareExchange(&c->consumed,1,0)){
    r.transactionStatus=STATUS_INVALID_PARAMETER;*out=r;WdfRequestCompleteWithInformation(req,STATUS_SUCCESS,sizeof(r));return;
  }

  bool crstWritten=false,gprocenWritten=false,gprocenRestored=false;
  PciConfigAttestation pci;auto st=pci.Capture(WdfIoQueueGetDevice(q));r.transactionStatus=st;
  if(NT_SUCCESS(st)&&pci.Valid()){
    const auto&p=pci.Snapshot();r.flags|=H15nAttestationValid;r.vendorId=p.vendorId;r.deviceId=p.deviceId;r.headerType=p.headerType;r.firstCapability=p.firstCapability;r.capabilityCount=p.capabilityCount;r.pgctl=p.pgctl;r.cgctl=p.cgctl;
    if(p.pgctl==kH15nExpectedPgctl&&p.cgctl==kH15nExpectedCgctl&&ReadObs(c,&r.before)&&Baseline(r.before)){
      r.flags|=H15nExpectedBaselineMatch|H15nBeforeCaptured|H15nHdaTransportIdle;
      if(DelayUs(500)&&WriteGctlCrst(c,true)){crstWritten=true;r.flags|=H15nCrstSetWritten;
        if(PollGctlCrst(c,true)&&DelayUs(1000)){r.flags|=H15nCrstReadyObserved;
          if(ReadObs(c,&r.ready)){r.flags|=H15nReadyCaptured;
            if(DiscoverExactPp(c,&r)){r.flags|=H15nPpExactDiscovered;
              if(r.ppctlBefore==0&&r.ppstsBefore==0){r.flags|=H15nPpctlZeroBaseline;
                if(WriteGprocen(c,true)){gprocenWritten=true;r.flags|=H15nGprocenSetWritten;
                  if(PollGprocen(c,true)){r.flags|=H15nGprocenSetObserved;
                    if(PollCpa0(c))r.flags|=H15nCpa0Observed;
                    r.ppctlEnabled=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpctlOff));
                    r.ppstsEnabled=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpstsOff));
                    if(ReadObs(c,&r.enabled))r.flags|=H15nEnabledCaptured;
                  }
                }
              }
            }
          }
        }
      }
    }else r.transactionStatus=STATUS_DEVICE_CONFIGURATION_ERROR;
  }

  if(gprocenWritten){
    if(WriteGprocen(c,false)){r.flags|=H15nGprocenClearWritten;
      if(PollGprocen(c,false)){r.flags|=H15nGprocenClearObserved;
        r.ppctlDisabled=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpctlOff));
        r.ppstsDisabled=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpstsOff));
        if(r.ppctlDisabled==r.ppctlBefore){r.flags|=H15nPpctlRestoredExact;gprocenRestored=true;}
        if(ReadObs(c,&r.disabled))r.flags|=H15nDisabledCaptured;
      }
    }
  }
  if(crstWritten&&(!gprocenWritten||gprocenRestored)){
    if(WriteGctlCrst(c,false)){r.flags|=H15nCrstClearWritten;
      if(PollGctlCrst(c,false)){r.flags|=H15nCrstRestoredObserved;
        if(ReadObs(c,&r.finalState)){r.flags|=H15nFinalCaptured;if(r.finalState.hdaGctl==r.before.hdaGctl)r.flags|=H15nGctlRestoredExact;}
      }
    }
  }

  if((r.flags&kH15nRequiredFlags)==kH15nRequiredFlags)r.transactionStatus=STATUS_SUCCESS;
  else if(NT_SUCCESS(r.transactionStatus))r.transactionStatus=STATUS_DEVICE_CONFIGURATION_ERROR;
  *out=r;WdfRequestCompleteWithInformation(req,STATUS_SUCCESS,sizeof(r));
}
