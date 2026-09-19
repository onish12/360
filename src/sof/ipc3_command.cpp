// SPDX-License-Identifier: MIT
#include "ipc3_command.h"
namespace phaser360 { namespace sof {
namespace {
uint32_t U32(const uint8_t* p) noexcept {
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
void Put(uint8_t* p,uint32_t v) noexcept {
    for(unsigned j=0;j<4;++j) p[j]=static_cast<uint8_t>(v>>(8*j));
}
bool RequestCommand(uint32_t cmd) noexcept {
    const uint32_t group=cmd>>28;
    return (group==3 || group==5 || group==6 || group==8 || group==9) && (cmd&0x0fff0000u);
}
}
bool Ipc3Command::Bind(const RomIo& io,const Ipc3Receive& gate) noexcept {
    if(state_!=State::Unbound) return false;
    const auto* windows=gate.Windows();
    if(!windows || !gate.Ready() || !io.read || !io.write || !io.now_us || !io.delay_us ||
       io.length>0x100000 || io.length<0x82000) return false;
    const auto box=windows->region[0];
    if((box.offset&3) || box.offset<0x80000 || box.offset>io.length || box.size<12 ||
       (box.size&3) || box.size>io.length-box.offset) return false;
    const auto up=windows->region[1];
    if((up.offset&3) || up.offset<0x80000 || up.offset>io.length || up.size<76 ||
       up.size>io.length-up.offset || (up.size&3) ||
       (up.offset<box.offset+box.size && box.offset<up.offset+up.size)) return false;
    io_=io; box_=box; uplink_=up; state_=State::Ready; return true;
}
void Ipc3Command::Close() noexcept { state_=State::Closed; io_={}; box_={}; uplink_={}; head_=0; count_=0; }
bool Ipc3Command::Fail(CommandStatus status) noexcept {
    result_.status=status; state_=State::Fault; return false;
}
bool Ipc3Command::Time() noexcept {
    const uint64_t now=io_.now_us(io_.context);
    if(now<previous_) return Fail(CommandStatus::Clock);
    if(now-start_>=500000) return Fail(CommandStatus::Timeout);
    previous_=now; return true;
}
bool Ipc3Command::Read(uint32_t off,uint32_t& v,bool reg) noexcept {
    v=0;
    if(!Time()) return false;
    if((off&3) || off>io_.length || io_.length-off<4 || !io_.read(io_.context,off,&v)) return Fail(CommandStatus::Io);
    if(!Time()) return false;
    if(reg && v==0xffffffffu) return Fail(CommandStatus::Io);
    return true;
}
bool Ipc3Command::Write(uint32_t off,uint32_t v) noexcept {
    if(!Time()) return false;
    if((off&3) || off>io_.length || io_.length-off<4 || !io_.write(io_.context,off,v)) return Fail(CommandStatus::Io);
    return Time();
}
bool Ipc3Command::Registers(uint32_t& notification,uint32_t& done,uint32_t& request) noexcept {
    return Read(0x40,notification) && Drain(notification) && Read(0x4c,done) && Read(0x48,request);
}
bool Ipc3Command::Drain(uint32_t& doorbell) noexcept {
    while(doorbell&0x80000000u) {
        if(count_==4) return Fail(CommandStatus::QueueFull);
        uint32_t size=0,cmd=0;
        if(!Read(uplink_.offset,size,false) || !Read(uplink_.offset+4,cmd,false)) return false;
        const uint32_t type=cmd&0xffff0000u;
        NotificationKind kind;
        if(type==0x600a0000u && size==76) kind=NotificationKind::Position;
        else if(type==0x60090000u && size==76) kind=NotificationKind::Xrun;
        else if(cmd==0x90020000u && size==24) kind=NotificationKind::TracePosition;
        else return Fail(CommandStatus::Notification);
        if(size>uplink_.size || (cmd&0x7fffffffu)!=(doorbell&0x7fffffffu)) return Fail(CommandStatus::Notification);
        auto& event=queue_[(head_+count_)%4]; event={};
        event.kind=kind; event.command=cmd; event.bytes=size;
        Put(event.data,size); Put(event.data+4,cmd);
        for(uint32_t off=8;off<size;off+=4) {
            uint32_t v=0; if(!Read(uplink_.offset+off,v,false)) return false; Put(event.data+off,v);
        }
        if(U32(event.data+8)!=0 || (kind!=NotificationKind::TracePosition &&
           U32(event.data+12)!=(cmd&0xffff))) return Fail(CommandStatus::Notification);
        uint32_t current=0,checkSize=0,checkCmd=0;
        if(!Read(0x40,current) || !Read(uplink_.offset,checkSize,false) ||
           !Read(uplink_.offset+4,checkCmd,false)) return false;
        if(current!=doorbell || size!=checkSize || cmd!=checkCmd) return Fail(CommandStatus::Notification);
        if(!Time()) return false;
        // Retain the capture even if the ACK write or its completion is uncertain.
        ++count_; event.ackAttempted=true;
        if(!Write(0x40,doorbell)) return false;
        event.acknowledged=true;
        // A next notification may already be BUSY, including an identical event.
        if(!Read(0x40,doorbell)) return false;
    }
    return true;
}
CommandStatus Ipc3Command::PollNotifications() noexcept {
    if(state_!=State::Ready) return CommandStatus::State;
    result_={}; state_=State::Active; start_=io_.now_us(io_.context); previous_=start_;
    uint32_t cs=0,ctl=0,interrupts=0,doorbell=0;
    if(!Read(4,cs) || !Read(0x50,ctl) || !Read(8,interrupts)) return result_.status;
    if((cs&0x01010101)!=0x01010000 || (ctl&3) || (interrupts&1)) {
        Fail(CommandStatus::State); return result_.status;
    }
    if(!Read(0x40,doorbell) || !Drain(doorbell)) return result_.status;
    state_=State::Ready; return CommandStatus::Ok;
}
bool Ipc3Command::PopNotification(IpcNotification* out) noexcept {
    if(!out || !count_ || state_==State::Active || state_==State::Closed) return false;
    *out=queue_[head_]; queue_[head_]={}; head_=(head_+1)%4; --count_; return true;
}
CommandResult Ipc3Command::Exchange(const uint8_t* request,size_t n,uint32_t expected,
                                    uint8_t* reply,size_t capacity) noexcept {
    if(state_!=State::Ready) return {};
    result_={};
    if(!request || !reply || n<8 || n>kIpc3MessageLimit || n>box_.size || (n&3) ||
       capacity<12 || capacity>kIpc3MessageLimit || capacity>box_.size || (capacity&3) ||
       U32(request)!=n || !RequestCommand(U32(request+4)) ||
       (expected!=0x10000000 && expected!=U32(request+4))) {
        result_.status=CommandStatus::Argument; return result_;
    }
    for(size_t i=0;i<n;++i) tx_[i]=request[i];
    state_=State::Active; start_=io_.now_us(io_.context); previous_=start_;
    uint32_t cs=0,ctl=0,interrupts=0,notification=0,done=0,busy=0;
    if(!Read(4,cs) || !Read(0x50,ctl) || !Read(8,interrupts)) return result_;
    if((cs&0x01010101)!=0x01010000 || (ctl&3) || (interrupts&1)) {
        Fail(CommandStatus::State); return result_;
    }
    if(!Registers(notification,done,busy)) return result_;
    if((busy&0x80000000u) || (done&0x40000000)) { Fail(CommandStatus::Busy); return result_; }
    for(uint32_t off=0;off<n;off+=4)
        if(!Write(box_.offset+off,U32(tx_+off))) return result_;
    // Recheck before publishing; never overwrite or clear another transaction.
    if(!Registers(notification,done,busy)) return result_;
    if((busy&0x80000000u) || (done&0x40000000)) { Fail(CommandStatus::Busy); return result_; }
    // Set before attempting the posted doorbell write: failure is ambiguous.
    result_.submitted=true;
    if(!Write(0x48,0x80000000)) return result_;
    bool arrived=false;
    for(unsigned attempt=0;attempt<1000;++attempt) {
        if(!Registers(notification,done,busy)) return result_;
        if(done&0x40000000) {
            if(busy&0x80000000u) { Fail(CommandStatus::Reply); return result_; }
            arrived=true; break;
        }
        if(!io_.delay_us(io_.context,500)) { Fail(CommandStatus::Io); return result_; }
    }
    if(!arrived) { Fail(CommandStatus::Timeout); return result_; }
    // DSP overwrites the HOST/downlink mailbox with its reply, not the uplink.
    for(uint32_t off=0;off<12;off+=4) {
        uint32_t v=0; if(!Read(box_.offset+off,v,false)) return result_; Put(rx_+off,v);
    }
    const uint32_t size=U32(rx_),cmd=U32(rx_+4),error=U32(rx_+8);
    const bool negative=(error&0x80000000u)!=0;
    if(size<12 || (size&3) || size>capacity || size>box_.size ||
       (error && !negative) ||
       (negative ? (cmd!=0x10000000 || size!=12) : (cmd!=expected)) ||
       (cmd==0x10000000 && size!=12)) { Fail(CommandStatus::Reply); return result_; }
    for(uint32_t off=12;off<size;off+=4) {
        uint32_t v=0; if(!Read(box_.offset+off,v,false)) return result_; Put(rx_+off,v);
    }
    // Revalidate ownership and header before acknowledging the captured reply.
    if(!Registers(notification,done,busy)) return result_;
    if(!(done&0x40000000) || (busy&0x80000000u)) { Fail(CommandStatus::Reply); return result_; }
    for(uint32_t off=0;off<12;off+=4) {
        uint32_t v=0; if(!Read(box_.offset+off,v,false)) return result_;
        if(v!=U32(rx_+off)) { Fail(CommandStatus::Reply); return result_; }
    }
    if(!Write(0x4c,done|0x40000000)) return result_;
    if(!Read(0x4c,done) || !Read(0x48,busy)) return result_;
    if((done&0x40000000) || (busy&0x80000000u)) { Fail(CommandStatus::Reply); return result_; }
    result_.acknowledged=true; state_=State::Ready;
    if(negative) {
        result_.firmwareError=-static_cast<int>(~error)-1;
        result_.status=CommandStatus::FirmwareError; return result_;
    }
    for(size_t i=0;i<size;++i) reply[i]=rx_[i];
    result_.replyBytes=size; result_.status=CommandStatus::Ok; return result_;
}
}}
