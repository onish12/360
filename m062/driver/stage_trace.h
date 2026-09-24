// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>

namespace phaser360 { namespace windows {

// {EBAED0DB-F9DB-42EA-A162-5F4111384051}
extern const GUID kStageTraceProviderGuid;

// R4 diagnostic-only ETW provider. It does not alter hardware state.
NTSTATUS StageTraceRegister() noexcept;
void StageTraceUnregister() noexcept;
void StageTraceStatus(PCWSTR stage,NTSTATUS status) noexcept;
inline void StageTrace(PCWSTR stage) noexcept {
    StageTraceStatus(stage,STATUS_SUCCESS);
}

} }
