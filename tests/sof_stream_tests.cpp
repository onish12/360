// SPDX-License-Identifier: MIT
#include "../src/sof/hda_stream.h"
#include "../src/sof/hda_controller.h"
#include <array>
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace phaser360::sof;
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
constexpr uint32_t sd=0x160, pp=0x500, spib=0x700, bit=128;
struct WriteEvent { uint32_t offset; unsigned width; uint32_t value; };
struct Hardware {
    std::array<uint8_t,0x4000> memory{};
    std::vector<WriteEvent> writes;
    unsigned operations=0,failAt=0,delay=0;
    bool resetLow=false,resetHigh=false,runHigh=false,runLow=false,gone=false,dropBdlClear=false;
    bool controllerEnterStuck=false,controllerExitStuck=false;
    Hardware() {
        Set(0,2,0x6701); Set(8,4,1); Set(0x14,4,pp);
        Set(pp,4,0x10030000u|spib); Set(spib,4,0x10040000);
        Set(pp+4,4,0x40000002); // unrelated PROCEN bit must survive
    }
    void Set(uint32_t o,unsigned w,uint32_t v) {
        CHECK(o+w<=memory.size());
        for(unsigned i=0;i<w;++i) memory[o+i]=static_cast<uint8_t>(v>>(8*i));
    }
    uint32_t Get(uint32_t o,unsigned w) {
        CHECK(o+w<=memory.size()); uint32_t value=0;
        for(unsigned i=0;i<w;++i) value|=uint32_t(memory[o+i])<<(8*i);
        return value;
    }
    static bool Read(void* p,uint32_t o,unsigned w,uint32_t* v) {
        auto& h=*static_cast<Hardware*>(p);
        CHECK(o%w==0 && o+w<=h.memory.size());
        if(++h.operations==h.failAt) return false;
        *v=h.gone ? (w==4 ? 0xffffffffu : (uint32_t{1}<<(8*w))-1) : h.Get(o,w);
        return true;
    }
    static bool Write(void* p,uint32_t o,unsigned w,uint32_t v) {
        auto& h=*static_cast<Hardware*>(p);
        CHECK(o%w==0 && o+w<=h.memory.size());
        CHECK(!(o==sd && w!=1)); // no accidental DWORD write across W1C status
        const uint32_t gcap=h.Get(0,2);
        const uint32_t total=((gcap>>8)&15)+((gcap>>12)&15);
        const bool streamStatus=w==1 && o>=0x83 && o<0x80+total*0x20 &&
            ((o-0x83)%0x20)==0;
        if(streamStatus) CHECK(v==0x1c);
        if(o==sd+0xc || o==sd+0x12) CHECK(w==2);
        if(o==sd+0x12) CHECK((h.Get(pp+4,4)&bit)==0); // format quirk ordering
        ++h.operations;
        h.writes.push_back({o,w,v});
        if(h.gone) return false;
        if(o==8 && w==4) {
            const bool requestReady=(v&1)!=0;
            if(!requestReady && h.controllerEnterStuck) v|=1;
            if(requestReady && h.controllerExitStuck) v&=~1u;
            if(!requestReady && !(v&1)) {
                h.Set(0x20,4,0); h.Set(0x38,4,0); h.Set(0x70,4,0); h.Set(0x74,4,0);
                h.Set(pp+4,4,0); h.Set(spib+4,4,0);
                for(uint32_t i=0;i<total;++i) {
                    const uint32_t s=0x80+i*0x20;
                    h.Set(s,4,0); h.Set(s+8,4,0); h.Set(s+0xc,2,0);
                    h.Set(s+0x12,2,0); h.Set(s+0x18,4,0); h.Set(s+0x1c,4,0);
                    h.Set(spib+8+i*8,4,0);
                }
            }
        }
        if(o==sd) {
            if(h.resetLow) v&=~1u;
            if(h.resetHigh) v|=1;
            if(h.runHigh) v|=2;
            if(h.runLow) v&=~2u;
        }
        if(streamStatus) h.Set(o,w,h.Get(o,w)&~v);
        else if(!(h.dropBdlClear && o==sd+0x18 && v==0)) h.Set(o,w,v);
        // Failure may occur AFTER a posted write took effect.
        return h.operations!=h.failAt;
    }
    static void Delay(void* p,unsigned us) {
        auto& h=*static_cast<Hardware*>(p);
        CHECK(us==3 || us==10 || us==500 || us==1000); h.delay+=us;
    }
    RegisterIo Io() { return {this,Read,Write,Delay,0x4000}; }
};
static void Prepare(Hardware& h,BootStream& s) {
    CHECK(s.Select(h.Io())); CHECK(h.writes.empty());
    CHECK(s.Configure(0x200000,286720,69));
}
int main() {
    unsigned selectOps=0,configOps=0,startOps=0,stopOps=0;

    // H15B takes ownership from a dirty controller baseline before BootStream.
    {
        Hardware h; HdaController controller; BootStream stream;
        h.Set(8,4,1); h.Set(0x20,4,0xc0000080u); h.Set(0x38,4,0x80);
        h.Set(0x70,4,1); h.Set(0x1030,4,0x2000);
        h.Set(sd,1,0x1e); h.Set(sd+2,1,0xf0); h.Set(sd+0x18,4,0x12345000);
        h.Set(pp+4,4,0xffffffffu); h.Set(spib+4,4,0xffffffffu);
        h.Set(spib+8+7*8,4,0x1234);
        CHECK(controller.Initialize(h.Io()));
        CHECK(controller.Ready() && (h.Get(8,4)&1)==1);
        CHECK(h.Get(0x20,4)==0 && h.Get(0x38,4)==0 && (h.Get(0x70,4)&1)==0);
        CHECK((h.Get(0x1030,4)&0x2000)==0);
        CHECK((h.Get(pp+4,4)&0xc0001fffu)==0x40000000u);
        CHECK((h.Get(spib+4,4)&0x1fffu)==0 && h.Get(spib+8+7*8,4)==0);
        CHECK(h.Get(sd,1)==0 && h.Get(sd+0x18,4)==0);
        CHECK(stream.Select(h.Io()));
        CHECK(controller.Quiesce());
        CHECK((h.Get(pp+4,4)&0xc0001fffu)==0 && (h.Get(0x1030,4)&0x2000)==0x2000);
    }

    for(unsigned mode=0;mode<2;++mode) {
        Hardware h; HdaController controller;
        h.controllerEnterStuck=mode==0; h.controllerExitStuck=mode==1;
        CHECK(!controller.Initialize(h.Io()));
        h.controllerEnterStuck=false; h.controllerExitStuck=false;
        CHECK(controller.Quiesce());
    }
    {
        Hardware h; HdaController controller;
        h.Set(pp,4,0x10050000u|spib);
        CHECK(!controller.Initialize(h.Io()));
        CHECK(controller.Quiesce());
    }

    {
        Hardware h; BootStream s;
        CHECK(!s.Start() && !s.StopDetach() && !s.IsDetached());
        CHECK(s.Select(h.Io())); selectOps=h.operations;
        CHECK(h.writes.empty() && s.Tag()==1);
        CHECK(!s.Configure(0x200080,286720,69)); CHECK(h.writes.empty());
        CHECK(!s.Configure(0x200000,286720,70)); CHECK(h.writes.empty());
        CHECK(s.Configure(0x200000,286720,69)); configOps=h.operations-selectOps;
        CHECK(s.State()==StreamState::Prepared && h.delay==3);
        CHECK(h.Get(sd+8,4)==286720 && h.Get(sd+0xc,2)==69);
        CHECK(h.Get(sd+0x12,2)==0x40 && h.Get(spib+8+7*8,4)==286720);
        CHECK(h.Get(pp+4,4)==(0x40000002u|bit));
        unsigned before=h.operations;
        CHECK(s.Start()); startOps=h.operations-before;
        CHECK(s.State()==StreamState::Running && h.Get(sd,1)==2);
        CHECK(!s.Start());
        before=h.operations;
        CHECK(s.StopDetach()); stopOps=h.operations-before;
        CHECK(s.IsDetached() && h.Get(sd+0x18,4)==0);
        CHECK(h.Get(pp+4,4)==0x40000002u && h.Get(spib+4,4)==0);
        h.Set(sd,1,2); CHECK(!s.IsDetached());
    }
    // Every read in selection must fail without causing writes.
    for(unsigned i=1;i<=selectOps;++i) {
        Hardware h; BootStream s; h.failAt=i;
        CHECK(!s.Select(h.Io())); CHECK(h.writes.empty());
    }
    // Every configuration I/O may fail, including a write already applied.
    for(unsigned i=1;i<=configOps;++i) {
        Hardware h; BootStream s; CHECK(s.Select(h.Io())); h.failAt=h.operations+i;
        CHECK(!s.Configure(0x200000,286720,69)); CHECK(!s.IsDetached());
        CHECK(s.StopDetach()); CHECK(s.IsDetached());
    }
    for(unsigned i=1;i<=startOps;++i) {
        Hardware h; BootStream s; Prepare(h,s); h.failAt=h.operations+i;
        CHECK(!s.Start()); CHECK(!s.IsDetached());
        CHECK(s.StopDetach() && s.IsDetached());
    }
    for(unsigned i=1;i<=stopOps;++i) {
        Hardware h; BootStream s; Prepare(h,s); CHECK(s.Start()); h.failAt=h.operations+i;
        CHECK(!s.StopDetach()); CHECK(!s.IsDetached());
        CHECK(s.StopDetach() && s.IsDetached());
    }
    // Refuse malformed capability chains and active global DMA policy.
    for(unsigned mode=0;mode<10;++mode) {
        Hardware h; BootStream s;
        switch(mode) {
        case 0: h.Set(pp,4,0x10030000u|pp); break;
        case 1: h.Set(0x14,4,0x160); break;
        case 2: h.Set(spib,4,0x10030000); break;
        case 3: h.Set(pp,4,0x10030000u|0x504); break;
        case 4: h.Set(0,2,0x0701); break;
        case 5: h.Set(sd,1,2); break;
        case 6: h.Set(0x70,4,1); break;
        case 7: h.Set(0x1030,4,0x2000); break;
        case 8: h.Set(0x20,4,bit); break;
        case 9: h.Set(pp+4,4,0); break;
        }
        CHECK(!s.Select(h.Io())); CHECK(h.writes.empty());
    }
    for(unsigned mode=0;mode<2;++mode) {
        Hardware h; BootStream s; CHECK(s.Select(h.Io()));
        h.resetLow=mode==0; h.resetHigh=mode==1;
        CHECK(!s.Configure(0x200000,286720,69)); CHECK(!s.IsDetached());
        CHECK(h.delay>=10000 && h.delay<=10003);
        h.resetLow=false; h.resetHigh=false;
        CHECK(s.StopDetach());
    }
    {
        Hardware h; BootStream s; Prepare(h,s); h.runLow=true;
        CHECK(!s.Start()); CHECK(h.delay==10003); CHECK(s.StopDetach());
    }
    {
        Hardware h; BootStream s; Prepare(h,s); CHECK(s.Start()); h.runHigh=true;
        const auto before=h.writes.size();
        CHECK(!s.StopDetach()); CHECK(!s.IsDetached());
        CHECK(h.writes.size()==before+1 && h.Get(sd+0x18,4)==0x200000);
        CHECK(h.delay==10003);
        h.runHigh=false; CHECK(s.StopDetach());
    }
    {
        Hardware h; BootStream s; Prepare(h,s); CHECK(s.Start()); h.dropBdlClear=true;
        CHECK(!s.StopDetach() && !s.IsDetached());
        h.dropBdlClear=false; CHECK(s.StopDetach());
        h.gone=true; CHECK(!s.IsDetached());
    }
    {
        Hardware h; BootStream s; Prepare(h,s); h.Set(sd+3,1,0x18);
        CHECK(!s.Start()); CHECK(s.StopDetach());
    }
    std::printf("SOF_STREAM_TESTS=%u PASS; fault_points=%u; hardware=SIMULATED\n",
                checks,selectOps+configOps+startOps+stopOps);
}
