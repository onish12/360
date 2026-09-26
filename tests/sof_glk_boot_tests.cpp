// SPDX-License-Identifier: MIT
// Executes production Windows wrappers against fake WDF/MMIO, not a kernel.
#include "../m062/driver/glk_boot.h"
#include "../m062/driver/ipc_interrupt.h"
#include "../m062/driver/cold_power.h"
#include "../m062/driver/device_lifecycle.h"
#include "../m062/driver/repeated_device_lifecycle.h"
#include <vector>
#include "sof_ipc_fixture.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <new>
using namespace phaser360::windows;
using phaser360::sof::RomError;
static unsigned checks=0,live=0,dmaLive=0,dspWrites=0,irql=0,sequence=0;
static unsigned alignmentCalls=0,alignmentValue=0;
static BootDma sharedDma;
static uint64_t ticks=100000;
static bool unmapped=false,dropIrqUnmask=false,dropIrqMask=false,irqHeld=false,mutexHeld=false,queued=false;
static bool dpcQueued=false,workQueued=false,finishDpcDuringCancel=false;
static unsigned cancelCalls=0,flushCalls=0;
static WDFDPC dpcHandle=nullptr;
static WDFWORKITEM workHandle=nullptr;
static WDF_DPC_CONFIG dpcConfig={};
static WDF_WORKITEM_CONFIG workConfig={};
static unsigned createFailure=0;
static unsigned sessionMemoryCreates=0;
static bool connected=false,forbidMmio=false;
static unsigned synchronizeCalls=0;
static HardwareAccessGate accessGate;
static WDF_INTERRUPT_CONFIG irqConfig={};
static bool irqCreatedWithAssignedDescriptors=false;
static WDFINTERRUPT irqHandle=nullptr;
static WDFWAITLOCK serialHandle=nullptr;
static std::vector<UCHAR> hda(0x4000),dsp(0x100000),pciConfig(256);
static unsigned pciQueryCalls=0,pciReadCalls=0,pciWriteCalls=0,pciDereferenceCalls=0;
static bool failPciQuery=false,shortPciRead=false,shortPciWriteOnce=false,rejectPciWrite=false;
static HardwareAccessGate* surpriseGateAfterPciWrite=nullptr;
static bool stuckRun=false,noRun=false,power=true,halt=false,missingReady=false,badReady=false,commandTimeout=false;
static bool sspReadZero=false;
static bool stickRunAfterStart=false;
static bool rejectPinnedEnter=false;
static HardwareAccessGate* removeDuringPinnedEnter=nullptr;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
static ULONG Get(const std::vector<UCHAR>& b,size_t o,unsigned w) {
    CHECK(o+w<=b.size()); ULONG v=0;
    for(unsigned i=0;i<w;++i) v|=ULONG(b[o+i])<<(8*i);
    return v;
}
static void Put(std::vector<UCHAR>& b,size_t o,unsigned w,ULONG v) {
    CHECK(o+w<=b.size());
    for(unsigned i=0;i<w;++i) b[o+i]=static_cast<UCHAR>(v>>(8*i));
}
static bool In(void* p,const std::vector<UCHAR>& b) {
    const auto a=reinterpret_cast<uintptr_t>(p),base=reinterpret_cast<uintptr_t>(b.data());
    return a>=base && a-base<b.size();
}
static ULONG Read(void* p,unsigned w) {
    CHECK(!forbidMmio);
    CHECK(In(p,hda)||In(p,dsp)); auto& b=In(p,hda)?hda:dsp;
    const auto off=reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(b.data());
    if(&b==&dsp) CHECK(!unmapped);
    if(irql>0) CHECK(&b==&dsp && off<=0x50 && irqHeld);
    return Get(b,off,w);
}
static void Write(void* p,unsigned w,ULONG v) {
    CHECK(!forbidMmio);
    CHECK(In(p,hda)||In(p,dsp)); auto& b=In(p,hda)?hda:dsp;
    const auto o=reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(b.data());
    if(&b==&dsp) {
        CHECK(!unmapped);
        if(irql>0) CHECK(o<=0x50 && irqHeld);
        if(dropIrqUnmask && o==8 && (v&1)) v&=~1u;
        if(dropIrqMask && o==8 && !(v&1)) v|=Get(b,o,w)&1;
        ++dspWrites;
        if(sspReadZero && w==4 && o>=0x2004 && o<=0x7004 &&
           ((o-0x2004)%0x1000)==0) return;
        if(o==0x40) v=Get(b,o,w)&~(v&0x80000000u);
        if(o==0x48 && v==0x80000000 && !commandTimeout) {
            CHECK(Get(b,0xa0000,4)==8 && Get(b,0xa0004,4)==0x30020000);
            Put(b,0xa0000,4,12); Put(b,0xa0004,4,0x10000000); Put(b,0xa0008,4,0);
            Put(b,0x4c,4,0x40000000); v=0;
        }
        if(o==0x4c) v=Get(b,o,w)&~v;
        if(o==4) {
            v=(v&~0x03000000u)|(power?((v&0x30000)<<8):(Get(b,o,w)&0x03000000));
            if((v&0x01010101)==0x01010000 && (Get(b,0x48,4)&0x80000000u)) {
                Put(b,0x4c,4,0x40000000); Put(b,0x48,4,Get(b,0x48,4)&0x7fffffff);
                Put(b,0x80000,4,1);
            }
        }
    } else {
        const ULONG gcap=Get(b,0,2);
        const ULONG total=((gcap>>8)&15)+((gcap>>12)&15);
        const bool streamStatus=w==1 && o>=0x83 && o<0x80+total*0x20 &&
            ((o-0x83)%0x20)==0;
        if(streamStatus) v=Get(b,o,w)&~v;
        if(o==0x160) {
            if(stickRunAfterStart && (v&2)) stuckRun=true;
            if(stuckRun) v|=2;
            if(noRun) v&=~2u;
            if(v&2) {
                Put(dsp,0x80000,4,halt?0x80000005u:5u);
                if(!missingReady) { IpcReadyBytes(dsp,0x81000); Put(dsp,0x40,4,0xf0000000); }
                if(badReady) Put(dsp,0x81000,4,0);
            }
        }
    }
    Put(b,o,w,v);
}
UCHAR READ_REGISTER_UCHAR(UCHAR* p) { return static_cast<UCHAR>(Read(p,1)); }
USHORT READ_REGISTER_USHORT(USHORT* p) { return static_cast<USHORT>(Read(p,2)); }
ULONG READ_REGISTER_ULONG(ULONG* p) { return Read(p,4); }
void WRITE_REGISTER_UCHAR(UCHAR* p,UCHAR v) { Write(p,1,v); }
void WRITE_REGISTER_USHORT(USHORT* p,USHORT v) { Write(p,2,v); }
void WRITE_REGISTER_ULONG(ULONG* p,ULONG v) { Write(p,4,v); }
unsigned KeGetCurrentIrql() { return irql; }
void KeMemoryBarrier() {}
void KeStallExecutionProcessor(unsigned us) { ticks+=us*10; }
NTSTATUS KeDelayExecutionThread(unsigned mode,bool alert,LARGE_INTEGER* delay) {
    CHECK(mode==KernelMode && !alert && delay && delay->QuadPart<0);
    ticks+=static_cast<uint64_t>(-delay->QuadPart); return STATUS_SUCCESS;
}
ULONGLONG KeQueryInterruptTime() { return ticks; }

