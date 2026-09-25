// SPDX-License-Identifier: MIT
#include "boot_sequence.h"
namespace phaser360 { namespace sof {
bool BuildGlkRomControl(uint8_t tag, uint32_t* command) noexcept {
    if (!command) return false;
    *command = 0;
    if (!tag || tag > 15) return false;
    *command = 0x81004000u | (uint32_t(tag - 1u) << 9);
    return true;
}
namespace {
bool RomState(uint32_t value, uint32_t expected) noexcept {
    return !(value & 0x80000000u) && (value & 0x00ffffffu) == expected;
}
bool TimeoutValid(uint32_t ms) noexcept { return ms > 0 && ms <= 60000; }
}
BootResult RunBootSequence(const uint8_t* bytes, size_t size,
                           const BootPolicy& p, const BootBackend& b) noexcept {
    BootResult r{};
    r.status = BootStatus::InvalidArgument;
    r.image_status = ParseStatus::InvalidArgument;
    r.ready_status = ReadyStatus::InvalidArgument;
    r.cleanup_confirmed = true; // no mutation yet
    if (!bytes || !b.verify_image || !b.prepare || !b.init_rom || !b.start ||
        !b.wait_entered || !b.quiesce || !b.wait_ready || !b.release || !b.shutdown ||
        !TimeoutValid(p.rom_init_timeout_ms) || !TimeoutValid(p.firmware_timeout_ms) ||
        !TimeoutValid(p.ready_timeout_ms) || p.max_abi_minor > 0xfffu) return r;
    FirmwareImage image{};
    r.image_status = ParseFirmwareImage(bytes, size, &image);
    if (r.image_status != ParseStatus::Ok) { r.status = BootStatus::InvalidImage; return r; }
    if (!b.verify_image(b.context, bytes, size)) { r.status = BootStatus::ImageRejected; return r; }
    bool rom_attempted = false, stopped = false, stop_attempted = false;
    auto finish = [&](BootStatus status) noexcept {
        r.status = status;
        // Even failed preparation/start can be partial; prove quiescence.
        if (!stop_attempted) {
            stop_attempted = true;
            stopped = b.quiesce(b.context);
        }
        if (!stopped) {
            r.cleanup_confirmed = false;
            r.resources_retained = true;
            return r;
        }
        if (status != BootStatus::ReadyHeaderValidated && rom_attempted)
            r.shutdown_failed = !b.shutdown(b.context);
        r.release_failed = !b.release(b.context);
        r.resources_retained = r.release_failed;
        r.cleanup_confirmed = !r.release_failed && !r.shutdown_failed;
        if (r.release_failed && status == BootStatus::ReadyHeaderValidated) {
            r.status = BootStatus::ReleaseFailed;
            r.shutdown_failed = !b.shutdown(b.context);
        }
        return r;
    };
    uint8_t tag = 0;
    if (!b.prepare(b.context, bytes + image.payload_offset, image.payload_bytes, &tag))
        return finish(BootStatus::PrepareFailed);
    uint32_t command = 0;
    if (!BuildGlkRomControl(tag, &command)) return finish(BootStatus::InvalidStream);
    rom_attempted = true;
    if (!b.init_rom(b.context, command, p.rom_init_timeout_ms, &r.rom_status))
        return finish(BootStatus::RomInitFailed);
    if (!RomState(r.rom_status, 1)) return finish(BootStatus::RomStateInvalid);
    if (!b.start(b.context)) return finish(BootStatus::StartFailed);
    if (!b.wait_entered(b.context, p.firmware_timeout_ms, &r.rom_status))
        return finish(BootStatus::FirmwareEntryFailed);
    if (!RomState(r.rom_status, 5)) return finish(BootStatus::FirmwareStateInvalid);
    stop_attempted = true;
    stopped = b.quiesce(b.context);
    if (!stopped) return finish(BootStatus::StopFailed);
    uint8_t ready[kIpc3ReadyBytes]{};
    size_t received = 0;
    if (!b.wait_ready(b.context, p.ready_timeout_ms, ready, sizeof(ready), &received))
        return finish(BootStatus::ReadyWaitFailed);
    r.ready_status = ParseIpc3Ready(ready, received, p.max_abi_minor, &r.ready);
    if (r.ready_status != ReadyStatus::Ok) return finish(BootStatus::ReadyInvalid);
    return finish(BootStatus::ReadyHeaderValidated);
}
}}
