// SPDX-License-Identifier: MIT
#pragma once
#include "boot_dma.h"
#include "hardware_access_gate.h"
#include "pci_config_attestation.h"
#include "pci_config_boot_policy.h"
#include "../../src/sof/hda_controller.h"
#include "../../src/sof/hda_stream.h"
namespace phaser360 { namespace windows {
// Caller owns a translated, resident, read/write, noncached HDA BAR mapping.
// DMA common buffers are owned by the surrounding PrepareHardware lifetime.
class HdaTransport final {
public:
    HdaTransport() noexcept = default;
    HdaTransport(const HdaTransport&) = delete;
    HdaTransport& operator=(const HdaTransport&) = delete;
    bool BindAccessGate(HardwareAccessGate* gate) noexcept {
        if(attempted_ || !gate || gate->Removed() || (gate_ && gate_!=gate)) return false;
        gate_=gate; return true;
    }
    bool BindDma(BootDma* dma) noexcept {
        if(attempted_ || !dma || !dma->HardwarePrepared() ||
           (dma_ && dma_!=dma)) return false;
        dma_=dma; return true;
    }
    BootDma* DmaOwner() const noexcept { return dma_; }

    // Single boot attempt per object. All operations serialized at PASSIVE_LEVEL.
    NTSTATUS Prepare(WDFDEVICE device,UCHAR* mappedHda,ULONG length,
                     const UCHAR* approvedPayload,SIZE_T bytes) noexcept;
    bool Start() noexcept;
    bool StopAndRelease() noexcept;
    bool QuiesceController() noexcept;
    UCHAR Tag() const noexcept { return stream_.Tag(); }
private:
    BootDma* dma_=nullptr;
    PciConfigAttestation pci_;
    PciConfigBootPolicy pciPolicy_;
    sof::HdaController controller_;
    sof::BootStream stream_;
    HardwareAccessGate* gate_=nullptr;
    UCHAR* base_=nullptr;
    ULONG length_=0;
    bool allocated_=false,published_=false,attempted_=false,controllerAttempted_=false;
    static bool Read(void*,ULONG,unsigned,ULONG*) noexcept;
    static bool Write(void*,ULONG,unsigned,ULONG) noexcept;
    static void Delay(void*,unsigned) noexcept;
    static bool Verify(void*) noexcept;
    bool Valid(ULONG,unsigned) const noexcept;
};
} }