static ULONG FakeGetBusData(void*,ULONG which,void* buffer,ULONG offset,ULONG length) {
    CHECK(which==PCI_WHICHSPACE_CONFIG && buffer && offset<=pciConfig.size() &&
          length<=pciConfig.size()-offset);
    ++pciReadCalls;
    ULONG actual=length;
    if(shortPciRead && actual) --actual;
    RtlCopyMemory(buffer,pciConfig.data()+offset,actual);
    return actual;
}
static ULONG FakeSetBusData(void*,ULONG which,void* buffer,ULONG offset,ULONG length) {
    CHECK(which==PCI_WHICHSPACE_CONFIG && buffer && offset<=pciConfig.size() &&
          length<=pciConfig.size()-offset);
    ++pciWriteCalls;
    if(rejectPciWrite) return 0;
    ULONG actual=length;
    if(shortPciWriteOnce && actual) { --actual; shortPciWriteOnce=false; }
    RtlCopyMemory(pciConfig.data()+offset,buffer,actual);
    if(surpriseGateAfterPciWrite) {
        auto* gate=surpriseGateAfterPciWrite;
        surpriseGateAfterPciWrite=nullptr;
        gate->SurpriseRemove();
    }
    return actual;
}
static void FakeBusReference(void*) {}
static void FakeBusDereference(void*) { ++pciDereferenceCalls; }
NTSTATUS WdfFdoQueryForInterface(
    WDFDEVICE device,LPCGUID guid,PINTERFACE out,USHORT size,USHORT version,void* specific) {
    CHECK(irql==PASSIVE_LEVEL && device && guid && out && !specific &&
          size==sizeof(BUS_INTERFACE_STANDARD) && version==1);
    ++pciQueryCalls;
    if(failPciQuery) return STATUS_NOT_SUPPORTED;
    auto* bus=reinterpret_cast<BUS_INTERFACE_STANDARD*>(out);
    *bus={};
    bus->Size=size; bus->Version=version; bus->Context=&checks;
    bus->InterfaceReference=FakeBusReference;
    bus->InterfaceDereference=FakeBusDereference;
    bus->SetBusData=FakeSetBusData;
    bus->GetBusData=FakeGetBusData;
    return STATUS_SUCCESS;
}
struct FakeObject { unsigned id; std::vector<UCHAR> bytes; bool dma=false; };
static std::vector<FakeObject*> dmaChildren;
void WdfDeviceSetAlignmentRequirement(WDFDEVICE device,ULONG alignment) {
    CHECK(device && irql==PASSIVE_LEVEL); ++alignmentCalls; alignmentValue=alignment;
}
NTSTATUS WdfDmaEnablerCreate(WDFDEVICE,WDF_DMA_ENABLER_CONFIG* config,void*,WDFDMAENABLER* out) {
    CHECK(irql==PASSIVE_LEVEL && config &&
          config->profile==WdfDmaProfileScatterGather && config->maximum==1048576);
    *out=new FakeObject{++sequence,{},true}; ++dmaLive; dmaChildren.push_back(*out);
    return STATUS_SUCCESS;
}
NTSTATUS WdfCommonBufferCreateWithConfig(WDFDMAENABLER parent,size_t n,WDF_COMMON_BUFFER_CONFIG* config,void*,WDFCOMMONBUFFER* out) {
    CHECK(parent && parent->dma && config &&
          config->alignment==FILE_4096_BYTE_ALIGNMENT);
    *out=new FakeObject{++sequence,std::vector<UCHAR>(n),true};
    ++dmaLive; dmaChildren.push_back(*out); return STATUS_SUCCESS;
}
PHYSICAL_ADDRESS WdfCommonBufferGetAlignedLogicalAddress(WDFCOMMONBUFFER b) { return {int64_t(b->id)*0x100000}; }
void* WdfCommonBufferGetAlignedVirtualAddress(WDFCOMMONBUFFER b) { return b->bytes.data(); }
void WdfObjectDelete(FakeObject* b) {
    CHECK(b);
    if(b->dma) {
        auto it=std::find(dmaChildren.begin(),dmaChildren.end(),b);
        CHECK(it!=dmaChildren.end() && dmaLive>0);
        dmaChildren.erase(it); --dmaLive;
    } else {
        CHECK(live>0); --live;
    }
    delete b;
}
NTSTATUS WdfMemoryCreate(WDF_OBJECT_ATTRIBUTES* a,unsigned pool,ULONG tag,SIZE_T n,
                         WDFMEMORY* out,void** storage) {
    CHECK(irql==0 && a && a->ParentObject && pool==NonPagedPoolNx &&
          tag==0x35534850u && n>0 && out && storage);
    if(createFailure==5) return STATUS_INSUFFICIENT_RESOURCES;
    *out=new FakeObject{++sequence,std::vector<UCHAR>(n)};
    *storage=(*out)->bytes.data(); ++live; ++sessionMemoryCreates;
    return STATUS_SUCCESS;
}
void* FakeWdfContext(WDFINTERRUPT h) { return h->bytes.data(); }
NTSTATUS WdfWaitLockCreate(WDF_OBJECT_ATTRIBUTES* a,WDFWAITLOCK* out) {
    CHECK(irql==0 && a->ParentObject);
    if(createFailure==1) return STATUS_INSUFFICIENT_RESOURCES;
    *out=new FakeObject{++sequence,{}}; serialHandle=*out; ++live; return STATUS_SUCCESS;
}
NTSTATUS WdfWaitLockAcquire(WDFWAITLOCK h,LONGLONG* timeout) {
    CHECK(h==serialHandle && irql==0 && !mutexHeld && !timeout); mutexHeld=true; return STATUS_SUCCESS;
}
void WdfWaitLockRelease(WDFWAITLOCK h) { CHECK(h==serialHandle && irql==0 && mutexHeld); mutexHeld=false; }
NTSTATUS WdfInterruptCreate(WDFDEVICE,WDF_INTERRUPT_CONFIG* c,WDF_OBJECT_ATTRIBUTES* a,WDFINTERRUPT* out) {
    CHECK(irql==0 && !c->PassiveHandling && !c->AutomaticSerialization && !c->EvtInterruptDpc);
    CHECK(((c->InterruptRaw && c->InterruptTranslated) ||
           (!c->InterruptRaw && !c->InterruptTranslated)) &&
          !c->EvtInterruptWorkItem && a->contextSize);
    irqCreatedWithAssignedDescriptors=(c->InterruptRaw!=nullptr);
    if(createFailure==2) return STATUS_INSUFFICIENT_RESOURCES;
    *out=new FakeObject{++sequence,std::vector<UCHAR>(a->contextSize)};
    irqHandle=*out; irqConfig=*c; ++live; return STATUS_SUCCESS;
}
BOOLEAN WdfInterruptSynchronize(WDFINTERRUPT h,PFN_WDF_INTERRUPT_SYNCHRONIZE cb,WDFCONTEXT p) {
    ++synchronizeCalls;
    CHECK(connected && h==irqHandle && !irqHeld && irql==0 && mutexHeld);
    irql=5; irqHeld=true; const auto result=cb(h,p); irqHeld=false; irql=0; return result;
}
NTSTATUS WdfWorkItemCreate(WDF_WORKITEM_CONFIG* c,WDF_OBJECT_ATTRIBUTES* a,WDFWORKITEM* out) {
    CHECK(irql==0 && !c->AutomaticSerialization && a->ParentObject && a->contextSize);
    if(createFailure==3) return STATUS_INSUFFICIENT_RESOURCES;
    *out=new FakeObject{++sequence,std::vector<UCHAR>(a->contextSize)};
    workHandle=*out; workConfig=*c; ++live; return STATUS_SUCCESS;
}
NTSTATUS WdfDpcCreate(WDF_DPC_CONFIG* c,WDF_OBJECT_ATTRIBUTES* a,WDFDPC* out) {
    CHECK(irql==0 && !c->AutomaticSerialization && a->ParentObject && a->contextSize);
    if(createFailure==4) return STATUS_INSUFFICIENT_RESOURCES;
    *out=new FakeObject{++sequence,std::vector<UCHAR>(a->contextSize)};
    dpcHandle=*out; dpcConfig=*c; ++live; return STATUS_SUCCESS;
}
BOOLEAN WdfDpcEnqueue(WDFDPC h) {
    CHECK(h==dpcHandle && irql==5 && irqHeld);
    const bool fresh=!dpcQueued; dpcQueued=true; queued=true; return fresh?TRUE:FALSE;
}
void WdfWorkItemEnqueue(WDFWORKITEM h) {
    CHECK(h==workHandle && irql==2 && !irqHeld && !mutexHeld);
    workQueued=true; queued=true;
}
static void RunDpc() {
    CHECK(dpcQueued && irql==0 && !mutexHeld); dpcQueued=false; queued=workQueued;
    irql=2; dpcConfig.EvtDpcFunc(dpcHandle); irql=0;
}
BOOLEAN WdfDpcCancel(WDFDPC h,BOOLEAN wait) {
    CHECK(h==dpcHandle && wait && irql==0 && !mutexHeld && !irqHeld); ++cancelCalls;
    if(finishDpcDuringCancel && dpcQueued) { RunDpc(); return FALSE; }
    const bool wasQueued=dpcQueued; dpcQueued=false; queued=workQueued;
    return wasQueued?TRUE:FALSE;
}
static NTSTATUS FrameworkEnable(bool enable) {
    CHECK(irql==0 && !irqHeld); irql=5; irqHeld=true;
    if(enable) connected=true;
    const auto result=enable?irqConfig.EvtInterruptEnable(irqHandle,&checks):irqConfig.EvtInterruptDisable(irqHandle,&checks);
    irqHeld=false; irql=0; if(!enable) connected=false; return result;
}
static bool Interrupt() {
    CHECK(irql==0 && !irqHeld); irql=5; irqHeld=true;
    const auto result=irqConfig.EvtInterruptIsr(irqHandle,0); irqHeld=false; irql=0; return result!=FALSE;
}
static void RunWork() {
    CHECK(queued && irql==0); if(dpcQueued) RunDpc();
    CHECK(workQueued); workQueued=false; queued=dpcQueued; workConfig.EvtWorkItem(workHandle);
}
void WdfWorkItemFlush(WDFWORKITEM h) {
    CHECK(h==workHandle && irql==0 && !mutexHeld && !irqHeld && !dpcQueued);
    ++flushCalls; if(workQueued) RunWork();
}
static void FrameworkDeleteChildren() {
    CHECK(!queued && !mutexHeld && !irqHeld);
    if(sharedDma.HardwarePrepared()) {
        const auto status=sharedDma.ReleaseHardware();
        if(status==STATUS_DEVICE_BUSY) CHECK(sharedDma.AbandonForRemoval());
        else CHECK(NT_SUCCESS(status));
    }
    if(!dmaChildren.empty()) {
        const auto leftovers=dmaChildren;
        for(auto* child:leftovers) WdfObjectDelete(child);
    }
    CHECK(dmaLive==0 && dmaChildren.empty());
    WdfObjectDelete(irqHandle); WdfObjectDelete(dpcHandle);
    WdfObjectDelete(workHandle); WdfObjectDelete(serialHandle);
    irqHandle=nullptr; serialHandle=nullptr;
    // A real WDFDEVICE teardown destroys the BootDma owner. Reconstruct the
    // fixture object so a later independent test does not inherit terminal
    // AbandonForRemoval state from the previous simulated device lifetime.
    sharedDma.~BootDma();
    ::new (&sharedDma) BootDma();
}
static void Notify() {
    IpcPut(dsp,0x81000,24); IpcPut(dsp,0x81004,0x90020000); IpcPut(dsp,0x81008,0);
    IpcPut(dsp,0x40,0x90020000); IpcPut(dsp,0xc,1);
}
static void ResetPciConfig() {
    pciConfig.assign(256,0);
    Put(pciConfig,0x00,2,0x8086);
    Put(pciConfig,0x02,2,0x3198);
    Put(pciConfig,0x06,2,0x0010); // conventional capability-list present
    pciConfig[0x0e]=0x00;         // type-0 function header
    pciConfig[0x34]=0x50;
    Put(pciConfig,0x44,4,0x00000004);
    Put(pciConfig,0x48,4,0x001b01f9);
    pciConfig[0x50]=0x01; pciConfig[0x51]=0x60;
    pciConfig[0x60]=0x05; pciConfig[0x61]=0;
}
static void ResetColdRegisters() {
    for(auto& v:hda) v=0;
    for(auto& v:dsp) v=0;
    Put(hda,0,2,0x6701); Put(hda,8,4,1); Put(hda,0x14,4,0x500);
    Put(hda,0x500,4,0x10030700); Put(hda,0x700,4,0x10040000); Put(hda,0x504,4,0x40000000);
}
static void Reset() {
    if(sharedDma.HardwarePrepared()) CHECK(NT_SUCCESS(sharedDma.ReleaseHardware()));
    CHECK(dmaLive==0 && dmaChildren.empty());
    CHECK(accessGate.CloseForRelease());
    CHECK(accessGate.OpenForPrepare());
    dpcQueued=false; workQueued=false; finishDpcDuringCancel=false; cancelCalls=0; flushCalls=0;
    connected=false; forbidMmio=false; synchronizeCalls=0; unmapped=false; dropIrqUnmask=false; dropIrqMask=false; createFailure=0; queued=false;
    irqCreatedWithAssignedDescriptors=false;
    CHECK(live==0); hda.assign(0x4000,0); dsp.assign(0x100000,0);
    dspWrites=0; irql=0; sequence=0; ticks=100000; sessionMemoryCreates=0;
    alignmentCalls=0; alignmentValue=0;
    stuckRun=false; noRun=false; power=true; halt=false; missingReady=false; badReady=false; commandTimeout=false;
    sspReadZero=false;
    rejectPinnedEnter=false; removeDuringPinnedEnter=nullptr;
    pciQueryCalls=0; pciReadCalls=0; pciWriteCalls=0; pciDereferenceCalls=0;
    failPciQuery=false; shortPciRead=false; shortPciWriteOnce=false; rejectPciWrite=false;
    surpriseGateAfterPciWrite=nullptr;
    ResetPciConfig();
    ResetColdRegisters();
}
static NTSTATUS BindDmaForBoot(GlkBoot& boot) {
    if(!sharedDma.HardwarePrepared()) {
        const auto status=sharedDma.PrepareHardware(&checks,286720);
        if(!NT_SUCCESS(status)) return status;
        CHECK(alignmentCalls>=1 && alignmentValue==FILE_4096_BYTE_ALIGNMENT);
    }
    return boot.BindDma(&sharedDma)?STATUS_SUCCESS:STATUS_INVALID_DEVICE_STATE;
}
static NTSTATUS Prepare(GlkBoot& boot) {
    if(!boot.BindAccessGate(&accessGate)) return STATUS_INVALID_DEVICE_STATE;
    const auto dmaStatus=BindDmaForBoot(boot);
    if(!NT_SUCCESS(dmaStatus)) return dmaStatus;
    static std::vector<UCHAR> image(286720,0xaa);
    auto x=IpcXman();
    return boot.Prepare(&checks,hda.data(),0x4000,dsp.data(),0x100000,image.data(),image.size(),x.data(),x.size(),20);
}

