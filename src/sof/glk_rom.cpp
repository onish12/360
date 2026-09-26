// SPDX-License-Identifier: MIT
#include "glk_rom.h"
namespace phaser360 { namespace sof {
namespace { constexpr uint32_t cs=4, hipci=0x48, hipcie=0x4c, rom=0x80000; }
bool GlkRom::Fail(RomError e) noexcept { error_=e; state_=RomState::Fault; return false; }
bool GlkRom::Read(uint32_t o,uint32_t& v) noexcept {
    lastOffset_=o;
    v=0;
    if((o&3) || o>io_.length || io_.length-o<4 || !io_.read(io_.context,o,&v))
        return Fail(RomError::Io);
    last_=v;
    if(v==0xffffffffu) return Fail(RomError::Io);
    return true;
}
bool GlkRom::Write(uint32_t o,uint32_t v) noexcept {
    lastOffset_=o;
    if((o&3) || o>io_.length || io_.length-o<4 || !io_.write(io_.context,o,v))
        return Fail(RomError::Io);
    return true;
}
bool GlkRom::Update(uint32_t o,uint32_t mask,uint32_t value) noexcept {
    uint32_t v=0; return Read(o,v) && Write(o,(v&~mask)|(value&mask));
}
bool GlkRom::Poll(uint32_t o,uint32_t mask,uint32_t value,uint32_t timeout,bool status) noexcept {
    const uint64_t start=io_.now_us(io_.context);
    uint64_t previous=start;
    // Hard attempt cap also bounds a broken/frozen clock callback.
    for(unsigned i=0;i<10000;++i) {
        uint64_t now=io_.now_us(io_.context);
        if(now<previous) return Fail(RomError::Clock);
        if(now-start>=timeout) return Fail(RomError::Timeout);
        previous=now;
        uint32_t v=0; if(!Read(o,v)) return false;
        now=io_.now_us(io_.context);
        if(now<previous) return Fail(RomError::Clock);
        if(now-start>=timeout) return Fail(RomError::Timeout);
        previous=now;
        if(status && (v&0x80000000u)) return Fail(RomError::Halted);
        if((v&mask)==value) return true;
        if(!io_.delay_us(io_.context,500)) return Fail(RomError::Io);
    }
    return Fail(RomError::Timeout);
}
bool GlkRom::Bind(const RomIo& io) noexcept {
    if(state_!=RomState::Unbound) { error_=RomError::State; return false; }
    if(!io.read || !io.write || !io.delay_us || !io.now_us ||
       io.length<0x80004 || io.length>0x100000) { error_=RomError::Argument; return false; }
    io_=io; state_=RomState::Bound; return true;
}
bool GlkRom::DownCores(uint32_t cores) noexcept {
    return Update(cs,cores<<8,cores<<8) && Update(cs,cores,cores) &&
        Poll(cs,cores,cores,50000) && Update(cs,cores<<16,0) &&
        Poll(cs,cores<<24,0,50000) &&
        Poll(cs,cores|(cores<<8)|(cores<<16)|(cores<<24),cores|(cores<<8),50000);
}
bool GlkRom::PowerDown() noexcept {
    if(state_==RomState::Unbound) { error_=RomError::State; return false; }
    state_=RomState::Fault; error_=RomError::None;
    // This polling implementation does not install/enable an IPC ISR.
    phase_=RomPhase::PowerDownInterruptMask;
    if(!Update(8,1,0) || !Poll(8,1,0,50000)) return false;
    phase_=RomPhase::PowerDownIpcControl;
    if(!Update(0x50,3,0) || !Poll(0x50,3,0,50000)) return false;
    phase_=RomPhase::PowerDownCores;
    if(!DownCores(3)) return false;
    phase_=RomPhase::None; state_=RomState::Cold; return true;
}
bool GlkRom::Initialize(uint8_t tag) noexcept {
    if(state_!=RomState::Cold) { error_=RomError::State; return false; }
    if(tag==0 || tag>15) { error_=RomError::Argument; return false; }
    error_=RomError::None;
    sspObservedMask_=0; sspMismatchMask_=0;
    uint32_t v=0;

    phase_=RomPhase::InitPreAdspcs;
    if(!Read(cs,v)) return false;
    if((v&0x03030303u)!=0x303) return Fail(RomError::Precondition);

    phase_=RomPhase::InitPreHipci;
    if(!Read(hipci,v)) return false;
    if(v&0x80000000u) return Fail(RomError::Precondition);

    state_=RomState::Initializing;

    // Clear stale W1C DONE before command; require observed clear before issue.
    phase_=RomPhase::InitClearStaleDone;
    if(!Write(hipcie,0x40000000) || !Poll(hipcie,0x40000000,0,50000)) return false;

    phase_=RomPhase::InitPowerUpCores;
    if(!Update(cs,0x30000,0x30000) || !Poll(cs,0x03000000,0x03000000,50000)) return false;

    phase_=RomPhase::InitConfigureSsp;
    for(uint32_t i=0;i<6;++i) {
        const uint32_t offset=0x2004+i*0x1000;
        // Linux v6.12 hda_ssp_set_cbp_cfp() performs a masked update here;
        // SSC1 is not a ROM acknowledgement register. R7 incorrectly added
        // a mandatory 50 ms readback handshake before even issuing ROM_CONTROL.
        // Keep one bounded observation for diagnosis and hard I/O/all-ones
        // checks, but let actual ROM DONE and INIT_DONE determine readiness.
        if(!Update(offset,0x03000000,0x03000000) || !Read(offset,v)) return false;
        sspObserved_[i]=v; sspObservedMask_|=uint32_t{1}<<i;
        if((v&0x03000000)!=0x03000000) sspMismatchMask_|=uint32_t{1}<<i;
    }

    const uint32_t command=0x81004000u|((static_cast<uint32_t>(tag)-1)<<9);
    phase_=RomPhase::InitWriteRomCommand;
    if(!Write(hipci,command)) return false;

    phase_=RomPhase::InitRunCore0;
    if(!Update(cs,1,0) || !Poll(cs,1,0,50000) ||
       !Update(cs,0x100,0) || !Poll(cs,0x01010101,0x01010000,50000)) return false;

    phase_=RomPhase::InitWaitRomDone;
    if(!Poll(hipcie,0x40000000,0x40000000,500000)) return false;

    phase_=RomPhase::InitClearRomDone;
    if(!Write(hipcie,0x40000000) || !Poll(hipcie,0x40000000,0,50000)) return false;

    phase_=RomPhase::InitPowerDownCore1;
    if(!DownCores(2)) return false;

    phase_=RomPhase::InitWaitRomReady;
    if(!Poll(rom,0xffffff,1,150000,true)) return false;

    phase_=RomPhase::None; state_=RomState::DownloadReady; return true;
}
bool GlkRom::WaitEntered() noexcept {
    if(state_!=RomState::DownloadReady) { error_=RomError::State; return false; }
    phase_=RomPhase::WaitFirmwareEntered;
    if(!Poll(rom,0xffffff,5,3000000,true)) return false;
    phase_=RomPhase::None; state_=RomState::Entered; return true;
}
} }
