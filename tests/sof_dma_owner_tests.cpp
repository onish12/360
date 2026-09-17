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
static unsigned checks=0, calls=0, failAt=0, irql=0, barriers=0, verifierCalls=0;
static bool badAddress=false, verified=false;
static std::vector<FakeObject*> live;
static std::vector<unsigned> deleted;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
unsigned KeGetCurrentIrql() { return irql; }
void KeMemoryBarrier() { ++barriers; }
static NTSTATUS Allocate(size_t n,FakeObject** out) {
    ++calls;
    if (calls == failAt) return STATUS_INSUFFICIENT_RESOURCES;
    *out = new FakeObject{calls,std::vector<UCHAR>(n,0xaa)};
    live.push_back(*out); return STATUS_SUCCESS;
}
NTSTATUS WdfDmaEnablerCreate(WDFDEVICE,WDF_DMA_ENABLER_CONFIG* c,void*,WDFDMAENABLER* out) {
    CHECK(c->profile == WdfDmaProfileScatterGather && c->maximum == 1048576);
    return Allocate(0,out);
}
NTSTATUS WdfCommonBufferCreateWithConfig(WDFDMAENABLER p,size_t n,WDF_COMMON_BUFFER_CONFIG* c,void*,WDFCOMMONBUFFER* out) {
    CHECK(p && c->alignment == 4095);
    return Allocate(n,out);
}
PHYSICAL_ADDRESS WdfCommonBufferGetAlignedLogicalAddress(WDFCOMMONBUFFER b) {
    return {badAddress ? 0x100000000ll : int64_t(b->id)*0x100000};
}
void* WdfCommonBufferGetAlignedVirtualAddress(WDFCOMMONBUFFER b) { return b->data.data(); }
void WdfObjectDelete(FakeObject* o) {
    auto i=std::find(live.begin(),live.end(),o);
    CHECK(i != live.end()); deleted.push_back(o->id); live.erase(i); delete o;
}
static bool Verify(void*) { ++verifierCalls; return verified; }
static void Reset() {
    CHECK(live.empty());
    calls=0;failAt=0;irql=0;barriers=0;verifierCalls=0;
    badAddress=false;verified=false;deleted.clear();
}
int main() {
    std::vector<UCHAR> payload(286720,0x5a);
    int device = 0;
    for (unsigned failure=1;failure<=3;++failure) {
        Reset(); failAt=failure; BootDma owner;
        CHECK(owner.Prepare(&device,payload.data(),payload.size()) == STATUS_INSUFFICIENT_RESOURCES);
        CHECK(live.empty());
        CHECK(owner.Release(nullptr,nullptr) == STATUS_SUCCESS);
    }
    Reset(); {
        BootDma owner; badAddress=true;
        CHECK(owner.Prepare(&device,payload.data(),payload.size()) == STATUS_DEVICE_CONFIGURATION_ERROR);
        CHECK(live.empty());
        CHECK(deleted == std::vector<unsigned>({3,2,1}));
    }
    Reset(); {
        BootDma owner; BootDmaView view{};
        irql=2; CHECK(owner.Prepare(&device,payload.data(),payload.size()) == STATUS_INVALID_DEVICE_STATE);
        irql=0;
        CHECK(owner.Prepare(&device,nullptr,1) == STATUS_INVALID_PARAMETER);
        CHECK(owner.Prepare(&device,payload.data(),1048577) == STATUS_INVALID_PARAMETER);
        CHECK(calls == 0);
        CHECK(owner.Publish(&view) == STATUS_INVALID_DEVICE_STATE);
        CHECK(owner.Prepare(&device,payload.data(),payload.size()) == STATUS_SUCCESS);
        CHECK(live.size() == 3 && live[1]->data == payload);
        CHECK(live[2]->data[2] == 0x20); // payload device address 0x200000.
        CHECK(owner.Prepare(&device,payload.data(),payload.size()) == STATUS_INVALID_DEVICE_STATE);
        CHECK(owner.Publish(nullptr) == STATUS_INVALID_PARAMETER);
        CHECK(owner.Publish(&view) == STATUS_SUCCESS);
        CHECK(view.bdlLogical == 0x300000 && view.payloadBytes == payload.size() && view.lastValidIndex == 69);
        CHECK(barriers == 1);
        CHECK(owner.Publish(&view) == STATUS_INVALID_DEVICE_STATE);
        CHECK(owner.Release(nullptr,nullptr) == STATUS_DEVICE_BUSY && live.size() == 3);
        CHECK(owner.Release(Verify,nullptr) == STATUS_DEVICE_BUSY && live.size() == 3 && verifierCalls == 1);
        irql=2; verified=true;
        CHECK(owner.Release(Verify,nullptr) == STATUS_INVALID_DEVICE_STATE && verifierCalls == 1);
        irql=0;
        CHECK(owner.Release(Verify,nullptr) == STATUS_SUCCESS && live.empty());
        CHECK(barriers == 2 && deleted == std::vector<unsigned>({3,2,1}));
        CHECK(owner.Release(nullptr,nullptr) == STATUS_SUCCESS); // idempotent
    }
    Reset(); {
        BootDma owner;
        CHECK(owner.Prepare(&device,payload.data(),payload.size()) == STATUS_SUCCESS);
        CHECK(owner.Release(nullptr,nullptr) == STATUS_SUCCESS && live.empty()); // never published
        CHECK(verifierCalls == 0);
    }
    std::printf("SOF_DMA_OWNER_TESTS=%u PASS (WDF shim, not hardware)\n",checks);
}
