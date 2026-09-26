// SPDX-License-Identifier: MIT
// Protocol regression for the R7 SSP readback failure; no hardware access.
// Independent contract: Linux v6.12 hda-loader.c, hda.h and apl.c.
#include "../src/sof/glk_rom.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace phaser360::sof;
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)

struct HardwareModel {
    // Literal reference addresses, not computed from production constants.
    const std::array<uint32_t,6> ssp={{0x2004,0x3004,0x4004,0x5004,0x6004,0x7004}};
    std::array<unsigned,6> writes{};
    std::array<uint32_t,6> sent{};
    std::vector<uint32_t> registers=std::vector<uint32_t>(0x80004/4,0);
    uint64_t time=0;
    unsigned delays=0,commands=0;
    int noEcho=-1,readFailure=-1,writeFailure=-1,allOnes=-1;
    bool ack=true,ready=true;
    HardwareModel() { registers[1]=0x303; }
    int Port(uint32_t offset) const {
        for(unsigned i=0;i<ssp.size();++i) if(ssp[i]==offset) return static_cast<int>(i);
        return -1;
    }
    static bool Read(void* p,uint32_t offset,uint32_t* value) {
        auto& m=*static_cast<HardwareModel*>(p);
        CHECK((offset&3)==0 && offset<0x80004);
        const int port=m.Port(offset);
        if(port>=0 && port==m.readFailure) return false;
        *value=port>=0 && port==m.allOnes ? 0xffffffffu : m.registers[offset/4];
        return true;
    }
    static bool Write(void* p,uint32_t offset,uint32_t value) {
        auto& m=*static_cast<HardwareModel*>(p);
        CHECK((offset&3)==0 && offset<0x80004);
        const int port=m.Port(offset);
        if(port>=0) {
            const auto i=static_cast<unsigned>(port);
            ++m.writes[i]; m.sent[i]=value;
            if(port==m.writeFailure) return false;
            // Read-as-zero/no echo is a protocol scenario, not a silicon claim.
            if(port!=m.noEcho) m.registers[offset/4]=value;
            return true;
        }
        // Prohibit any unreviewed DSP writes, including SSP enable registers.
        CHECK(offset==4 || offset==8 || offset==0x48 || offset==0x4c || offset==0x50);
        if(offset==0x4c) { m.registers[offset/4]&=~value; return true; }
        if(offset==0x48) ++m.commands;
        if(offset==4) {
            value=(value&~0x03000000u)|((value&0x30000u)<<8);
            if((value&0x01010101u)==0x01010000u && (m.registers[0x48/4]&0x80000000u)) {
                if(m.ack) {
                    m.registers[0x4c/4]|=0x40000000u;
                    m.registers[0x48/4]&=0x7fffffffu;
                }
                m.registers[0x80000/4]=m.ready?1u:0u;
            }
        }
        m.registers[offset/4]=value;
        return true;
    }
    static bool Delay(void* p,unsigned us) {
        auto& m=*static_cast<HardwareModel*>(p); m.time+=us; ++m.delays; return true;
    }
    static uint64_t Now(void* p) { return static_cast<HardwareModel*>(p)->time; }
    RomIo Io() { return {this,Read,Write,Delay,Now,0x80004}; }
};

int main() {
    // Every port can read zero after a successful write. This must not replace
    // the actual ROM DONE/INIT_DONE protocol with an invented SSP handshake.
    for(int port=0;port<6;++port) {
        HardwareModel m; GlkRom rom; m.noEcho=port;
        CHECK(rom.Bind(m.Io()) && rom.PowerDown());
        CHECK(rom.Initialize(1)); // Fails here with unmodified R7.
        CHECK(rom.SspObservedMask()==0x3fu);
        CHECK(rom.SspMismatchMask()==(uint32_t{1}<<static_cast<unsigned>(port)));
        CHECK(rom.SspObserved(static_cast<unsigned>(port))==0);
        CHECK(rom.State()==RomState::DownloadReady && m.commands==1 && m.delays==0);
        for(unsigned i=0;i<6;++i) CHECK(m.writes[i]==1 && m.sent[i]==0x03000000u);
        CHECK(rom.PowerDown());
    }
    // A non-echoing SSP never makes a missing ROM response successful.
    for(unsigned mode=0;mode<2;++mode) {
        HardwareModel m; GlkRom rom; m.noEcho=2;
        if(mode==0) m.ack=false; else m.ready=false;
        CHECK(rom.Bind(m.Io()) && rom.PowerDown());
        CHECK(!rom.Initialize(1) && rom.Error()==RomError::Timeout);
        CHECK(rom.Phase()==(mode==0?RomPhase::InitWaitRomDone:RomPhase::InitWaitRomReady));
        CHECK(m.commands==1);
    }
    // MMIO access failure and all-ones reads remain hard failures before ROM.
    for(unsigned mode=0;mode<3;++mode) {
        HardwareModel m; GlkRom rom;
        if(mode==0) m.readFailure=3;
        if(mode==1) m.writeFailure=3;
        if(mode==2) m.allOnes=3;
        CHECK(rom.Bind(m.Io()) && rom.PowerDown());
        CHECK(!rom.Initialize(1) && rom.Error()==RomError::Io);
        CHECK(rom.Phase()==RomPhase::InitConfigureSsp && m.commands==0);
        CHECK(rom.LastOffset()==0x5004u);
    }
    // Consumer bits are the entire write scope; unrelated bits are preserved.
    {
        HardwareModel m; GlkRom rom;
        for(unsigned i=0;i<6;++i) m.registers[m.ssp[i]/4]=0x00400011u;
        CHECK(rom.Bind(m.Io()) && rom.PowerDown() && rom.Initialize(15));
        for(unsigned i=0;i<6;++i) CHECK(m.sent[i]==0x03400011u && m.writes[i]==1);
        CHECK((m.registers[0x48/4]&0x7fffffffu)==0x01005c00u);
    }
    std::printf("SOF_SSP_CONTRACT_TESTS=%u PASS; scenarios=12; hardware=NONE\n",checks);
}
