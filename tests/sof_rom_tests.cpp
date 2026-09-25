// SPDX-License-Identifier: MIT
#include "../src/sof/glk_rom.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace phaser360::sof;
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
struct Model {
    std::vector<uint32_t> words=std::vector<uint32_t>(0x80004/4,0);
    unsigned ops=0,failAt=0,writes=0,delays=0;
    uint64_t time=10000;
    bool done=true,init=true,halt=false,power=true,reset=true,gone=false,frozen=false,back=false;
    bool dropClear=false,delayFails=false;
    Model() { words[1]=0x00c0003c; words[0x4c/4]=0x40000000; }
    static bool Read(void* p,uint32_t o,uint32_t* v) {
        auto& m=*static_cast<Model*>(p); CHECK(!(o&3) && o<0x80004);
        if(++m.ops==m.failAt) return false;
        *v=m.gone ? 0xffffffffu : m.words[o/4]; return true;
    }
    static bool Write(void* p,uint32_t o,uint32_t v) {
        auto& m=*static_cast<Model*>(p); CHECK(!(o&3) && o<0x80004);
        ++m.ops; ++m.writes;
        if(o==0x4c) { CHECK(v==0x40000000); if(!m.dropClear) m.words[o/4]&=~v; }
        else if(o==4) {
            const auto old=m.words[1];
            if(!m.reset) v=(v&~3u)|(old&3);
            v=(v&~0x03000000u)|(m.power ? ((v&0x30000)<<8) : (old&0x03000000));
            m.words[1]=v;
            if((v&0x01010101)==0x01010000 && (m.words[0x48/4]&0x80000000u)) {
                if(m.done) { m.words[0x4c/4]|=0x40000000; m.words[0x48/4]&=0x7fffffff; }
                m.words[0x80000/4]=m.halt ? 0x80000001u : (m.init ? 1u : 0u);
            }
        } else m.words[o/4]=v;
        return m.ops!=m.failAt; // posted write may have happened on failure
    }
    static bool Delay(void* p,unsigned us) {
        auto& m=*static_cast<Model*>(p); CHECK(us==500); ++m.delays;
        if(!m.frozen) m.time+=us;
        return !m.delayFails;
    }
    static uint64_t Now(void* p) {
        auto& m=*static_cast<Model*>(p);
        return m.back && m.delays ? 1 : m.time;
    }
    RomIo Io() { return {this,Read,Write,Delay,Now,0x100000}; }
};
static void Cold(Model& m,GlkRom& r) { CHECK(r.Bind(m.Io())); CHECK(r.PowerDown()); }
int main() {
    unsigned initOps=0,downOps=0;
    {
        Model m; GlkRom r; CHECK(!r.Initialize(1)); CHECK(!r.WaitEntered());
        CHECK(r.Bind(m.Io())); CHECK(m.writes==0);
        CHECK(r.PowerDown()); downOps=m.ops;
        CHECK(r.State()==RomState::Cold && (m.words[1]&0x03030303)==0x303);
        CHECK(!r.Initialize(0)); CHECK(!r.Initialize(16));
        const auto before=m.ops; CHECK(r.Initialize(3)); initOps=m.ops-before;
        CHECK(r.State()==RomState::DownloadReady);
        CHECK((m.words[0x48/4]&0x7fffffff)==0x01004400);
        CHECK((m.words[1]&0x03030303)==0x01010202);
        CHECK((m.words[1]&0x00c0003c)==0x00c0003c);
        for(unsigned i=0;i<6;++i) CHECK((m.words[(0x2004+i*0x1000)/4]&0x03000000)==0x03000000);
        CHECK((m.words[0x4c/4]&0x40000000)==0);
        m.words[0x80000/4]=5; CHECK(r.WaitEntered()); CHECK(r.State()==RomState::Entered);
        CHECK(r.PowerDown());
    }
    for(unsigned i=1;i<=downOps;++i) {
        Model m; GlkRom r; CHECK(r.Bind(m.Io())); m.failAt=i;
        CHECK(!r.PowerDown()); CHECK(r.State()==RomState::Fault);
        CHECK(r.PowerDown());
    }
    for(unsigned i=1;i<=initOps;++i) {
        Model m; GlkRom r; Cold(m,r); m.failAt=m.ops+i;
        CHECK(!r.Initialize(1)); CHECK(r.State()==RomState::Fault);
        CHECK(r.PowerDown());
    }
    for(unsigned mode=0;mode<7;++mode) {
        Model m; GlkRom r; Cold(m,r);
        if(mode==0) m.done=false; // stale DONE must not satisfy new command
        if(mode==1) m.init=false;
        if(mode==2) m.halt=true;
        if(mode==3) m.power=false;
        if(mode==4) m.dropClear=true;
        if(mode==5) { m.done=false; m.delayFails=true; }
        if(mode==6) { m.done=false; m.back=true; }
        CHECK(!r.Initialize(1));
        CHECK(r.Error()==(mode==2 ? RomError::Halted : mode==5 ? RomError::Io :
                         mode==6 ? RomError::Clock : RomError::Timeout));
        if(mode==0) CHECK(r.Phase()==RomPhase::InitWaitRomDone);
        if(mode==3) CHECK(r.Phase()==RomPhase::InitPowerUpCores);
        if(mode==4) CHECK(r.Phase()==RomPhase::InitClearStaleDone);
        CHECK(m.delays<=1000);
    }
    {
        Model m; GlkRom r; Cold(m,r); m.words[0x48/4]=0x80000000;
        auto w=m.writes; CHECK(!r.Initialize(1)); CHECK(m.writes==w);
        CHECK(r.Error()==RomError::Precondition);
        CHECK(r.Phase()==RomPhase::InitPreHipci);
    }
    {
        Model m; GlkRom r; Cold(m,r); CHECK(r.Initialize(1));
        CHECK(!r.WaitEntered()); CHECK(r.Error()==RomError::Timeout); CHECK(m.delays==6000);
    }
    {
        Model m; GlkRom r; Cold(m,r); CHECK(r.Initialize(1));
        m.words[0x80000/4]=0x80000005; CHECK(!r.WaitEntered()); CHECK(r.Error()==RomError::Halted);
    }
    {
        Model m; GlkRom r; Cold(m,r); CHECK(r.Initialize(1)); m.gone=true;
        CHECK(!r.WaitEntered()); CHECK(r.Error()==RomError::Io);
    }
    {
        Model m; GlkRom r; Cold(m,r); CHECK(r.Initialize(1)); m.frozen=true;
        CHECK(!r.WaitEntered()); CHECK(r.Error()==RomError::Timeout); CHECK(m.delays==10000);
    }
    {
        Model m; GlkRom r; auto io=m.Io(); io.length=0x80000;
        CHECK(!r.Bind(io)); CHECK(m.writes==0);
    }
    std::printf("SOF_ROM_TESTS=%u PASS; fault_points=%u; hardware=SIMULATED\n",checks,initOps+downOps);
}
