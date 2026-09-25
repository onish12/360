// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>

namespace phaser360 { namespace windows {

#if defined(PHASER_KERNEL_BUILD)

// {EBAED0DB-F9DB-42EA-A162-5F4111384051}
extern const GUID kStageTraceProviderGuid;

// M1 diagnostic ETW provider. It does not alter hardware state.
NTSTATUS StageTraceRegister() noexcept;
void StageTraceUnregister() noexcept;
void StageTraceStatus(const wchar_t* stage,NTSTATUS status) noexcept;
void StageTraceStatusValue(const wchar_t* stage,NTSTATUS status,ULONG value) noexcept;
inline void StageTrace(const wchar_t* stage) noexcept {
    StageTraceStatus(stage,STATUS_SUCCESS);
}

#else

// Host regression binaries compile selected kernel sources against lightweight
// compatibility headers. Keep tracing completely inert outside the real driver.
inline NTSTATUS StageTraceRegister() noexcept { return STATUS_SUCCESS; }
inline void StageTraceUnregister() noexcept {}
inline void StageTraceStatus(const wchar_t*,NTSTATUS) noexcept {}
inline void StageTraceStatusValue(const wchar_t*,NTSTATUS,ULONG) noexcept {}
inline void StageTrace(const wchar_t*) noexcept {}

#endif

} }
