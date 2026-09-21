// SPDX-License-Identifier: MIT
// Executes production Windows wrappers against fake WDF/MMIO, not a kernel.
#include "../m062/driver/glk_boot.h"
#include "../m062/driver/ipc_interrupt.h"
#include "../m062/driver/cold_power.h"
#include <vector>
#include "sof_ipc_fixture.h"
#include <cstdio>
#include <cstdlib>
using namespace phaser360::windows;
using phaser360::sof::RomError;
static unsigned checks=0,live=0,dspWrites=0,irql=0,sequence=0;
static uint64_t ticks=100000;
static bool unmapped=false,dropIrqUnmask=false,dropIrqMask=false,irqHeld=false,mutexHeld=false,queued=false;
static bool dpcQueued=false,workQueued=false,finishDpcDuringCancel=false;
static unsigned cancelCalls=0,flushCalls=0;
static WDFDPC dpcHandle=nullptr;
static WDFWORKITEM workHandle=nullptr;
static WDF_DPC_CONFIG dpcConfig={};
static WDF_WORKITEM_CONFIG workConfig={};
static unsigned createFailure=0;
static bool connected=false,forbidMmio=false;
static unsigned synchronizeCalls=0;
static HardwareAccessGate accessGate;
static WDF_INTERRUPT_CONFIG irqConfig={};
static bool irqCreatedWithAssignedDescriptors=false;
static WDFINTERRUPT irqHandle=nullptr;
static WDFWAITLOCK serialHandle=nullptr;
static std::vector<UCHAR> hda(0x4000),dsp(0x100000);
static bool stuckRun=false,noRun=false,power=true,halt=false,missingReady=false,badReady=false,commandTimeout=false;
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
        if(o==0x163) v=Get(b,o,w)&~v;
        if(o==0x160) {
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
    CHECK(mode==KernelMode && !alert && delay->QuadPart==-5000);
    ticks+=5000; return STATUS_SUCCESS;
}
ULONGLONG KeQueryInterruptTime() { return ticks; }
struct FakeObject { unsigned id; std::vector<UCHAR> bytes; };
NTSTATUS WdfDmaEnablerCreate(WDFDEVICE,WDF_DMA_ENABLER_CONFIG*,void*,WDFDMAENABLER* out) {
    *out=new FakeObject{++sequence,{}}; ++live; return STATUS_SUCCESS;
}
NTSTATUS WdfCommonBufferCreateWithConfig(WDFDMAENABLER,size_t n,WDF_COMMON_BUFFER_CONFIG*,void*,WDFCOMMONBUFFER* out) {
    *out=new FakeObject{++sequence,std::vector<UCHAR>(n)}; ++live; return STATUS_SUCCESS;
}
PHYSICAL_ADDRESS WdfCommonBufferGetAlignedLogicalAddress(WDFCOMMONBUFFER b) { return {int64_t(b->id)*0x100000}; }
void* WdfCommonBufferGetAlignedVirtualAddress(WDFCOMMONBUFFER b) { return b->bytes.data(); }
void WdfObjectDelete(FakeObject* b) { CHECK(live>0); --live; delete b; }
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
    CHECK(!queued && !mutexHeld && !irqHeld); WdfObjectDelete(irqHandle); WdfObjectDelete(dpcHandle); WdfObjectDelete(workHandle); WdfObjectDelete(serialHandle);
    irqHandle=nullptr; serialHandle=nullptr;
}
static void Notify() {
    IpcPut(dsp,0x81000,24); IpcPut(dsp,0x81004,0x90020000); IpcPut(dsp,0x81008,0);
    IpcPut(dsp,0x40,0x90020000); IpcPut(dsp,0xc,1);
}
static void Reset() {
    CHECK(accessGate.CloseForRelease());
    CHECK(accessGate.OpenForPrepare());
    dpcQueued=false; workQueued=false; finishDpcDuringCancel=false; cancelCalls=0; flushCalls=0;
    connected=false; forbidMmio=false; synchronizeCalls=0; unmapped=false; dropIrqUnmask=false; dropIrqMask=false; createFailure=0; queued=false;
    irqCreatedWithAssignedDescriptors=false;
    CHECK(live==0); hda.assign(0x4000,0); dsp.assign(0x100000,0);
    dspWrites=0; irql=0; sequence=0; ticks=100000;
    stuckRun=false; noRun=false; power=true; halt=false; missingReady=false; badReady=false; commandTimeout=false;
    Put(hda,0,2,0x6701); Put(hda,8,4,1); Put(hda,0x14,4,0x500);
    Put(hda,0x500,4,0x10030700); Put(hda,0x700,4,0x10040000); Put(hda,0x504,4,0x40000000);
}
static NTSTATUS Prepare(GlkBoot& boot) {
    CHECK(boot.BindAccessGate(&accessGate));
    static std::vector<UCHAR> image(286720,0xaa);
    auto x=IpcXman();
    return boot.Prepare(&checks,hda.data(),0x4000,dsp.data(),0x100000,image.data(),image.size(),x.data(),x.size(),20);
}
int main() {
    Reset(); { GlkBoot boot; CHECK(NT_SUCCESS(Prepare(boot))); CHECK(live==3);
        auto r=boot.Transfer(); CHECK(r.started && r.firmwareEntered && r.dmaReleased && r.ipcReady && r.commandReady && live==0);
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
    Reset(); { GlkBoot boot; Put(hda,8,4,0); CHECK(!NT_SUCCESS(Prepare(boot)));
        CHECK(dspWrites==0 && live==0); CHECK(boot.Shutdown()); CHECK(dspWrites==0);
    }
    Reset(); { GlkBoot boot; power=false; CHECK(!NT_SUCCESS(Prepare(boot)));
        CHECK(boot.RomError()==RomError::Timeout && live==3);
        power=true; CHECK(boot.Shutdown()); CHECK(live==0 && boot.RomError()==RomError::Timeout);
    }
    Reset(); { GlkBoot boot; CHECK(NT_SUCCESS(Prepare(boot))); stuckRun=true;
        auto r=boot.Transfer(); CHECK(r.started && r.firmwareEntered && !r.dmaReleased && live==3);
        CHECK(!r.ipcReady);
        auto before=dspWrites; CHECK(!boot.Shutdown()); CHECK(dspWrites==before && live==3);
        stuckRun=false; CHECK(boot.Shutdown()); CHECK(live==0);
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
        CHECK(!boot.Windows() && live==0); CHECK(boot.Shutdown());
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

        ColdPower session(boot,bridge,accessGate);
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
        ColdPower session(boot,bridge,accessGate);
        std::vector<UCHAR> image(286720,0xaa); auto x=IpcXman();
        Put(hda,8,4,0); // deterministic HDA failure before framework Enable
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
        Reset(); GlkBoot boot; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge; ColdPower session(boot,bridge,accessGate);
        CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        std::vector<UCHAR> image(286720,0xaa); auto x=IpcXman();
        if(mode==1) Put(hda,8,4,0); // HDA failure before DSP mutation
        if(mode==2) missingReady=true;
        if(mode==3) stuckRun=true; // failed DMA stop must retain all buffers
        CHECK(!session.CanReleaseMappings());
        const auto result=session.Enter(&checks,hda.data(),0x4000,dsp.data(),0x100000,
                                        image.data(),image.size(),x.data(),x.size(),20);
        if(mode>=1 && mode<=3) {
            CHECK(!NT_SUCCESS(result) && !connected);
            CHECK(session.CanReleaseMappings()==(mode!=3));
            if(mode==3) {
                CHECK(live==7); auto before=dspWrites;
                CHECK(!session.RetryEarlyCleanup() && dspWrites==before && live==7);
                stuckRun=false; CHECK(session.RetryEarlyCleanup());
            }
            CHECK(live==4 && session.CanReleaseMappings());
            unmapped=true; CHECK(!bridge.Arm() && bridge.Stop());
            CHECK(NT_SUCCESS(FrameworkEnable(true))); // canceled callback does no MMIO
            CHECK(NT_SUCCESS(FrameworkEnable(false))); FrameworkDeleteChildren();
            continue;
        }
        CHECK(NT_SUCCESS(result) && session.TransferEvidence().commandReady && live==4);
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
        Reset(); GlkBoot first,second; CHECK(first.BindAccessGate(&accessGate)); IpcInterrupt bridge; ColdPower session(first,bridge,accessGate);
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
            CHECK(session.CanReleaseMappings() && !connected && live==4);
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
        Reset(); GlkBoot boot,next; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge; ColdPower session(boot,bridge,accessGate);
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
        CHECK(synchronizeCalls==0 && live==4);
        FrameworkDeleteChildren();
    }
    // Regression: failed pre-disable Stop must close admission even though its
    // hardware mask failed. Queued DPC/work callbacks cannot touch the old IRQ.
    for(unsigned mode=0;mode<4;++mode) {
        Reset(); GlkBoot boot,next; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge; ColdPower session(boot,bridge,accessGate);
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
            CHECK(synchronizeCalls==before && live==4);
        }
        FrameworkDeleteChildren();
    }
    // Missing pre-disable admission closure is not silently accepted for an
    // already armed session. This is a negative contract test, with no queued work.
    Reset(); {
        GlkBoot boot; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge; ColdPower session(boot,bridge,accessGate);
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
    // M0.6.15A: surprise-removal is terminal for hardware access.
    Reset(); {
        GlkBoot boot; CHECK(boot.BindAccessGate(&accessGate)); IpcInterrupt bridge;
        ColdPower session(boot,bridge,accessGate);
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
