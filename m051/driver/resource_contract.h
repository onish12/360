#pragma once

namespace phaser360 {
// No user-mode CRT headers in the kernel build. The same primitive types are
// used by the host test, with their widths checked on both toolchains.
using resource_address = unsigned long long;
using resource_length = unsigned int;
static_assert(sizeof(resource_address) == 8 && sizeof(resource_length) == 4,
              "Resource contract requires 64-bit addresses and 32-bit lengths");
// PCI BAR0 and BAR4, in translated resource order. Addresses are assigned by PnP.
constexpr bool valid_resources(resource_address hda, resource_length hda_length,
                               resource_address dsp, resource_length dsp_length) noexcept {
    return hda_length == 0x4000u && dsp_length == 0x100000u &&
        hda != 0 && dsp != 0 &&
        (hda & 0x3fffu) == 0 && (dsp & 0xfffffu) == 0 &&
        hda <= 0x7fffffffffffffffull - hda_length &&
        dsp <= 0x7fffffffffffffffull - dsp_length &&
        (hda + hda_length <= dsp || dsp + dsp_length <= hda);
}
}
