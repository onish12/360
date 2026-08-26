#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>
#include "adsp_bus_contract.h"

#define PHASER_ENABLE_HARDWARE_CALLS 0

#if PHASER_ENABLE_HARDWARE_CALLS != 0
#error M0.1 is interface-query-only. Hardware callbacks must remain disabled.
#endif

typedef struct _PHASER_DEVICE_CONTEXT {
    PHASER_ADSP_BUS_INTERFACE_V1 Adsp;
    BOOLEAN InterfaceAcquired;
} PHASER_DEVICE_CONTEXT, *PPHASER_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    PHASER_DEVICE_CONTEXT,
    PhaserGetDeviceContext
)

extern "C" DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD PhaserEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE PhaserEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE PhaserEvtDeviceReleaseHardware;

static void
PhaserLog(
    _In_z_ PCSTR Message
)
{
    //
    // KdPrintEx can compile away depending on build configuration.
    // Explicitly mark the parameter as referenced so /WX does not
    // turn warning C4100 into a build failure.
    //
    UNREFERENCED_PARAMETER(Message);

    KdPrintEx((
        DPFLTR_IHVDRIVER_ID,
        DPFLTR_INFO_LEVEL,
        "PHASER360_PROBE: %s\n",
        Message
    ));
}

extern "C"
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(
        &config,
        PhaserEvtDeviceAdd
    );

    KdPrintEx((
        DPFLTR_IHVDRIVER_ID,
        DPFLTR_INFO_LEVEL,
        "PHASER360_PROBE: DriverEntry M0.1 read-only\n"
    ));

    return WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
    );
}

NTSTATUS
PhaserEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    UNREFERENCED_PARAMETER(Driver);

    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);

    pnp.EvtDevicePrepareHardware =
        PhaserEvtDevicePrepareHardware;

    pnp.EvtDeviceReleaseHardware =
        PhaserEvtDeviceReleaseHardware;

    WdfDeviceInitSetPnpPowerEventCallbacks(
        DeviceInit,
        &pnp
    );

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        PHASER_DEVICE_CONTEXT
    );

    WDFDEVICE device;

    NTSTATUS status = WdfDeviceCreate(
        &DeviceInit,
        &attributes,
        &device
    );

    if (!NT_SUCCESS(status)) {
        KdPrintEx((
            DPFLTR_IHVDRIVER_ID,
            DPFLTR_ERROR_LEVEL,
            "PHASER360_PROBE: WdfDeviceCreate failed 0x%08X\n",
            status
        ));

        return status;
    }

    auto ctx = PhaserGetDeviceContext(device);

    RtlZeroMemory(
        ctx,
        sizeof(*ctx)
    );

    PhaserLog(
        "device created; no hardware access performed"
    );

    return STATUS_SUCCESS;
}

NTSTATUS
PhaserEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
)
{
    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    auto ctx = PhaserGetDeviceContext(Device);

    RtlZeroMemory(
        &ctx->Adsp,
        sizeof(ctx->Adsp)
    );

    ctx->InterfaceAcquired = FALSE;

    NTSTATUS status = WdfFdoQueryForInterface(
        Device,
        &GUID_ADSP_BUS_INTERFACE,
        reinterpret_cast<PINTERFACE>(&ctx->Adsp),
        static_cast<USHORT>(sizeof(ctx->Adsp)),
        PHASER_ADSP_INTERFACE_VERSION,
        nullptr
    );

    if (!NT_SUCCESS(status)) {
        KdPrintEx((
            DPFLTR_IHVDRIVER_ID,
            DPFLTR_ERROR_LEVEL,
            "PHASER360_PROBE: GUID_ADSP_BUS_INTERFACE v1 query failed 0x%08X\n",
            status
        ));

        return status;
    }

    ctx->InterfaceAcquired = TRUE;

    KdPrintEx((
        DPFLTR_IHVDRIVER_ID,
        DPFLTR_INFO_LEVEL,
        "PHASER360_PROBE: interface OK size=%hu version=%hu CtlrDevId=0x%04hX\n",
        ctx->Adsp.Size,
        ctx->Adsp.Version,
        ctx->Adsp.CtlrDevId
    ));

    if (
        ctx->Adsp.Version != PHASER_ADSP_INTERFACE_VERSION ||
        ctx->Adsp.Size != sizeof(PHASER_ADSP_BUS_INTERFACE_V1)
    ) {
        KdPrintEx((
            DPFLTR_IHVDRIVER_ID,
            DPFLTR_ERROR_LEVEL,
            "PHASER360_PROBE: interface ABI mismatch\n"
        ));

        return STATUS_REVISION_MISMATCH;
    }

    if (
        ctx->Adsp.CtlrDevId !=
        PHASER_GEMINI_LAKE_AUDIO_DEVICE_ID
    ) {
        KdPrintEx((
            DPFLTR_IHVDRIVER_ID,
            DPFLTR_ERROR_LEVEL,
            "PHASER360_PROBE: unexpected controller 0x%04hX "
            "(expected 0x3198)\n",
            ctx->Adsp.CtlrDevId
        ));

        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    //
    // M0.1 intentionally stops here.
    //
    // No callbacks returned by the ADSP interface are invoked:
    //
    // - no GetResources
    // - no SetDSPPowerState
    // - no RegisterInterrupt
    // - no DMA allocation
    // - no stream preparation
    // - no stream trigger
    // - no SSP/PDM programming
    // - no SOF firmware loading
    // - no speaker amplifier enable
    //
    PhaserLog(
        "PASS: Gemini Lake ADSP interface acquired; "
        "zero hardware callbacks invoked"
    );

    return STATUS_SUCCESS;
}

NTSTATUS
PhaserEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
)
{
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    auto ctx = PhaserGetDeviceContext(Device);

    if (
        ctx->InterfaceAcquired &&
        ctx->Adsp.InterfaceDereference != nullptr
    ) {
        ctx->Adsp.InterfaceDereference(
            ctx->Adsp.Context
        );
    }

    RtlZeroMemory(
        &ctx->Adsp,
        sizeof(ctx->Adsp)
    );

    ctx->InterfaceAcquired = FALSE;

    PhaserLog(
        "interface released"
    );

    return STATUS_SUCCESS;
}
