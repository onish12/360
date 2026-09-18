// SPDX-License-Identifier: MIT
#pragma once
#include "ipc3_receive.h"
namespace phaser360 { namespace sof {
constexpr size_t kIpc3MessageLimit=384;
enum class CommandStatus { Ok, Argument, State, Io, Timeout, Clock, PendingNotification,
                           Busy, Reply, FirmwareError };
struct CommandResult {
    CommandStatus status=CommandStatus::State;
    bool submitted=false, acknowledged=false;
    int firmwareError=0;
    size_t replyBytes=0;
};
// Serialized polling transport, one command at a time. No PM/compound/debug
// commands, notification dispatch or retries after an ambiguous transaction.
class Ipc3Command final {
public:
    Ipc3Command() noexcept = default;
    Ipc3Command(const Ipc3Command&)=delete;
    Ipc3Command& operator=(const Ipc3Command&)=delete;
    bool Bind(const RomIo&,const Ipc3Receive&) noexcept; // completed gate required; no I/O
    void Close() noexcept; // no I/O; must run before power-down/unmapping
    bool Usable() const noexcept { return state_==State::Ready; }
    // Output untouched on failure, including firmware rejection. Output/request
    // buffers must be caller-owned, stable and not overlap this object.
    CommandResult Exchange(const uint8_t*,size_t,uint32_t expectedReplyCommand,
                           uint8_t* reply,size_t replyCapacity) noexcept;
private:
    enum class State { Unbound, Ready, Active, Fault, Closed };
    State state_=State::Unbound;
    RomIo io_={};
    IpcRegion box_={};
    uint8_t tx_[kIpc3MessageLimit]={},rx_[kIpc3MessageLimit]={};
    uint64_t start_=0,previous_=0;
    CommandResult result_={};
    bool Fail(CommandStatus) noexcept;
    bool Time() noexcept;
    bool Read(uint32_t,uint32_t&,bool reg=true) noexcept;
    bool Write(uint32_t,uint32_t) noexcept;
    bool Registers(uint32_t&,uint32_t&,uint32_t&) noexcept;
};
}}
