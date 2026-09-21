// SPDX-License-Identifier: MIT
#pragma once
#include "pinned_firmware.h"
#include "pnp_resources.h"

namespace phaser360 { namespace windows {

// M0.6.15H4: one reviewed D0 session composed behind the real PnP callback
// ordering. Still a static-library component, not an installable driver.
//
// PnpResources owns mappings. IpcInterrupt owns the device-lifetime framework
// interrupt shell. GlkBoot/ColdPower own one boot attempt. PinnedFirmware owns
// the approved immutable firmware snapshot. H4 deliberately supports one D0
// attempt per GlkBoot; fresh-boot ownership for arbitrary suspend/resume is a
// later milestone.
class DeviceLifecycle final {
public:
    DeviceLifecycle(IpcInterrupt& irq,GlkBoot& boot,PinnedFirmware& firmware,
                    HardwareAccessGate& gate) noexcept
        : irq_(irq),boot_(boot),firmware_(firmware),gate_(gate),
          power_(boot,irq,gate) {}
    DeviceLifecycle(const DeviceLifecycle&)=delete;
    DeviceLifecycle& operator=(const DeviceLifecycle&)=delete;

    // DeviceAdd stage, before PrepareHardware. Creates the dormant WDF shell.
    NTSTATUS CreateInterruptShell(WDFDEVICE) noexcept;
    // Copy this into PnpResources::InstallLifecycle before PrepareHardware.
    PnpLifecycleOps Ops() noexcept;

    bool Bound() const noexcept { return bound_; }
    bool D0Consumed() const noexcept { return d0Consumed_; }
    bool Removed() const noexcept { return removed_; }

private:
    IpcInterrupt& irq_;
    GlkBoot& boot_;
    PinnedFirmware& firmware_;
    HardwareAccessGate& gate_;
    ColdPower power_;
    bool shellCreated_=false;
    bool bound_=false;
    bool d0Attempted_=false;
    bool d0Consumed_=false;
    bool removed_=false;

    NTSTATUS Prepared(const PnpResourceView&,const PnpDormantInterruptBinding&) noexcept;
    NTSTATUS D0Entry(WDFDEVICE,const PnpResourceView&) noexcept;
    NTSTATUS PostInterruptsEnabled() noexcept;
    NTSTATUS PreInterruptsDisabled() noexcept;
    NTSTATUS D0Exit() noexcept;
    NTSTATUS Release() noexcept;
    void SurpriseRemoval() noexcept;
    bool CleanupFailedEntryWithoutFramework() noexcept;
    bool AbandonRemovedBeforeEnable() noexcept;

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
