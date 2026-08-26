#pragma once

// Compatibility declaration for the public sklhdaudbus ADSP bus interface.
// Source contract: https://github.com/coolstar/sklhdaudbus/blob/master/sklhdaudbus/adsp.h
// sklhdaudbus is BSD-3-Clause. This project does not include proprietary
// csaudiointcsof code.

#include <ntddk.h>
#include <wdm.h>
#include <guiddef.h>

// {752A2CAE-3455-4D18-A184-8B34B22632CE}
DEFINE_GUID(GUID_ADSP_BUS_INTERFACE,
    0x752a2cae, 0x3455, 0x4d18, 0xa1, 0x84, 0x8b, 0x34, 0xb2, 0x26, 0x32, 0xce);

// M0.1 deliberately types the hardware callbacks as opaque function pointers.
// Their order and pointer width preserve the version-1 interface ABI on x64,
// while making accidental invocation impossible without an explicit later
// milestone changing this header.
typedef struct _PHASER_ADSP_BUS_INTERFACE_V1 {
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;

    USHORT CtlrDevId;

    PVOID GetResources;
    PVOID SetDSPPowerState;
    PVOID RegisterInterrupt;
    PVOID UnregisterInterrupt;
    PVOID GetRenderStream;
    PVOID GetCaptureStream;
    PVOID FreeStream;
    PVOID PrepareDSP;
    PVOID CleanupDSP;
    PVOID TriggerDSP;
    PVOID StreamPosition;
    PVOID DSPEnableSPIB;
    PVOID DSPDisableSPIB;
} PHASER_ADSP_BUS_INTERFACE_V1, *PPHASER_ADSP_BUS_INTERFACE_V1;

#if defined(_WIN64)
static_assert(sizeof(PHASER_ADSP_BUS_INTERFACE_V1) == 144,
    "Unexpected ADSP v1 ABI size; do not query the bus with a mismatched structure.");
#endif

#define PHASER_ADSP_INTERFACE_VERSION 1u
#define PHASER_GEMINI_LAKE_AUDIO_DEVICE_ID 0x3198u
