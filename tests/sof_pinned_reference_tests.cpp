// SPDX-License-Identifier: MIT
// Real Windows CNG + production snapshot owner; boot/WDF allocation simulated.
#include "../m062/driver/pinned_firmware.h"
#include <vector>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstdlib>
using namespace phaser360::windows;
static unsigned checks=0,live=0,entries=0;
static const UCHAR* original=nullptr;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
struct FakeObject { std::vector<UCHAR> bytes; };
unsigned KeGetCurrentIrql() { return 0; }
NTSTATUS WdfMemoryCreate(WDF_OBJECT_ATTRIBUTES*,unsigned,ULONG,SIZE_T n,WDFMEMORY* out,void** p) {
    *out=new FakeObject{std::vector<UCHAR>(n)}; *p=(*out)->bytes.data(); ++live; return STATUS_SUCCESS;
}
void WdfObjectDelete(FakeObject* p) { CHECK(live>0); --live; delete p; }
namespace phaser360 { namespace windows {
NTSTATUS ColdPower::Enter(WDFDEVICE,UCHAR*,ULONG,UCHAR*,ULONG,const UCHAR* payload,SIZE_T bytes,
                          const UCHAR* xman,SIZE_T xmanBytes,USHORT minor) noexcept {
    ++entries;
    CHECK(xman!=original && payload==xman+768 && bytes==286720 && xmanBytes==768 && minor==20);
    CHECK(MatchesFirmwarePin(xman,kPinnedImageBytes));
    return STATUS_SUCCESS;
}
}}
int main(int argc,char** argv) {
    CHECK(argc==2); std::ifstream file(argv[1],std::ios::binary); CHECK(file.good());
    std::vector<UCHAR> input{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    CHECK(input.size()==kPinnedImageBytes); original=input.data();
    HardwareAccessGate gate; CHECK(gate.OpenForPrepare());
    GlkBoot boot; IpcInterrupt irq; ColdPower power(boot,irq,gate);
    PinnedFirmware owner; CHECK(owner.Load(&checks,input.data(),input.size())==STATUS_SUCCESS);
    input[0]^=1; input[768]^=1;
    CHECK(owner.Enter(power,&checks,nullptr,0,nullptr,0)==STATUS_SUCCESS && entries==1);
    PinnedFirmware corrupt; CHECK(corrupt.Load(&checks,input.data(),input.size())==STATUS_INVALID_IMAGE_HASH);
    CHECK(corrupt.Enter(power,&checks,nullptr,0,nullptr,0)==STATUS_INVALID_DEVICE_STATE && entries==1);
    CHECK(owner.Release() && corrupt.Release() && live==0);
    std::printf("SOF_PINNED_REFERENCE_TESTS=%u PASS; crypto=REAL_WINDOWS_CNG; boot=SIMULATED; hardware=NONE\n",checks);
}