namespace phaser360 { namespace windows {
// H4 composition test seam: ownership/hash behavior is tested independently in
// sof_pinned_owner and real CNG tests. Here Enter must exercise the production
// ColdPower path without duplicating a 287488-byte fixture.
NTSTATUS PinnedFirmware::Enter(ColdPower& powerOwner,WDFDEVICE device,UCHAR* mappedHda,
                               ULONG hdaLength,UCHAR* mappedDsp,ULONG dspLength) noexcept {
    if(rejectPinnedEnter) return STATUS_INVALID_IMAGE_HASH;
    static std::vector<UCHAR> payload(286720,0xaa);
    auto x=IpcXman();
    const auto status=powerOwner.Enter(device,mappedHda,hdaLength,mappedDsp,dspLength,
                                       payload.data(),payload.size(),x.data(),x.size(),20);
    if(NT_SUCCESS(status) && removeDuringPinnedEnter)
        removeDuringPinnedEnter->SurpriseRemove();
    return status;
}
} }
int main() {
    // H15C: use BUS_INTERFACE_STANDARD GetBusData only, prove the exact target
    // and the vendor-specific 0x40..0x4f window before any HDA/DSP MMIO.
    Reset(); {
        PciConfigAttestation attestation;
        CHECK(NT_SUCCESS(attestation.Capture(&checks)));
        const auto& evidence=attestation.Snapshot();
        CHECK(attestation.Valid() && evidence.vendorId==0x8086 &&
              evidence.deviceId==0x3198 && (evidence.headerType&0x7f)==0 &&
              evidence.firstCapability==0x50 && evidence.capabilityCount==2 &&
              evidence.pgctl==0x00000004 && evidence.cgctl==0x001b01f9);
        CHECK(pciQueryCalls==1 && pciReadCalls==1 && pciWriteCalls==0 &&
              pciDereferenceCalls==1);
        CHECK(!NT_SUCCESS(attestation.Capture(&checks)));
    }
    Reset(); {
        PciConfigAttestation attestation;
        Put(pciConfig,0x00,2,0x1234);
        CHECK(!NT_SUCCESS(attestation.Capture(&checks)));
        CHECK(pciWriteCalls==0 && pciDereferenceCalls==1);
    }
    Reset(); {
        PciConfigAttestation attestation;
        pciConfig[0x34]=0x40;
        pciConfig[0x40]=0x01; pciConfig[0x41]=0;
        CHECK(!NT_SUCCESS(attestation.Capture(&checks)));
        CHECK(pciWriteCalls==0);
    }
    Reset(); {
        PciConfigAttestation attestation;
        pciConfig[0x61]=0x40;
        pciConfig[0x40]=0x01; pciConfig[0x41]=0;
        CHECK(!NT_SUCCESS(attestation.Capture(&checks)));
        CHECK(pciWriteCalls==0);
    }
    Reset(); {
        PciConfigAttestation attestation;
        pciConfig[0x61]=0x50;
        CHECK(!NT_SUCCESS(attestation.Capture(&checks)));
        CHECK(pciWriteCalls==0);
    }
    Reset(); {
        PciConfigAttestation attestation;
        shortPciRead=true;
        CHECK(!NT_SUCCESS(attestation.Capture(&checks)));
        CHECK(pciReadCalls==1 && pciDereferenceCalls==1 && pciWriteCalls==0);
    }
    Reset(); {
        PciConfigAttestation attestation;
        failPciQuery=true;
        CHECK(attestation.Capture(&checks)==STATUS_NOT_SUPPORTED);
        CHECK(pciReadCalls==0 && pciDereferenceCalls==0 && pciWriteCalls==0);
    }
    // H15D: only ADSPPGD (0x44 bit 2) and ADSPDCGE (0x48 bit 1)
    // may change. Preserve every unrelated bit and restore the exact baseline.
    Reset(); {
        Put(pciConfig,0x44,4,0xa5a50000u);
        Put(pciConfig,0x48,4,0x5a5a0002u);
        PciConfigAttestation attestation;
        CHECK(NT_SUCCESS(attestation.Capture(&checks)));
        const ULONG originalPg=Get(pciConfig,0x44,4);
        const ULONG originalCg=Get(pciConfig,0x48,4);
        PciConfigBootPolicy policy;
        CHECK(NT_SUCCESS(policy.Apply(&checks,attestation.Snapshot(),accessGate)));
        CHECK(policy.Applied() && policy.Dirty());
        CHECK((Get(pciConfig,0x44,4)&PciConfigBootPolicy::PgctlOwnedMask())!=0);
        CHECK((Get(pciConfig,0x48,4)&PciConfigBootPolicy::CgctlOwnedMask())==0);
        CHECK((Get(pciConfig,0x44,4)&~PciConfigBootPolicy::PgctlOwnedMask())==
              (originalPg&~PciConfigBootPolicy::PgctlOwnedMask()));
        CHECK((Get(pciConfig,0x48,4)&~PciConfigBootPolicy::CgctlOwnedMask())==
              (originalCg&~PciConfigBootPolicy::CgctlOwnedMask()));
        CHECK(policy.Restore());
        CHECK(Get(pciConfig,0x44,4)==originalPg && Get(pciConfig,0x48,4)==originalCg);
        CHECK(!policy.Applied() && !policy.Dirty() && pciWriteCalls==4);
    }
    // A short SetBusData is treated as possibly mutating hardware. Dirty state
    // must already be set so the failed Apply restores the changed bytes.
    Reset(); {
        Put(pciConfig,0x44,4,0x00000004u); // already at boot policy
        Put(pciConfig,0x48,4,0x001b01fbu); // ADSPDCGE set: one write required
        PciConfigAttestation attestation;
        CHECK(NT_SUCCESS(attestation.Capture(&checks)));
        const ULONG originalPg=Get(pciConfig,0x44,4);
        const ULONG originalCg=Get(pciConfig,0x48,4);
        shortPciWriteOnce=true;
        PciConfigBootPolicy policy;
        CHECK(!NT_SUCCESS(policy.Apply(&checks,attestation.Snapshot(),accessGate)));
        CHECK(!shortPciWriteOnce);
        CHECK(Get(pciConfig,0x44,4)==originalPg && Get(pciConfig,0x48,4)==originalCg);
        CHECK(!policy.Applied() && !policy.Dirty() && pciWriteCalls==2);
    }
    // A rejected write must fail closed and leave the captured baseline intact.
    Reset(); {
        Put(pciConfig,0x44,4,0x00000004u);
        Put(pciConfig,0x48,4,0x001b01fbu);
        PciConfigAttestation attestation;
        CHECK(NT_SUCCESS(attestation.Capture(&checks)));
        const ULONG originalCg=Get(pciConfig,0x48,4);
        rejectPciWrite=true;
        PciConfigBootPolicy policy;
        CHECK(!NT_SUCCESS(policy.Apply(&checks,attestation.Snapshot(),accessGate)));
        CHECK(Get(pciConfig,0x48,4)==originalCg);
        CHECK(!policy.Applied() && !policy.Dirty() && pciWriteCalls==1);
    }
    // R6: unrelated live capability payload/status drift after the H20
    // attestation must not be mistaken for a different PCI function. The
    // policy still requires exact identity/resource structure and exact owned
    // 0x44/0x48 dwords immediately before the first write.
    Reset(); {
        PciConfigAttestation attestation;
        CHECK(NT_SUCCESS(attestation.Capture(&checks)));
        pciConfig[0x52]^=1u; // payload byte inside capability at 0x50
        const auto unrelated=pciConfig[0x52];
        PciConfigBootPolicy policy;
        CHECK(NT_SUCCESS(policy.Apply(&checks,attestation.Snapshot(),accessGate)));
        CHECK(policy.Applied());
        CHECK(policy.Restore());
        CHECK(pciConfig[0x52]==unrelated && !policy.Dirty() && !policy.Applied());
    }
    // Structural capability drift remains fatal and cannot issue PCI writes.
    Reset(); {
        PciConfigAttestation attestation;
        CHECK(NT_SUCCESS(attestation.Capture(&checks)));
        pciConfig[0x51]=0; // change next capability pointer
        const auto writesBefore=pciWriteCalls;
        PciConfigBootPolicy policy;
        CHECK(policy.Apply(&checks,attestation.Snapshot(),accessGate)==
              STATUS_DEVICE_CONFIGURATION_ERROR);
        CHECK(pciWriteCalls==writesBefore && !policy.Dirty() && !policy.Applied());
    }
    // Either owned dword changing between capture and Apply remains fatal.
    Reset(); {
        PciConfigAttestation attestation;
        CHECK(NT_SUCCESS(attestation.Capture(&checks)));
        Put(pciConfig,0x48,4,Get(pciConfig,0x48,4)^0x4u);
        const auto writesBefore=pciWriteCalls;
        PciConfigBootPolicy policy;
        CHECK(policy.Apply(&checks,attestation.Snapshot(),accessGate)==
              STATUS_DEVICE_CONFIGURATION_ERROR);
        CHECK(pciWriteCalls==writesBefore && !policy.Dirty() && !policy.Applied());
    }
    // Surprise removal after the first PCI write terminally closes the gate.
    // The policy must not read back, issue the second write, or query again to restore.
    Reset(); {
        Put(pciConfig,0x44,4,0x00000000u);
        Put(pciConfig,0x48,4,0x00000002u);
        PciConfigAttestation attestation;
        CHECK(NT_SUCCESS(attestation.Capture(&checks)));
        HardwareAccessGate removalGate;
        CHECK(removalGate.OpenForPrepare());
        surpriseGateAfterPciWrite=&removalGate;
        const auto readsBeforeApply=pciReadCalls;
        const auto queriesBeforeApply=pciQueryCalls;
        PciConfigBootPolicy policy;
        CHECK(policy.Apply(&checks,attestation.Snapshot(),removalGate)==STATUS_DELETE_PENDING);
        CHECK(removalGate.Removed());
        CHECK(pciWriteCalls==1);
        CHECK(pciReadCalls==readsBeforeApply+1);
        CHECK(pciQueryCalls==queriesBeforeApply+1);
        CHECK(policy.Dirty() && !policy.Applied());
        CHECK(!policy.Restore());
        CHECK(pciWriteCalls==1 && pciQueryCalls==queriesBeforeApply+1);
    }
    Reset(); { GlkBoot boot; CHECK(NT_SUCCESS(Prepare(boot))); CHECK(live==0 && dmaLive==3);
        auto r=boot.Transfer(); CHECK(r.started && r.firmwareEntered && r.dmaReleased && r.ipcReady && r.commandReady && live==0 && dmaLive==3);
        std::vector<uint8_t> request(8,0),reply(12,0);
        IpcPut(request,0,8); IpcPut(request,4,0x30020000);
        auto command=boot.Command(request.data(),request.size(),0x10000000,reply.data(),reply.size());
        CHECK(command.status==phaser360::sof::CommandStatus::Ok && command.acknowledged && reply[0]==12);
        irql=2; auto before=dspWrites;
        CHECK(boot.Command(request.data(),8,0x10000000,reply.data(),12).status==phaser360::sof::CommandStatus::State);
        CHECK(dspWrites==before); irql=0;
        CHECK(boot.Windows() && boot.Windows()->region[0].offset==0xa0000);
        IpcPut(dsp,0x81000,24); IpcPut(dsp,0x81004,0x90020000); IpcPut(dsp,0x81008,0);
        IpcPut(dsp,0x40,0x90020000);
        CHECK(boot.PollNotifications()==phaser360::sof::CommandStatus::Ok);
        phaser360::sof::IpcNotification event;
        CHECK(boot.PopNotification(&event) && event.acknowledged && event.bytes==24);
        CHECK(boot.Shutdown()); CHECK(!boot.Windows()); CHECK((Get(dsp,4,4)&0x03030303)==0x303);
    }
    // R7 physical failure shape: SSP readback stays zero. The protocol must
    // still require ROM and FW_READY, and retain full cleanup on either path.
    for(unsigned mode=0;mode<2;++mode) {
        Reset(); GlkBoot boot; sspReadZero=true; missingReady=(mode==1);
        CHECK(NT_SUCCESS(Prepare(boot)));
        const auto result=boot.Transfer();
        CHECK(result.started && result.firmwareEntered && result.dmaReleased);
        CHECK(result.ipcReady==(mode==0) && result.commandReady==(mode==0));
        CHECK(boot.Shutdown() && (Get(dsp,4,4)&0x03030303)==0x303);
        CHECK(live==0 && dmaLive==3);
    }
    // H15B establishes HDA global state instead of inheriting Intel's state.
    Reset(); { GlkBoot boot;
        Put(hda,8,4,0); Put(hda,0x20,4,0xc0000080u); Put(hda,0x38,4,0x80);
        Put(hda,0x70,4,1); Put(hda,0x1030,4,0x2000); Put(hda,0x504,4,0);
        CHECK(NT_SUCCESS(Prepare(boot)));
        CHECK((Get(hda,8,4)&1)==1 && Get(hda,0x20,4)==0 && Get(hda,0x38,4)==0);
        CHECK((Get(hda,0x70,4)&1)==0 && (Get(hda,0x1030,4)&0x2000)==0);
        CHECK((Get(hda,0x504,4)&0x40000000u)!=0);
        auto transfer=boot.Transfer();
        CHECK(transfer.dmaReleased && (Get(hda,0x504,4)&0x40000000u)!=0);
        CHECK(boot.Shutdown());
        CHECK((Get(hda,0x504,4)&0x40000000u)==0 && (Get(hda,0x1030,4)&0x2000)!=0);
    }
    Reset(); { GlkBoot boot; Put(hda,0,2,0xffff); CHECK(!NT_SUCCESS(Prepare(boot)));
        CHECK(dspWrites==0 && live==0 && dmaLive==3); CHECK(boot.Shutdown()); CHECK(dspWrites==0);
    }
    Reset(); { GlkBoot boot; power=false; CHECK(!NT_SUCCESS(Prepare(boot)));
        CHECK(boot.RomError()==RomError::Timeout && live==0 && dmaLive==3);
        power=true; CHECK(boot.Shutdown()); CHECK(live==0 && dmaLive==3 && boot.RomError()==RomError::Timeout);
    }
    Reset(); { GlkBoot boot; CHECK(NT_SUCCESS(Prepare(boot))); stuckRun=true;
        auto r=boot.Transfer(); CHECK(r.started && r.firmwareEntered && !r.dmaReleased && live==0 && dmaLive==3);
        CHECK(!r.ipcReady);
        auto before=dspWrites; CHECK(!boot.Shutdown()); CHECK(dspWrites==before && live==0 && dmaLive==3);
        stuckRun=false; CHECK(boot.Shutdown()); CHECK(live==0 && dmaLive==3);
    }
    Reset(); { GlkBoot boot; CHECK(NT_SUCCESS(Prepare(boot))); halt=true;
        auto r=boot.Transfer(); CHECK(r.started && !r.firmwareEntered && r.dmaReleased);
        CHECK(r.romError==RomError::Halted); CHECK(boot.Shutdown()); CHECK(boot.RomError()==RomError::Halted);
    }
    Reset(); { GlkBoot boot; CHECK(NT_SUCCESS(Prepare(boot))); noRun=true;
        auto r=boot.Transfer(); CHECK(!r.started && !r.firmwareEntered && r.dmaReleased); CHECK(boot.Shutdown());
    }
    Reset(); { GlkBoot boot; irql=2; CHECK(!NT_SUCCESS(Prepare(boot))); CHECK(dspWrites==0 && live==0);
        CHECK(!boot.Shutdown()); irql=0; CHECK(boot.Shutdown());
    }
    for(unsigned mode=0;mode<2;++mode) {
        Reset(); GlkBoot boot; CHECK(NT_SUCCESS(Prepare(boot)));
        missingReady=(mode==0); badReady=(mode==1);
        auto r=boot.Transfer(); CHECK(r.started && r.firmwareEntered && r.dmaReleased && !r.ipcReady);
        CHECK(!boot.Windows() && live==0 && dmaLive==3); CHECK(boot.Shutdown());
    }
    Reset(); { GlkBoot boot; CHECK(boot.BindAccessGate(&accessGate)); auto x=IpcXman(); x[0]=0;
        const UCHAR image[4]={};
        CHECK(!NT_SUCCESS(boot.Prepare(&checks,hda.data(),0x4000,dsp.data(),0x100000,image,4,x.data(),x.size(),20)));
        CHECK(dspWrites==0 && live==0); CHECK(boot.IpcError()==phaser360::sof::ReceiveError::Windows);
        CHECK(boot.Shutdown() && dspWrites==0);
    }
    Reset(); { GlkBoot boot; IpcReadyBytes(dsp,0x81000); Put(dsp,0x40,4,0xf0000000);
        CHECK(NT_SUCCESS(Prepare(boot))); CHECK(!(Get(dsp,0x40,4)&0x80000000));
        missingReady=true; auto result=boot.Transfer(); CHECK(!result.ipcReady); CHECK(boot.Shutdown());
    }
    Reset(); { GlkBoot boot; std::vector<uint8_t> q(8,0),reply(12,0xa5);
        IpcPut(q,0,8); IpcPut(q,4,0x30020000);
        CHECK(boot.Command(q.data(),8,0x10000000,reply.data(),12).status==phaser360::sof::CommandStatus::State);
        CHECK(NT_SUCCESS(Prepare(boot))); CHECK(boot.Transfer().commandReady); commandTimeout=true;
        CHECK(boot.Command(q.data(),8,0x10000000,reply.data(),12).status==phaser360::sof::CommandStatus::Timeout);
        auto before=dspWrites;
        CHECK(boot.Command(q.data(),8,0x10000000,reply.data(),12).status==phaser360::sof::CommandStatus::State);
        CHECK(dspWrites==before && reply[0]==0xa5); CHECK(boot.Shutdown()); before=dspWrites;
        CHECK(boot.Command(q.data(),8,0x10000000,reply.data(),12).status==phaser360::sof::CommandStatus::State);
        CHECK(dspWrites==before);
    }
    // M0.6.15H1: a DeviceAdd interrupt shell must remain hardware-inert.
    Reset(); {
        IpcInterrupt bridge;
        CHECK(!NT_SUCCESS(bridge.CreateDormant(nullptr)));
        irql=2; CHECK(!NT_SUCCESS(bridge.CreateDormant(&checks))); irql=0;
        CHECK(NT_SUCCESS(bridge.CreateDormant(&checks)));
        CHECK(!irqCreatedWithAssignedDescriptors);
        CHECK(irqConfig.InterruptRaw==nullptr && irqConfig.InterruptTranslated==nullptr);
        const auto writesBefore=dspWrites;
        const auto syncBefore=synchronizeCalls;
        forbidMmio=true;
        CHECK(NT_SUCCESS(FrameworkEnable(true)));
        CHECK(!Interrupt() && !queued);
        CHECK(!bridge.Running() && !bridge.Arm());
        phaser360::sof::IpcNotification event;
        CHECK(!bridge.Pop(&event));
        CHECK(bridge.Command(nullptr,0,0,nullptr,0).status==phaser360::sof::CommandStatus::State);
        CHECK(NT_SUCCESS(FrameworkEnable(false)));
        CHECK(dspWrites==writesBefore && synchronizeCalls==syncBefore && !queued);
        forbidMmio=false;
        FrameworkDeleteChildren();
    }
    // M0.6.15H2: bind the dormant shell to one admitted PnP/DSP lifetime,
    // but do not grant hardware enable. ColdPower and Sync must remain blocked.
    Reset(); {
        GlkBoot boot;
        IpcInterrupt bridge;
        CHECK(NT_SUCCESS(bridge.CreateDormant(&checks)));
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={};
        CM_PARTIAL_RESOURCE_DESCRIPTOR translated={};
        raw.Type=CmResourceTypeInterrupt;
        translated.Type=CmResourceTypeInterrupt;
        raw.Flags=CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        translated.Flags=CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        PnpDormantInterruptBinding binding{};
        binding.gate=&accessGate;
        binding.dsp=dsp.data();
        binding.dspLength=0x100000;
        binding.raw=&raw;
        binding.translated=&translated;
        binding.kind=PnpInterruptKind::LineBased;
        binding.messageCount=0;

        auto bad=binding;
        bad.dspLength=0x4000;
        CHECK(!bridge.BindDormant(bad,&boot));
        bad=binding; bad.messageCount=1;
        CHECK(!bridge.BindDormant(bad,&boot));
        irql=2; CHECK(!bridge.BindDormant(binding,&boot)); irql=0;

        CHECK(bridge.BindDormant(binding,&boot));
        CHECK(boot.AccessGate()==&accessGate && boot.Fresh());
        CHECK(!bridge.BindDormant(binding,&boot));
        CHECK(!bridge.CanStartBeforeEnable());

        const auto writesBefore=dspWrites;
        const auto syncBefore=synchronizeCalls;
        forbidMmio=true;
        CHECK(NT_SUCCESS(FrameworkEnable(true)));
        CHECK(!bridge.UnbindDormant()); // connected shell cannot drop BAR lifetime
        CHECK(!Interrupt() && !queued);
        CHECK(!bridge.Running() && !bridge.Arm());
        CHECK(!bridge.CanStartBeforeEnable());
        CHECK(NT_SUCCESS(FrameworkEnable(false)));
        CHECK(dspWrites==writesBefore && synchronizeCalls==syncBefore && !queued);
        CHECK(bridge.UnbindDormant());
        CHECK(!bridge.UnbindDormant());

        // Device-lifetime shell can take a fresh prepared-resource binding
        // again after framework disconnect and software unbind.
        CHECK(bridge.BindDormant(binding,&boot));
        CHECK(!bridge.CanStartBeforeEnable());
        CHECK(bridge.UnbindDormant());
        forbidMmio=false;
        FrameworkDeleteChildren();
    }
    // M0.6.15H3: two-stage permission for a DeviceAdd shell. Boot start is
    // granted first; framework IRQ MMIO is granted only after commandReady.
    Reset(); {
        GlkBoot boot,second;
        IpcInterrupt bridge;
        CHECK(NT_SUCCESS(bridge.CreateDormant(&checks)));
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={};
        CM_PARTIAL_RESOURCE_DESCRIPTOR translated={};
        raw.Type=CmResourceTypeInterrupt;
        translated.Type=CmResourceTypeInterrupt;
        raw.Flags=CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        translated.Flags=CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        PnpDormantInterruptBinding binding{};
        binding.gate=&accessGate; binding.dsp=dsp.data(); binding.dspLength=0x100000;
        binding.raw=&raw; binding.translated=&translated;
        binding.kind=PnpInterruptKind::LineBased; binding.messageCount=0;
        CHECK(bridge.BindDormant(binding,&boot));
        CHECK(!bridge.CanStartBeforeEnable());

        const auto preGrantWrites=dspWrites;
        const auto preGrantSync=synchronizeCalls;
        forbidMmio=true;
        CHECK(!bridge.GrantFrameworkEnableAfterBoot());
        CHECK(bridge.GrantBootStart());
        CHECK(!bridge.GrantBootStart());
        CHECK(bridge.CanStartBeforeEnable());
        CHECK(!bridge.GrantFrameworkEnableAfterBoot());
        CHECK(dspWrites==preGrantWrites && synchronizeCalls==preGrantSync);
        forbidMmio=false;

        CHECK(NT_SUCCESS(BindDmaForBoot(boot))); ColdPower session(boot,bridge,accessGate);
        std::vector<UCHAR> image(286720,0xaa); auto x=IpcXman();
        CHECK(NT_SUCCESS(session.Enter(&checks,hda.data(),0x4000,dsp.data(),0x100000,
                                      image.data(),image.size(),x.data(),x.size(),20)));
        CHECK(session.TransferEvidence().commandReady);
        const auto postBootWrites=dspWrites;
        const auto postBootSync=synchronizeCalls;
        forbidMmio=true;
        CHECK(bridge.GrantFrameworkEnableAfterBoot());
        CHECK(!bridge.GrantFrameworkEnableAfterBoot());
        CHECK(dspWrites==postBootWrites && synchronizeCalls==postBootSync);
        forbidMmio=false;

        CHECK(NT_SUCCESS(FrameworkEnable(true)));
        CHECK(NT_SUCCESS(session.AfterInterruptsEnabled()));
        Notify(); CHECK(Interrupt() && queued);
        CHECK(session.BeforeInterruptsDisabled() && session.CanReleaseMappings());
        CHECK(!bridge.ResetDormantClosedSession()); // framework still connected
        CHECK(NT_SUCCESS(FrameworkEnable(false)));
        const auto closedWrites=dspWrites;
        const auto closedSync=synchronizeCalls;
        forbidMmio=true;
        CHECK(bridge.ResetDormantClosedSession());
        CHECK(!bridge.CanStartBeforeEnable());
        CHECK(dspWrites==closedWrites && synchronizeCalls==closedSync);
        // A new prepared/D0 lifetime needs a fresh GlkBoot and fresh grants.
        CHECK(bridge.BindDormant(binding,&second));
        CHECK(!bridge.CanStartBeforeEnable());
        CHECK(bridge.UnbindDormant());
        forbidMmio=false;
        FrameworkDeleteChildren();
    }

    // Failed D0Entry cleanup must also clear a granted boot-start lifetime
    // before ReleaseHardware can later retire/reassign the mapped BAR.
    Reset(); {
        GlkBoot boot;
        IpcInterrupt bridge;
        CHECK(NT_SUCCESS(bridge.CreateDormant(&checks)));
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={};
        CM_PARTIAL_RESOURCE_DESCRIPTOR translated={};
        raw.Type=CmResourceTypeInterrupt; translated.Type=CmResourceTypeInterrupt;
        PnpDormantInterruptBinding binding{};
        binding.gate=&accessGate; binding.dsp=dsp.data(); binding.dspLength=0x100000;
        binding.raw=&raw; binding.translated=&translated;
        binding.kind=PnpInterruptKind::LineBased;
        CHECK(bridge.BindDormant(binding,&boot));
        CHECK(bridge.GrantBootStart() && bridge.CanStartBeforeEnable());
        CHECK(NT_SUCCESS(BindDmaForBoot(boot))); ColdPower session(boot,bridge,accessGate);
        std::vector<UCHAR> image(286720,0xaa); auto x=IpcXman();
        // H15B owns/reinitializes GCTL, so GCTL=0 is no longer a failure.
        // Use malformed GCAP to keep this cleanup regression deterministic.
        Put(hda,0,2,0xffff);
        CHECK(!NT_SUCCESS(session.Enter(&checks,hda.data(),0x4000,dsp.data(),0x100000,
                                       image.data(),image.size(),x.data(),x.size(),20)));
        CHECK(session.CanReleaseMappings());
        const auto writesAfterCleanup=dspWrites;
        const auto syncAfterCleanup=synchronizeCalls;
        forbidMmio=true;
        CHECK(bridge.ResetDormantClosedSession());
        CHECK(!bridge.CanStartBeforeEnable());
        CHECK(dspWrites==writesAfterCleanup && synchronizeCalls==syncAfterCleanup);
        forbidMmio=false;
        FrameworkDeleteChildren();
    }
    Reset(); { GlkBoot boot; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge; CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt},translated=raw;
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&translated,&boot,dsp.data(),0x100000)));
        CHECK(!bridge.Arm()); CHECK(NT_SUCCESS(FrameworkEnable(true)));
        CHECK(NT_SUCCESS(Prepare(boot))); CHECK(boot.Transfer().commandReady); CHECK(bridge.Arm() && bridge.Running());
        CHECK((Get(dsp,8,4)&1) && (Get(dsp,0x50,4)&3)==1);
        CHECK(!Interrupt() && !queued); Notify(); CHECK(Interrupt() && queued);
        CHECK(!(Get(dsp,8,4)&1) && !(Get(dsp,0x50,4)&3)); CHECK(!Interrupt()); RunWork();
        phaser360::sof::IpcNotification event; CHECK(bridge.Pop(&event) && event.acknowledged); CHECK(bridge.Running());
        std::vector<UCHAR> q(8,0),reply(12,0); IpcPut(q,0,8); IpcPut(q,4,0x30020000);
        CHECK(bridge.Command(q.data(),8,0x10000000,reply.data(),12).status==phaser360::sof::CommandStatus::Ok);
        CHECK(bridge.Running()); Notify(); CHECK(Interrupt() && queued);
        CHECK(bridge.Stop()); CHECK(boot.Shutdown()); unmapped=true;
        RunWork(); CHECK(!bridge.Arm() && !bridge.Running() && !bridge.Pop(&event));
        CHECK(bridge.Command(q.data(),8,0x10000000,reply.data(),12).status==phaser360::sof::CommandStatus::State);
        CHECK(!Interrupt()); CHECK(NT_SUCCESS(FrameworkEnable(false))); FrameworkDeleteChildren();
    }
    for(unsigned failure=1;failure<=4;++failure) {
        Reset(); GlkBoot boot; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge; CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        createFailure=failure;
        CHECK(!NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        CHECK(live==0 && dspWrites==0 && !bridge.Arm() && !bridge.Stop());
    }
    Reset(); { GlkBoot boot; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge; CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        CHECK(NT_SUCCESS(FrameworkEnable(true))); CHECK(NT_SUCCESS(Prepare(boot))); CHECK(boot.Transfer().commandReady);
        dropIrqUnmask=true; CHECK(!bridge.Arm() && !bridge.Running());
        CHECK(!(Get(dsp,8,4)&1) && !(Get(dsp,0x50,4)&3)); CHECK(!Interrupt());
        CHECK(bridge.Stop()); CHECK(boot.Shutdown()); CHECK(NT_SUCCESS(FrameworkEnable(false))); FrameworkDeleteChildren();
    }
    Reset(); { GlkBoot boot; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge; CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        CHECK(NT_SUCCESS(FrameworkEnable(true))); CHECK(NT_SUCCESS(Prepare(boot))); CHECK(boot.Transfer().commandReady);
        CHECK(bridge.Arm()); Notify(); IpcPut(dsp,0xc,2); auto before=dspWrites;
        CHECK(!Interrupt() && !queued && dspWrites==before); IpcPut(dsp,0xc,1);
        IpcPut(dsp,0x81004,0xdead0000); CHECK(Interrupt()); RunWork();
        CHECK(!bridge.Running() && !bridge.Arm()); CHECK(!(Get(dsp,8,4)&1));
        CHECK(bridge.Stop()); CHECK(boot.Shutdown()); CHECK(NT_SUCCESS(FrameworkEnable(false))); FrameworkDeleteChildren();
    }
    Reset(); { GlkBoot boot; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge; CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        CHECK(NT_SUCCESS(FrameworkEnable(true))); CHECK(NT_SUCCESS(Prepare(boot))); CHECK(boot.Transfer().commandReady);
        CHECK(bridge.Arm()); dropIrqMask=true; CHECK(!bridge.Stop());
        CHECK(Get(dsp,8,4)&1); dropIrqMask=false; CHECK(bridge.Stop());
        CHECK(boot.Shutdown()); CHECK(NT_SUCCESS(FrameworkEnable(false))); FrameworkDeleteChildren();
    }
    // D0Entry runs BEFORE the framework connects/enables the interrupt.
    for(unsigned mode=0;mode<6;++mode) {
        Reset(); GlkBoot boot; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge; CHECK(NT_SUCCESS(BindDmaForBoot(boot))); ColdPower session(boot,bridge,accessGate);
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        std::vector<UCHAR> image(286720,0xaa); auto x=IpcXman();
        if(mode==1) Put(hda,0,2,0xffff); // malformed GCAP before DSP mutation
        if(mode==2) missingReady=true;
        if(mode==3) stuckRun=true; // failed DMA stop must retain all buffers
        CHECK(!session.CanReleaseMappings());
        const auto result=session.Enter(&checks,hda.data(),0x4000,dsp.data(),0x100000,
                                        image.data(),image.size(),x.data(),x.size(),20);
        if(mode>=1 && mode<=3) {
            CHECK(!NT_SUCCESS(result) && !connected);
            CHECK(session.CanReleaseMappings()==(mode!=3));
            if(mode==3) {
                CHECK(live==4 && dmaLive==3); auto before=dspWrites;
                CHECK(!session.RetryEarlyCleanup() && dspWrites==before && live==4 && dmaLive==3);
                stuckRun=false; CHECK(session.RetryEarlyCleanup());
            }
            CHECK(live==4 && dmaLive==3 && session.CanReleaseMappings());
            unmapped=true; CHECK(!bridge.Arm() && bridge.Stop());
            CHECK(NT_SUCCESS(FrameworkEnable(true))); // canceled callback does no MMIO
            CHECK(NT_SUCCESS(FrameworkEnable(false))); FrameworkDeleteChildren();
            continue;
        }
        CHECK(NT_SUCCESS(result) && session.TransferEvidence().commandReady && live==4 && dmaLive==3);
        CHECK(NT_SUCCESS(FrameworkEnable(true)));
        CHECK(!bridge.CanStartBeforeEnable() && !bridge.CancelBeforeEnable());
        if(mode==4) dropIrqUnmask=true;
        const auto armed=session.AfterInterruptsEnabled();
        CHECK(NT_SUCCESS(armed)==(mode!=4));
        if(mode!=4) {
            Notify(); CHECK(Interrupt() && queued);
            if(mode==5) {
                Put(dsp,8,4,1); dropIrqMask=true;
                CHECK(!session.BeforeInterruptsDisabled() && !session.CanReleaseMappings());
                CHECK((Get(dsp,4,4)&0x10000)!=0); // DSP still running
                dropIrqMask=false;
            }
            CHECK(session.BeforeInterruptsDisabled());
        }
        CHECK(session.CanReleaseMappings());
        CHECK(NT_SUCCESS(FrameworkEnable(false))); unmapped=true;
        if(queued) RunWork();
        CHECK(session.BeforeInterruptsDisabled() && bridge.Stop()); // no disconnected synchronization
        CHECK(!NT_SUCCESS(session.Enter(&checks,hda.data(),0x4000,dsp.data(),0x100000,
                                        image.data(),image.size(),x.data(),x.size(),20)));
        FrameworkDeleteChildren();
    }
    // Reuse the framework interrupt only after old work and power exit finish.
    for(unsigned fail=0;fail<2;++fail) {
        Reset(); GlkBoot first,second; CHECK(first.BindAccessGate(&accessGate)); IpcInterrupt bridge; CHECK(NT_SUCCESS(BindDmaForBoot(first))); ColdPower session(first,bridge,accessGate);
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&first,dsp.data(),0x100000)));
        std::vector<UCHAR> image(286720,0xaa); auto x=IpcXman();
        auto enter=[&]() { return session.Enter(&checks,hda.data(),0x4000,dsp.data(),0x100000,
                                               image.data(),image.size(),x.data(),x.size(),20); };
        CHECK(NT_SUCCESS(enter())); CHECK(NT_SUCCESS(FrameworkEnable(true)));
        CHECK(NT_SUCCESS(session.AfterInterruptsEnabled()));
        CHECK(!session.NextD0(second,dsp.data(),0x100000));
        Notify(); CHECK(Interrupt() && queued); CHECK(session.BeforeInterruptsDisabled());
        CHECK(!session.NextD0(second,dsp.data(),0x100000)); // not disabled
        CHECK(NT_SUCCESS(FrameworkEnable(false))); unmapped=true;
        CHECK(!queued && cancelCalls==1 && flushCalls==1);
        CHECK(!session.NextD0(first,dsp.data(),0x100000)); // single-attempt owner
        CHECK(!session.NextD0(second,dsp.data()+1,0x100000));
        irql=2; CHECK(!session.NextD0(second,dsp.data(),0x100000)); irql=0;
        CHECK(session.NextD0(second,dsp.data(),0x100000)); // no MMIO while powered off
        CHECK(!session.CanReleaseMappings() && !session.TransferEvidence().commandReady);
        // Simulate fresh D0 register state while preserving the WDF objects.
        unmapped=false; hda.assign(0x4000,0); dsp.assign(0x100000,0);
        Put(hda,0,2,0x6701); Put(hda,8,4,1); Put(hda,0x14,4,0x500);
        Put(hda,0x500,4,0x10030700); Put(hda,0x700,4,0x10040000); Put(hda,0x504,4,0x40000000);
        missingReady=(fail!=0);
        const auto result=enter(); CHECK(NT_SUCCESS(result)==(fail==0));
        if(!fail) {
            CHECK(NT_SUCCESS(FrameworkEnable(true))); CHECK(NT_SUCCESS(session.AfterInterruptsEnabled()));
            Notify(); CHECK(Interrupt()); RunWork(); phaser360::sof::IpcNotification event;
            CHECK(bridge.Pop(&event) && event.acknowledged);
            CHECK(session.BeforeInterruptsDisabled()); CHECK(NT_SUCCESS(FrameworkEnable(false)));
        } else {
            CHECK(session.CanReleaseMappings() && !connected && live==4 && dmaLive==3);
        }
        unmapped=true; CHECK(bridge.Stop()); FrameworkDeleteChildren();
    }
    for(unsigned mode=0;mode<3;++mode) {
        Reset(); GlkBoot boot,next; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge;
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        CHECK(!bridge.DrainStopped() && cancelCalls==0 && flushCalls==0);
        CHECK(NT_SUCCESS(FrameworkEnable(true))); CHECK(NT_SUCCESS(Prepare(boot)));
        CHECK(boot.Transfer().commandReady && bridge.Arm()); Notify(); CHECK(Interrupt());
        if(mode==1) RunDpc(); // DPC finished, worker still queued
        if(mode==2) finishDpcDuringCancel=true; // model DPC finishing during Cancel(TRUE)
        dropIrqMask=true; Put(dsp,8,4,1); CHECK(!bridge.Stop());
        CHECK(!bridge.DrainStopped() && cancelCalls==0); dropIrqMask=false;
        CHECK(bridge.Stop()); CHECK(NT_SUCCESS(FrameworkEnable(false)));
        CHECK(!bridge.RebindStopped(&next,dsp.data(),0x100000));
        unmapped=true; CHECK(bridge.DrainStopped()); // drain never accesses MMIO
        CHECK(!queued && !dpcQueued && !workQueued && cancelCalls==1 && flushCalls==1);
        CHECK(bridge.DrainStopped() && cancelCalls==1 && flushCalls==1);
        unmapped=false; CHECK(boot.Shutdown()); unmapped=true;
        FrameworkDeleteChildren();
    }
    // The official KMDF implementation can skip Disable when Enable failed.
    // Model framework disconnect directly: do NOT manufacture a Disable callback.
    for(unsigned mode=0;mode<4;++mode) {
        Reset(); GlkBoot boot,next; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge; CHECK(NT_SUCCESS(BindDmaForBoot(boot))); ColdPower session(boot,bridge,accessGate);
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        CHECK(!session.AfterInterruptsDisconnected()); // Fresh, no boot
        CHECK(!bridge.Stop() && synchronizeCalls==0); // not connected yet
        std::vector<UCHAR> image(286720,0xaa); auto x=IpcXman();
        CHECK(NT_SUCCESS(session.Enter(&checks,hda.data(),0x4000,dsp.data(),0x100000,
                                      image.data(),image.size(),x.data(),x.size(),20)));
        Put(dsp,8,4,1); dropIrqMask=true;
        if(mode!=3) CHECK(!NT_SUCCESS(FrameworkEnable(true)) && !queued);
        // mode 3: framework connection itself failed; Enable was never invoked.
        connected=false;
        if(mode!=1) dropIrqMask=false;
        irql=2; CHECK(!bridge.StopAfterDisconnect() && !session.AfterInterruptsDisconnected()); irql=0;
        CHECK(bridge.StopAfterDisconnect()==(mode!=1));
        forbidMmio=true;
        CHECK(!bridge.Arm() && !bridge.Running());
        CHECK(bridge.DrainStopped() && !queued && synchronizeCalls==0);
        CHECK(!session.CanReleaseMappings()); // closed IRQ is not DSP shutdown
        forbidMmio=false;
        if(mode==1) {
            CHECK(!session.AfterInterruptsDisconnected() && !session.CanReleaseMappings());
            CHECK((Get(dsp,4,4)&0x10000)!=0); // masking failure leaves DSP running
            CHECK(!session.NextD0(next,dsp.data(),0x100000));
            CHECK(!bridge.RebindStopped(&next,dsp.data(),0x100000));
            CHECK(synchronizeCalls==0);
            // Retry only while this simulated D0Exit still owns accessible hardware.
            dropIrqMask=false;
            CHECK(session.AfterInterruptsDisconnected() && session.CanReleaseMappings());
        } else {
            if(mode==2) {
                power=false; CHECK(!session.AfterInterruptsDisconnected());
                CHECK(!session.CanReleaseMappings()); power=true;
            }
            CHECK(session.AfterInterruptsDisconnected() && session.CanReleaseMappings());
        }
        forbidMmio=true; CHECK(session.AfterInterruptsDisconnected());
        CHECK(synchronizeCalls==0 && live==4 && dmaLive==3);
        FrameworkDeleteChildren();
    }
    // Regression: failed pre-disable Stop must close admission even though its
    // hardware mask failed. Queued DPC/work callbacks cannot touch the old IRQ.
    for(unsigned mode=0;mode<4;++mode) {
        Reset(); GlkBoot boot,next; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge; CHECK(NT_SUCCESS(BindDmaForBoot(boot))); ColdPower session(boot,bridge,accessGate);
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        std::vector<UCHAR> image(286720,0xaa); auto x=IpcXman();
        CHECK(NT_SUCCESS(session.Enter(&checks,hda.data(),0x4000,dsp.data(),0x100000,
                                      image.data(),image.size(),x.data(),x.size(),20)));
        CHECK(NT_SUCCESS(FrameworkEnable(true)) && NT_SUCCESS(session.AfterInterruptsEnabled()));
        CHECK(!bridge.StopAfterDisconnect());
        Notify(); CHECK(Interrupt() && queued);
        if(mode==1) RunDpc(); // worker is already queued
        if(mode==2) finishDpcDuringCancel=true; // DPC completes while Cancel waits
        Put(dsp,8,4,1); dropIrqMask=true;
        CHECK(!session.BeforeInterruptsDisabled() && !session.CanReleaseMappings());
        const auto before=synchronizeCalls;
        forbidMmio=true;
        CHECK(!bridge.Arm() && !bridge.Running());
        phaser360::sof::IpcNotification event;
        CHECK(!bridge.Pop(&event));
        (void)bridge.Command(nullptr,0,0,nullptr,0);
        CHECK(synchronizeCalls==before && !Interrupt());
        forbidMmio=false;
        if(mode!=3) dropIrqMask=false;
        CHECK(NT_SUCCESS(FrameworkEnable(false))==(mode!=3));
        forbidMmio=true;
        if(mode<2) RunWork(); // before post-disable adoption, must also be safe
        CHECK(bridge.StopAfterDisconnect()==(mode!=3));
        CHECK(bridge.DrainStopped() && !queued && synchronizeCalls==before);
        CHECK(!session.CanReleaseMappings());
        if(mode==3) {
            CHECK(!session.AfterInterruptsDisconnected() && !session.CanReleaseMappings());
            CHECK(!session.NextD0(next,dsp.data(),0x100000));
            CHECK(!bridge.Stop() && synchronizeCalls==before);
            // Restore only the simulated device for test-fixture teardown.
            forbidMmio=false; dropIrqMask=false; CHECK(boot.Shutdown());
        } else {
            forbidMmio=false;
            CHECK(session.AfterInterruptsDisconnected() && session.CanReleaseMappings());
            forbidMmio=true; CHECK(session.AfterInterruptsDisconnected());
            CHECK(synchronizeCalls==before && live==4 && dmaLive==3);
        }
        FrameworkDeleteChildren();
    }
    // Missing pre-disable admission closure is not silently accepted for an
    // already armed session. This is a negative contract test, with no queued work.
    Reset(); {
        GlkBoot boot; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge; CHECK(NT_SUCCESS(BindDmaForBoot(boot))); ColdPower session(boot,bridge,accessGate);
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        std::vector<UCHAR> image(286720,0xaa); auto x=IpcXman();
        CHECK(NT_SUCCESS(session.Enter(&checks,hda.data(),0x4000,dsp.data(),0x100000,
                                      image.data(),image.size(),x.data(),x.size(),20)));
        CHECK(NT_SUCCESS(FrameworkEnable(true)) && NT_SUCCESS(session.AfterInterruptsEnabled()));
        CHECK(NT_SUCCESS(FrameworkEnable(false)));
        forbidMmio=true; const auto before=synchronizeCalls;
        CHECK(!bridge.StopAfterDisconnect() && !bridge.DrainStopped());
        CHECK(!session.AfterInterruptsDisconnected() && !session.CanReleaseMappings());
        CHECK(synchronizeCalls==before && !queued);
        // Test-fixture teardown, not a production recovery path.
        forbidMmio=false; CHECK(boot.Shutdown()); FrameworkDeleteChildren();
    }
    // M0.6.15H4: composed callback consumer, normal start/stop including
    // a failed pre-disable Stop recovered only after framework disconnect.
    Reset(); {
        GlkBoot boot; IpcInterrupt bridge; PinnedFirmware firmware;
        CHECK(NT_SUCCESS(BindDmaForBoot(boot))); DeviceLifecycle lifecycle(bridge,boot,firmware,accessGate);
        CHECK(NT_SUCCESS(lifecycle.CreateInterruptShell(&checks)));
        auto ops=lifecycle.Ops(); CHECK(ops.context==&lifecycle);
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={},translated={};
        raw.Type=CmResourceTypeInterrupt; translated.Type=CmResourceTypeInterrupt;
        PnpResourceView view{};
        view.hda=hda.data(); view.hdaLength=0x4000;
        view.dsp=dsp.data(); view.dspLength=0x100000;
        view.interruptCount=1;
        PnpDormantInterruptBinding binding{};
        binding.gate=&accessGate; binding.dsp=dsp.data(); binding.dspLength=0x100000;
        binding.raw=&raw; binding.translated=&translated;
        CHECK(NT_SUCCESS(ops.prepared(ops.context,view,binding)));
        CHECK(lifecycle.Bound() && !lifecycle.D0Consumed());
        CHECK(NT_SUCCESS(ops.d0Entry(ops.context,&checks,view)));
        CHECK(NT_SUCCESS(FrameworkEnable(true)));
        CHECK(NT_SUCCESS(ops.postInterruptsEnabled(ops.context)));
        Notify(); CHECK(Interrupt() && queued);
        RunWork(); CHECK(!queued); // notification consumed and IRQ rearmed

        dropIrqMask=true;
        CHECK(!NT_SUCCESS(ops.preInterruptsDisabled(ops.context)));
        dropIrqMask=false;
        CHECK(NT_SUCCESS(FrameworkEnable(false)));
        CHECK(NT_SUCCESS(ops.d0Exit(ops.context)));
        CHECK(!lifecycle.Bound() && lifecycle.D0Consumed());
        CHECK(NT_SUCCESS(ops.release(ops.context)));
        // One GlkBoot is one attempt; H4 deliberately rejects a second D0.
        CHECK(!NT_SUCCESS(ops.d0Entry(ops.context,&checks,view)));
        CHECK(!queued && live==4 && dmaLive==3);
        FrameworkDeleteChildren();
    }

    // D0Entry failure after ColdPower starts must clean/reset without a
    // synthetic D0Exit, because KMDF does not provide one for failed entry.
    Reset(); {
        GlkBoot boot; IpcInterrupt bridge; PinnedFirmware firmware;
        CHECK(NT_SUCCESS(BindDmaForBoot(boot))); DeviceLifecycle lifecycle(bridge,boot,firmware,accessGate);
        CHECK(NT_SUCCESS(lifecycle.CreateInterruptShell(&checks)));
        auto ops=lifecycle.Ops();
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={},translated={};
        raw.Type=CmResourceTypeInterrupt; translated.Type=CmResourceTypeInterrupt;
        PnpResourceView view{};
        view.hda=hda.data(); view.hdaLength=0x4000;
        view.dsp=dsp.data(); view.dspLength=0x100000;
        PnpDormantInterruptBinding binding{};
        binding.gate=&accessGate; binding.dsp=dsp.data(); binding.dspLength=0x100000;
        binding.raw=&raw; binding.translated=&translated;
        CHECK(NT_SUCCESS(ops.prepared(ops.context,view,binding)));
        Put(hda,0,2,0xffff); // H15B: malformed GCAP remains a hard HDA failure
        CHECK(!NT_SUCCESS(ops.d0Entry(ops.context,&checks,view)));
        CHECK(!lifecycle.Bound() && lifecycle.D0Consumed());
        CHECK(NT_SUCCESS(ops.release(ops.context)));
        CHECK(live==4 && dmaLive==3 && dspWrites==0);
        FrameworkDeleteChildren();
    }

    // Firmware admission can fail before ColdPower is entered. In that case H4
    // must unbind the still-dormant resource lifetime directly.
    Reset(); {
        GlkBoot boot; IpcInterrupt bridge; PinnedFirmware firmware;
        CHECK(NT_SUCCESS(BindDmaForBoot(boot))); DeviceLifecycle lifecycle(bridge,boot,firmware,accessGate);
        CHECK(NT_SUCCESS(lifecycle.CreateInterruptShell(&checks)));
        auto ops=lifecycle.Ops();
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={},translated={};
        raw.Type=CmResourceTypeInterrupt; translated.Type=CmResourceTypeInterrupt;
        PnpResourceView view{};
        view.hda=hda.data(); view.hdaLength=0x4000;
        view.dsp=dsp.data(); view.dspLength=0x100000;
        PnpDormantInterruptBinding binding{};
        binding.gate=&accessGate; binding.dsp=dsp.data(); binding.dspLength=0x100000;
        binding.raw=&raw; binding.translated=&translated;
        CHECK(NT_SUCCESS(ops.prepared(ops.context,view,binding)));
        rejectPinnedEnter=true;
        const auto writesBefore=dspWrites;
        CHECK(ops.d0Entry(ops.context,&checks,view)==STATUS_INVALID_IMAGE_HASH);
        CHECK(!lifecycle.Bound() && lifecycle.D0Consumed());
        CHECK(NT_SUCCESS(ops.release(ops.context)) && dspWrites==writesBefore);
        FrameworkDeleteChildren();
    }

    // SurpriseRemoval can win after command-ready boot but before framework
    // Enable. H4 must abandon the terminal session inside D0Entry itself.
    Reset(); {
        HardwareAccessGate terminalGate; CHECK(terminalGate.OpenForPrepare());
        GlkBoot boot; IpcInterrupt bridge; PinnedFirmware firmware;
        CHECK(NT_SUCCESS(BindDmaForBoot(boot))); DeviceLifecycle lifecycle(bridge,boot,firmware,terminalGate);
        CHECK(NT_SUCCESS(lifecycle.CreateInterruptShell(&checks)));
        auto ops=lifecycle.Ops();
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={},translated={};
        raw.Type=CmResourceTypeInterrupt; translated.Type=CmResourceTypeInterrupt;
        PnpResourceView view{};
        view.hda=hda.data(); view.hdaLength=0x4000;
        view.dsp=dsp.data(); view.dspLength=0x100000;
        PnpDormantInterruptBinding binding{};
        binding.gate=&terminalGate; binding.dsp=dsp.data(); binding.dspLength=0x100000;
        binding.raw=&raw; binding.translated=&translated;
        CHECK(NT_SUCCESS(ops.prepared(ops.context,view,binding)));
        removeDuringPinnedEnter=&terminalGate;
        CHECK(!NT_SUCCESS(ops.d0Entry(ops.context,&checks,view)));
        CHECK(terminalGate.Removed() && lifecycle.Removed());
        CHECK(!lifecycle.Bound() && lifecycle.D0Consumed());
        CHECK(NT_SUCCESS(ops.release(ops.context)));
        removeDuringPinnedEnter=nullptr;
        CHECK(live==4 && dmaLive==3);
        FrameworkDeleteChildren();
    }

    // Terminal surprise removal: after the gate closes, all remaining teardown
    // is software-only. Disable may report failure because no hardware mask can
    // be confirmed; D0Exit drains and abandons stale pointers without MMIO.
    Reset(); {
        HardwareAccessGate terminalGate; CHECK(terminalGate.OpenForPrepare());
        GlkBoot boot; IpcInterrupt bridge; PinnedFirmware firmware;
        CHECK(NT_SUCCESS(BindDmaForBoot(boot))); DeviceLifecycle lifecycle(bridge,boot,firmware,terminalGate);
        CHECK(NT_SUCCESS(lifecycle.CreateInterruptShell(&checks)));
        auto ops=lifecycle.Ops();
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={},translated={};
        raw.Type=CmResourceTypeInterrupt; translated.Type=CmResourceTypeInterrupt;
        PnpResourceView view{};
        view.hda=hda.data(); view.hdaLength=0x4000;
        view.dsp=dsp.data(); view.dspLength=0x100000;
        PnpDormantInterruptBinding binding{};
        binding.gate=&terminalGate; binding.dsp=dsp.data(); binding.dspLength=0x100000;
        binding.raw=&raw; binding.translated=&translated;
        CHECK(NT_SUCCESS(ops.prepared(ops.context,view,binding)));
        CHECK(NT_SUCCESS(ops.d0Entry(ops.context,&checks,view)));
        CHECK(NT_SUCCESS(FrameworkEnable(true)));
        CHECK(NT_SUCCESS(ops.postInterruptsEnabled(ops.context)));
        Notify(); CHECK(Interrupt() && queued);

        terminalGate.SurpriseRemove(); ops.surpriseRemoval(ops.context);
        CHECK(lifecycle.Removed() && terminalGate.Removed());
        const auto writesBefore=dspWrites, syncBefore=synchronizeCalls;
        forbidMmio=true;
        CHECK(NT_SUCCESS(ops.preInterruptsDisabled(ops.context)));
        CHECK(!NT_SUCCESS(FrameworkEnable(false)));
        CHECK(NT_SUCCESS(ops.d0Exit(ops.context)));
        CHECK(NT_SUCCESS(ops.release(ops.context)));
        CHECK(!lifecycle.Bound() && lifecycle.D0Consumed());
        CHECK(dspWrites==writesBefore && synchronizeCalls==syncBefore && !queued);
        forbidMmio=false;
        FrameworkDeleteChildren();
    }

    // M0.6.15H5/H11: fresh GlkBoot+ColdPower ownership for every D0
    // attempt, with an atomic software telemetry mirror verified against the
    // lifecycle state transitions.
    Reset(); {
        IpcInterrupt bridge; PinnedFirmware firmware; TelemetryState telemetry;
        RepeatedDeviceLifecycle lifecycle(bridge,firmware,accessGate,&telemetry);
        CHECK(NT_SUCCESS(lifecycle.CreateInterruptShell(&checks)));
        auto ops=lifecycle.Ops();
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={},translated={};
        raw.Type=CmResourceTypeInterrupt; translated.Type=CmResourceTypeInterrupt;
        raw.Flags=CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        translated.Flags=CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        PnpResourceView view{};
        view.hda=hda.data(); view.hdaLength=0x4000;
        view.dsp=dsp.data(); view.dspLength=0x100000; view.interruptCount=1;
        PnpDormantInterruptBinding binding{};
        binding.gate=&accessGate; binding.dsp=dsp.data(); binding.dspLength=0x100000;
        binding.raw=&raw; binding.translated=&translated;
        binding.kind=PnpInterruptKind::LineBased; binding.messageCount=0;
        CHECK(NT_SUCCESS(ops.prepared(ops.context,view,binding)));
        CHECK(dmaLive==3 && alignmentValue==FILE_4096_BYTE_ALIGNMENT);
        CHECK(lifecycle.PreparedResources() && !lifecycle.ActiveD0());
        TelemetrySnapshotV1 snapshot{};
        telemetry.Snapshot(&snapshot);
        CHECK((snapshot.flags & TelemetryResourcesPrepared)!=0 &&
              (snapshot.flags & TelemetryD0Active)==0 &&
              snapshot.sessionGeneration==0 &&
              snapshot.completedD0==0 && snapshot.failedD0==0);

        CHECK(NT_SUCCESS(ops.d0Entry(ops.context,&checks,view)));
        CHECK(lifecycle.SessionGeneration()==1 && lifecycle.ActiveD0());
        telemetry.Snapshot(&snapshot);
        CHECK((snapshot.flags & TelemetryD0Active)!=0 &&
              snapshot.sessionGeneration==1 &&
              snapshot.lastD0Status==STATUS_SUCCESS);
        CHECK(NT_SUCCESS(FrameworkEnable(true)));
        CHECK(NT_SUCCESS(ops.postInterruptsEnabled(ops.context)));
        Notify(); CHECK(Interrupt() && queued); RunWork(); CHECK(!queued);
        CHECK(NT_SUCCESS(ops.preInterruptsDisabled(ops.context)));
        CHECK(NT_SUCCESS(FrameworkEnable(false)));
        CHECK(NT_SUCCESS(ops.d0Exit(ops.context)));
        CHECK(!lifecycle.ActiveD0() && lifecycle.CompletedD0()==1 &&
              lifecycle.FailedD0()==0 && live==4 && dmaLive==3);
        telemetry.Snapshot(&snapshot);
        CHECK((snapshot.flags & TelemetryD0Active)==0 &&
              snapshot.sessionGeneration==1 &&
              snapshot.completedD0==1 && snapshot.failedD0==0);

        ResetColdRegisters(); missingReady=true;
        CHECK(!NT_SUCCESS(ops.d0Entry(ops.context,&checks,view)));
        CHECK(lifecycle.SessionGeneration()==2 && !lifecycle.ActiveD0() &&
              lifecycle.CompletedD0()==1 && lifecycle.FailedD0()==1 && live==4 && dmaLive==3);
        telemetry.Snapshot(&snapshot);
        CHECK((snapshot.flags & TelemetryD0Active)==0 &&
              snapshot.sessionGeneration==2 &&
              snapshot.completedD0==1 && snapshot.failedD0==1 &&
              snapshot.lastD0Status!=STATUS_SUCCESS);
        missingReady=false;

        ResetColdRegisters();
        CHECK(NT_SUCCESS(ops.d0Entry(ops.context,&checks,view)));
        CHECK(lifecycle.SessionGeneration()==3 && lifecycle.ActiveD0());
        CHECK(NT_SUCCESS(FrameworkEnable(true)));
        CHECK(NT_SUCCESS(ops.postInterruptsEnabled(ops.context)));
        Notify(); CHECK(Interrupt() && queued); RunWork(); CHECK(!queued);
        CHECK(NT_SUCCESS(ops.preInterruptsDisabled(ops.context)));
        CHECK(NT_SUCCESS(FrameworkEnable(false)));
        CHECK(NT_SUCCESS(ops.d0Exit(ops.context)));
        CHECK(!lifecycle.ActiveD0() && lifecycle.CompletedD0()==2 &&
              lifecycle.FailedD0()==1 && sessionMemoryCreates==3 && live==4 && dmaLive==3);
        telemetry.Snapshot(&snapshot);
        CHECK((snapshot.flags & TelemetryD0Active)==0 &&
              snapshot.sessionGeneration==3 &&
              snapshot.completedD0==2 && snapshot.failedD0==1 &&
              snapshot.lastD0Status==STATUS_SUCCESS);

        CHECK(NT_SUCCESS(ops.release(ops.context)));
        CHECK(dmaLive==0);
        CHECK(!lifecycle.PreparedResources());
        telemetry.Snapshot(&snapshot);
        CHECK((snapshot.flags & TelemetryResourcesPrepared)==0);

        std::vector<UCHAR> nextHda(0x4000),nextDsp(0x100000);
        PnpResourceView nextView=view;
        nextView.hda=nextHda.data(); nextView.dsp=nextDsp.data();
        auto nextBinding=binding; nextBinding.dsp=nextDsp.data();
        CHECK(NT_SUCCESS(ops.prepared(ops.context,nextView,nextBinding)));
        CHECK(dmaLive==3);
        CHECK(lifecycle.PreparedResources());
        telemetry.Snapshot(&snapshot);
        CHECK((snapshot.flags & TelemetryResourcesPrepared)!=0);
        CHECK(NT_SUCCESS(ops.release(ops.context)));
        CHECK(dmaLive==0);
        FrameworkDeleteChildren();
    }

    // R8: an early cleanup failure is not a lifetime fence against WDF parent
    // disposal. Retain buffers in D0; retire software only at the documented
    // ReleaseHardware boundary (disconnected and powered off by KMDF).
    for(unsigned releaseCase=0;releaseCase<3;++releaseCase) {
        Reset();
        HardwareAccessGate releaseGate; CHECK(releaseGate.OpenForPrepare());
        IpcInterrupt bridge; PinnedFirmware firmware;
        RepeatedDeviceLifecycle lifecycle(bridge,firmware,releaseGate);
        CHECK(NT_SUCCESS(lifecycle.CreateInterruptShell(&checks)));
        auto ops=lifecycle.Ops(); CHECK(ops.releaseAfterHardware!=nullptr);
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={},translated={};
        raw.Type=CmResourceTypeInterrupt; translated.Type=CmResourceTypeInterrupt;
        PnpResourceView view{};
        view.hda=hda.data(); view.hdaLength=0x4000;
        view.dsp=dsp.data(); view.dspLength=0x100000;
        PnpDormantInterruptBinding binding{};
        binding.gate=&releaseGate; binding.dsp=dsp.data(); binding.dspLength=0x100000;
        binding.raw=&raw; binding.translated=&translated;
        CHECK(NT_SUCCESS(ops.prepared(ops.context,view,binding)));
        if(releaseCase==0) stickRunAfterStart=true;
        const auto entry=ops.d0Entry(ops.context,&checks,view);
        if(releaseCase==0) {
            CHECK(!NT_SUCCESS(entry) && lifecycle.ActiveD0() && dmaLive==3);
        } else {
            CHECK(NT_SUCCESS(entry));
            if(releaseCase==1) { dropIrqMask=true; Put(dsp,8,4,1); }
            const auto enable=FrameworkEnable(true);
            if(releaseCase==1) {
                CHECK(!NT_SUCCESS(enable));
                connected=false; // framework disconnect after failed Enable
            } else {
                CHECK(NT_SUCCESS(enable));
                CHECK(NT_SUCCESS(ops.postInterruptsEnabled(ops.context)));
                Notify(); CHECK(Interrupt() && queued);
                dropIrqMask=true;
                Put(dsp,8,4,1);
                CHECK(!NT_SUCCESS(ops.preInterruptsDisabled(ops.context)));
                CHECK(!NT_SUCCESS(FrameworkEnable(false)));
                CHECK(!NT_SUCCESS(ops.d0Exit(ops.context)));
            }
        }
        CHECK(!NT_SUCCESS(ops.release(ops.context))); // not a power-off witness
        CHECK(lifecycle.ActiveD0() && dmaLive==3);
        CHECK(!bridge.ReleaseAfterHardware()); // terminal gate is required
        const auto writes=dspWrites,syncs=synchronizeCalls;
        const auto pciReads=pciReadCalls,pciWrites=pciWriteCalls;
        const auto cancelled=cancelCalls,flushed=flushCalls;
        CHECK(!connected && releaseGate.CloseForRelease());
        forbidMmio=true;
        CHECK(NT_SUCCESS(ops.releaseAfterHardware(ops.context)));
        CHECK(releaseGate.Removed() && !lifecycle.ActiveD0() &&
              !lifecycle.PreparedResources() && !queued);
        CHECK(dspWrites==writes && synchronizeCalls==syncs &&
              pciReadCalls==pciReads && pciWriteCalls==pciWrites);
        CHECK(cancelCalls==cancelled+1 && flushCalls==flushed+1);
        CHECK(dmaLive==3); // actual buffers remain owned by WDF until disposal
        FrameworkDeleteChildren(); CHECK(dmaLive==0);
        stickRunAfterStart=false;
    }

    // Per-D0 allocation failure is retryable and cannot leave an IRQ binding.
    Reset(); {
        IpcInterrupt bridge; PinnedFirmware firmware;
        RepeatedDeviceLifecycle lifecycle(bridge,firmware,accessGate);
        CHECK(NT_SUCCESS(lifecycle.CreateInterruptShell(&checks)));
        auto ops=lifecycle.Ops();
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={},translated={};
        raw.Type=CmResourceTypeInterrupt; translated.Type=CmResourceTypeInterrupt;
        PnpResourceView view{};
        view.hda=hda.data(); view.hdaLength=0x4000;
        view.dsp=dsp.data(); view.dspLength=0x100000;
        PnpDormantInterruptBinding binding{};
        binding.gate=&accessGate; binding.dsp=dsp.data(); binding.dspLength=0x100000;
        binding.raw=&raw; binding.translated=&translated;
        CHECK(NT_SUCCESS(ops.prepared(ops.context,view,binding)));
        createFailure=5;
        CHECK(ops.d0Entry(ops.context,&checks,view)==STATUS_INSUFFICIENT_RESOURCES);
        CHECK(!lifecycle.ActiveD0() && lifecycle.SessionGeneration()==0 &&
              sessionMemoryCreates==0 && live==4 && dmaLive==3);
        createFailure=0;
        CHECK(NT_SUCCESS(ops.d0Entry(ops.context,&checks,view)));
        CHECK(lifecycle.SessionGeneration()==1 && sessionMemoryCreates==1);
        CHECK(NT_SUCCESS(FrameworkEnable(true)));
        CHECK(NT_SUCCESS(ops.postInterruptsEnabled(ops.context)));
        CHECK(NT_SUCCESS(ops.preInterruptsDisabled(ops.context)));
        CHECK(NT_SUCCESS(FrameworkEnable(false)));
        CHECK(NT_SUCCESS(ops.d0Exit(ops.context)) && live==4 && dmaLive==3);
        CHECK(NT_SUCCESS(ops.release(ops.context)));
        CHECK(dmaLive==0);
        FrameworkDeleteChildren();
    }

    // Terminal removal after an earlier clean D0. Generation #2 is abandoned
    // only after framework disconnect; no register access is allowed afterward.
    Reset(); {
        HardwareAccessGate terminalGate; CHECK(terminalGate.OpenForPrepare());
        IpcInterrupt bridge; PinnedFirmware firmware;
        RepeatedDeviceLifecycle lifecycle(bridge,firmware,terminalGate);
        CHECK(NT_SUCCESS(lifecycle.CreateInterruptShell(&checks)));
        auto ops=lifecycle.Ops();
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={},translated={};
        raw.Type=CmResourceTypeInterrupt; translated.Type=CmResourceTypeInterrupt;
        PnpResourceView view{};
        view.hda=hda.data(); view.hdaLength=0x4000;
        view.dsp=dsp.data(); view.dspLength=0x100000;
        PnpDormantInterruptBinding binding{};
        binding.gate=&terminalGate; binding.dsp=dsp.data(); binding.dspLength=0x100000;
        binding.raw=&raw; binding.translated=&translated;
        CHECK(NT_SUCCESS(ops.prepared(ops.context,view,binding)));

        CHECK(NT_SUCCESS(ops.d0Entry(ops.context,&checks,view)));
        CHECK(NT_SUCCESS(FrameworkEnable(true)));
        CHECK(NT_SUCCESS(ops.postInterruptsEnabled(ops.context)));
        CHECK(NT_SUCCESS(ops.preInterruptsDisabled(ops.context)));
        CHECK(NT_SUCCESS(FrameworkEnable(false)));
        CHECK(NT_SUCCESS(ops.d0Exit(ops.context)));
        CHECK(lifecycle.CompletedD0()==1 && lifecycle.SessionGeneration()==1);
        ResetColdRegisters();

        CHECK(NT_SUCCESS(ops.d0Entry(ops.context,&checks,view)));
        CHECK(NT_SUCCESS(FrameworkEnable(true)));
        CHECK(NT_SUCCESS(ops.postInterruptsEnabled(ops.context)));
        Notify(); CHECK(Interrupt() && queued);
        terminalGate.SurpriseRemove(); ops.surpriseRemoval(ops.context);
        const auto writesBefore=dspWrites, syncBefore=synchronizeCalls;
        forbidMmio=true;
        CHECK(NT_SUCCESS(ops.preInterruptsDisabled(ops.context)));
        CHECK(!NT_SUCCESS(FrameworkEnable(false)));
        CHECK(NT_SUCCESS(ops.d0Exit(ops.context)));
        CHECK(!lifecycle.ActiveD0() && lifecycle.Removed() &&
              lifecycle.SessionGeneration()==2 && sessionMemoryCreates==2);
        CHECK(dspWrites==writesBefore && synchronizeCalls==syncBefore && !queued);
        CHECK(!NT_SUCCESS(ops.d0Entry(ops.context,&checks,view)));
        CHECK(NT_SUCCESS(ops.release(ops.context)));
        forbidMmio=false;
        FrameworkDeleteChildren();
    }

    // Terminal race after commandReady but before framework Enable: the fresh
    // session is abandoned inside failed D0Entry, with no later D0Exit required.
    Reset(); {
        HardwareAccessGate terminalGate; CHECK(terminalGate.OpenForPrepare());
        IpcInterrupt bridge; PinnedFirmware firmware;
        RepeatedDeviceLifecycle lifecycle(bridge,firmware,terminalGate);
        CHECK(NT_SUCCESS(lifecycle.CreateInterruptShell(&checks)));
        auto ops=lifecycle.Ops();
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={},translated={};
        raw.Type=CmResourceTypeInterrupt; translated.Type=CmResourceTypeInterrupt;
        PnpResourceView view{};
        view.hda=hda.data(); view.hdaLength=0x4000;
        view.dsp=dsp.data(); view.dspLength=0x100000;
        PnpDormantInterruptBinding binding{};
        binding.gate=&terminalGate; binding.dsp=dsp.data(); binding.dspLength=0x100000;
        binding.raw=&raw; binding.translated=&translated;
        CHECK(NT_SUCCESS(ops.prepared(ops.context,view,binding)));
        removeDuringPinnedEnter=&terminalGate;
        CHECK(!NT_SUCCESS(ops.d0Entry(ops.context,&checks,view)));
        CHECK(lifecycle.Removed() && !lifecycle.ActiveD0() &&
              lifecycle.SessionGeneration()==1 && live==4 && dmaLive==3);
        CHECK(NT_SUCCESS(ops.release(ops.context)));
        removeDuringPinnedEnter=nullptr;
        FrameworkDeleteChildren();
    }

    // M0.6.15A: surprise-removal is terminal for hardware access.
    Reset(); {
        GlkBoot boot; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge;
        CHECK(NT_SUCCESS(BindDmaForBoot(boot))); ColdPower session(boot,bridge,accessGate);
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        std::vector<UCHAR> image(286720,0xaa); auto x=IpcXman();
        CHECK(NT_SUCCESS(session.Enter(&checks,hda.data(),0x4000,dsp.data(),0x100000,
                                      image.data(),image.size(),x.data(),x.size(),20)));
        CHECK(NT_SUCCESS(FrameworkEnable(true)) && NT_SUCCESS(session.AfterInterruptsEnabled()));
        Notify(); CHECK(Interrupt() && queued);
        const auto writesBefore=dspWrites;
        const auto syncBefore=synchronizeCalls;
        accessGate.SurpriseRemove();
        forbidMmio=true;
        CHECK(accessGate.Removed() && !accessGate.Allowed());
        // An ISR racing after the terminal gate must not reach fake MMIO.
        CHECK(!Interrupt());
        CHECK(dspWrites==writesBefore && synchronizeCalls==syncBefore);
        CHECK(bridge.FenceForSurpriseRemoval());
        CHECK(!bridge.Running() && !bridge.Arm());
        phaser360::sof::IpcNotification event;
        CHECK(!bridge.Pop(&event));
        (void)bridge.Command(nullptr,0,0,nullptr,0);
        CHECK(!session.BeforeInterruptsDisabled());
        if(dpcQueued) RunDpc();
        if(workQueued) RunWork();
        CHECK(dspWrites==writesBefore && synchronizeCalls==syncBefore);
        CHECK(!bridge.DrainStopped()); // framework has not disconnected/disabled yet
        CHECK(!NT_SUCCESS(FrameworkEnable(false)));
        CHECK(dspWrites==writesBefore && synchronizeCalls==syncBefore);
        CHECK(bridge.DrainStopped());
        CHECK(!boot.Shutdown());
        CHECK(dspWrites==writesBefore && synchronizeCalls==syncBefore && !queued);
        FrameworkDeleteChildren();
    }
    std::printf("SOF_GLK_BOOT_TESTS=%u PASS; windows_api=SIMULATED; hardware=NONE\n",checks);
}
