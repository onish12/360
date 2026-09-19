// SPDX-License-Identifier: MIT
// Executes production Windows wrappers against fake WDF/MMIO, not a kernel.
#include "../m062/driver/glk_boot.h"
#include "../m062/driver/ipc_interrupt.h"
#include <vector>
#include "sof_ipc_fixture.h"
#include <cstdio>
#include <cstdlib>
using namespace phaser360::windows;
using phaser360::sof::RomError;
static unsigned checks=0,live=0,dspWrites=0,irql=0,sequence=0;
static uint64_t ticks=100000;
static bool unmapped=false,dropIrqUnmask=false,dropIrqMask=false,irqHeld=false,mutexHeld=false,queued=false;
static unsigned createFailure=0;
static WDF_INTERRUPT_CONFIG irqConfig={};
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
    CHECK(In(p,hda)||In(p,dsp)); auto& b=In(p,hda)?hda:dsp;
    const auto off=reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(b.data());
    if(&b==&dsp) CHECK(!unmapped);
    if(irql>0) CHECK(&b==&dsp && off<=0x50 && irqHeld);
    return Get(b,off,w);
}
static void Write(void* p,unsigned w,ULONG v) {
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
    CHECK(c->InterruptRaw && c->InterruptTranslated && c->EvtInterruptWorkItem && a->contextSize);
    if(createFailure==2) return STATUS_INSUFFICIENT_RESOURCES;
    *out=new FakeObject{++sequence,std::vector<UCHAR>(a->contextSize)};
    irqHandle=*out; irqConfig=*c; ++live; return STATUS_SUCCESS;
}
BOOLEAN WdfInterruptSynchronize(WDFINTERRUPT h,PFN_WDF_INTERRUPT_SYNCHRONIZE cb,WDFCONTEXT p) {
    CHECK(h==irqHandle && !irqHeld && irql==0 && mutexHeld);
    irql=5; irqHeld=true; const auto result=cb(h,p); irqHeld=false; irql=0; return result;
}
BOOLEAN WdfInterruptQueueWorkItemForIsr(WDFINTERRUPT h) {
    CHECK(h==irqHandle && irql==5 && irqHeld); const bool fresh=!queued; queued=true; return fresh?TRUE:FALSE;
}
static NTSTATUS FrameworkEnable(bool enable) {
    CHECK(irql==0 && !irqHeld); irql=5; irqHeld=true;
    const auto result=enable?irqConfig.EvtInterruptEnable(irqHandle,&checks):irqConfig.EvtInterruptDisable(irqHandle,&checks);
    irqHeld=false; irql=0; return result;
}
static bool Interrupt() {
    CHECK(irql==0 && !irqHeld); irql=5; irqHeld=true;
    const auto result=irqConfig.EvtInterruptIsr(irqHandle,0); irqHeld=false; irql=0; return result!=FALSE;
}
static void RunWork() { CHECK(queued && irql==0); queued=false; irqConfig.EvtInterruptWorkItem(irqHandle,&checks); }
static void FrameworkDeleteChildren() {
    CHECK(!queued && !mutexHeld && !irqHeld); WdfObjectDelete(irqHandle); WdfObjectDelete(serialHandle);
    irqHandle=nullptr; serialHandle=nullptr;
}
static void Notify() {
    IpcPut(dsp,0x81000,24); IpcPut(dsp,0x81004,0x90020000); IpcPut(dsp,0x81008,0);
    IpcPut(dsp,0x40,0x90020000); IpcPut(dsp,0xc,1);
}
static void Reset() {
    unmapped=false; dropIrqUnmask=false; dropIrqMask=false; createFailure=0; queued=false;
    CHECK(live==0); hda.assign(0x4000,0); dsp.assign(0x100000,0);
    dspWrites=0; irql=0; sequence=0; ticks=100000;
    stuckRun=false; noRun=false; power=true; halt=false; missingReady=false; badReady=false; commandTimeout=false;
    Put(hda,0,2,0x6701); Put(hda,8,4,1); Put(hda,0x14,4,0x500);
    Put(hda,0x500,4,0x10030700); Put(hda,0x700,4,0x10040000); Put(hda,0x504,4,0x40000000);
}
static NTSTATUS Prepare(GlkBoot& boot) {
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
    Reset(); { GlkBoot boot; auto x=IpcXman(); x[0]=0;
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
    Reset(); { GlkBoot boot; IpcInterrupt bridge; CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt},translated=raw;
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
    for(unsigned failure=1;failure<=2;++failure) {
        Reset(); GlkBoot boot; IpcInterrupt bridge; CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        createFailure=failure;
        CHECK(!NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        CHECK(live==0 && dspWrites==0 && !bridge.Arm() && !bridge.Stop());
    }
    Reset(); { GlkBoot boot; IpcInterrupt bridge; CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        CHECK(NT_SUCCESS(FrameworkEnable(true))); CHECK(NT_SUCCESS(Prepare(boot))); CHECK(boot.Transfer().commandReady);
        dropIrqUnmask=true; CHECK(!bridge.Arm() && !bridge.Running());
        CHECK(!(Get(dsp,8,4)&1) && !(Get(dsp,0x50,4)&3)); CHECK(!Interrupt());
        CHECK(bridge.Stop()); CHECK(boot.Shutdown()); CHECK(NT_SUCCESS(FrameworkEnable(false))); FrameworkDeleteChildren();
    }
    Reset(); { GlkBoot boot; IpcInterrupt bridge; CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        CHECK(NT_SUCCESS(FrameworkEnable(true))); CHECK(NT_SUCCESS(Prepare(boot))); CHECK(boot.Transfer().commandReady);
        CHECK(bridge.Arm()); Notify(); IpcPut(dsp,0xc,2); auto before=dspWrites;
        CHECK(!Interrupt() && !queued && dspWrites==before); IpcPut(dsp,0xc,1);
        IpcPut(dsp,0x81004,0xdead0000); CHECK(Interrupt()); RunWork();
        CHECK(!bridge.Running() && !bridge.Arm()); CHECK(!(Get(dsp,8,4)&1));
        CHECK(bridge.Stop()); CHECK(boot.Shutdown()); CHECK(NT_SUCCESS(FrameworkEnable(false))); FrameworkDeleteChildren();
    }
    Reset(); { GlkBoot boot; IpcInterrupt bridge; CM_PARTIAL_RESOURCE_DESCRIPTOR raw={CmResourceTypeInterrupt};
        CHECK(NT_SUCCESS(bridge.Create(&checks,&raw,&raw,&boot,dsp.data(),0x100000)));
        CHECK(NT_SUCCESS(FrameworkEnable(true))); CHECK(NT_SUCCESS(Prepare(boot))); CHECK(boot.Transfer().commandReady);
        CHECK(bridge.Arm()); dropIrqMask=true; CHECK(!bridge.Stop());
        CHECK(Get(dsp,8,4)&1); dropIrqMask=false; CHECK(bridge.Stop());
        CHECK(boot.Shutdown()); CHECK(NT_SUCCESS(FrameworkEnable(false))); FrameworkDeleteChildren();
    }
    std::printf("SOF_GLK_BOOT_TESTS=%u PASS; windows_api=SIMULATED; hardware=NONE\n",checks);
}
