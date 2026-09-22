// SPDX-License-Identifier: MIT
#pragma once
#include "d0_session.h"
#include "pinned_firmware.h"
#include "pnp_resources.h"
#include "telemetry.h"

namespace phaser360 { namespace windows {

// M0.6.15H5 repeated-D0 owner.
//
// Resource lifetime (PnpResources), device-lifetime IRQ shell (IpcInterrupt),
// firmware identity (PinnedFirmware), and each D0 boot/power session have
// separate ownership. A fresh GlkBoot+ColdPower pair is allocated for every
// D0Entry attempt and destroyed after confirmed clean exit or terminal removal.
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
    // HardwareAccessGate is the single atomic terminal-removal truth shared
    // with the unsynchronized EvtDeviceSurpriseRemoval callback.
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
