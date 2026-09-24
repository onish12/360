// SPDX-License-Identifier: MIT
#include "hda_controller.h"

namespace phaser360 { namespace sof {

namespace {
constexpr uint32_t kGcap=0x00;
constexpr uint32_t kGctl=0x08;
constexpr uint32_t kLlch=0x14;
constexpr uint32_t kIntctl=0x20;
constexpr uint32_t kSsync=0x38;
constexpr uint32_t kDplbase=0x70;
constexpr uint32_t kStreamBase=0x80;
constexpr uint32_t kStreamStride=0x20;
constexpr uint32_t kIntelEm2=0x1030;
constexpr uint32_t kIntelEm2L1sen=0x2000;
constexpr uint32_t kGctlCrst=0x1;
constexpr uint32_t kPpPie=0x80000000u;
constexpr uint32_t kPpGprocen=0x40000000u;
constexpr uint32_t kPpCapId=3;
constexpr uint32_t kSpibCapId=4;
}

bool HdaController::Read(uint32_t o,unsigned w,uint32_t& v) noexcept {
    v=0;
    return (w==1 || w==2 || w==4) && o%w==0 && o<=io_.length &&
        w<=io_.length-o && io_.read(io_.context,o,w,&v);
}
bool HdaController::Write(uint32_t o,unsigned w,uint32_t v) noexcept {
    return (w==1 || w==2 || w==4) && o%w==0 && o<=io_.length &&
        w<=io_.length-o && io_.write(io_.context,o,w,v);
}
bool HdaController::Update(uint32_t o,unsigned w,uint32_t mask,uint32_t value) noexcept {
    uint32_t before=0;
    return Read(o,w,before) && Write(o,w,(before&~mask)|(value&mask));
}
bool HdaController::Match(uint32_t o,unsigned w,uint32_t mask,uint32_t value) noexcept {
    uint32_t actual=0;
    return Read(o,w,actual) && (actual&mask)==value;
}
bool HdaController::Poll(uint32_t o,unsigned w,uint32_t mask,uint32_t value,
                         unsigned attempts) noexcept {
    for(unsigned i=0;i<attempts;++i) {
        uint32_t actual=0;
        if(!Read(o,w,actual)) return false;
        const uint32_t allOnes=w==1?0xffu:(w==2?0xffffu:0xffffffffu);
        if(actual==allOnes) return false;
        if((actual&mask)==value) return true;
        io_.delay_us(io_.context,10);
    }
    return false;
}
uint32_t HdaController::StreamMask() const noexcept {
    return total_ ? ((uint32_t{1}<<total_)-1u) : 0;
}

bool HdaController::DiscoverCapabilities() noexcept {
    uint32_t head=0;
    if(!Read(kLlch,4,head)) return false;
    uint32_t seen[32]={},count=0,next=head&0xffff;
    pp_=0; spib_=0;
    while(next) {
        if(count==32 || next<0x400 || next<kStreamBase+total_*kStreamStride ||
           (next&3) || next>io_.length-4) return false;
        for(uint32_t i=0;i<count;++i) if(seen[i]==next) return false;
        seen[count++]=next;
        uint32_t header=0;
        if(!Read(next,4,header) || header==0xffffffffu) return false;
        const uint32_t id=(header>>16)&0xfff;
        if(id==kPpCapId) { if(pp_) return false; pp_=next; }
        if(id==kSpibCapId) { if(spib_) return false; spib_=next; }
        next=header&0xffff;
    }
    const uint32_t ppBytes=0x10+total_*0x20;
    const uint32_t spibBytes=8+total_*8;
    if(!pp_ || !spib_ ||
       pp_>io_.length || ppBytes>io_.length-pp_ ||
       spib_>io_.length || spibBytes>io_.length-spib_ ||
       !(pp_+ppBytes<=spib_ || spib_+spibBytes<=pp_)) return false;
    for(uint32_t i=0;i<count;++i) {
        if(seen[i]!=pp_ && seen[i]>pp_ && seen[i]<pp_+ppBytes) return false;
        if(seen[i]!=spib_ && seen[i]>spib_ && seen[i]<spib_+spibBytes) return false;
    }
    return true;
}

bool HdaController::VerifyColdStreams() noexcept {
    for(uint32_t i=0;i<total_;++i) {
        const uint32_t sd=kStreamBase+i*kStreamStride;
        if(!Write(sd+3,1,0x1c) || !Match(sd+3,1,0x1c,0) ||
           !Match(sd,1,0x1f,0) || !Match(sd+2,1,0xf0,0) ||
           !Match(sd+0x18,4,0xffffffffu,0) ||
           !Match(sd+0x1c,4,0xffffffffu,0))
            return false;
    }
    return true;
}

bool HdaController::Initialize(const RegisterIo& io) noexcept {
    if(state_!=HdaControllerState::Empty || !io.read || !io.write || !io.delay_us ||
       io.length<0x104c || io.length>0x4000) return false;
    io_=io; state_=HdaControllerState::Fault;

    uint32_t gcap=0,gctl=0,em2=0;
    if(!Read(kGcap,2,gcap) || gcap==0xffff || (gcap&0xf8)!=0 ||
       !Read(kGctl,4,gctl) || gctl==0xffffffffu ||
       !Read(kIntelEm2,4,em2) || em2==0xffffffffu)
        return false;
    inputs_=(gcap>>8)&15;
    outputs_=(gcap>>12)&15;
    total_=inputs_+outputs_;
    if(!outputs_ || !total_ || total_>30) return false;
    l1Captured_=true; l1WasSet_=(em2&kIntelEm2L1sen)!=0;

    if(!Write(kIntctl,4,0) || !Write(kSsync,4,0) ||
       !Update(kDplbase,4,1,0)) return false;

    // Linux SOF uses the same CRST reset -> ready sequence on APL/GLK.
    if(!Update(kGctl,4,kGctlCrst,0) ||
       !Poll(kGctl,4,kGctlCrst,0,1000)) return false;
    io_.delay_us(io_.context,500);
    if(!Update(kGctl,4,kGctlCrst,kGctlCrst) ||
       !Poll(kGctl,4,kGctlCrst,kGctlCrst,1000)) return false;
    io_.delay_us(io_.context,1000);

    if(!Write(kIntctl,4,0) || !Write(kSsync,4,0) ||
       !Update(kDplbase,4,1,0) ||
       !Update(kIntelEm2,4,kIntelEm2L1sen,0) ||
       !Match(kIntelEm2,4,kIntelEm2L1sen,0))
        return false;

    if(!DiscoverCapabilities() || !VerifyColdStreams()) return false;

    const uint32_t streamMask=StreamMask();
    if(!Update(pp_+4,4,kPpPie|kPpGprocen|streamMask,kPpGprocen) ||
       !Match(pp_+4,4,kPpPie|kPpGprocen|streamMask,kPpGprocen))
        return false;
    if(!Update(spib_+4,4,streamMask,0)) return false;
    for(uint32_t i=0;i<total_;++i)
        if(!Write(spib_+8+i*8,4,0)) return false;
    if(!Match(spib_+4,4,streamMask,0)) return false;

    state_=HdaControllerState::Initialized;
    return true;
}

bool HdaController::Quiesce() noexcept {
    if(state_==HdaControllerState::Empty || state_==HdaControllerState::Quiesced)
        return true;
    if(!io_.read || !io_.write || !io_.delay_us) return false;

    bool ok=true;
    if(!Write(kIntctl,4,0)) ok=false;
    if(!Write(kSsync,4,0)) ok=false;
    if(!Update(kDplbase,4,1,0)) ok=false;
    const uint32_t streamMask=StreamMask();
    if(spib_) {
        if(!Update(spib_+4,4,streamMask,0)) ok=false;
        for(uint32_t i=0;i<total_;++i)
            if(!Write(spib_+8+i*8,4,0)) ok=false;
    }
    if(pp_ && !Update(pp_+4,4,kPpPie|kPpGprocen|streamMask,0)) ok=false;
    if(l1Captured_ &&
       !Update(kIntelEm2,4,kIntelEm2L1sen,l1WasSet_?kIntelEm2L1sen:0))
        ok=false;
    state_=ok?HdaControllerState::Quiesced:HdaControllerState::Fault;
    return ok;
}

} }
