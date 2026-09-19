// SPDX-License-Identifier: MIT
#pragma once
#include "boot_dma.h"
#include "../../src/sof/hda_stream.h"
namespace phaser360 { namespace windows {
// Caller owns a translated, resident, read/write, noncached HDA BAR mapping.
// No physical address literals, mapping, PnP binding or device power policy here.
class HdaTransport final {
public:
    HdaTransport() noexcept = default;
    HdaTransport(const HdaTransport&) = delete;
    HdaTransport& operator=(const HdaTransport&) = delete;
    // Single boot attempt per object. All operations serialized at PASSIVE_LEVEL.
    // Failure after allocation requires StopAndRelease before parent teardown.
    NTSTATUS Prepare(WDFDEVICE device, UCHAR* mappedHda, ULONG length,
                     const UCHAR* approvedPayload, SIZE_T bytes) noexcept;
    bool Start() noexcept;
    bool StopAndRelease() noexcept;
    UCHAR Tag() const noexcept { return stream_.Tag(); }
private:
    BootDma dma_;
    sof::BootStream stream_;
    UCHAR* base_=nullptr;
    ULONG length_=0;
    bool allocated_=false, published_=false, attempted_=false;
    static bool Read(void*,ULONG,unsigned,ULONG*) noexcept;
    static bool Write(void*,ULONG,unsigned,ULONG) noexcept;
    static void Delay(void*,unsigned) noexcept;
    static bool Verify(void*) noexcept;
    bool Valid(ULONG,unsigned) const noexcept;
};
} }
