// SPDX-License-Identifier: MIT
#include "../src/sof/ipc3_command.h"
#include "sof_ipc_fixture.h"
#include <cstdio>
#include <cstdlib>
#include <climits>
using namespace phaser360::sof;
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
struct Model {
    std::vector<uint8_t> b=std::vector<uint8_t>(0x100000,0);
    unsigned ops=0,failAt=0,writes=0,delays=0,sends=0,acks=0,arrive=1;
    uint64_t time=100;
    uint32_t replySize=12,replyCmd=0x10000000,error=0;
    bool active=false,frozen=false,back=false,dropAck=false,notification=false,change=false;
    bool immediate=false,delayFail=false,postedFailure=false,late=false,replyBusy=false,beforeKick=false;
    Model() { IpcPut(b,4,0x303); IpcReadyBytes(b,0x81000); }
    uint32_t Get(uint32_t o) {
        uint32_t v=0; for(unsigned j=0;j<4;++j) v|=uint32_t(b[o+j])<<(8*j); return v;
    }
    void Reply() {
        IpcPut(b,0x48,replyBusy?0x80000000u:0u); IpcPut(b,0x4c,0x40000000);
        IpcPut(b,0xa0000,replySize); IpcPut(b,0xa0004,replyCmd); IpcPut(b,0xa0008,error);
        for(uint32_t o=12;o<384;o+=4) IpcPut(b,0xa0000+o,0x12345678);
        if(notification) IpcPut(b,0x40,0xdead0000);
    }
    static bool Read(void* p,uint32_t o,uint32_t* v) {
        auto& m=*static_cast<Model*>(p); CHECK(!(o&3) && o<=0xffffc);
        if(m.active) CHECK(o==4 || o==8 || o==0x40 || o==0x48 || o==0x4c || o==0x50 || (o>=0xa0000 && o<0xa0180));
        if(++m.ops==m.failAt) return false;
        *v=m.Get(o);
        if(m.active && o==0xa0008 && m.change) IpcPut(m.b,0xa0000,16);
        if(m.active && o==0xa0008 && m.late) m.time+=500000;
        return true;
    }
    static bool Write(void* p,uint32_t o,uint32_t v) {
        auto& m=*static_cast<Model*>(p); ++m.ops; ++m.writes;
        const bool fail=m.ops==m.failAt;
        if(fail && !m.postedFailure) return false;
        if(o==0x40) { CHECK(!m.active); IpcPut(m.b,o,v&0x7fffffffu); }
        else if(o==0x48) {
            CHECK(v==0x80000000); CHECK(m.active); ++m.sends;
            CHECK(m.Get(0xa0000)>=8); IpcPut(m.b,o,v); if(m.immediate) m.Reply();
        } else if(o==0x4c) {
            CHECK(m.active && (v&0x40000000)); ++m.acks;
            if(!m.dropAck) IpcPut(m.b,o,m.Get(o)&~0x40000000u);
        } else { CHECK(m.active && o>=0xa0000 && o<0xa0180); IpcPut(m.b,o,v); if(m.beforeKick && o==0xa0004) IpcPut(m.b,0x40,0xdead0000); }
        return !fail;
    }
    static bool Delay(void* p,unsigned us) {
        auto& m=*static_cast<Model*>(p); CHECK(us==500); ++m.delays;
        if(!m.frozen) m.time+=us;
        if(m.arrive && m.delays==m.arrive) m.Reply();
        return !m.delayFail;
    }
    static uint64_t Now(void* p) { auto& m=*static_cast<Model*>(p); return m.back && m.delays?0:m.time; }
    RomIo Io() { return {this,Read,Write,Delay,Now,0x100000}; }
    void Bind(Ipc3Command& command) {
        Ipc3Receive ready; auto x=IpcXman();
        CHECK(ready.Configure(Io(),x.data(),x.size(),20)); CHECK(ready.Arm());
        IpcPut(b,0x40,0xf0000000); CHECK(ready.Receive()); CHECK(command.Bind(Io(),ready));
        IpcPut(b,4,0x01010000); active=true; ops=0; writes=0;
    }
};
static std::vector<uint8_t> Request(size_t n=8) {
    std::vector<uint8_t> r(n,0x5a); IpcPut(r,0,static_cast<uint32_t>(n)); IpcPut(r,4,0x30020000); return r;
}
static CommandResult Run(Ipc3Command& c,std::vector<uint8_t>& output,uint32_t expected=0x10000000,size_t n=8) {
    const auto q=Request(n); return c.Exchange(q.data(),q.size(),expected,output.data(),output.size());
}
static void Untouched(const std::vector<uint8_t>& output) { for(auto v:output) CHECK(v==0xa5); }
int main() {
    unsigned operationCount=0;
    { Model m; Ipc3Command c; m.Bind(c); std::vector<uint8_t> out(384,0xa5);
      auto v=Run(c,out); CHECK(v.status==CommandStatus::Ok && v.submitted && v.acknowledged && v.replyBytes==12);
      CHECK(out[0]==12 && out[12]==0xa5 && m.sends==1 && m.acks==1 && c.Usable()); operationCount=m.ops;
      m.delays=0; CHECK(Run(c,out).status==CommandStatus::Ok); CHECK(m.sends==2);
      c.Close(); auto before=m.ops; CHECK(Run(c,out).status==CommandStatus::State && before==m.ops); }
    for(unsigned fail=1;fail<=operationCount;++fail) for(bool posted:{false,true}) {
        Model m; Ipc3Command c; m.Bind(c); m.failAt=fail; m.postedFailure=posted; std::vector<uint8_t> out(384,0xa5);
        auto v=Run(c,out); CHECK(v.status==CommandStatus::Io); CHECK(!c.Usable()); Untouched(out);
        auto before=m.ops; CHECK(Run(c,out).status==CommandStatus::State && before==m.ops);
    }
    for(unsigned mode=0;mode<10;++mode) {
        Model m; Ipc3Command c; m.Bind(c); std::vector<uint8_t> out(384,0xa5);
        if(mode==0) m.arrive=0;
        if(mode==1) {m.arrive=0; m.frozen=true;}
        if(mode==2) m.back=true;
        if(mode==3) m.dropAck=true;
        if(mode==4) m.notification=true;
        if(mode==5) m.change=true;
        if(mode==6) m.late=true;
        if(mode==7) m.delayFail=true;
        if(mode==8) IpcPut(m.b,0x4c,0x40000000);
        if(mode==9) IpcPut(m.b,0x40,0xdead0000);
        CHECK(Run(c,out).status!=CommandStatus::Ok); CHECK(!c.Usable()); Untouched(out);
        CHECK(m.delays<=1000); if(mode>=8) CHECK(m.writes==0);
    }
    for(uint32_t size:{0u,4u,8u,13u,388u,0xffffffffu}) {
        Model m; Ipc3Command c; m.Bind(c); m.replySize=size; std::vector<uint8_t> out(384,0xa5);
        CHECK(Run(c,out).status==CommandStatus::Reply); CHECK(m.acks==0); Untouched(out);
    }
    for(uint32_t reg:{4u,8u,0x50u,0x40u,0x48u,0x4cu}) {
        Model m; Ipc3Command c; m.Bind(c); IpcPut(m.b,reg,0xffffffff); std::vector<uint8_t> out(384,0xa5);
        CHECK(Run(c,out).status==CommandStatus::Io); CHECK(m.writes==0); Untouched(out);
    }
    for(uint32_t error:{0xffffffffu,0x80000000u}) {
        Model m; Ipc3Command c; m.Bind(c); m.error=error; std::vector<uint8_t> out(384,0xa5);
        auto v=Run(c,out); CHECK(v.status==CommandStatus::FirmwareError && v.acknowledged);
        CHECK(v.firmwareError==(error==0xffffffffu?-1:INT_MIN)); CHECK(c.Usable()); Untouched(out);
    }
    { Model m; Ipc3Command c; m.Bind(c); m.error=1; std::vector<uint8_t> out(384,0xa5);
      CHECK(Run(c,out).status==CommandStatus::Reply); CHECK(m.acks==0); }
    { Model m; Ipc3Command c; m.Bind(c); m.replyCmd=0x30020000; m.replySize=384; m.immediate=true;
      std::vector<uint8_t> out(384,0xa5); auto v=Run(c,out,0x30020000,384);
      CHECK(v.status==CommandStatus::Ok && v.replyBytes==384 && out[383]==0x12 && m.delays==0); }
    { Model m; Ipc3Command c; m.Bind(c); m.replyCmd=0x30020000; std::vector<uint8_t> out(384,0xa5);
      CHECK(Run(c,out).status==CommandStatus::Reply); CHECK(m.acks==0); }
    { Model m; Ipc3Command c; m.Bind(c); m.replySize=16; std::vector<uint8_t> out(12,0xa5);
      CHECK(Run(c,out).status==CommandStatus::Reply); CHECK(m.acks==0); }
    { Model m; Ipc3Command c; m.Bind(c); std::vector<uint8_t> out(384,0xa5); m.arrive=999;
      CHECK(Run(c,out).status==CommandStatus::Ok); }
    { Model m; Ipc3Command c; m.Bind(c); std::vector<uint8_t> out(384,0xa5); m.arrive=1000;
      CHECK(Run(c,out).status==CommandStatus::Timeout); CHECK(m.acks==0); }
    { Model m; Ipc3Command c; m.Bind(c); std::vector<uint8_t> out(384,0xa5);
      for(uint32_t cmd:{0u,0x10000000u,0x20010000u,0x40010000u,0x70000000u,0xb0010000u}) {
          auto q=Request(); IpcPut(q,4,cmd);
          CHECK(c.Exchange(q.data(),q.size(),0x10000000,out.data(),out.size()).status==CommandStatus::Argument);
      }
      for(size_t n:{0u,4u,7u,9u,388u}) { auto q=Request(388);
          CHECK(c.Exchange(q.data(),n,0x10000000,out.data(),out.size()).status==CommandStatus::Argument); }
      CHECK(m.ops==0 && c.Usable()); Untouched(out);
    }
    { Model m; Ipc3Command c; Ipc3Receive gate; CHECK(!c.Bind(m.Io(),gate)); CHECK(m.ops==0); }
    for(unsigned mode=0;mode<6;++mode) {
        Model m; Ipc3Command c; m.Bind(c); std::vector<uint8_t> out(384,0xa5);
        if(mode==0) IpcPut(m.b,0x48,0x80000000);
        if(mode==1) IpcPut(m.b,4,0x303);
        if(mode==2) IpcPut(m.b,8,1);
        if(mode==3) IpcPut(m.b,0x50,3);
        if(mode==4) m.replyBusy=true;
        if(mode==5) m.beforeKick=true;
        CHECK(Run(c,out).status!=CommandStatus::Ok); CHECK(!c.Usable()); Untouched(out);
        if(mode<4) CHECK(m.writes==0);
        if(mode==5) CHECK(m.sends==0);
        CHECK(m.acks==0);
    }
    std::printf("SOF_COMMAND_TESTS=%u PASS; io_fault_points=%u; hardware=NONE\n",checks,operationCount);
}
