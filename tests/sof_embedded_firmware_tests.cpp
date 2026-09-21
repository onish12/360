// SPDX-License-Identifier: MIT
// H7: generated embedded source -> production staging contract -> owned CNG pin.
#include "../m062/driver/firmware_source.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace phaser360::windows;

static unsigned checks=0,live=0,providerCalls=0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)

struct FakeObject { std::vector<UCHAR> bytes; };

unsigned KeGetCurrentIrql() { return PASSIVE_LEVEL; }

NTSTATUS WdfMemoryCreate(WDF_OBJECT_ATTRIBUTES* a,unsigned pool,ULONG tag,SIZE_T n,
                         WDFMEMORY* out,void** storage) {
    CHECK(a && a->ParentObject && pool==NonPagedPoolNx &&
          tag==0x46534850u && n==kPinnedImageBytes && out && storage);
    *out=new FakeObject{std::vector<UCHAR>(n)};
    *storage=(*out)->bytes.data();
    ++live;
    return STATUS_SUCCESS;
}

void WdfObjectDelete(FakeObject* object) {
    CHECK(object && live>0);
    --live;
    delete object;
}

namespace phaser360 { namespace windows {
NTSTATUS ColdPower::Enter(WDFDEVICE,UCHAR*,ULONG,UCHAR*,ULONG,const UCHAR*,SIZE_T,
                          const UCHAR*,SIZE_T,USHORT) noexcept {
    return STATUS_DEVICE_CONFIGURATION_ERROR;
}
}}

static bool FalseProvider(FirmwareSourceView* out) noexcept {
    ++providerCalls;
    if(out) *out=FirmwareSourceView{};
    return false;
}

static bool NullProvider(FirmwareSourceView* out) noexcept {
    ++providerCalls;
    if(!out) return false;
    out->data=nullptr;
    out->bytes=kPinnedImageBytes;
    return true;
}

static bool WrongExtentProvider(FirmwareSourceView* out) noexcept {
    ++providerCalls;
    static const UCHAR byte=0;
    if(!out) return false;
    out->data=&byte;
    out->bytes=kPinnedImageBytes-1;
    return true;
}

int main() {
    FirmwareSourceView embedded{};
    CHECK(!GetEmbeddedFirmwareSource(nullptr));
    CHECK(GetEmbeddedFirmwareSource(&embedded));
    CHECK(embedded.data!=nullptr && embedded.bytes==kPinnedImageBytes);
    CHECK(MatchesFirmwarePin(embedded.data,static_cast<unsigned>(embedded.bytes)));

    PinnedFirmware owner;
    CHECK(!owner.Loaded());
    CHECK(StagePinnedFirmwareFromSource(owner,&checks,nullptr)==STATUS_INVALID_DEVICE_STATE);

    providerCalls=0;
    CHECK(StagePinnedFirmwareFromSource(owner,&checks,FalseProvider)==STATUS_INVALID_DEVICE_STATE);
    CHECK(providerCalls==1 && !owner.Loaded() && live==0);

    providerCalls=0;
    CHECK(StagePinnedFirmwareFromSource(owner,&checks,NullProvider)==STATUS_INVALID_IMAGE_HASH);
    CHECK(providerCalls==1 && !owner.Loaded() && live==0);

    providerCalls=0;
    CHECK(StagePinnedFirmwareFromSource(owner,&checks,WrongExtentProvider)==STATUS_INVALID_IMAGE_HASH);
    CHECK(providerCalls==1 && !owner.Loaded() && live==0);

    CHECK(StagePinnedFirmwareFromSource(owner,&checks,GetEmbeddedFirmwareSource)==STATUS_SUCCESS);
    CHECK(owner.Loaded() && live==1);

    // Once the owned snapshot exists, the source provider is not called again.
    providerCalls=0;
    CHECK(StagePinnedFirmwareFromSource(owner,&checks,FalseProvider)==STATUS_INVALID_DEVICE_STATE);
    CHECK(providerCalls==0 && owner.Loaded() && live==1);

    CHECK(owner.Release() && !owner.Loaded() && live==0);

    std::printf("H7_EMBEDDED_FIRMWARE_TESTS=%u PASS; source=GENERATED_BUILD_TIME; "
                "runtime_file_io=NONE; crypto=REAL_WINDOWS_CNG; hardware=NONE\n",checks);
}
