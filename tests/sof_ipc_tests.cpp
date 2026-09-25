// SPDX-License-Identifier: MIT
#include "../src/sof/ipc3_receive.h"
#include "sof_ipc_fixture.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
using namespace phaser360::sof;
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
struct Model {
    std::vector<uint8_t> bytes=std::vector<uint8_t>(0x100000,0);
    unsigned ops=0,failAt=0,writes=0,delays=0,arrive=1;
    uint64_t time=100;
    bool frozen=false,back=false,drop=false,change=false,late=false,delayFail=false;
    Model() { IpcPut(bytes,4,0x303); IpcReadyBytes(bytes,0x81000); }
    static bool Read(void* p,uint32_t o,uint32_t* v) {
        auto& m=*static_cast<Model*>(p); CHECK(!(o&3) && o<=0xffffc);
        // No descriptor-supplied address may be followed by this receiver.
        CHECK(o==4 || o==8 || o==0x40 || o==0x50 || (o>=0x81000 && o<0x8106c));
        if(++m.ops==m.failAt) return false;
        *v=0; for(unsigned j=0;j<4;++j) *v|=uint32_t(m.bytes[o+j])<<(8*j);
        if(o==0x81068 && m.change) IpcPut(m.bytes,0x40,0x90000000);
        if(o==0x81068 && m.late) m.time+=5000000;
        return true;
    }
    static bool Write(void* p,uint32_t o,uint32_t v) {
        auto& m=*static_cast<Model*>(p); CHECK(o==0x40 && (v&0x80000000u)); ++m.writes;
        if(++m.ops==m.failAt) return false;
        if(!m.drop) IpcPut(m.bytes,o,v&0x7fffffffu);
        return true;
    }
    static bool Delay(void* p,unsigned us) {
        auto& m=*static_cast<Model*>(p); CHECK(us==500); ++m.delays;
        if(!m.frozen) m.time+=us;
        if(m.arrive && m.delays==m.arrive) IpcPut(m.bytes,0x40,0xf0000000);
        return !m.delayFail;
    }
    static uint64_t Now(void* p) { auto& m=*static_cast<Model*>(p); return m.back && m.delays?0:m.time; }
    RomIo Io() { return {this,Read,Write,Delay,Now,0x100000}; }
};
static void Configure(Model& m,Ipc3Receive& receiver) {
    auto x=IpcXman(); CHECK(receiver.Configure(m.Io(),x.data(),x.size(),20)); CHECK(m.writes==0);
}
static void Arm(Model& m,Ipc3Receive& receiver) { Configure(m,receiver); CHECK(receiver.Arm()); }
int main(int argc,char** argv) {
    auto x=IpcXman(); IpcWindows windows={};
    CHECK(ParseIpc3Windows(x.data(),x.size(),0x100000,&windows));
    CHECK(windows.region[0].offset==0xa0000 && windows.region[1].offset==0x81000);
    for(size_t n=0;n<x.size();++n) CHECK(!ParseIpc3Windows(x.data(),n,0x100000,&windows));
    // Every descriptor field is bounded, including multiplication/overflow and aliasing.
    const size_t offsets[]={0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60};
    for(auto o:offsets) { auto bad=x; IpcPut(bad,o,0xffffffff); CHECK(!ParseIpc3Windows(bad.data(),bad.size(),0x100000,&windows)); CHECK(!windows.region[0].size); }
    { auto bad=x; IpcPut(bad,68,5); CHECK(!ParseIpc3Windows(bad.data(),bad.size(),0x100000,&windows)); }
    { auto bad=x; IpcPut(bad,84,0); CHECK(!ParseIpc3Windows(bad.data(),bad.size(),0x100000,&windows)); }
    CHECK(!ParseIpc3Windows(x.data(),x.size(),0x90000,&windows));
    CHECK(!ParseIpc3Windows(nullptr,x.size(),0x100000,&windows));
    CHECK(!ParseIpc3Windows(x.data(),x.size(),0x100000,nullptr));
    { auto duplicate=x; duplicate.insert(duplicate.end(),x.begin()+16,x.end());
      IpcPut(duplicate,4,static_cast<uint32_t>(duplicate.size()));
      CHECK(!ParseIpc3Windows(duplicate.data(),duplicate.size(),0x100000,&windows)); }
    unsigned operations=0;
    { Model m; Ipc3Receive receiver; CHECK(!receiver.Receive()); Arm(m,receiver);
      CHECK(receiver.Receive()); operations=m.ops; CHECK(m.writes==1);
      CHECK(receiver.Ready() && receiver.Windows()); CHECK(receiver.Ready()->abi==0x3014000);
      CHECK(!receiver.Receive()); CHECK(!receiver.Ready() && !receiver.Windows()); }
    for(unsigned i=1;i<=operations;++i) {
        Model m; Ipc3Receive receiver; Configure(m,receiver); m.failAt=i;
        CHECK(!receiver.Arm() || !receiver.Receive()); CHECK(receiver.Error()==ReceiveError::Io);
        CHECK(!receiver.Ready() && !receiver.Windows());
    }
    { Model m; Ipc3Receive receiver; IpcPut(m.bytes,0x40,0xf0000000); Arm(m,receiver);
      CHECK(m.writes==1); m.arrive=0; CHECK(!receiver.Receive()); CHECK(receiver.Error()==ReceiveError::Timeout); CHECK(m.writes==1); }
    { Model m; Ipc3Receive receiver; IpcPut(m.bytes,0x40,0xf0000000); m.drop=true; Configure(m,receiver);
      CHECK(!receiver.Arm()); CHECK(receiver.Error()==ReceiveError::Doorbell); }
    { Model m; Ipc3Receive receiver; IpcPut(m.bytes,4,0x01010000); Configure(m,receiver); CHECK(!receiver.Arm()); CHECK(m.writes==0); }
    for(uint32_t reg:{8u,0x50u}) { Model m; Ipc3Receive receiver; IpcPut(m.bytes,reg,1); Configure(m,receiver); CHECK(!receiver.Arm()); CHECK(m.writes==0); }
    for(unsigned mode=0;mode<6;++mode) {
        Model m; Ipc3Receive receiver; Arm(m,receiver);
        if(mode==0) {m.frozen=true; m.arrive=0;}
        if(mode==1) m.back=true;
        if(mode==2) m.change=true;
        if(mode==3) m.late=true;
        if(mode==4) m.delayFail=true;
        if(mode==5) {m.arrive=0; IpcPut(m.bytes,0x40,0xdead0000);}
        CHECK(!receiver.Receive()); CHECK(!receiver.Ready()); CHECK(m.writes==0);
        CHECK(m.delays<=10000);
    }
    // Invalid fixed headers never receive a success ACK.
    for(size_t off:{0u,4u,24u,64u,72u,92u}) {
        Model m; Ipc3Receive receiver; Arm(m,receiver); IpcPut(m.bytes,0x81000+off,0xffffffff);
        CHECK(!receiver.Receive()); CHECK(receiver.Error()==ReceiveError::Header); CHECK(m.writes==0);
    }
    { Model m; Ipc3Receive receiver; Arm(m,receiver); m.arrive=9999; CHECK(receiver.Receive()); }
    { Model m; Ipc3Receive receiver; Arm(m,receiver); m.arrive=10000; CHECK(!receiver.Receive()); CHECK(m.writes==0); }
    if(argc==2) {
        std::ifstream file(argv[1],std::ios::binary); CHECK(file.good());
        std::vector<uint8_t> real((std::istreambuf_iterator<char>(file)),{});
        CHECK(real.size()==287488); CHECK(ParseIpc3Windows(real.data(),768,0x100000,&windows));
        CHECK(windows.region[0].offset==0xa0000 && windows.region[0].size==8192);
        CHECK(windows.region[1].offset==0x81000 && windows.region[1].size==4096);
        std::puts("OFFICIAL_XMAN_WINDOWS=PASS (caller must verify fixture hash)");
    }
    std::printf("SOF_IPC_TESTS=%u PASS; fault_points=%u; hardware=NONE\n",checks,operations);
}
