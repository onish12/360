// SPDX-License-Identifier: MIT
#include "../m062/driver/pnp_resources.h"
#include <cstdlib>
#include <iostream>
#include <vector>
#include <array>

using phaser360::windows::HardwareAccessGate;
using phaser360::windows::PnpResources;
using phaser360::windows::PnpResourceView;
using phaser360::windows::PnpPowerPhase;
using phaser360::windows::PnpInterruptKind;
using phaser360::windows::PnpInterruptResource;
using phaser360::windows::PnpDormantInterruptBinding;
using phaser360::windows::PnpLifecycleOps;

static unsigned checks=0,irql=0,mapCalls=0,failMap=0;
static unsigned failedNoRestartCalls=0;
static WDFDEVICE lastFailedDevice=nullptr;
static void check(bool ok) { ++checks; if(!ok) { std::cerr<<"PNP check failed: "<<checks<<'\n'; std::exit(1); } }

struct FakeObject { void* owner=nullptr; };
struct FakeDeviceInit { WDF_PNPPOWER_EVENT_CALLBACKS callbacks={}; };
struct FakeResourceList { std::vector<CM_PARTIAL_RESOURCE_DESCRIPTOR> entries; int nullAt=-1; };
struct Mapping { void* ptr; SIZE_T bytes; LONGLONG address; };

static std::vector<Mapping> live;
static std::vector<SIZE_T> unmaps;
static std::array<LONGLONG,2> expectedAddresses={0xd1000000,0xd2000000};
static HardwareAccessGate* activeGate=nullptr;
static void(*surpriseCallback)(WDFDEVICE)=nullptr;
static WDFDEVICE surpriseDevice=nullptr;
static unsigned surpriseMapAt=0;

struct HookTrace {
    std::vector<unsigned> sequence;
    bool failPrepared=false;
    bool failPre=false;
    bool failD0=false,failPost=false;
    bool removeDuringPrepared=false;
    HardwareAccessGate* removeAfterEntry=nullptr;
};
static NTSTATUS HookPrepared(void* p,const PnpResourceView& view,
                             const PnpDormantInterruptBinding& binding) noexcept {
    auto& h=*static_cast<HookTrace*>(p); h.sequence.push_back(1);
    check(view.hda && view.dsp && view.hdaLength==0x4000 && view.dspLength==0x100000);
    check(binding.gate && binding.dsp==view.dsp && binding.dspLength==view.dspLength);
    if(h.removeDuringPrepared && surpriseCallback && surpriseDevice)
        surpriseCallback(surpriseDevice);
    return h.failPrepared?STATUS_DEVICE_CONFIGURATION_ERROR:STATUS_SUCCESS;
}
static NTSTATUS HookD0Entry(void* p,WDFDEVICE,const PnpResourceView& view) noexcept {
    auto& h=*static_cast<HookTrace*>(p); h.sequence.push_back(2);
    check(view.hda && view.dsp);
    if(h.removeAfterEntry) h.removeAfterEntry->SurpriseRemove();
    return h.failD0?STATUS_DEVICE_CONFIGURATION_ERROR:STATUS_SUCCESS;
}
static NTSTATUS HookPost(void* p) noexcept {
    auto& h=*static_cast<HookTrace*>(p); h.sequence.push_back(3);
    return h.failPost?STATUS_DEVICE_CONFIGURATION_ERROR:STATUS_SUCCESS;
}
static NTSTATUS HookPre(void* p) noexcept {
    auto& h=*static_cast<HookTrace*>(p); h.sequence.push_back(4);
    return h.failPre?STATUS_DEVICE_CONFIGURATION_ERROR:STATUS_SUCCESS;
}
static NTSTATUS HookExit(void* p) noexcept {
    static_cast<HookTrace*>(p)->sequence.push_back(5); return STATUS_SUCCESS;
}
static NTSTATUS HookRelease(void* p) noexcept {
    static_cast<HookTrace*>(p)->sequence.push_back(6); return STATUS_SUCCESS;
}
static NTSTATUS HookReleaseAfterHardware(void* p) noexcept {
    check(activeGate && !activeGate->Allowed());
    check(live.size()==2); // mappings remain until software users are retired
    static_cast<HookTrace*>(p)->sequence.push_back(8); return STATUS_SUCCESS;
}
static void HookSurprise(void* p) noexcept {
    static_cast<HookTrace*>(p)->sequence.push_back(7);
}
static PnpLifecycleOps MakeHooks(HookTrace& h) {
    PnpLifecycleOps ops{}; ops.context=&h; ops.prepared=HookPrepared;
    ops.d0Entry=HookD0Entry; ops.postInterruptsEnabled=HookPost;
    ops.preInterruptsDisabled=HookPre; ops.d0Exit=HookExit;
    ops.release=HookRelease; ops.surpriseRemoval=HookSurprise; return ops;
}

