// SPDX-License-Identifier: MIT
#pragma once
#include "glk_rom.h"
#include "ipc3_ready.h"
namespace phaser360 { namespace sof {
struct IpcRegion { uint32_t offset, size; };
struct IpcWindows { IpcRegion region[7]; };
// Exact XMan bytes from the SAME caller-authenticated firmware as the payload.
// Structural validation is NOT authentication. Output cleared on failure.
bool ParseIpc3Windows(const uint8_t*,size_t,uint32_t barLength,IpcWindows*) noexcept;
enum class ReceiveError { None, Argument, State, Windows, Io, Clock, Timeout, Header, Doorbell };
class Ipc3Receive final {
public:
    Ipc3Receive() noexcept = default;
    Ipc3Receive(const Ipc3Receive&)=delete;
    Ipc3Receive& operator=(const Ipc3Receive&)=delete;
    bool Configure(const RomIo&,const uint8_t* approvedXman,size_t,uint16_t maxAbiMinor) noexcept;
    bool Arm() noexcept; // cold powered-down cores; clears stale HIPCT BUSY
    bool Receive() noexcept; // once, after ROM entry AND confirmed DMA stop
    ReceiveError Error() const noexcept { return error_; }
    const ReadyInfo* Ready() const noexcept { return complete_?&ready_:nullptr; }
    const IpcWindows* Windows() const noexcept { return complete_?&windows_:nullptr; }
private:
    RomIo io_={};
    IpcWindows windows_={};
    ReadyInfo ready_={};
    uint16_t maxMinor_=0;
    bool configured_=false, armed_=false, attempted_=false, complete_=false;
    ReceiveError error_=ReceiveError::None;
    bool Fail(ReceiveError) noexcept;
    bool Read(uint32_t,uint32_t&) noexcept;
};
}}
