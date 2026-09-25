// SPDX-License-Identifier: MIT
#pragma once
#include "d0_session.h"
#include "pinned_firmware.h"
#include "pnp_resources.h"
#include "telemetry.h"

namespace phaser360 { namespace windows {

// Repeated-D0 owner with PrepareHardware-lifetime DMA resources.
//
// PnpResources owns BAR mappings and HardwareAccessGate. This owner keeps one
// dormant framework interrupt shell plus one BootDma resource bundle for the
// whole prepared PnP lifetime. Every D0 still gets a fresh GlkBoot+ColdPower
// session; those sessions only borrow the already-created DMA resources.
class RepeatedDeviceLifecycle final {
public:
    RepeatedDeviceLifecycle(IpcInterrupt& irq,PinnedFirmware& firmware,
                            HardwareAccessGate& gate,
                            TelemetryState* telemetry=nullptr) noexcept
        : irq_(irq),firmware_(firmware),gate_(gate),telemetry_(telemetry) {}
    RepeatedDeviceLifecycle(const RepeatedDeviceLifecycle&)=delete;
    RepeatedDeviceLifecycle& operator=(const RepeatedDeviceLifecycle&)=delete;

    NTSTATUS CreateInterruptShell(WDFDEVICE) noexcept;
    PnpLifecycleOps Ops() noexcept;

    bool PreparedResources() const noexcept { return prepared_; }
    bool ActiveD0() const noexcept { return active_; }
    bool Removed() const noexcept { return gate_.Removed(); }
    ULONG SessionGeneration() const noexcept { return sessions_.Generation(); }
    ULONG CompletedD0() const noexcept { return completedD0_; }
    ULONG FailedD0() const noexcept { return failedD0_; }

private:
    IpcInterrupt& irq_;
    PinnedFirmware& firmware_;
    HardwareAccessGate& gate_;
    TelemetryState* telemetry_=nullptr;
    D0SessionOwner sessions_;
    BootDma dma_;
    WDFDEVICE device_=nullptr;
    PnpDormantInterruptBinding binding_={};
    bool shellCreated_=false;
    bool prepared_=false;
    bool active_=false;
    bool irqBound_=false;
    ULONG completedD0_=0;
    ULONG failedD0_=0;

    NTSTATUS Prepared(const PnpResourceView&,const PnpDormantInterruptBinding&) noexcept;
    NTSTATUS D0Entry(WDFDEVICE,const PnpResourceView&) noexcept;
    NTSTATUS PostInterruptsEnabled() noexcept;
    NTSTATUS PreInterruptsDisabled() noexcept;
    NTSTATUS D0Exit() noexcept;
    NTSTATUS Release() noexcept;
    void SurpriseRemoval() noexcept;

    bool SamePreparedView(const PnpResourceView&) const noexcept;
    bool CleanupFailedEntry() noexcept;
    bool AbandonRemovedBeforeEnable() noexcept;
    bool FinishCleanSession() noexcept;
    bool FinishRemovedSession() noexcept;
    NTSTATUS RecordD0Status(NTSTATUS) noexcept;

    static NTSTATUS PreparedThunk(void*,const PnpResourceView&,
                                  const PnpDormantInterruptBinding&) noexcept;
    static NTSTATUS D0EntryThunk(void*,WDFDEVICE,const PnpResourceView&) noexcept;
    static NTSTATUS PostThunk(void*) noexcept;
    static NTSTATUS PreThunk(void*) noexcept;
    static NTSTATUS ExitThunk(void*) noexcept;
    static NTSTATUS ReleaseThunk(void*) noexcept;
    static void SurpriseThunk(void*) noexcept;
};

} }
