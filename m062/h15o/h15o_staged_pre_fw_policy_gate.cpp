// SPDX-License-Identifier: MIT
#include "h15o_staged_pre_fw_policy_gate.h"
#include <wdmguid.h>
using namespace phaser360::windows;
inline void* operator new(SIZE_T,void* place) noexcept { return place; }
inline void operator delete(void*,void*) noexcept {}

const GUID phaser360::windows::kH15oInterfaceGuid={
  0x8c1b3150,0x6d12,0x4f88,{0x9d,0x36,0x15,0xf9,0x00,0x31,0x98,0x05}
};

namespace {
constexpr ULONG kGcap=0x0000u,kVmin=0x0002u,kVmaj=0x0003u,kGctl=0x0008u,kLlch=0x0014u;
constexpr ULONG kCorbctl=0x004cu,kRirbctl=0x005cu,kStreamBase=0x0080u,kStreamStride=0x20u,kRunBit=0x2u;
constexpr ULONG kEm2=0x1030u,kEm2L1sen=0x00002000u,kGctlCrst=0x1u;
constexpr ULONG kAdspcs=0x0004u,kAdspic=0x0008u,kAdspis=0x000cu,kHipci=0x0048u,kHipcie=0x004cu,kHipcctl=0x0050u,kRom=0x80000u;
constexpr ULONG kPpOffset=0x0800u,kPpHeader=0x00030500u,kPpctlOff=0x04u,kPpstsOff=0x08u,kGprocen=0x40000000u,kCpa0=0x01000000u;
constexpr ULONG kPgOffset=0x44u,kCgOffset=0x48u,kPgMask=1u<<2,kCgMask=1u<<1;
constexpr ULONG kCapOffsets[5]={0x0c00u,0x0800u,0x0500u,0x1f00u,0x0700u};
constexpr ULONG kCapHeaders[5]={0x00020800u,0x00030500u,0x00011f00u,0x00050700u,0x00040000u};

HardwareAccessGate* Gate(H15oDeviceContext*c) noexcept {return c&&c->gateConstructed?reinterpret_cast<HardwareAccessGate*>(c->gateStorage):nullptr;}
void Store32(UCHAR*p,ULONG v) noexcept {p[0]=static_cast<UCHAR>(v);p[1]=static_cast<UCHAR>(v>>8);p[2]=static_cast<UCHAR>(v>>16);p[3]=static_cast<UCHAR>(v>>24);}
bool DelayUs(unsigned us) noexcept {
  if(KeGetCurrentIrql()!=PASSIVE_LEVEL||!us||us>1000)return false;
  LARGE_INTEGER x{};x.QuadPart=-static_cast<LONGLONG>(us)*10;
  return KeDelayExecutionThread(KernelMode,FALSE,&x)==STATUS_SUCCESS;
}
bool ReadObs(H15oDeviceContext*c,H15oObservation*r) noexcept {
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
  return r->hdaGcap!=0xffffu&&r->hdaGctl!=0xffffffffu;
}
bool Idle(const H15oObservation&r) noexcept {return (r.hdaCorbctl&kRunBit)==0&&(r.hdaRirbctl&kRunBit)==0&&r.totalStreams==13u&&r.streamRunMask==0;}
bool Baseline(const H15oObservation&r) noexcept {
  return r.hdaGcap==0x6701u&&r.hdaVmin==0&&r.hdaVmaj==1&&r.hdaGctl==0&&Idle(r)&&
    r.hdaIntelEm2==kH15oExpectedEm2&&r.dspAdspcs==0x001d003cu&&r.dspAdspic==0&&r.dspAdspis==0&&
    r.dspHipci==0&&r.dspHipcie==0x00420000u&&r.dspHipcctl==0&&r.dspRomStatus==0x01006701u;
}
bool CoreRestored(const H15oObservation&r) noexcept {
  return r.hdaGctl==0&&r.hdaIntelEm2==kH15oExpectedEm2&&r.dspAdspcs==0x001d003cu&&
    r.dspAdspic==0&&r.dspHipci==0&&r.dspHipcie==0x00420000u&&r.dspHipcctl==0&&r.dspRomStatus==0x01006701u;
}
bool WriteGctlCrst(H15oDeviceContext*c,bool set) noexcept {
  auto*g=Gate(c);if(!c||!g||!g->Allowed()||g->Removed()||!c->hda||KeGetCurrentIrql()!=PASSIVE_LEVEL)return false;
  auto*reg=reinterpret_cast<volatile ULONG*>(c->hda+kGctl);const ULONG v=READ_REGISTER_ULONG(reg);if(v==0xffffffffu)return false;
  WRITE_REGISTER_ULONG(reg,set?(v|kGctlCrst):(v&~kGctlCrst));return true;
}
bool PollGctlCrst(H15oDeviceContext*c,bool set) noexcept {
  auto*reg=reinterpret_cast<volatile ULONG*>(c->hda+kGctl);const ULONG want=set?kGctlCrst:0u;
  for(unsigned i=0;i<1000;++i){const ULONG v=READ_REGISTER_ULONG(reg);if(v==0xffffffffu)return false;if((v&kGctlCrst)==want)return true;KeStallExecutionProcessor(10);}return false;
}
bool DiscoverExactPp(H15oDeviceContext*c,H15oResultV1*r) noexcept {
  r->llch=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kLlch));
  ULONG off=r->llch&0xffffu;if(off!=kCapOffsets[0])return false;
  for(ULONG i=0;i<5;++i){const ULONG h=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+off));
    if(off!=kCapOffsets[i]||h!=kCapHeaders[i])return false;const ULONG next=h&0xffffu;
    if(i<4){if(next!=kCapOffsets[i+1])return false;off=next;}else if(next!=0)return false;}
  r->ppOffset=kPpOffset;r->ppHeader=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset));
  r->ppctlBefore=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpctlOff));
  r->ppstsBefore=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpstsOff));
  return r->ppHeader==kPpHeader;
}
bool WriteGprocen(H15oDeviceContext*c,bool set) noexcept {
  auto*g=Gate(c);if(!c||!g||!g->Allowed()||g->Removed()||!c->hda)return false;
  auto*reg=reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpctlOff);const ULONG v=READ_REGISTER_ULONG(reg);if(v==0xffffffffu)return false;
  const ULONG next=set?(v|kGprocen):(v&~kGprocen);if(((v^next)&~kGprocen)!=0)return false;WRITE_REGISTER_ULONG(reg,next);return true;
}
bool PollGprocen(H15oDeviceContext*c,bool set) noexcept {
  auto*reg=reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpctlOff);const ULONG want=set?kGprocen:0u;
  for(unsigned i=0;i<1000;++i){const ULONG v=READ_REGISTER_ULONG(reg);if(v==0xffffffffu)return false;if((v&kGprocen)==want)return true;KeStallExecutionProcessor(10);}return false;
}
bool WriteEm2(H15oDeviceContext*c,bool l1sen) noexcept {
  auto*g=Gate(c);if(!c||!g||!g->Allowed()||g->Removed()||!c->hda)return false;
  auto*reg=reinterpret_cast<volatile ULONG*>(c->hda+kEm2);const ULONG v=READ_REGISTER_ULONG(reg);if(v==0xffffffffu)return false;
  const ULONG next=l1sen?(v|kEm2L1sen):(v&~kEm2L1sen);if(((v^next)&~kEm2L1sen)!=0)return false;WRITE_REGISTER_ULONG(reg,next);return true;
}
bool PollEm2(H15oDeviceContext*c,ULONG expected) noexcept {
  auto*reg=reinterpret_cast<volatile ULONG*>(c->hda+kEm2);
  for(unsigned i=0;i<1000;++i){const ULONG v=READ_REGISTER_ULONG(reg);if(v==expected)return true;if(v==0xffffffffu)return false;KeStallExecutionProcessor(10);}return false;
}
bool PciUpdateMasked(WDFDEVICE dev,HardwareAccessGate*g,ULONG off,ULONG mask,ULONG bits,ULONG*after) noexcept {
  if(!dev||!g||!g->Allowed()||g->Removed()||KeGetCurrentIrql()!=PASSIVE_LEVEL)return false;
  BUS_INTERFACE_STANDARD bus{};const auto st=WdfFdoQueryForInterface(dev,&GUID_BUS_INTERFACE_STANDARD,reinterpret_cast<PINTERFACE>(&bus),static_cast<USHORT>(sizeof(bus)),1,nullptr);
  if(!NT_SUCCESS(st)||!bus.GetBusData||!bus.SetBusData||!bus.InterfaceDereference){if(NT_SUCCESS(st)&&bus.InterfaceDereference)bus.InterfaceDereference(bus.Context);return false;}
  ULONG cur=0;bool ok=bus.GetBusData(bus.Context,PCI_WHICHSPACE_CONFIG,&cur,off,sizeof(cur))==sizeof(cur);
  ULONG desired=(cur&~mask)|(bits&mask);
  if(ok&&desired!=cur)ok=bus.SetBusData(bus.Context,PCI_WHICHSPACE_CONFIG,&desired,off,sizeof(desired))==sizeof(desired);
  ULONG verify=0;if(ok)ok=bus.GetBusData(bus.Context,PCI_WHICHSPACE_CONFIG,&verify,off,sizeof(verify))==sizeof(verify)&&verify==desired;
  if(after)*after=verify;bus.InterfaceDereference(bus.Context);return ok;
}
bool CapturePciExact(WDFDEVICE dev,const PciConfigSnapshot&base,ULONG pg,ULONG cg,ULONG*outPg,ULONG*outCg) noexcept {
  PciConfigAttestation a;const auto st=a.Capture(dev);if(!NT_SUCCESS(st)||!a.Valid())return false;
  const auto&s=a.Snapshot();if(outPg)*outPg=s.pgctl;if(outCg)*outCg=s.cgctl;
  UCHAR expected[kPciConfigSnapshotBytes]={};RtlCopyMemory(expected,base.config,kPciConfigSnapshotBytes);Store32(expected+kPgOffset,pg);Store32(expected+kCgOffset,cg);
  return s.pgctl==pg&&s.cgctl==cg&&RtlCompareMemory(expected,s.config,kPciConfigSnapshotBytes)==kPciConfigSnapshotBytes;
}
bool ObserveCpa0(H15oDeviceContext*c,unsigned polls) noexcept {
  auto*reg=reinterpret_cast<volatile ULONG*>(c->dsp+kAdspcs);
  for(unsigned i=0;i<polls;++i){const ULONG v=READ_REGISTER_ULONG(reg);if(v!=0xffffffffu&&(v&kCpa0))return true;if(!DelayUs(500))return false;}return false;
}
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT o,PUNICODE_STRING r){WDF_DRIVER_CONFIG c;WDF_DRIVER_CONFIG_INIT(&c,H15oEvtDeviceAdd);return WdfDriverCreate(o,r,WDF_NO_OBJECT_ATTRIBUTES,&c,WDF_NO_HANDLE);}
NTSTATUS phaser360::windows::H15oEvtDeviceAdd(WDFDRIVER d,PWDFDEVICE_INIT i){
  UNREFERENCED_PARAMETER(d);if(!i||KeGetCurrentIrql()!=PASSIVE_LEVEL)return STATUS_INVALID_DEVICE_STATE;WdfDeviceInitSetDeviceType(i,kH15oDeviceType);
  WDF_PNPPOWER_EVENT_CALLBACKS p;WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&p);p.EvtDevicePrepareHardware=H15oEvtPrepareHardware;p.EvtDeviceReleaseHardware=H15oEvtReleaseHardware;p.EvtDeviceD0Entry=H15oEvtD0Entry;p.EvtDeviceD0Exit=H15oEvtD0Exit;p.EvtDeviceSurpriseRemoval=H15oEvtSurpriseRemoval;WdfDeviceInitSetPnpPowerEventCallbacks(i,&p);
  WDF_OBJECT_ATTRIBUTES a;WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&a,H15oDeviceContext);a.EvtCleanupCallback=H15oEvtCleanup;a.ExecutionLevel=WdfExecutionLevelPassive;a.SynchronizationScope=WdfSynchronizationScopeDevice;
  WDFDEVICE dev=nullptr;auto s=WdfDeviceCreate(&i,&a,&dev);if(!NT_SUCCESS(s))return s;auto*c=H15oGetContext(dev);RtlZeroMemory(c,sizeof(*c));(void)::new(c->gateStorage)HardwareAccessGate();c->gateConstructed=TRUE;
  WDF_IO_QUEUE_CONFIG q;WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&q,WdfIoQueueDispatchSequential);q.PowerManaged=WdfFalse;q.EvtIoDeviceControl=H15oEvtIoDeviceControl;
  s=WdfIoQueueCreate(dev,&q,WDF_NO_OBJECT_ATTRIBUTES,WDF_NO_HANDLE);if(!NT_SUCCESS(s))return s;return WdfDeviceCreateDeviceInterface(dev,&kH15oInterfaceGuid,nullptr);
}
void phaser360::windows::H15oEvtCleanup(WDFOBJECT o){auto*c=H15oGetContext(o);auto*g=Gate(c);if(g){g->~HardwareAccessGate();c->gateConstructed=FALSE;}}
NTSTATUS phaser360::windows::H15oEvtPrepareHardware(WDFDEVICE dev,WDFCMRESLIST raw,WDFCMRESLIST tr){
  auto*c=H15oGetContext(dev);auto*g=Gate(c);if(!c||!g||KeGetCurrentIrql()!=PASSIVE_LEVEL)return STATUS_INVALID_DEVICE_STATE;
  const ULONG rc=WdfCmResourceListGetCount(raw),tc=WdfCmResourceListGetCount(tr);if(!rc||rc!=tc)return STATUS_DEVICE_CONFIGURATION_ERROR;
  PHYSICAL_ADDRESS pa[2]{};ULONG ln[2]{},m=0,irq=0;
  for(ULONG x=0;x<tc;++x){auto*td=WdfCmResourceListGetDescriptor(tr,x);auto*rd=WdfCmResourceListGetDescriptor(raw,x);if(!td||!rd||td->Type!=rd->Type)return STATUS_DEVICE_CONFIGURATION_ERROR;
    if(td->Type==CmResourceTypeMemory){if(m>=2)return STATUS_DEVICE_CONFIGURATION_ERROR;pa[m]=td->u.Memory.Start;ln[m]=td->u.Memory.Length;++m;}else if(td->Type==CmResourceTypeInterrupt)++irq;}
  if(m!=2||irq!=1||ln[0]!=kH15oHdaBytes||ln[1]!=kH15oDspBytes)return STATUS_DEVICE_CONFIGURATION_ERROR;
  auto*h=static_cast<UCHAR*>(MmMapIoSpaceEx(pa[0],ln[0],PAGE_READWRITE|PAGE_NOCACHE));if(!h)return STATUS_INSUFFICIENT_RESOURCES;
  auto*d=static_cast<UCHAR*>(MmMapIoSpaceEx(pa[1],ln[1],PAGE_READONLY|PAGE_NOCACHE));if(!d){MmUnmapIoSpace(h,ln[0]);return STATUS_INSUFFICIENT_RESOURCES;}
  if(!g->OpenForPrepare()){MmUnmapIoSpace(d,ln[1]);MmUnmapIoSpace(h,ln[0]);return STATUS_INVALID_DEVICE_STATE;}
  c->hda=h;c->dsp=d;c->hdaLength=ln[0];c->dspLength=ln[1];c->hdaPhysical=pa[0].QuadPart;c->dspPhysical=pa[1].QuadPart;InterlockedIncrement(&c->generation);InterlockedExchange(&c->ready,1);return STATUS_SUCCESS;
}
NTSTATUS phaser360::windows::H15oEvtD0Entry(WDFDEVICE dev,WDF_POWER_DEVICE_STATE){auto*c=H15oGetContext(dev);auto*g=Gate(c);const bool ok=c&&g&&g->Allowed()&&!g->Removed()&&c->hda&&c->dsp&&InterlockedCompareExchange(&c->ready,0,0);if(c)InterlockedExchange(&c->d0,ok?1:0);return ok?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;}
NTSTATUS phaser360::windows::H15oEvtD0Exit(WDFDEVICE dev,WDF_POWER_DEVICE_STATE){if(dev){auto*c=H15oGetContext(dev);if(c)InterlockedExchange(&c->d0,0);}return STATUS_SUCCESS;}
void phaser360::windows::H15oEvtSurpriseRemoval(WDFDEVICE dev){if(!dev)return;auto*c=H15oGetContext(dev);auto*g=Gate(c);if(c){InterlockedExchange(&c->d0,0);InterlockedExchange(&c->ready,0);}if(g)g->SurpriseRemove();}
NTSTATUS phaser360::windows::H15oEvtReleaseHardware(WDFDEVICE dev,WDFCMRESLIST){auto*c=H15oGetContext(dev);auto*g=Gate(c);if(!c)return STATUS_INVALID_DEVICE_STATE;InterlockedExchange(&c->d0,0);InterlockedExchange(&c->ready,0);if(g)(void)g->CloseForRelease();auto*d=c->dsp;auto dl=c->dspLength;auto*h=c->hda;auto hl=c->hdaLength;c->dsp=nullptr;c->hda=nullptr;if(d)MmUnmapIoSpace(d,dl);if(h)MmUnmapIoSpace(h,hl);return STATUS_SUCCESS;}

