// SPDX-License-Identifier: MIT
#include "../m062/driver/pnp_resources.h"
#include <cstdlib>
#include <iostream>
#include <vector>
#include <array>

using phaser360::windows::HardwareAccessGate;
using phaser360::windows::PnpResources;
using phaser360::windows::PnpResourceView;

static unsigned checks=0,irql=0,mapCalls=0,failMap=0;
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

unsigned KeGetCurrentIrql() { return irql; }
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
static CM_PARTIAL_RESOURCE_DESCRIPTOR interrupt() {
    CM_PARTIAL_RESOURCE_DESCRIPTOR d={}; d.Type=CmResourceTypeInterrupt; return d;
}
static FakeResourceList validTranslated() {
    return {{memory(expectedAddresses[0],0x4000),interrupt(),memory(expectedAddresses[1],0x100000)},-1};
}
static FakeResourceList validRaw() {
    return {{memory(0x10000,0x4000),interrupt(),memory(0x200000,0x100000)},-1};
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
    check(prepare!=nullptr && release!=nullptr && surprise!=nullptr);

    HardwareAccessGate gate,secondGate;
    activeGate=&gate;
    FakeObject device,other;
    PnpResources owner(gate),second(secondGate);
    auto translated=validTranslated();
    auto raw=validRaw();

    check(!gate.Allowed() && !gate.Removed());
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
            a.entries.push_back(interrupt()); b.entries.push_back(interrupt());
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
        check(owner.Prepared() && gate.Allowed() && live.size()==2);
        PnpResourceView view={};
        check(owner.CopyPreparedView(&view));
        check(view.hda==live[0].ptr && view.hdaLength==0x4000);
        check(view.dsp==live[1].ptr && view.dspLength==0x100000);
        check(view.interruptCount==1);
        check(view.interrupts[0].raw==&raw.entries[1]);
        check(view.interrupts[0].translated==&translated.entries[1]);
        check(!NT_SUCCESS(prepare(&device,&raw,&translated)) && mapCalls==2 && live.size()==2);
        irql=2; check(!NT_SUCCESS(release(&device,nullptr)) && live.size()==2); irql=0;
        check(NT_SUCCESS(release(&device,nullptr)));
        check(!owner.Prepared() && !gate.Allowed() && live.empty());
        view.hda=reinterpret_cast<UCHAR*>(1); view.dsp=reinterpret_cast<UCHAR*>(1);
        check(!owner.CopyPreparedView(&view) && view.hda==nullptr && view.dsp==nullptr && view.interruptCount==0);
        check(unmaps==std::vector<SIZE_T>({0x100000,0x4000}));
        check(NT_SUCCESS(release(&device,nullptr)) && unmaps.size()==2);
    }

    // Surprise removal is terminal. Release may unmap resource-only mappings,
    // but the same owner/gate can never be prepared again.
    expectedAddresses={0x200004000LL,0x200100000LL};
    translated=validTranslated(); raw=validRaw(); mapCalls=0; unmaps.clear();
    check(NT_SUCCESS(prepare(&device,&raw,&translated)) && gate.Allowed());
    surprise(&device);
    check(gate.Removed() && !gate.Allowed() && !owner.Prepared());
    PnpResourceView view={}; check(!owner.CopyPreparedView(&view));
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
    expectedAddresses={0x300004000LL,0x300100000LL};
    auto raceTranslated=validTranslated(); auto raceRaw=validRaw();
    mapCalls=0; unmaps.clear(); surpriseCallback=surprise; surpriseDevice=&raceDevice; surpriseMapAt=1;
    check(prepare(&raceDevice,&raceRaw,&raceTranslated)==STATUS_INVALID_DEVICE_STATE);
    check(mapCalls==1 && live.empty() && raceGate.Removed() && !raceGate.Allowed());
    check(unmaps==std::vector<SIZE_T>({0x4000}));
    check(NT_SUCCESS(release(&raceDevice,nullptr)) && raceGate.Removed());
    check(!raceGate.OpenForPrepare() && raceGate.CloseForRelease());
    surpriseCallback=nullptr; surpriseDevice=nullptr; surpriseMapAt=0;

    std::cout<<"SOF_PNP_RESOURCES_TESTS="<<checks
             <<" PASS; surprise_callback=REGISTERED; paired_raw_translated=YES; irq_selection=DEFERRED; hardware=NOT_TOUCHED\n";
}
