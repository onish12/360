// SPDX-License-Identifier: MIT
#pragma once
#include "hda_transport.h"
#include "../../src/sof/glk_rom.h"
#include "../../src/sof/ipc3_receive.h"
#include "../../src/sof/ipc3_command.h"
namespace phaser360 { namespace windows {
struct TransferResult {
    bool started=false, firmwareEntered=false, dmaReleased=false, ipcReady=false, commandReady=false;
    sof::ReceiveError ipcError=sof::ReceiveError::None;
    sof::RomError romError=sof::RomError::None;
};
// Exact GLK device ownership and both translated BAR mappings supplied by the
// future PnP/power driver. Caller authenticates payload before any operation.
// Payload and XMan must come from the same caller-authenticated image.
// Serialized PASSIVE_LEVEL calls, single attempt, explicit Shutdown mandatory.
class GlkBoot final {
public:
    NTSTATUS Prepare(WDFDEVICE,UCHAR* hda,ULONG hdaLength,UCHAR* dsp,ULONG dspLength,
                     const UCHAR* approvedPayload,SIZE_T bytes,
                     const UCHAR* approvedXman,SIZE_T xmanBytes,USHORT maxAbiMinor) noexcept;
    TransferResult Transfer() noexcept;
    const sof::IpcWindows* Windows() const noexcept { return ipcLive_?ipc_.Windows():nullptr; }
    bool CommandUsable() const noexcept { return ipcLive_ && commands_.Usable(); }
    sof::CommandResult Command(const UCHAR*,SIZE_T,ULONG expectedReplyCommand,UCHAR*,SIZE_T) noexcept;
    sof::CommandStatus PollNotifications() noexcept;
    bool PopNotification(sof::IpcNotification*) noexcept;
    bool Shutdown() noexcept; // preserves DMA if stop fails; no DSP writes then
    sof::ReceiveError IpcError() const noexcept { return ipc_.Error(); }
    sof::RomError RomError() const noexcept { return primaryError_; }
private:
    HdaTransport hda_;
    sof::GlkRom rom_;
    sof::Ipc3Receive ipc_;
    sof::Ipc3Command commands_;
    UCHAR* dsp_=nullptr;
    ULONG length_=0;
    bool ipcLive_=false;
    bool attempted_=false, dspTouched_=false, prepared_=false;
    sof::RomError primaryError_=sof::RomError::None;
    static bool Read(void*,ULONG,ULONG*) noexcept;
    static bool Write(void*,ULONG,ULONG) noexcept;
    static bool Delay(void*,unsigned) noexcept;
    static ULONGLONG Now(void*) noexcept;
    bool Valid(ULONG) const noexcept;
};
} }