void phaser360::windows::H15oEvtIoDeviceControl(WDFQUEUE q,WDFREQUEST req,SIZE_T,SIZE_T inLen,ULONG code){
  if(code!=IOCTL_PHASER360_H15O_TRANSACTION){WdfRequestComplete(req,STATUS_INVALID_DEVICE_REQUEST);return;}
  auto dev=WdfIoQueueGetDevice(q);auto*c=H15oGetContext(dev);auto*g=Gate(c);
  if(!c||!g||!g->Allowed()||g->Removed()||!c->d0||inLen!=sizeof(H15oRequestV1)){WdfRequestComplete(req,STATUS_DEVICE_NOT_READY);return;}
  H15oRequestV1*in=nullptr;H15oResultV1*out=nullptr;SIZE_T n=0;
  if(!NT_SUCCESS(WdfRequestRetrieveInputBuffer(req,sizeof(*in),reinterpret_cast<void**>(&in),&n))||!NT_SUCCESS(WdfRequestRetrieveOutputBuffer(req,sizeof(*out),reinterpret_cast<void**>(&out),&n))){WdfRequestComplete(req,STATUS_BUFFER_TOO_SMALL);return;}
  H15oResultV1 r{};r.version=1u;r.size=sizeof(r);r.flags=H15oNoDspMmioWrite|H15oNoDmaIrqFirmwarePlayback;
  r.generation=InterlockedCompareExchange(&c->generation,0,0);r.hdaPhysical=c->hdaPhysical;r.dspPhysical=c->dspPhysical;r.hdaLength=c->hdaLength;r.dspLength=c->dspLength;
  if(in->version!=1u||in->size!=sizeof(*in)||in->expectedPgctl!=kH15oExpectedPgctl||in->expectedCgctl!=kH15oExpectedCgctl||InterlockedCompareExchange(&c->consumed,1,0)){r.transactionStatus=STATUS_INVALID_PARAMETER;*out=r;WdfRequestCompleteWithInformation(req,STATUS_SUCCESS,sizeof(r));return;}

  bool crst=false,gp=false,cg=false,em=false,pg=false;PciConfigSnapshot base{};
  PciConfigAttestation a;auto st=a.Capture(dev);r.transactionStatus=st;
  if(NT_SUCCESS(st)&&a.Valid()){
    base=a.Snapshot();r.flags|=H15oAttestationValid;r.vendorId=base.vendorId;r.deviceId=base.deviceId;r.headerType=base.headerType;r.firstCapability=base.firstCapability;r.capabilityCount=base.capabilityCount;r.pgctl=base.pgctl;r.cgctl=base.cgctl;
    if(base.pgctl==kH15oExpectedPgctl&&base.cgctl==kH15oExpectedCgctl&&ReadObs(c,&r.before)&&Baseline(r.before)){
      r.flags|=H15oExpectedBaselineMatch|H15oBeforeCaptured|H15oHdaTransportIdle;
      if(DelayUs(500)&&WriteGctlCrst(c,true)){crst=true;r.flags|=H15oCrstSetWritten;
        if(PollGctlCrst(c,true)&&DelayUs(1000)){r.flags|=H15oCrstReadyObserved;if(ReadObs(c,&r.ready)){r.flags|=H15oReadyCaptured;
          if(DiscoverExactPp(c,&r)){r.flags|=H15oPpExactDiscovered;
            if(r.ppctlBefore==0&&r.ppstsBefore==0){r.flags|=H15oPpctlZeroBaseline;
              if(WriteGprocen(c,true)){gp=true;r.flags|=H15oGprocenSetWritten;
                if(PollGprocen(c,true)){r.flags|=H15oGprocenSetObserved;r.ppctlEnabled=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpctlOff));r.ppstsEnabled=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpstsOff));
                  if(ReadObs(c,&r.gprocenOnly)){r.flags|=H15oGprocenCaptured;if(ObserveCpa0(c,10)){r.cpaStageMask|=1u;r.flags|=H15oCpa0ObservedAny;}
                    cg=true;ULONG t=0;if(PciUpdateMasked(dev,g,kCgOffset,kCgMask,0,&t)&&CapturePciExact(dev,base,kH15oExpectedPgctl,kH15oAppliedCgctl,&r.pgAfterCg,&r.cgAfterCg)){r.flags|=H15oCgClearWritten|H15oCgExact;
                      if(ReadObs(c,&r.cgOff)){r.flags|=H15oCgCaptured;if(ObserveCpa0(c,10)){r.cpaStageMask|=2u;r.flags|=H15oCpa0ObservedAny;}
                        em=true;if(WriteEm2(c,false)&&PollEm2(c,kH15oAppliedEm2)){r.flags|=H15oEm2ClearWritten|H15oEm2Exact;
                          if(ReadObs(c,&r.em2Off)){r.flags|=H15oEm2Captured;if(ObserveCpa0(c,10)){r.cpaStageMask|=4u;r.flags|=H15oCpa0ObservedAny;}
                            pg=true;if(PciUpdateMasked(dev,g,kPgOffset,kPgMask,kPgMask,&t)&&CapturePciExact(dev,base,kH15oAppliedPgctl,kH15oAppliedCgctl,&r.pgAfterPg,&r.cgAfterPg)){r.flags|=H15oPgSetWritten|H15oPgExact;
                              if(ReadObs(c,&r.pgOn)){r.flags|=H15oPgCaptured;if(ObserveCpa0(c,100)){r.cpaStageMask|=8u;r.flags|=H15oCpa0ObservedAny;}}
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }}
      }
    }else r.transactionStatus=STATUS_DEVICE_CONFIGURATION_ERROR;
  }

  ULONG t=0;
  if(cg&&PciUpdateMasked(dev,g,kCgOffset,kCgMask,kCgMask,&t))r.flags|=H15oCgRestoreExact;
  if(em&&WriteEm2(c,true)&&PollEm2(c,kH15oExpectedEm2))r.flags|=H15oEm2RestoreExact;
  if(pg&&PciUpdateMasked(dev,g,kPgOffset,kPgMask,0,&t))r.flags|=H15oPgRestoreExact;
  if(cg||pg){
    PciConfigAttestation f;if(NT_SUCCESS(f.Capture(dev))&&f.Valid()){const auto&s=f.Snapshot();r.pgRestored=s.pgctl;r.cgRestored=s.cgctl;if(s.pgctl==kH15oExpectedPgctl&&s.cgctl==kH15oExpectedCgctl&&RtlCompareMemory(base.config,s.config,kPciConfigSnapshotBytes)==kPciConfigSnapshotBytes)r.flags|=H15oFullPciRestoreExact;}
  }else{r.pgRestored=base.pgctl;r.cgRestored=base.cgctl;r.flags|=H15oCgRestoreExact|H15oEm2RestoreExact|H15oPgRestoreExact|H15oFullPciRestoreExact;}

  if(gp&&WriteGprocen(c,false)&&PollGprocen(c,false)){r.ppctlRestored=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpctlOff));r.ppstsRestored=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(c->hda+kPpOffset+kPpstsOff));if(r.ppctlRestored==r.ppctlBefore)r.flags|=H15oGprocenClearExact;}
  else if(!gp){r.ppctlRestored=r.ppctlBefore;r.ppstsRestored=r.ppstsBefore;r.flags|=H15oGprocenClearExact;}

  if(crst&&WriteGctlCrst(c,false)&&PollGctlCrst(c,false)){r.flags|=H15oCrstClearExact;if(ReadObs(c,&r.finalState)){r.flags|=H15oFinalCaptured;if(CoreRestored(r.finalState))r.flags|=H15oCoreRegsRestored;}}
  else if(!crst){if(ReadObs(c,&r.finalState)){r.flags|=H15oCrstClearExact|H15oFinalCaptured;if(CoreRestored(r.finalState))r.flags|=H15oCoreRegsRestored;}}

  if((r.flags&kH15oRequiredFlags)==kH15oRequiredFlags)r.transactionStatus=STATUS_SUCCESS;else if(NT_SUCCESS(r.transactionStatus))r.transactionStatus=STATUS_DEVICE_CONFIGURATION_ERROR;
  *out=r;WdfRequestCompleteWithInformation(req,STATUS_SUCCESS,sizeof(r));
}
