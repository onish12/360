// SPDX-License-Identifier: MIT
#pragma once
#include "glk_boot.h"
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
    bool Stop() noexcept; // terminal; false means retain mapping and recover
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
    bool closed_=false; // PASSIVE serial lock only
    volatile LONG pendingWork_=0;
    bool disableSeen_=false;
    bool enableSeen_=false;
    bool created_=false,armed_=false,enabled_=false,ready_=false,fault_=false,stopped_=false;
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
