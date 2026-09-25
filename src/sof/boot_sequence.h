// SPDX-License-Identifier: MIT
#ifndef PHASER360_SOF_BOOT_SEQUENCE_H
#define PHASER360_SOF_BOOT_SEQUENCE_H
#include "firmware_image.h"
#include "ipc3_ready.h"
namespace phaser360 { namespace sof {
enum class BootStatus {
    InvalidArgument, InvalidImage, ImageRejected, PrepareFailed, InvalidStream,
    RomInitFailed, RomStateInvalid, StartFailed, FirmwareEntryFailed,
    FirmwareStateInvalid, StopFailed, ReadyWaitFailed, ReadyInvalid,
    ReleaseFailed, ReadyHeaderValidated
};
struct BootPolicy {
    uint32_t rom_init_timeout_ms, firmware_timeout_ms, ready_timeout_ms;
    uint16_t max_abi_minor;
};
struct BootResult {
    BootStatus status;
    ParseStatus image_status;
    ReadyStatus ready_status;
    bool cleanup_confirmed;
    bool resources_retained;
    bool shutdown_failed;
    bool release_failed;
    uint32_t rom_status;
    ReadyInfo ready;
};
// Backend CONTRACT, not a Windows implementation. Callbacks must be bounded,
// synchronous and nonthrowing. They must never enable amplifiers/audio streams.
// Caller exclusively owns the device and keeps image bytes immutable.
// verify_image approves/authenticates the exact bytes before any mutation.
// prepare copies only CPD payload and arms a fresh ready event for this boot.
// Partial preparation must remain quiesce-able. init_rom handles APL/GLK cores,
// clocks, ROM command and IPC setup. Wait operations enforce supplied deadlines
// and return fresh observations, never a prior boot's cached ready message.
// quiesce proves owned DMA cannot access buffers, even after partial failure.
// release clears descriptor references before freeing allocations.
// shutdown disables IPC and resets/powers down only the owned DSP on failure.
struct BootBackend {
    void* context;
    bool (*verify_image)(void*, const uint8_t*, size_t);
    bool (*prepare)(void*, const uint8_t*, size_t, uint8_t*);
    bool (*init_rom)(void*, uint32_t, uint32_t, uint32_t*);
    bool (*start)(void*);
    bool (*wait_entered)(void*, uint32_t, uint32_t*);
    bool (*quiesce)(void*);
    bool (*wait_ready)(void*, uint32_t, uint8_t*, size_t, size_t*);
    bool (*release)(void*);
    bool (*shutdown)(void*);
};
// Cold boot only, no retries/IMR. Failed quiescence forbids memory release.
// ReadyHeaderValidated does NOT mean validated IPC windows or working audio.
BootResult RunBootSequence(const uint8_t*, size_t, const BootPolicy&,
                           const BootBackend&) noexcept;
bool BuildGlkRomControl(uint8_t stream_tag, uint32_t* command) noexcept;
}}
#endif
