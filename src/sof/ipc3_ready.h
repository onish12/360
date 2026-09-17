// SPDX-License-Identifier: MIT
#ifndef PHASER360_IPC3_READY_H
#define PHASER360_IPC3_READY_H
#include <stddef.h>
#include <stdint.h>
namespace phaser360 { namespace sof {
constexpr size_t kIpc3ReadyBytes = 108;
constexpr uint32_t kIpc3ReadyCommand = 0x70000000u;
enum class ReadyStatus { Ok, InvalidArgument, Size, Command, VersionSize, Abi, Reserved };
struct ReadyInfo {
    uint16_t major, minor, micro, build;
    uint32_t abi, source_hash;
    uint32_t dspbox_offset, hostbox_offset, dspbox_size, hostbox_size;
    uint64_t flags;
};
// Fixed IPC3 header only. Mailbox fields remain UNTRUSTED data, not MMIO
// addresses. Extended window descriptors require a separate validation gate.
// No allocation/I/O; output must not alias input and is cleared on failure.
ReadyStatus ParseIpc3Ready(const uint8_t*, size_t, uint16_t max_abi_minor,
                          ReadyInfo*) noexcept;
}}
#endif
