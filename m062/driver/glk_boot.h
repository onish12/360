// SPDX-License-Identifier: MIT
#pragma once
#include "hda_transport.h"
#include "../../src/sof/glk_rom.h"
#include "../../src/sof/ipc3_receive.h"
#include "../../src/sof/ipc3_command.h"
namespace phaser360 { namespace windows {
struct TransferResult {
    bool started=false,firmwareEntered=false,dmaReleased=false,ipcReady=false,commandReady=false;
    sof::ReceiveError ipcError=sof::ReceiveError::None;
    sof::RomError romError=sof::RomError::None;
};
class GlkBoot final {
public:
    bool BindAccessGate(HardwareAccessGate* gate) noexcept {
        if(attempted_ || !gate || gate->Removed() || (gate_ && gate_!=gate)) return false;
        if(!hda_.BindAccessGate(gate)) return false;
        gate_=gate; return true;
    }
    bool BindDma(BootDma* dma) noexcept {
        return !attempted_ && hda_.BindDma(dma);
    }
    BootDma* DmaOwner() const noexcept { return hda_.DmaOwner(); }
    HardwareAccessGate* AccessGate() const noexcept { return gate_; }
    bool AccessAllowed() const noexcept { return gate_ && gate_->Allowed(); }
    NTSTATUS Prepare(WDFDEVICE,UCHAR* hda,ULONG hdaLength,UCHAR* dsp,ULONG dspLength,
                     const UCHAR* approvedPayload,SIZE_T bytes,
                     const UCHAR* approvedXman,SIZE_T xmanBytes,USHORT maxAbiMinor) noexcept;
    TransferResult Transfer() noexcept;
    const sof::IpcWindows* Windows() const noexcept { return ipcLive_?ipc_.Windows():nullptr; }
    bool Fresh() const noexcept { return !attempted_; }
    bool CommandUsable() const noexcept { return ipcLive_ && commands_.Usable(); }
    sof::CommandResult Command(const UCHAR*,SIZE_T,ULONG expectedReplyCommand,UCHAR*,SIZE_T) noexcept;
    sof::CommandStatus PollNotifications() noexcept;
    bool PopNotification(sof::IpcNotification*) noexcept;
    bool Shutdown() noexcept;
    sof::ReceiveError IpcError() const noexcept { return ipc_.Error(); }
    sof::RomError RomError() const noexcept { return primaryError_; }
private:
    HardwareAccessGate* gate_=nullptr;
    HdaTransport hda_;
    sof::GlkRom rom_;
    sof::Ipc3Receive ipc_;
    sof::Ipc3Command commands_;
    UCHAR* dsp_=nullptr;
    ULONG length_=0;
    bool ipcLive_=false;
    bool attempted_=false,dspTouched_=false,prepared_=false;
    sof::RomError primaryError_=sof::RomError::None;
    static bool Read(void*,ULONG,ULONG*) noexcept;
    static bool Write(void*,ULONG,ULONG) noexcept;
    static bool Delay(void*,unsigned) noexcept;
    static ULONGLONG Now(void*) noexcept;
    bool Valid(ULONG) const noexcept;
};
} }