unsigned KeGetCurrentIrql() { return irql; }
void WdfDeviceSetFailed(WDFDEVICE device,WDF_DEVICE_FAILED_ACTION action) {
    check(device!=nullptr && action==WdfDeviceFailedNoRestart);
    ++failedNoRestartCalls; lastFailedDevice=device;
}
void* FakeWdfContext(WDFINTERRUPT object) { return &object->owner; }
void WdfDeviceInitSetPnpPowerEventCallbacks(PWDFDEVICE_INIT init,WDF_PNPPOWER_EVENT_CALLBACKS* c) { init->callbacks=*c; }
ULONG WdfCmResourceListGetCount(WDFCMRESLIST list) { return static_cast<ULONG>(list->entries.size()); }
PCM_PARTIAL_RESOURCE_DESCRIPTOR WdfCmResourceListGetDescriptor(WDFCMRESLIST list,ULONG index) {
    if(static_cast<int>(index)==list->nullAt || index>=list->entries.size()) return nullptr;
    return &list->entries[index];
}
void* MmMapIoSpaceEx(PHYSICAL_ADDRESS address,SIZE_T bytes,ULONG flags) {
    ++mapCalls;
    check(irql==PASSIVE_LEVEL && flags==(PAGE_READWRITE|PAGE_NOCACHE));
    check(activeGate!=nullptr && !activeGate->Allowed());
    check(mapCalls<=2 && address.QuadPart==expectedAddresses[mapCalls-1]);
    check(bytes==(mapCalls==1?0x4000u:0x100000u));
    if(mapCalls==failMap) return nullptr;
    void* ptr=std::malloc(bytes); check(ptr!=nullptr); live.push_back({ptr,bytes,address.QuadPart});
    if(surpriseCallback && surpriseMapAt==mapCalls) surpriseCallback(surpriseDevice);
    return ptr;
}
void MmUnmapIoSpace(void* ptr,SIZE_T bytes) {
    check(irql==PASSIVE_LEVEL && activeGate!=nullptr && !activeGate->Allowed());
    check(!live.empty() && live.back().ptr==ptr && live.back().bytes==bytes);
    std::free(ptr); live.pop_back(); unmaps.push_back(bytes);
}

static CM_PARTIAL_RESOURCE_DESCRIPTOR memory(LONGLONG address,ULONG bytes) {
    CM_PARTIAL_RESOURCE_DESCRIPTOR d={}; d.Type=CmResourceTypeMemory;
    d.u.Memory.Start.QuadPart=address; d.u.Memory.Length=bytes; return d;
}
static CM_PARTIAL_RESOURCE_DESCRIPTOR lineInterrupt(ULONG level,ULONG vector,
                                                    ULONG_PTR affinity,UCHAR share=3,
                                                    USHORT flags=CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE) {
    CM_PARTIAL_RESOURCE_DESCRIPTOR d={}; d.Type=CmResourceTypeInterrupt;
    d.ShareDisposition=share; d.Flags=flags;
    d.u.Interrupt.Level=level; d.u.Interrupt.Vector=vector; d.u.Interrupt.Affinity=affinity;
    return d;
}
static CM_PARTIAL_RESOURCE_DESCRIPTOR messageRaw(USHORT count,ULONG vector,
                                                ULONG_PTR affinity,UCHAR share=2,
                                                USHORT extraFlags=CM_RESOURCE_INTERRUPT_LATCHED) {
    CM_PARTIAL_RESOURCE_DESCRIPTOR d={}; d.Type=CmResourceTypeInterrupt;
    d.ShareDisposition=share; d.Flags=USHORT(extraFlags|CM_RESOURCE_INTERRUPT_MESSAGE);
    d.u.MessageInterrupt.Raw.MessageCount=count;
    d.u.MessageInterrupt.Raw.Vector=vector;
    d.u.MessageInterrupt.Raw.Affinity=affinity;
    return d;
}
static CM_PARTIAL_RESOURCE_DESCRIPTOR messageTranslated(ULONG level,ULONG vector,
                                                       ULONG_PTR affinity,UCHAR share=2,
                                                       USHORT extraFlags=CM_RESOURCE_INTERRUPT_LATCHED) {
    CM_PARTIAL_RESOURCE_DESCRIPTOR d={}; d.Type=CmResourceTypeInterrupt;
    d.ShareDisposition=share; d.Flags=USHORT(extraFlags|CM_RESOURCE_INTERRUPT_MESSAGE);
    d.u.MessageInterrupt.Translated.Level=level;
    d.u.MessageInterrupt.Translated.Vector=vector;
    d.u.MessageInterrupt.Translated.Affinity=affinity;
    return d;
}
static FakeResourceList validTranslated() {
    return {{memory(expectedAddresses[0],0x4000),lineInterrupt(5,0x51,0x0f),memory(expectedAddresses[1],0x100000)},-1};
}
static FakeResourceList validRaw() {
    return {{memory(0x10000,0x4000),lineInterrupt(3,0x11,0x03),memory(0x200000,0x100000)},-1};
}

