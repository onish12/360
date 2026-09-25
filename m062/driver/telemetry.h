// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>

namespace phaser360 { namespace windows {

enum TelemetryFlags : ULONG {
    TelemetryFirmwareLoaded   = 1u<<0,
    TelemetryResourcesPrepared= 1u<<1,
    TelemetryD0Active         = 1u<<2,
    TelemetryRemoved          = 1u<<3
};

struct TelemetrySnapshotV1 {
    ULONG version=1;
    ULONG size=32u;
    ULONG flags=0;
    ULONG sessionGeneration=0;
    ULONG completedD0=0;
    ULONG failedD0=0;
    LONG lastD0Status=STATUS_INVALID_DEVICE_STATE;
    ULONG reserved=0;
};
static_assert(sizeof(TelemetrySnapshotV1)==32u,"telemetry ABI must remain 32 bytes");

class TelemetryState final {
public:
    TelemetryState() noexcept = default;
    TelemetryState(const TelemetryState&)=delete;
    TelemetryState& operator=(const TelemetryState&)=delete;

    void SetFlag(ULONG mask,bool enabled) noexcept {
        volatile LONG* target=&flags_;
        LONG oldValue=InterlockedCompareExchange(target,0,0);
        for(;;) {
            const ULONG uold=static_cast<ULONG>(oldValue);
            const ULONG unew=enabled ? (uold|mask) : (uold&~mask);
            const LONG desired=static_cast<LONG>(unew);
            const LONG observed=InterlockedCompareExchange(target,desired,oldValue);
            if(observed==oldValue) return;
            oldValue=observed;
        }
    }
    void SetSessionGeneration(ULONG value) noexcept {
        InterlockedExchange(&sessionGeneration_,static_cast<LONG>(value));
    }
    void SetLastD0Status(NTSTATUS status) noexcept {
        InterlockedExchange(&lastD0Status_,static_cast<LONG>(status));
    }
    void IncrementCompleted() noexcept { Increment(&completedD0_); }
    void IncrementFailed() noexcept { Increment(&failedD0_); }

    void Snapshot(TelemetrySnapshotV1* out) const noexcept {
        if(!out) return;
        TelemetrySnapshotV1 snapshot{};
        snapshot.flags=Read(&flags_);
        snapshot.sessionGeneration=Read(&sessionGeneration_);
        snapshot.completedD0=Read(&completedD0_);
        snapshot.failedD0=Read(&failedD0_);
        snapshot.lastD0Status=static_cast<LONG>(Read(&lastD0Status_));
        *out=snapshot;
    }

private:
    volatile LONG flags_=0;
    volatile LONG sessionGeneration_=0;
    volatile LONG completedD0_=0;
    volatile LONG failedD0_=0;
    volatile LONG lastD0Status_=STATUS_INVALID_DEVICE_STATE;

    static ULONG Read(const volatile LONG* value) noexcept {
        auto* mutableValue=const_cast<volatile LONG*>(value);
        return static_cast<ULONG>(InterlockedCompareExchange(mutableValue,0,0));
    }
    static void Increment(volatile LONG* value) noexcept {
        LONG oldValue=InterlockedCompareExchange(value,0,0);
        for(;;) {
            const LONG desired=oldValue+1;
            const LONG observed=InterlockedCompareExchange(value,desired,oldValue);
            if(observed==oldValue) return;
            oldValue=observed;
        }
    }
};

} }
