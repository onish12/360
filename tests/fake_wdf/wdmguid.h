// SPDX-License-Identifier: MIT
#pragma once
#include "ntddk.h"

// Host-test identity only. Real WDK builds use the system wdmguid.h.
inline constexpr GUID GUID_BUS_INTERFACE_STANDARD{
    0x496b8280u,0x6f25u,0x11d0u,{0x9c,0xe7,0x08,0x00,0x3e,0x30,0x1f,0x73}
};