int main() {
    FakeDeviceInit init; WDF_OBJECT_ATTRIBUTES attrs={};
    check(!NT_SUCCESS(PnpResources::Configure(nullptr,&attrs)));
    check(!NT_SUCCESS(PnpResources::Configure(&init,nullptr)));
    irql=2; check(!NT_SUCCESS(PnpResources::Configure(&init,&attrs))); irql=0;
    check(NT_SUCCESS(PnpResources::Configure(&init,&attrs)));
    check(attrs.contextSize==sizeof(void*) && attrs.ParentObject==nullptr);
    const auto prepare=init.callbacks.EvtDevicePrepareHardware;
    const auto release=init.callbacks.EvtDeviceReleaseHardware;
    const auto surprise=init.callbacks.EvtDeviceSurpriseRemoval;
    const auto d0Entry=init.callbacks.EvtDeviceD0Entry;
    const auto postEnable=init.callbacks.EvtDeviceD0EntryPostInterruptsEnabled;
    const auto preDisable=init.callbacks.EvtDeviceD0ExitPreInterruptsDisabled;
    const auto d0Exit=init.callbacks.EvtDeviceD0Exit;
    check(prepare!=nullptr && release!=nullptr && surprise!=nullptr &&
          d0Entry!=nullptr && postEnable!=nullptr && preDisable!=nullptr && d0Exit!=nullptr);

    HardwareAccessGate gate,secondGate;
    activeGate=&gate;
    FakeObject device,other;
    PnpResources owner(gate),second(secondGate);
    auto translated=validTranslated();
    auto raw=validRaw();

    check(!gate.Allowed() && !gate.Removed() && owner.PowerPhase()==PnpPowerPhase::NoResources);
    check(!NT_SUCCESS(d0Entry(&device,WdfPowerDeviceD3Final)));
    check(!NT_SUCCESS(postEnable(&device,WdfPowerDeviceD3Final)));
    check(!NT_SUCCESS(preDisable(&device,WdfPowerDeviceD3Final)));
    check(!NT_SUCCESS(d0Exit(&device,WdfPowerDeviceD3Final)));
    check(!NT_SUCCESS(prepare(&device,&raw,&translated)) && mapCalls==0);
    check(NT_SUCCESS(release(&device,nullptr)) && live.empty());
    check(!NT_SUCCESS(owner.Attach(nullptr)));
    irql=2; check(!NT_SUCCESS(owner.Attach(&device))); irql=0;
    check(NT_SUCCESS(owner.Attach(&device)));
    // Unattached device context must not affect the prepared owner's gate.
    surprise(&other); check(!gate.Removed() && !gate.Allowed());
    check(!NT_SUCCESS(owner.Attach(&other)) && !NT_SUCCESS(second.Attach(&device)));
    check(NT_SUCCESS(release(&device,nullptr)));
    check(!NT_SUCCESS(prepare(&device,nullptr,nullptr)) && mapCalls==0);

    const auto rejected=[&](FakeResourceList badRaw,FakeResourceList badTranslated) {
        mapCalls=0;
        check(prepare(&device,&badRaw,&badTranslated)==STATUS_DEVICE_CONFIGURATION_ERROR);
        check(mapCalls==0 && live.empty() && !owner.Prepared() && !gate.Allowed());
        PnpResourceView view={};
        check(!owner.CopyPreparedView(&view) && view.hda==nullptr && view.dsp==nullptr);
        check(NT_SUCCESS(release(&device,nullptr)));
    };

    rejected({{},-1},{{},-1});
    { auto a=raw,b=translated; b.entries.resize(65); rejected(a,b); }
    { auto a=raw,b=translated; b.nullAt=1; rejected(a,b); }
    { auto a=raw,b=translated; a.nullAt=1; rejected(a,b); }
    { auto a=raw,b=translated; b.entries.pop_back(); rejected(a,b); }
    { auto a=raw,b=translated; a.entries[1].Type=CmResourceTypeMemory; rejected(a,b); }
    // Message/line disagreement makes the valid union member ambiguous.
    { auto a=raw,b=translated; a.entries[1]=messageRaw(1,0x20,1); rejected(a,b); }
    // Message resources with no messages are structurally unusable.
    { auto a=raw,b=translated;
      a.entries[1]=messageRaw(0,0x20,1);
      b.entries[1]=messageTranslated(6,0x60,1);
      rejected(a,b); }
    { auto a=raw,b=translated; b.entries.push_back(memory(0xd3000000,0x4000)); a.entries.push_back(memory(0x300000,0x4000)); rejected(a,b); }
    { auto a=raw,b=translated; std::swap(b.entries[0],b.entries[2]); rejected(a,b); }
    { auto a=raw,b=translated; b.entries[0].Type=CmResourceTypeMemoryLarge; a.entries[0].Type=CmResourceTypeMemoryLarge; rejected(a,b); }
    for(auto flags : {CM_RESOURCE_MEMORY_READ_ONLY,CM_RESOURCE_MEMORY_WRITE_ONLY}) {
        auto a=raw,b=translated; b.entries[2].Flags=flags; rejected(a,b);
    }
    for(LONGLONG address : {LONGLONG{0},LONGLONG{-1},LONGLONG{0xd1000001},LONGLONG{0xd2004000}}) {
        auto a=raw,b=translated; b.entries[0].u.Memory.Start.QuadPart=address; rejected(a,b);
    }
    { auto a=raw,b=translated; b.entries[2].u.Memory.Start.QuadPart=0x7ffffffffff00000LL; rejected(a,b); }
    { auto a=raw,b=translated; b.entries[0].u.Memory.Length=0x4001; rejected(a,b); }

    // The adapter records interrupt candidates but deliberately does not choose
    // a vector. Bound the borrowed descriptor set before mapping anything.
    {
        FakeResourceList a,b;
        a.entries.push_back(memory(0x10000,0x4000));
        b.entries.push_back(memory(expectedAddresses[0],0x4000));
        for(unsigned i=0;i<PnpResourceView::kMaxInterrupts+1;++i) {
            a.entries.push_back(lineInterrupt(3,0x11+i,1));
            b.entries.push_back(lineInterrupt(5,0x51+i,1));
        }
        a.entries.push_back(memory(0x200000,0x100000));
        b.entries.push_back(memory(expectedAddresses[1],0x100000));
        rejected(a,b);
    }

    for(unsigned fail=1;fail<=2;++fail) {
        translated=validTranslated(); raw=validRaw();
        mapCalls=0; failMap=fail; unmaps.clear();
        check(prepare(&device,&raw,&translated)==STATUS_INSUFFICIENT_RESOURCES);
        check(live.empty() && !owner.Prepared() && !gate.Allowed() && unmaps.size()==fail-1);
        check(NT_SUCCESS(release(&device,nullptr)) && unmaps.size()==fail-1);
    }
    failMap=0;

    // Repeated normal resource lifetimes, including reassigned addresses >4 GiB.
    for(unsigned cycle=0;cycle<3;++cycle) {
        expectedAddresses={0x100004000LL+LONGLONG{cycle}*0x1000000,
                           0x100100000LL+LONGLONG{cycle}*0x1000000};
        translated=validTranslated(); raw=validRaw(); mapCalls=0; unmaps.clear();
        irql=2; check(!NT_SUCCESS(prepare(&device,&raw,&translated)) && mapCalls==0); irql=0;
        check(NT_SUCCESS(prepare(&device,&raw,&translated)));
        check(owner.Prepared() && gate.Allowed() && live.size()==2 &&
              owner.PowerPhase()==PnpPowerPhase::Prepared);
        // Framework power skeleton: Prepare -> D0Entry -> [InterruptEnable] ->
        // PostInterruptsEnabled -> PreInterruptsDisabled -> [InterruptDisable] ->
        // D0Exit -> Release. No hardware access is performed by these callbacks.
        const auto mapsBeforePower=mapCalls;
        const auto unmapsBeforePower=unmaps.size();
        irql=2; check(!NT_SUCCESS(d0Entry(&device,WdfPowerDeviceD3Final))); irql=0;
        check(owner.PowerPhase()==PnpPowerPhase::Prepared);
        check(NT_SUCCESS(d0Entry(&device,WdfPowerDeviceD3Final)));
        check(owner.PowerPhase()==PnpPowerPhase::D0Entered);
        check(!NT_SUCCESS(d0Entry(&device,WdfPowerDeviceD3Final)));
        check(NT_SUCCESS(postEnable(&device,WdfPowerDeviceD3Final)));
        check(owner.PowerPhase()==PnpPowerPhase::Operational);
        check(!NT_SUCCESS(postEnable(&device,WdfPowerDeviceD3Final)));
        // Release must not unmap while the framework lifecycle still says D0.
        check(!NT_SUCCESS(release(&device,nullptr)) && live.size()==2 && gate.Allowed());
        check(NT_SUCCESS(preDisable(&device,WdfPowerDeviceD3Final)));
        check(owner.PowerPhase()==PnpPowerPhase::PreInterruptsDisabled);
        check(!NT_SUCCESS(preDisable(&device,WdfPowerDeviceD3Final)));
        check(NT_SUCCESS(d0Exit(&device,WdfPowerDeviceD3Final)));
        check(owner.PowerPhase()==PnpPowerPhase::Prepared);
        check(!NT_SUCCESS(d0Exit(&device,WdfPowerDeviceD3Final)));
        check(mapCalls==mapsBeforePower && unmaps.size()==unmapsBeforePower);
        PnpResourceView view={};
        check(owner.CopyPreparedView(&view));
        check(view.hda==live[0].ptr && view.hdaLength==0x4000);
        check(view.dsp==live[1].ptr && view.dspLength==0x100000);
        check(view.interruptCount==1);
        const auto& irq=view.interrupts[0];
        check(irq.raw==&raw.entries[1] && irq.translated==&translated.entries[1]);
        check(irq.kind==PnpInterruptKind::LineBased && irq.messageCount==0);
        check(irq.rawShareDisposition==3 && irq.translatedShareDisposition==3);
        check(irq.rawFlags==CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE);
        check(irq.translatedFlags==CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE);
        check(irq.rawLevel==3 && irq.rawVector==0x11 && irq.rawAffinity==0x03);
        check(irq.translatedLevel==5 && irq.translatedVector==0x51 &&
              irq.translatedAffinity==0x0f);
        PnpInterruptResource selected{};
        check(owner.CopySingleInterruptForCreate(&selected));
        check(selected.raw==irq.raw && selected.translated==irq.translated &&
              selected.kind==PnpInterruptKind::LineBased && selected.messageCount==0);
        PnpDormantInterruptBinding binding{};
        check(owner.CopyDormantInterruptBinding(&binding));
        check(binding.gate==&gate && binding.dsp==view.dsp &&
              binding.dspLength==0x100000 && binding.raw==irq.raw &&
              binding.translated==irq.translated &&
              binding.kind==PnpInterruptKind::LineBased &&
              binding.messageCount==0);
        check(!NT_SUCCESS(prepare(&device,&raw,&translated)) && mapCalls==2 && live.size()==2);
        irql=2; check(!NT_SUCCESS(release(&device,nullptr)) && live.size()==2); irql=0;
        check(NT_SUCCESS(release(&device,nullptr)));
        check(!owner.Prepared() && !gate.Allowed() && live.empty());
        view.hda=reinterpret_cast<UCHAR*>(1); view.dsp=reinterpret_cast<UCHAR*>(1);
        check(!owner.CopyPreparedView(&view) && view.hda==nullptr && view.dsp==nullptr && view.interruptCount==0);
        PnpInterruptResource releasedSelected{};
        releasedSelected.raw=reinterpret_cast<PCM_PARTIAL_RESOURCE_DESCRIPTOR>(1);
        check(!owner.CopySingleInterruptForCreate(&releasedSelected) && releasedSelected.raw==nullptr);
        PnpDormantInterruptBinding releasedBinding{};
        releasedBinding.dsp=reinterpret_cast<UCHAR*>(1);
        check(!owner.CopyDormantInterruptBinding(&releasedBinding) &&
              releasedBinding.dsp==nullptr && releasedBinding.gate==nullptr);
        check(unmaps==std::vector<SIZE_T>({0x100000,0x4000}));
        check(NT_SUCCESS(release(&device,nullptr)) && unmaps.size()==2);
    }

    // Message-signaled resources use a different CM descriptor union. Inventory
    // it without choosing a vector or creating a WDFINTERRUPT.
    expectedAddresses={0x180004000LL,0x180100000LL};
    translated=validTranslated(); raw=validRaw(); mapCalls=0; unmaps.clear();
    raw.entries[1]=messageRaw(4,0x21,0x03,2,
                              USHORT(CM_RESOURCE_INTERRUPT_LATCHED|CM_RESOURCE_INTERRUPT_WAKE_HINT));
    translated.entries[1]=messageTranslated(
        7,0x71,0x0c,2,
        USHORT(CM_RESOURCE_INTERRUPT_LATCHED|CM_RESOURCE_INTERRUPT_WAKE_HINT));
    check(NT_SUCCESS(prepare(&device,&raw,&translated)));
    {
        PnpResourceView messageView={};
        check(owner.CopyPreparedView(&messageView) && messageView.interruptCount==1);
        const auto& irq=messageView.interrupts[0];
        check(irq.kind==PnpInterruptKind::MessageSignaled);
        check(irq.raw==&raw.entries[1] && irq.translated==&translated.entries[1]);
        check(irq.messageCount==4 && irq.rawLevel==0);
        check(irq.rawVector==0x21 && irq.rawAffinity==0x03);
        check(irq.translatedLevel==7 && irq.translatedVector==0x71 &&
              irq.translatedAffinity==0x0c);
        check((irq.rawFlags & CM_RESOURCE_INTERRUPT_MESSAGE)!=0 &&
              (irq.translatedFlags & CM_RESOURCE_INTERRUPT_MESSAGE)!=0);
        check((irq.rawFlags & CM_RESOURCE_INTERRUPT_WAKE_HINT)!=0 &&
              (irq.translatedFlags & CM_RESOURCE_INTERRUPT_WAKE_HINT)!=0);
        PnpInterruptResource selected{};
        selected.raw=reinterpret_cast<PCM_PARTIAL_RESOURCE_DESCRIPTOR>(1);
        check(!owner.CopySingleInterruptForCreate(&selected) && selected.raw==nullptr);
        PnpDormantInterruptBinding binding{};
        check(!owner.CopyDormantInterruptBinding(&binding) && binding.dsp==nullptr);
    }
    check(NT_SUCCESS(release(&device,nullptr)) && live.empty());

    // Live PCI capability reports InterruptMessageMaximum=1. A single-message
    // descriptor is admissible without hard-coding vector/level/affinity.
    expectedAddresses={0x190004000LL,0x190100000LL};
    translated=validTranslated(); raw=validRaw(); mapCalls=0; unmaps.clear();
    raw.entries[1]=messageRaw(1,0x31,0x01);
    translated.entries[1]=messageTranslated(9,0x81,0x02);
    check(NT_SUCCESS(prepare(&device,&raw,&translated)));
    {
        PnpInterruptResource selected{};
        check(owner.CopySingleInterruptForCreate(&selected));
        check(selected.kind==PnpInterruptKind::MessageSignaled &&
              selected.messageCount==1 &&
              selected.raw==&raw.entries[1] &&
              selected.translated==&translated.entries[1]);
        PnpDormantInterruptBinding binding{};
        check(owner.CopyDormantInterruptBinding(&binding));
        check(binding.gate==&gate && binding.dsp==live[1].ptr &&
              binding.dspLength==0x100000 &&
              binding.kind==PnpInterruptKind::MessageSignaled &&
              binding.messageCount==1 &&
              binding.raw==&raw.entries[1] &&
              binding.translated==&translated.entries[1]);
    }
    check(NT_SUCCESS(release(&device,nullptr)) && live.empty());

    // More than one assigned interrupt pair remains inventory data only. The
    // DEV_3198 admission policy refuses to guess which pair should be created.
    expectedAddresses={0x1A0004000LL,0x1A0100000LL};
    raw={{memory(0x10000,0x4000),lineInterrupt(3,0x11,0x03),
          lineInterrupt(4,0x12,0x04),memory(0x200000,0x100000)},-1};
    translated={{memory(expectedAddresses[0],0x4000),lineInterrupt(5,0x51,0x0f),
                 lineInterrupt(6,0x52,0x10),memory(expectedAddresses[1],0x100000)},-1};
    mapCalls=0; unmaps.clear();
    check(NT_SUCCESS(prepare(&device,&raw,&translated)));
    {
        PnpResourceView view{};
        check(owner.CopyPreparedView(&view) && view.interruptCount==2);
        PnpInterruptResource selected{};
        check(!owner.CopySingleInterruptForCreate(&selected) && selected.raw==nullptr);
        PnpDormantInterruptBinding binding{};
        check(!owner.CopyDormantInterruptBinding(&binding) && binding.dsp==nullptr);
    }
    check(NT_SUCCESS(release(&device,nullptr)) && live.empty());

    // Surprise removal is terminal. Release may unmap resource-only mappings,
    // but the same owner/gate can never be prepared again.
    expectedAddresses={0x200004000LL,0x200100000LL};
    translated=validTranslated(); raw=validRaw(); mapCalls=0; unmaps.clear();
    check(NT_SUCCESS(prepare(&device,&raw,&translated)) && gate.Allowed());
    check(NT_SUCCESS(d0Entry(&device,WdfPowerDeviceD3Final)));
    check(NT_SUCCESS(postEnable(&device,WdfPowerDeviceD3Final)));
    check(owner.PowerPhase()==PnpPowerPhase::Operational);
    surprise(&device);
    check(gate.Removed() && !gate.Allowed() && !owner.Prepared());
    PnpResourceView view={}; check(!owner.CopyPreparedView(&view));
    PnpDormantInterruptBinding removedBinding{};
    check(!owner.CopyDormantInterruptBinding(&removedBinding) &&
          removedBinding.gate==nullptr && removedBinding.dsp==nullptr);
    // Surprise removal from D0 still unwinds through the pre-disable and D0Exit
    // callbacks; neither callback is allowed to require hardware access.
    check(!NT_SUCCESS(release(&device,nullptr)) && live.size()==2);
    check(NT_SUCCESS(preDisable(&device,WdfPowerDeviceD3Final)));
    check(NT_SUCCESS(d0Exit(&device,WdfPowerDeviceD3Final)));
    check(owner.PowerPhase()==PnpPowerPhase::Prepared);
    check(NT_SUCCESS(release(&device,nullptr)) && live.empty() && gate.Removed());
    mapCalls=0;
    check(prepare(&device,&raw,&translated)==STATUS_INVALID_DEVICE_STATE && mapCalls==0);
    check(!gate.OpenForPrepare() && gate.CloseForRelease() && gate.Removed());

    // Deterministic race: SurpriseRemoval fires from the first mapping call,
    // before Prepare can map the DSP BAR or open the gate. Preparation must
    // unwind the HDA map, keep Removed terminal and never perform map #2.
    HardwareAccessGate raceGate; PnpResources raceOwner(raceGate); FakeObject raceDevice;
    activeGate=&raceGate;
    check(NT_SUCCESS(raceOwner.Attach(&raceDevice)));
    check(raceOwner.PowerPhase()==PnpPowerPhase::NoResources);
    expectedAddresses={0x300004000LL,0x300100000LL};
    auto raceTranslated=validTranslated(); auto raceRaw=validRaw();
    mapCalls=0; unmaps.clear(); surpriseCallback=surprise; surpriseDevice=&raceDevice; surpriseMapAt=1;
    check(prepare(&raceDevice,&raceRaw,&raceTranslated)==STATUS_INVALID_DEVICE_STATE);
    check(mapCalls==1 && live.empty() && raceGate.Removed() && !raceGate.Allowed() &&
          raceOwner.PowerPhase()==PnpPowerPhase::NoResources);
    check(unmaps==std::vector<SIZE_T>({0x4000}));
    check(NT_SUCCESS(release(&raceDevice,nullptr)) && raceGate.Removed());
    check(!raceGate.OpenForPrepare() && raceGate.CloseForRelease());
    surpriseCallback=nullptr; surpriseDevice=nullptr; surpriseMapAt=0;

    // Failed D0Entry after a terminal gate must not require a synthetic D0Exit.
    HardwareAccessGate entryFailGate; PnpResources entryFailOwner(entryFailGate); FakeObject entryFailDevice;
    activeGate=&entryFailGate;
    expectedAddresses={0x400004000LL,0x400100000LL};
    auto entryFailTranslated=validTranslated(); auto entryFailRaw=validRaw();
    mapCalls=0; unmaps.clear();
    check(NT_SUCCESS(entryFailOwner.Attach(&entryFailDevice)));
    check(NT_SUCCESS(prepare(&entryFailDevice,&entryFailRaw,&entryFailTranslated)));
    surprise(&entryFailDevice);
    check(entryFailGate.Removed() && entryFailOwner.PowerPhase()==PnpPowerPhase::Prepared);
    check(!NT_SUCCESS(d0Entry(&entryFailDevice,WdfPowerDeviceD3Final)));
    check(entryFailOwner.PowerPhase()==PnpPowerPhase::Prepared);
    check(NT_SUCCESS(release(&entryFailDevice,nullptr)) && live.empty());
    check(entryFailGate.Removed() && entryFailOwner.PowerPhase()==PnpPowerPhase::NoResources);

    // H4 callback composition seam: prove PnP invokes the consumer only at the
    // documented lifecycle boundaries and still owns all map/unmap operations.
    {
        HardwareAccessGate hookGate; PnpResources hookOwner(hookGate); FakeObject hookDevice;
        HookTrace trace; auto hooks=MakeHooks(trace); activeGate=&hookGate;
        check(hookOwner.InstallLifecycle(hooks));
        check(!hookOwner.InstallLifecycle(hooks));
        check(NT_SUCCESS(hookOwner.Attach(&hookDevice)));
        expectedAddresses={0x500004000LL,0x500100000LL};
        auto hookTranslated=validTranslated(); auto hookRaw=validRaw();
        mapCalls=0; unmaps.clear();
        check(NT_SUCCESS(prepare(&hookDevice,&hookRaw,&hookTranslated)));
        check(trace.sequence==std::vector<unsigned>({1}));
        check(NT_SUCCESS(d0Entry(&hookDevice,WdfPowerDeviceD3Final)));
        check(NT_SUCCESS(postEnable(&hookDevice,WdfPowerDeviceD3Final)));
        trace.failPre=true;
        check(!NT_SUCCESS(preDisable(&hookDevice,WdfPowerDeviceD3Final)));
        check(hookOwner.PowerPhase()==PnpPowerPhase::PreInterruptsDisabled);
        check(NT_SUCCESS(d0Exit(&hookDevice,WdfPowerDeviceD3Final)));
        check(NT_SUCCESS(release(&hookDevice,nullptr)));
        check(trace.sequence==std::vector<unsigned>({1,2,3,4,5,6}));
        check(live.empty() && unmaps==std::vector<SIZE_T>({0x100000,0x4000}));

        // Re-prepared resources may be surprise-removed. Gate closes first, then
        // the consumer receives only its software fence notification.
        trace.failPre=false; trace.sequence.clear();
        expectedAddresses={0x510004000LL,0x510100000LL};
        hookTranslated=validTranslated(); hookRaw=validRaw(); mapCalls=0; unmaps.clear();
        check(NT_SUCCESS(prepare(&hookDevice,&hookRaw,&hookTranslated)));
        surprise(&hookDevice);
        check(hookGate.Removed() && trace.sequence==std::vector<unsigned>({1,7}));
        check(NT_SUCCESS(release(&hookDevice,nullptr)));
        check(trace.sequence==std::vector<unsigned>({1,7,6}) && live.empty());
    }

    // H15A: SurpriseRemoval may win inside the consumer Prepared callback.
    // PrepareHardware must recheck the atomic gate after that callback, unwind
    // the lifecycle/resource bundle, and return failure rather than publish a
    // successful preparation after terminal removal.
    {
        HardwareAccessGate racePreparedGate; PnpResources racePreparedOwner(racePreparedGate);
        FakeObject racePreparedDevice; HookTrace trace; trace.removeDuringPrepared=true;
        activeGate=&racePreparedGate;
        check(racePreparedOwner.InstallLifecycle(MakeHooks(trace)));
        check(NT_SUCCESS(racePreparedOwner.Attach(&racePreparedDevice)));
        expectedAddresses={0x512004000LL,0x512100000LL};
        auto rt=validTranslated(); auto rr=validRaw(); mapCalls=0; unmaps.clear();
        surpriseCallback=surprise; surpriseDevice=&racePreparedDevice;
        check(prepare(&racePreparedDevice,&rr,&rt)==STATUS_INVALID_DEVICE_STATE);
        check(racePreparedGate.Removed() &&
              racePreparedOwner.PowerPhase()==PnpPowerPhase::NoResources &&
              trace.sequence==std::vector<unsigned>({1,7,6}) && live.empty());
        check(NT_SUCCESS(release(&racePreparedDevice,nullptr)));
        check(trace.sequence==std::vector<unsigned>({1,7,6}));
        surpriseCallback=nullptr; surpriseDevice=nullptr; surpriseMapAt=0;
    }

    // SurpriseRemoval may win at the tail of a successful consumer D0Entry.
    // Because KMDF supplies no D0Exit after failed D0Entry, PnP compensates
    // immediately with terminal fence + exit hook before returning failure.
    {
        HardwareAccessGate raceEntryGate; PnpResources raceEntryOwner(raceEntryGate);
        FakeObject raceEntryDevice; HookTrace trace; trace.removeAfterEntry=&raceEntryGate;
        activeGate=&raceEntryGate;
        check(raceEntryOwner.InstallLifecycle(MakeHooks(trace)));
        check(NT_SUCCESS(raceEntryOwner.Attach(&raceEntryDevice)));
        expectedAddresses={0x515004000LL,0x515100000LL};
        auto rt=validTranslated(); auto rr=validRaw(); mapCalls=0; unmaps.clear();
        check(NT_SUCCESS(prepare(&raceEntryDevice,&rr,&rt)));
        check(d0Entry(&raceEntryDevice,WdfPowerDeviceD3Final)==STATUS_INVALID_DEVICE_STATE);
        check(raceEntryGate.Removed() &&
              raceEntryOwner.PowerPhase()==PnpPowerPhase::Prepared);
        check(trace.sequence==std::vector<unsigned>({1,2,7,5}));
        check(NT_SUCCESS(release(&raceEntryDevice,nullptr)));
        check(trace.sequence==std::vector<unsigned>({1,2,7,5,6}) && live.empty());
    }

    // A consumer that rejects Prepared must not leave mappings or an open gate.
    {
        HardwareAccessGate failGate; PnpResources failOwner(failGate); FakeObject failDevice;
        HookTrace trace; trace.failPrepared=true; activeGate=&failGate;
        check(failOwner.InstallLifecycle(MakeHooks(trace)));
        check(NT_SUCCESS(failOwner.Attach(&failDevice)));
        expectedAddresses={0x520004000LL,0x520100000LL};
        auto ft=validTranslated(); auto fr=validRaw(); mapCalls=0; unmaps.clear();
        check(prepare(&failDevice,&fr,&ft)==STATUS_DEVICE_CONFIGURATION_ERROR);
        check(trace.sequence==std::vector<unsigned>({1}));
        check(live.empty() && !failGate.Allowed() &&
              failOwner.PowerPhase()==PnpPowerPhase::NoResources);
    }

    // A failed boot must request no automatic PnP reload. Failed D0 needs no
    // synthetic D0Exit; post-enable failure retains the normal exit ordering.
    check(failedNoRestartCalls==0);
    for(unsigned mode=0;mode<2;++mode) {
        HardwareAccessGate failGate; PnpResources failOwner(failGate); FakeObject failDevice;
        HookTrace trace; trace.failD0=(mode==0); trace.failPost=(mode==1);
        activeGate=&failGate;
        check(failOwner.InstallLifecycle(MakeHooks(trace)));
        check(NT_SUCCESS(failOwner.Attach(&failDevice)));
        auto ft=validTranslated(); auto fr=validRaw(); mapCalls=0; unmaps.clear();
        check(NT_SUCCESS(prepare(&failDevice,&fr,&ft)));
        const auto before=failedNoRestartCalls;
        if(mode==0) {
            check(d0Entry(&failDevice,WdfPowerDeviceD3Final)==STATUS_DEVICE_CONFIGURATION_ERROR);
            check(failOwner.PowerPhase()==PnpPowerPhase::Prepared);
        } else {
            check(NT_SUCCESS(d0Entry(&failDevice,WdfPowerDeviceD3Final)));
            check(failedNoRestartCalls==before);
            check(postEnable(&failDevice,WdfPowerDeviceD3Final)==STATUS_DEVICE_CONFIGURATION_ERROR);
            check(failOwner.PowerPhase()==PnpPowerPhase::D0Entered);
            check(NT_SUCCESS(preDisable(&failDevice,WdfPowerDeviceD3Final)));
            check(NT_SUCCESS(d0Exit(&failDevice,WdfPowerDeviceD3Final)));
        }
        check(failedNoRestartCalls==before+1 && lastFailedDevice==&failDevice);
        check(NT_SUCCESS(release(&failDevice,nullptr)) && live.empty());
    }

    // The final KMDF callback, not early Prepare unwind, carries the hardware
    // release boundary. It must close MMIO access before invoking the consumer.
    for(unsigned mode=0;mode<3;++mode) {
        HardwareAccessGate finalGate; PnpResources finalOwner(finalGate); FakeObject finalDevice;
        HookTrace trace; trace.failPrepared=(mode==0); trace.failD0=(mode==1);
        auto hooks=MakeHooks(trace); hooks.releaseAfterHardware=HookReleaseAfterHardware;
        activeGate=&finalGate;
        check(finalOwner.InstallLifecycle(hooks));
        check(NT_SUCCESS(finalOwner.Attach(&finalDevice)));
        auto ft=validTranslated(); auto fr=validRaw(); mapCalls=0; unmaps.clear();
        const auto prepared=prepare(&finalDevice,&fr,&ft);
        if(mode==0) {
            check(!NT_SUCCESS(prepared) && live.empty());
            check(NT_SUCCESS(release(&finalDevice,nullptr)));
            check(trace.sequence==std::vector<unsigned>({1}));
        } else {
            check(NT_SUCCESS(prepared));
            const auto entry=d0Entry(&finalDevice,WdfPowerDeviceD3Final);
            check(NT_SUCCESS(entry)==(mode==2));
            // Mode 2 models final release after a later power-path failure;
            // no invented successful D0Exit is needed to retire resources.
            check(NT_SUCCESS(release(&finalDevice,nullptr)) && live.empty());
            check(trace.sequence==std::vector<unsigned>({1,2,8}));
            check(finalOwner.PowerPhase()==PnpPowerPhase::NoResources);
        }
    }

    std::cout<<"SOF_PNP_RESOURCES_TESTS="<<checks
             <<" PASS; irq_inventory=LINE_AND_MESSAGE; irq_admission=SINGLE_PAIR_LINE_OR_ONE_MESSAGE; dormant_binding=PNP_TO_IRQ_SHELL_SOFTWARE_ONLY; lifecycle_hooks=ORDERED_FAIL_CLOSED; wdf_interrupt_create=NO; power_skeleton=REGISTERED; surprise_callback=REGISTERED; paired_raw_translated=YES; hardware=NOT_TOUCHED\n";
}
