// SPDX-License-Identifier: MIT
#include "../m062/driver/pnp_resources.h"
#include <cstdlib>
#include <iostream>
#include <vector>
#include <array>
using phaser360::windows::PnpResources;
static unsigned checks=0,irql=0,mapCalls=0,failMap=0;
static void check(bool ok) { ++checks; if(!ok) { std::cerr<<"PNP check failed: "<<checks<<'\n'; std::exit(1); } }
struct FakeObject { void* owner=nullptr; };
struct FakeDeviceInit { WDF_PNPPOWER_EVENT_CALLBACKS callbacks={}; };
struct FakeResourceList { std::vector<CM_PARTIAL_RESOURCE_DESCRIPTOR> entries; int nullAt=-1; };
struct Mapping { void* ptr; SIZE_T bytes; LONGLONG address; };
static std::vector<Mapping> live;
static std::vector<SIZE_T> unmaps;
static std::array<LONGLONG,2> expectedAddresses={0xd1000000,0xd2000000};
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
    check(mapCalls<=2 && address.QuadPart==expectedAddresses[mapCalls-1]);
    check(bytes==(mapCalls==1?0x4000u:0x100000u));
    if(mapCalls==failMap) return nullptr;
    void* ptr=std::malloc(bytes); check(ptr!=nullptr); live.push_back({ptr,bytes,address.QuadPart}); return ptr;
}
void MmUnmapIoSpace(void* ptr,SIZE_T bytes) {
    check(irql==PASSIVE_LEVEL && !live.empty());
    check(live.back().ptr==ptr && live.back().bytes==bytes);
    std::free(ptr); live.pop_back(); unmaps.push_back(bytes);
}
static CM_PARTIAL_RESOURCE_DESCRIPTOR memory(LONGLONG address,ULONG bytes) {
    CM_PARTIAL_RESOURCE_DESCRIPTOR d={}; d.Type=CmResourceTypeMemory; d.u.Memory.Start.QuadPart=address; d.u.Memory.Length=bytes; return d;
}
static FakeResourceList valid() {
    return {{memory(expectedAddresses[0],0x4000),{CmResourceTypeInterrupt},memory(expectedAddresses[1],0x100000)},-1};
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
    check(prepare!=nullptr && release!=nullptr);
    FakeObject device,other; PnpResources owner,second; auto list=valid();
    check(!NT_SUCCESS(prepare(&device,nullptr,&list)) && mapCalls==0);
    check(NT_SUCCESS(release(&device,nullptr)) && live.empty());
    check(!NT_SUCCESS(owner.Attach(nullptr)));
    irql=2; check(!NT_SUCCESS(owner.Attach(&device))); irql=0;
    check(NT_SUCCESS(owner.Attach(&device)));
    check(!NT_SUCCESS(owner.Attach(&other)) && !NT_SUCCESS(second.Attach(&device)));
    check(NT_SUCCESS(release(&device,nullptr)));
    check(!NT_SUCCESS(prepare(&device,nullptr,nullptr)) && mapCalls==0);
    // All rejected layouts must fail before any mapping, then tolerate Release.
    const auto rejected=[&](FakeResourceList bad) {
        mapCalls=0;
        check(prepare(&device,nullptr,&bad)==STATUS_DEVICE_CONFIGURATION_ERROR);
        check(mapCalls==0 && live.empty() && !owner.Prepared());
        check(NT_SUCCESS(release(&device,nullptr)));
    };
    rejected({{},-1});
    { auto bad=valid(); bad.entries.resize(65); rejected(bad); }
    { auto bad=valid(); bad.nullAt=1; rejected(bad); }
    { auto bad=valid(); bad.entries.pop_back(); rejected(bad); }
    { auto bad=valid(); bad.entries.push_back(memory(0xd3000000,0x4000)); rejected(bad); }
    { auto bad=valid(); std::swap(bad.entries[0],bad.entries[2]); rejected(bad); }
    { auto bad=valid(); bad.entries[0].Type=CmResourceTypeMemoryLarge; rejected(bad); }
    for(auto flags : {CM_RESOURCE_MEMORY_READ_ONLY,CM_RESOURCE_MEMORY_WRITE_ONLY}) {
        auto bad=valid(); bad.entries[2].Flags=flags; rejected(bad);
    }
    for(LONGLONG address : {LONGLONG{0},LONGLONG{-1},LONGLONG{0xd1000001},LONGLONG{0xd2004000}}) {
        auto bad=valid(); bad.entries[0].u.Memory.Start.QuadPart=address; rejected(bad);
    }
    { auto bad=valid(); bad.entries[2].u.Memory.Start.QuadPart=0x7ffffffffff00000LL; rejected(bad); }
    { auto bad=valid(); bad.entries[0].u.Memory.Length=0x4001; rejected(bad); }
    for(unsigned fail=1;fail<=2;++fail) {
        mapCalls=0; failMap=fail; unmaps.clear();
        check(prepare(&device,nullptr,&list)==STATUS_INSUFFICIENT_RESOURCES);
        check(live.empty() && !owner.Prepared() && unmaps.size()==fail-1);
        check(NT_SUCCESS(release(&device,nullptr)) && unmaps.size()==fail-1);
    }
    failMap=0;
    // Repeated PnP start/stop with newly assigned addresses (including >4 GiB).
    for(unsigned cycle=0;cycle<3;++cycle) {
        expectedAddresses={0x100004000LL+LONGLONG{cycle}*0x1000000,0x100100000LL+LONGLONG{cycle}*0x1000000};
        list=valid(); mapCalls=0; unmaps.clear();
        irql=2; check(!NT_SUCCESS(prepare(&device,nullptr,&list)) && mapCalls==0); irql=0;
        // A deliberately unrelated raw list proves mapping uses translated addresses.
        FakeResourceList raw={{memory(0x10000,0x4000),memory(0x200000,0x100000)},-1};
        check(NT_SUCCESS(prepare(&device,&raw,&list)) && owner.Prepared() && live.size()==2);
        check(!NT_SUCCESS(prepare(&device,&raw,&list)) && mapCalls==2 && live.size()==2);
        irql=2; check(!NT_SUCCESS(release(&device,nullptr)) && live.size()==2); irql=0;
        check(NT_SUCCESS(release(&device,nullptr)) && !owner.Prepared() && live.empty());
        check(unmaps==std::vector<SIZE_T>({0x100000,0x4000}));
        check(NT_SUCCESS(release(&device,nullptr)) && unmaps.size()==2);
    }
    std::cout<<"SOF_PNP_RESOURCES_TESTS="<<checks<<" PASS; callbacks=SIMULATED; hardware=NOT_TOUCHED\n";
}
