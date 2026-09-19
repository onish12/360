// SPDX-License-Identifier: MIT
#include "hda_stream.h"
namespace phaser360 { namespace sof {
bool BootStream::Read(uint32_t o,unsigned w,uint32_t& v) noexcept {
    v=0;
    return (w==1 || w==2 || w==4) && o%w==0 && o<=io_.length &&
        w<=io_.length-o && io_.read(io_.context,o,w,&v);
}
bool BootStream::Write(uint32_t o,unsigned w,uint32_t v) noexcept {
    return (w==1 || w==2 || w==4) && o%w==0 && o<=io_.length &&
        w<=io_.length-o && io_.write(io_.context,o,w,v);
}
bool BootStream::Update(uint32_t o,unsigned w,uint32_t m,uint32_t v) noexcept {
    uint32_t before=0;
    return Read(o,w,before) && Write(o,w,(before & ~m)|(v & m));
}
bool BootStream::Match(uint32_t o,unsigned w,uint32_t m,uint32_t v) noexcept {
    uint32_t actual=0;
    return Read(o,w,actual) && (actual & m)==v;
}
bool BootStream::Poll(uint32_t mask,uint32_t value) noexcept {
    for(unsigned i=0;i<1000;++i) {
        uint32_t v=0;
        if(!Read(sd_,1,v) || v==0xff) return false;
        if((v & mask)==value) return true;
        io_.delay_us(io_.context,10);
    }
    return false;
}
bool BootStream::Select(const RegisterIo& io) noexcept {
    if(state_!=StreamState::Empty || !io.read || !io.write || !io.delay_us ||
       io.length<0x104c || io.length>0x4000) return false;
    io_=io;
    uint32_t gcap=0,head=0;
    if(!Read(0,2,gcap) || gcap==0xffff || (gcap & 0xf8)!=0 ||
       !Match(8,4,1,1) || !Match(0x1030,4,0x2000,0)) return false;
    const uint32_t inputs=(gcap>>8)&15, outputs=(gcap>>12)&15, total=inputs+outputs;
    if(!outputs || total>30 || !Match(0x20,4,0x3fffffff,0) ||
       !Match(0x38,4,0x3fffffff,0) || !Match(0x70,4,1,0)) return false;
    for(uint32_t i=0;i<total;++i) {
        const uint32_t s=0x80+i*0x20;
        if(!Match(s,1,0x1f,0) || !Match(s+2,1,0xf0,0) ||
           !Match(s+0x18,4,0xffffffffu,0) || !Match(s+0x1c,4,0xffffffffu,0)) return false;
    }
    if(!Read(0x14,4,head)) return false;
    uint32_t seen[32]={},count=0,pp=0,spib=0,next=head & 0xffff;
    while(next) {
        // Conservative GLK capability window; never treat stream/vendor registers as capabilities.
        if(count==32 || next<0x400 || next<0x80+total*0x20 || (next & 3) || next>=0x1000 || next>io.length-4) return false;
        for(uint32_t i=0;i<count;++i) if(seen[i]==next) return false;
        seen[count++]=next;
        uint32_t header=0;
        if(!Read(next,4,header) || header==0xffffffffu) return false;
        const uint32_t id=(header>>16)&0xfff;
        if(id==3) { if(pp) return false; pp=next; }
        if(id==4) { if(spib) return false; spib=next; }
        next=header & 0xffff;
    }
    const uint32_t ppBytes=0x10+total*0x20, spibBytes=8+total*8;
    if(!pp || !spib || ppBytes>0x1000-pp || spibBytes>0x1000-spib ||
       !(pp+ppBytes<=spib || spib+spibBytes<=pp)) return false;
    for(uint32_t i=0;i<count;++i) {
        if(seen[i]!=pp && seen[i]>pp && seen[i]<pp+ppBytes) return false;
        if(seen[i]!=spib && seen[i]>spib && seen[i]<spib+spibBytes) return false;
    }
    const uint32_t mask=uint32_t{1}<<inputs;
    if(!Match(pp+4,4,0x40000000u|mask,0x40000000u) ||
       !Match(spib+4,4,mask,0) || !Match(spib+8+inputs*8,4,0xffffffffu,0)) return false;
    sd_=0x80+inputs*0x20; index_=inputs; pp_=pp; spib_=spib;
    state_=StreamState::Selected; return true;
}
bool BootStream::Configure(uint64_t bdl,uint32_t bytes,uint16_t lvi) noexcept {
    if(state_!=StreamState::Selected || !bdl || (bdl & 4095) || bdl>0xfffff000ull ||
       !bytes || bytes>kMaxDmaBytes || lvi!=(bytes+4095)/4096-1) return false;
    state_=StreamState::Fault;
    const uint32_t mask=uint32_t{1}<<index_;
    if(!Update(pp_+4,4,mask,mask) || !Update(sd_,1,0x1e,0) || !Poll(2,0) ||
       !Write(sd_+3,1,0x1c) || !Update(sd_,1,1,1) || !Poll(1,1) ||
       !Update(sd_,1,1,0)) return false;
    io_.delay_us(io_.context,3);
    if(!Poll(1,0) || !Update(sd_+2,1,0xf0,0x10) || !Write(sd_+8,4,bytes) ||
       !Update(pp_+4,4,mask,0) || !Write(sd_+0x12,2,0x40) ||
       !Update(pp_+4,4,mask,mask) || !Write(sd_+0xc,2,lvi) ||
       !Write(sd_+0x18,4,static_cast<uint32_t>(bdl)) || !Write(sd_+0x1c,4,0) ||
       !Write(spib_+8+index_*8,4,bytes) || !Update(spib_+4,4,mask,mask)) return false;
    if(!Match(sd_,1,0x1f,0) || !Match(sd_+2,1,0xf0,0x10) ||
       !Match(sd_+8,4,0xffffffffu,bytes) || !Match(sd_+0xc,2,0xffff,lvi) ||
       !Match(sd_+0x12,2,0xffff,0x40) ||
       !Match(sd_+0x18,4,0xffffffffu,static_cast<uint32_t>(bdl)) ||
       !Match(sd_+0x1c,4,0xffffffffu,0) || !Match(pp_+4,4,mask,mask) ||
       !Match(spib_+4,4,mask,mask) || !Match(spib_+8+index_*8,4,0xffffffffu,bytes)) return false;
    state_=StreamState::Prepared; return true;
}
bool BootStream::Start() noexcept {
    if(state_!=StreamState::Prepared) return false;
    state_=StreamState::Fault;
    // Boot transport uses polling. No stream interrupts are enabled.
    if(!Update(sd_,1,0x1f,2) || !Poll(3,2) || !Match(sd_+3,1,0x18,0)) return false;
    state_=StreamState::Running; return true;
}
bool BootStream::StopDetach() noexcept {
    if(state_==StreamState::Empty) return false;
    state_=StreamState::Fault;
    const uint32_t mask=uint32_t{1}<<index_;
    if(!Update(sd_,1,0x1e,0) || !Poll(2,0) || !Update(sd_,1,1,0) || !Poll(1,0) ||
       !Update(spib_+4,4,mask,0) || !Write(spib_+8+index_*8,4,0) ||
       !Write(sd_+0x18,4,0) || !Write(sd_+0x1c,4,0) ||
       !Write(sd_+8,4,0) || !Write(sd_+0xc,2,0) || !Update(sd_+2,1,0xf0,0) ||
       !Update(pp_+4,4,mask,0) || !Write(sd_+3,1,0x1c)) return false;
    state_=StreamState::Detached;
    if(IsDetached()) return true;
    state_=StreamState::Fault; return false;
}
bool BootStream::IsDetached() noexcept {
    if(state_!=StreamState::Detached) return false;
    const uint32_t mask=uint32_t{1}<<index_;
    return Match(sd_,1,0x1f,0) && Match(sd_+0x18,4,0xffffffffu,0) &&
        Match(sd_+0x1c,4,0xffffffffu,0) && Match(spib_+4,4,mask,0) &&
        Match(spib_+8+index_*8,4,0xffffffffu,0) && Match(pp_+4,4,mask,0);
}
} }
