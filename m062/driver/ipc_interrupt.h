// SPDX-License-Identifier: MIT
#pragma once
#include "glk_boot.h"
#include "pnp_resources.h"
namespace phaser360 { namespace windows {
// Device-context lifetime. Create in PrepareHardware, Arm after successful boot
// in D0. Stop in D0ExitPreInterruptsDisabled BEFORE boot shutdown/BAR unmapping.
// RebindStopped requires old work drained and framework Disable completed.
// No direct GlkBoot access while armed. Child WDF objects die with the device.
class IpcInterrupt final {
public:
    IpcInterrupt() noexcept = default;
    IpcInterrupt(const IpcInterrupt&)=delete;
    IpcInterrupt& operator=(const IpcInterrupt&)=delete;
    // DeviceAdd-only shell: WDF owns one device-lifetime interrupt object and
    // receives its assigned resource later. No boot/DSP binding and no MMIO.
    NTSTATUS CreateDormant(WDFDEVICE) noexcept;
    // H2 software binding only. The WDF object already exists and remains
    // hardware-inert: this does not grant MMIO in Enable/ISR/Sync paths.
    bool BindDormant(const PnpDormantInterruptBinding&,GlkBoot*) noexcept;
    bool UnbindDormant() noexcept;
    // H3 two-stage permission. Both calls are software-only.
    // GrantBootStart permits ColdPower::Enter but not framework IRQ MMIO.
    bool GrantBootStart() noexcept;
    // Only after ColdPower::Enter established command-ready firmware.
    bool GrantFrameworkEnableAfterBoot() noexcept;
    // After confirmed ColdPower shutdown/cancel and framework disconnect (if it
    // ever connected), return a DeviceAdd shell to pristine dormant state.
    bool ResetDormantClosedSession() noexcept;
    // Legacy/precomposed test entry: creates with explicit assigned descriptors.
    NTSTATUS Create(WDFDEVICE,PCM_PARTIAL_RESOURCE_DESCRIPTOR raw,
                    PCM_PARTIAL_RESOURCE_DESCRIPTOR translated,GlkBoot*,UCHAR* dsp,ULONG length) noexcept;
    // Only in serialized PnP startup before the first framework Enable callback.
    bool CanStartBeforeEnable() noexcept;
    bool CancelBeforeEnable() noexcept; // no MMIO and no interrupt synchronization
    // Next D0 only after confirmed Stop, framework Disable, and drained work.
    bool RebindStopped(GlkBoot*,UCHAR* dsp,ULONG length) noexcept;
    bool Running() noexcept; // software gate health, not proof of hardware delivery
    bool Arm() noexcept;
    bool DrainStopped() noexcept; // PASSIVE PnP thread, never from this worker
    bool Stop() noexcept; // closes admission even if masking fails; retain mappings on false
    // EvtDeviceSurpriseRemoval path after HardwareAccessGate::SurpriseRemove().
    // Software-only fence: closes only PASSIVE admission. It deliberately does
    // not mutate IRQ-lock-owned state because SurpriseRemoval is unsynchronized
    // with Enable/Disable. No MMIO/synchronization/mask claim is made. A later
    // framework Disable/disconnect is still required before drain.
    bool FenceForSurpriseRemoval() noexcept;
    // D0Exit thread after framework disconnect, still D0 and hardware accessible.
    // Previously armed sessions require pre-disable Stop. Failed/missing Enable
    // may omit Disable; that branch retries masking at PASSIVE without IRQ sync.
    bool StopAfterDisconnect() noexcept;
    sof::CommandResult Command(const UCHAR*,SIZE_T,ULONG,UCHAR*,SIZE_T) noexcept;
    bool Pop(sof::IpcNotification*) noexcept;
private:
    WDFINTERRUPT interrupt_=nullptr;
    WDFWAITLOCK serial_=nullptr;
    WDFDPC dpc_=nullptr;
    WDFWORKITEM work_=nullptr;
    bool drained_=false;
    GlkBoot* boot_=nullptr;
    UCHAR* dsp_=nullptr;
    bool admissionClosed_=false; // PASSIVE lock; terminal even after failed Stop
    bool closed_=false; // PASSIVE lock; confirmed interrupt mask/cancellation
    bool disconnectedSeen_=false; // serialized D0Exit call, not a connection query
    bool enableFailed_=false,disableMasked_=false,everArmed_=false; // IRQ lock, then serialized PnP reader
    volatile LONG pendingWork_=0;
    bool disableSeen_=false;
    bool enableSeen_=false;
    bool deviceLifetimeShell_=false;
    bool bootStartAllowed_=false;      // permits ColdPower::Enter only
    bool hardwareEnableAllowed_=false; // permits Enable/Sync MMIO only after boot
    bool created_=false,armed_=false,enabled_=false,ready_=false,fault_=false,stopped_=false;
    NTSTATUS CreateObjects(WDFDEVICE,PCM_PARTIAL_RESOURCE_DESCRIPTOR raw,
                           PCM_PARTIAL_RESOURCE_DESCRIPTOR translated) noexcept;
    bool Read(ULONG,ULONG&) noexcept;
    bool Bits(ULONG,ULONG,ULONG) noexcept;
    bool Mask() noexcept;
    bool Unmask() noexcept;
    enum class Operation { Arm, Begin, Rearm, Stop, Check, Fault };
    struct SyncRequest { IpcInterrupt* self; Operation operation; };
    bool Sync(Operation) noexcept;
    static BOOLEAN Synchronized(WDFINTERRUPT,WDFCONTEXT);
    static BOOLEAN Isr(WDFINTERRUPT,ULONG);
    static NTSTATUS Enable(WDFINTERRUPT,WDFDEVICE);
    static NTSTATUS Disable(WDFINTERRUPT,WDFDEVICE);
    static void Deferred(WDFDPC);
    static void Work(WDFWORKITEM);
};
}}
