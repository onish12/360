# M0.6.15 H15F — function-driver ownership handoff

Purpose: validate the PnP ownership transition from the verified Intel baseline
to a PHASER360 function driver and back, before any M1 hardware mutation.

H15F deliberately contains no hardware-access implementation. Its only runtime
work is to record framework lifecycle transitions and inspect the translated
CM_RESOURCE_LIST metadata supplied by KMDF.

Success while H15F owns DEV_3198 requires:
- exact function-driver bind to PCI\\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06;
- PrepareHardware observed;
- exactly two translated memory resources;
- memory lengths 0x4000 and 0x100000 in the already verified HDA/DSP order;
- exactly one translated interrupt resource;
- D0Entry observed;
- read-only software snapshot available through a non-power-managed IOCTL.

The source and import gates forbid:
- MmMapIoSpace / MmMapIoSpaceEx / MmUnmapIoSpace;
- READ_REGISTER_* / WRITE_REGISTER_*;
- BUS_INTERFACE_STANDARD / GetBusData / SetBusData;
- WdfInterruptCreate;
- WDF DMA/common-buffer allocation;
- firmware loading or DSP boot;
- audio endpoint/playback code.

Deployment is intentionally forced because Microsoft documents that PnPUtil
will not force a lower-ranked driver onto a device. The runner first publishes
the signed H15F package, then calls UpdateDriverForPlugAndPlayDevicesW with
INSTALLFLAG_FORCE against the exact full hardware ID. Rollback deletes only the
published H15F OEM INF and requires the device to return to the exact preflight
Intel identity.

Live baseline pinned from the verified H15E transaction:
- Windows build 19044;
- service IntcAudioBus;
- Intel INF oem14.inf;
- driver version 9.22.0.4832;
- provider Intel(R) Corporation;
- HDA resource base 0xCEEE0000, length 0x4000;
- DSP resource base 0xCEF00000, length 0x100000.

H15F does not authorize DSP boot. It isolates function-driver ownership and
rollback as a separate physical gate.
