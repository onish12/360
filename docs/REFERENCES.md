# Public references

Primary references used for the bootstrap design:

- CoolStar `sklhdaudbus` — https://github.com/coolstar/sklhdaudbus
- CoolStar `da7219` — https://github.com/coolstar/da7219
- CoolStar `max98357a` — https://github.com/coolstar/max98357a
- Sound Open Firmware — https://github.com/thesofproject/sof
- Linux kernel audio sources — https://github.com/torvalds/linux
- Microsoft Windows driver samples — https://github.com/microsoft/Windows-driver-samples
- Microsoft WDK NuGet documentation — https://learn.microsoft.com/windows-hardware/drivers/install-the-wdk-using-nuget
- Microsoft WDF driver-defined interface documentation — https://learn.microsoft.com/windows-hardware/drivers/wdf/using-driver-defined-interfaces

The compatibility declaration in `adsp_bus_contract.h` follows the public BSD-3-Clause `sklhdaudbus` interface layout. No proprietary CoolStar SOF backend code is included.
