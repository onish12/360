// SPDX-License-Identifier: MIT
#pragma once
#include "hda_stream.h"

namespace phaser360 { namespace sof {

enum class HdaControllerState { Empty, Initialized, Fault, Quiesced };

// Minimal APL/GLK HDA global-state owner for the firmware code-loader path.
// It owns controller-wide state required before BootStream::Select.
// PCI configuration space, codecs, endpoints and audio routing are out of scope.
class HdaController final {
public:
    HdaController() noexcept = default;
    HdaController(const HdaController&)=delete;
    HdaController& operator=(const HdaController&)=delete;

    bool Initialize(const RegisterIo&) noexcept;
    bool Quiesce() noexcept;
    bool Ready() const noexcept { return state_==HdaControllerState::Initialized; }
    HdaControllerState State() const noexcept { return state_; }

private:
    RegisterIo io_={};
    HdaControllerState state_=HdaControllerState::Empty;
    uint32_t inputs_=0,outputs_=0,total_=0,pp_=0,spib_=0;
    bool l1Captured_=false,l1WasSet_=false;

    bool Read(uint32_t,unsigned,uint32_t&) noexcept;
    bool Write(uint32_t,unsigned,uint32_t) noexcept;
    bool Update(uint32_t,unsigned,uint32_t,uint32_t) noexcept;
    bool Match(uint32_t,unsigned,uint32_t,uint32_t) noexcept;
    bool Poll(uint32_t,unsigned,uint32_t,uint32_t,unsigned) noexcept;
    bool DiscoverCapabilities() noexcept;
    bool VerifyColdStreams() noexcept;
    uint32_t StreamMask() const noexcept;
};

} }
