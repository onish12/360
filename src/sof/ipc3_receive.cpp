// SPDX-License-Identifier: MIT
#include "ipc3_receive.h"
namespace phaser360 { namespace sof {
namespace {
uint32_t U32(const uint8_t* b) noexcept {
    return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);
}
}
bool ParseIpc3Windows(const uint8_t* b,size_t n,uint32_t length,IpcWindows* out) noexcept {
    if(!out) return false;
    *out={};
    if(!b || n<16 || n>65536 || (n&3) || length>0x100000 || length<0x82000 ||
       U32(b)!=0x6e614d58 || U32(b+4)!=n || U32(b+8)!=16 || (U32(b+12)>>24)!=1) return false;
    IpcWindows result={}; bool found=false;
    for(size_t pos=16;pos<n;) {
        if(n-pos<8) return false;
        const uint32_t type=U32(b+pos), size=U32(b+pos+4);
        if(size<8 || (size&3) || size>n-pos) return false;
        if(type==1) {
            // v1.9 fixed 16-element ABI, 8-byte element header and alignment padding.
            if(found || size<408) return false;
            found=true; const uint8_t* p=b+pos+8;
            if(U32(p)!=400 || U32(p+4)!=kIpc3ReadyCommand || U32(p+8)!=1) return false;
            const uint32_t count=U32(p+12);
            if(!count || count>16) return false;
            for(uint32_t i=0;i<count;++i) {
                const uint8_t* e=p+16+24*i;
                const uint32_t kind=U32(e+4),id=U32(e+8),bytes=U32(e+16),off=U32(e+20);
                // Upstream v1.9 leaves elem.hdr.size zero; accept zero or ABI size.
                if((U32(e)!=0 && U32(e)!=24) || kind>6 || id>3 || U32(e+12)!=0 ||
                   !bytes || (bytes&3) || (off&3) || off>=0x20000 || bytes>0x20000-off ||
                   result.region[kind].size) return false;
                const uint32_t address=0x80000+id*0x20000+off;
                if(address>length || bytes>length-address) return false;
                for(const auto& prev:result.region)
                    if(prev.size && address<prev.offset+prev.size && prev.offset<address+bytes) return false;
                result.region[kind]={address,bytes};
            }
        }
        pos+=size;
    }
    // This receiver supports only the reviewed GLK initial uplink layout.
    if(!found || result.region[1].offset!=0x81000 || result.region[1].size<108 ||
       result.region[0].size<8) return false;
    *out=result; return true;
}
bool Ipc3Receive::Fail(ReceiveError e) noexcept { error_=e; complete_=false; ready_={}; return false; }
bool Ipc3Receive::Read(uint32_t o,uint32_t& v) noexcept {
    v=0;
    if((o&3) || o>io_.length || io_.length-o<4 || !io_.read(io_.context,o,&v)) return Fail(ReceiveError::Io);
    return true;
}
bool Ipc3Receive::Configure(const RomIo& io,const uint8_t* x,size_t n,uint16_t minor) noexcept {
    if(configured_ || attempted_) return Fail(ReceiveError::State);
    if(!io.read || !io.write || !io.now_us || !io.delay_us || minor>0xfff) return Fail(ReceiveError::Argument);
    if(!ParseIpc3Windows(x,n,io.length,&windows_)) return Fail(ReceiveError::Windows);
    io_=io; maxMinor_=minor; configured_=true; error_=ReceiveError::None; return true;
}
bool Ipc3Receive::Arm() noexcept {
    if(!configured_ || attempted_) return Fail(ReceiveError::State);
    attempted_=true;
    uint32_t cs=0,ctl=0,interrupts=0,v=0;
    if(!Read(4,cs) || !Read(0x50,ctl) || !Read(8,interrupts)) return false;
    if((cs&0x03030303)!=0x303 || ctl==0xffffffffu || interrupts==0xffffffffu ||
       (ctl&3) || (interrupts&1)) return Fail(ReceiveError::State);
    if(!Read(0x40,v)) return false;
    if(v==0xffffffffu) return Fail(ReceiveError::Io);
    if(v&0x80000000u) {
        if(!io_.write(io_.context,0x40,v)) return Fail(ReceiveError::Io);
        if(!Read(0x40,v)) return false;
        if(v&0x80000000u) return Fail(ReceiveError::Doorbell);
    }
    armed_=true; return true;
}
bool Ipc3Receive::Receive() noexcept {
    if(!armed_) return Fail(ReceiveError::State);
    armed_=false;
    const uint64_t start=io_.now_us(io_.context); uint64_t previous=start;
    uint32_t doorbell=0;
    for(unsigned attempt=0;attempt<10000;++attempt) {
        uint64_t now=io_.now_us(io_.context);
        if(now<previous) return Fail(ReceiveError::Clock);
        if(now-start>=5000000) return Fail(ReceiveError::Timeout);
        previous=now;
        if(!Read(0x40,doorbell)) return false;
        now=io_.now_us(io_.context);
        if(now<previous) return Fail(ReceiveError::Clock);
        if(now-start>=5000000) return Fail(ReceiveError::Timeout);
        previous=now;
        if(doorbell==0xffffffffu) return Fail(ReceiveError::Io);
        if(doorbell&0x80000000u) break;
        if(attempt==9999) return Fail(ReceiveError::Timeout);
        if(!io_.delay_us(io_.context,500)) return Fail(ReceiveError::Io);
    }
    if(doorbell!=(0x80000000u|kIpc3ReadyCommand)) return Fail(ReceiveError::Doorbell);
    uint8_t bytes[kIpc3ReadyBytes]={};
    for(uint32_t off=0;off<kIpc3ReadyBytes;off+=4) {
        uint32_t v=0; if(!Read(0x81000+off,v)) return false;
        for(unsigned j=0;j<4;++j) bytes[off+j]=static_cast<uint8_t>(v>>(8*j));
    }
    ReadyInfo candidate={};
    if(ParseIpc3Ready(bytes,sizeof(bytes),maxMinor_,&candidate)!=ReadyStatus::Ok) return Fail(ReceiveError::Header);
    uint32_t current=0;
    if(!Read(0x40,current)) return false;
    if(current!=doorbell) return Fail(ReceiveError::Doorbell);
    const uint64_t now=io_.now_us(io_.context);
    if(now<previous) return Fail(ReceiveError::Clock);
    if(now-start>=5000000) return Fail(ReceiveError::Timeout);
    // Host-side HIPCT BUSY is W1C. Keep IRQs masked (no ISR installed).
    // Do not require BUSY=0 afterwards: a new notification may already arrive.
    if(!io_.write(io_.context,0x40,doorbell)) return Fail(ReceiveError::Io);
    ready_=candidate; complete_=true; error_=ReceiveError::None; return true;
}
}}
