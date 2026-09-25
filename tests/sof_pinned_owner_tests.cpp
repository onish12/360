// SPDX-License-Identifier: MIT
#include "../m062/driver/pinned_firmware.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace phaser360::windows;
static unsigned checks=0,irql=0,live=0,hashCalls=0,bootCalls=0;
static bool allocationFailure=false,pinMatch=true;
static const UCHAR* caller=nullptr;
static PinnedFirmware* current=nullptr;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
struct FakeObject { std::vector<UCHAR> bytes; };
unsigned KeGetCurrentIrql() { return irql; }
NTSTATUS WdfMemoryCreate(WDF_OBJECT_ATTRIBUTES* a,unsigned pool,ULONG tag,SIZE_T n,WDFMEMORY* out,void** p) {
    CHECK(a->ParentObject && pool==NonPagedPoolNx && tag==0x46534850u && n==kPinnedImageBytes);
    if(allocationFailure) return STATUS_INSUFFICIENT_RESOURCES;
    *out=new FakeObject{std::vector<UCHAR>(n)}; *p=(*out)->bytes.data(); ++live; return STATUS_SUCCESS;
}
void WdfObjectDelete(FakeObject* p) { CHECK(live==1); --live; delete p; }
namespace phaser360 { namespace windows {
bool MatchesFirmwarePin(const unsigned char* p,unsigned n) noexcept {
    ++hashCalls; CHECK(p!=caller && n==kPinnedImageBytes && p[0]==0x55 && p[n-1]==0x55);
    return pinMatch; // ownership tests only; real CNG tested separately on Windows
}
NTSTATUS ColdPower::Enter(WDFDEVICE,UCHAR*,ULONG,UCHAR*,ULONG,const UCHAR* payload,SIZE_T bytes,
                          const UCHAR* xman,SIZE_T xmanBytes,USHORT minor) noexcept {
    ++bootCalls; CHECK(bytes==kPinnedPayloadBytes && xmanBytes==kPinnedXmanBytes && minor==20);
    CHECK(payload==xman+768 && xman!=caller && xman[0]==0x55 && payload[0]==0x55);
    CHECK(payload[bytes-1]==0x55); CHECK(!current->Release());
    return STATUS_DEVICE_CONFIGURATION_ERROR; // propagate boot failure without losing owned image
}
}}
int main() {
    std::vector<UCHAR> input(kPinnedImageBytes,0x55); caller=input.data();
    HardwareAccessGate gate; CHECK(gate.OpenForPrepare());
    GlkBoot boot; IpcInterrupt irq; ColdPower power(boot,irq,gate); PinnedFirmware image; current=&image;
    CHECK(!image.Loaded());
    CHECK(image.Enter(power,&checks,nullptr,0,nullptr,0)==STATUS_INVALID_DEVICE_STATE && bootCalls==0);
    CHECK(image.Load(&checks,input.data(),input.size()-1)==STATUS_INVALID_PARAMETER && live==0);
    irql=2; CHECK(image.Load(&checks,input.data(),input.size())==STATUS_INVALID_DEVICE_STATE); irql=0;
    allocationFailure=true; CHECK(image.Load(&checks,input.data(),input.size())==STATUS_INSUFFICIENT_RESOURCES);
    CHECK(hashCalls==0 && live==0); allocationFailure=false;
    pinMatch=false; CHECK(image.Load(&checks,input.data(),input.size())==STATUS_INVALID_IMAGE_HASH && live==0);
    CHECK(image.Enter(power,&checks,nullptr,0,nullptr,0)==STATUS_INVALID_DEVICE_STATE && bootCalls==0);
    pinMatch=true; CHECK(image.Load(&checks,input.data(),input.size())==STATUS_SUCCESS && live==1);
    CHECK(image.Loaded());
    CHECK(image.Load(&checks,input.data(),input.size())==STATUS_INVALID_DEVICE_STATE);
    input.assign(input.size(),0xcc); // changing the original cannot change authorized bytes
    CHECK(image.Enter(power,&live,nullptr,0,nullptr,0)==STATUS_INVALID_DEVICE_STATE && bootCalls==0);
    CHECK(image.Enter(power,&checks,nullptr,0,nullptr,0)==STATUS_DEVICE_CONFIGURATION_ERROR && bootCalls==1);
    CHECK(image.Enter(power,&checks,nullptr,0,nullptr,0)==STATUS_DEVICE_CONFIGURATION_ERROR && bootCalls==2);
    irql=2; CHECK(!image.Release()); irql=0;
    CHECK(image.Release() && live==0 && image.Release());
    CHECK(!image.Loaded());
    CHECK(image.Enter(power,&checks,nullptr,0,nullptr,0)==STATUS_INVALID_DEVICE_STATE && bootCalls==2);
    std::printf("SOF_PINNED_OWNER_TESTS=%u PASS; hash=SIMULATED; hardware=NONE\n",checks);
}
