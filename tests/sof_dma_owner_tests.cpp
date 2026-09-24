// SPDX-License-Identifier: MIT
// Executes production boot_dma.cpp against a fault-injecting API shim.
// This is NOT Windows kernel or hardware execution.
#include "../m062/driver/boot_dma.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
using phaser360::windows::BootDma;
using phaser360::windows::BootDmaView;
struct FakeObject { unsigned id; std::vector<UCHAR> data; };
static unsigned checks=0,calls=0,failAt=0,irql=0,barriers=0,verifierCalls=0;
static unsigned alignmentCalls=0,alignmentValue=0;
static bool badAddress=false,verified=false;
static std::vector<FakeObject*> live;
static std::vector<unsigned> deleted;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
unsigned KeGetCurrentIrql() { return irql; }
void KeMemoryBarrier() { ++barriers; }
void WdfDeviceSetAlignmentRequirement(WDFDEVICE device,ULONG alignment) {
    CHECK(device!=nullptr);
    ++alignmentCalls; alignmentValue=alignment;
}
static NTSTATUS Allocate(size_t n,FakeObject** out) {
    ++calls;
    if(calls==failAt) return STATUS_INSUFFICIENT_RESOURCES;
    *out=new FakeObject{calls,std::vector<UCHAR>(n,0xaa)};
    live.push_back(*out); return STATUS_SUCCESS;
}
NTSTATUS WdfDmaEnablerCreate(WDFDEVICE,WDF_DMA_ENABLER_CONFIG* c,void*,WDFDMAENABLER* out) {
    CHECK(c->profile==WdfDmaProfileScatterGather && c->maximum==1048576);
    return Allocate(0,out);
}
NTSTATUS WdfCommonBufferCreateWithConfig(WDFDMAENABLER p,size_t n,WDF_COMMON_BUFFER_CONFIG* c,void*,WDFCOMMONBUFFER* out) {
    CHECK(p && c->alignment==FILE_4096_BYTE_ALIGNMENT);
    return Allocate(n,out);
}
PHYSICAL_ADDRESS WdfCommonBufferGetAlignedLogicalAddress(WDFCOMMONBUFFER b) {
    return {badAddress ? 0x100000000ll : int64_t(b->id)*0x100000};
}
void* WdfCommonBufferGetAlignedVirtualAddress(WDFCOMMONBUFFER b) { return b->data.data(); }
void WdfObjectDelete(FakeObject* o) {
    auto i=std::find(live.begin(),live.end(),o);
    CHECK(i!=live.end()); deleted.push_back(o->id); live.erase(i); delete o;
}
static bool Verify(void*) { ++verifierCalls; return verified; }
static void Reset() {
    CHECK(live.empty());
    calls=0;failAt=0;irql=0;barriers=0;verifierCalls=0;
    alignmentCalls=0;alignmentValue=0;
    badAddress=false;verified=false;deleted.clear();
}
int main() {
    std::vector<UCHAR> payload(286720,0x5a);
    int device=0;

    for(unsigned failure=1;failure<=3;++failure) {
        Reset(); failAt=failure; BootDma owner;
        CHECK(owner.PrepareHardware(&device,payload.size())==STATUS_INSUFFICIENT_RESOURCES);
        CHECK(alignmentCalls==1 && alignmentValue==FILE_4096_BYTE_ALIGNMENT);
        CHECK(live.empty());
        CHECK(owner.ReleaseHardware()==STATUS_SUCCESS);
    }

    Reset(); {
        BootDma owner; badAddress=true;
        CHECK(owner.PrepareHardware(&device,payload.size())==STATUS_DEVICE_CONFIGURATION_ERROR);
        CHECK(live.empty());
        CHECK(deleted==std::vector<unsigned>({3,2,1}));
    }

    Reset(); {
        BootDma owner; BootDmaView view{};
        irql=2;
        CHECK(owner.PrepareHardware(&device,payload.size())==STATUS_INVALID_DEVICE_STATE);
        irql=0;
        CHECK(owner.PrepareHardware(nullptr,payload.size())==STATUS_INVALID_PARAMETER);
        CHECK(owner.PrepareHardware(&device,0)==STATUS_INVALID_PARAMETER);
        CHECK(owner.PrepareHardware(&device,1048577)==STATUS_INVALID_PARAMETER);
        CHECK(calls==0 && alignmentCalls==0);
        CHECK(owner.Stage(payload.data(),payload.size())==STATUS_INVALID_DEVICE_STATE);
        CHECK(owner.Publish(&view)==STATUS_INVALID_DEVICE_STATE);

        CHECK(owner.PrepareHardware(&device,payload.size())==STATUS_SUCCESS);
        CHECK(owner.HardwarePrepared() && !owner.SessionActive());
        CHECK(alignmentCalls==1 && alignmentValue==FILE_4096_BYTE_ALIGNMENT);
        CHECK(live.size()==3);

        CHECK(owner.Stage(nullptr,1)==STATUS_INVALID_PARAMETER);
        CHECK(owner.Stage(payload.data(),payload.size()+1)==STATUS_INVALID_PARAMETER);
        CHECK(owner.Stage(payload.data(),payload.size())==STATUS_SUCCESS);
        CHECK(owner.SessionActive());
        CHECK(live[1]->data==payload);
        CHECK(live[2]->data[2]==0x20); // payload device address 0x200000.
        CHECK(owner.Stage(payload.data(),payload.size())==STATUS_INVALID_DEVICE_STATE);

        CHECK(owner.Publish(nullptr)==STATUS_INVALID_PARAMETER);
        CHECK(owner.Publish(&view)==STATUS_SUCCESS);
        CHECK(view.bdlLogical==0x300000 &&
              view.payloadBytes==payload.size() && view.lastValidIndex==69);
        CHECK(barriers==1);
        CHECK(owner.Publish(&view)==STATUS_INVALID_DEVICE_STATE);

        CHECK(owner.ReleaseHardware()==STATUS_DEVICE_BUSY && live.size()==3);
        CHECK(owner.ReleaseSession(nullptr,nullptr)==STATUS_DEVICE_BUSY && live.size()==3);
        CHECK(owner.ReleaseSession(Verify,nullptr)==STATUS_DEVICE_BUSY &&
              live.size()==3 && verifierCalls==1);
        irql=2; verified=true;
        CHECK(owner.ReleaseSession(Verify,nullptr)==STATUS_INVALID_DEVICE_STATE &&
              verifierCalls==1);
        irql=0;
        CHECK(owner.ReleaseSession(Verify,nullptr)==STATUS_SUCCESS);
        CHECK(!owner.SessionActive() && owner.HardwarePrepared() && live.size()==3);
        CHECK(barriers==2);
        CHECK(std::all_of(live[1]->data.begin(),live[1]->data.end(),[](UCHAR v){return v==0;}));
        CHECK(std::all_of(live[2]->data.begin(),live[2]->data.end(),[](UCHAR v){return v==0;}));

        CHECK(owner.ReleaseHardware()==STATUS_SUCCESS && live.empty());
        CHECK(deleted==std::vector<unsigned>({3,2,1}));
        CHECK(owner.ReleaseHardware()==STATUS_SUCCESS);
    }

    Reset(); {
        BootDma owner;
        CHECK(owner.PrepareHardware(&device,payload.size())==STATUS_SUCCESS);
        CHECK(owner.Stage(payload.data(),payload.size())==STATUS_SUCCESS);
        CHECK(owner.ReleaseSession(nullptr,nullptr)==STATUS_SUCCESS);
        CHECK(owner.HardwarePrepared() && live.size()==3 && verifierCalls==0);
        CHECK(owner.ReleaseHardware()==STATUS_SUCCESS && live.empty());
    }

    std::printf("SOF_DMA_OWNER_TESTS=%u PASS (WDF shim, not hardware)\n",checks);
}
