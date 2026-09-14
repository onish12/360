#pragma once
#include <stdint.h>

namespace phaser360 {
// PCI BAR0 and BAR4, in translated resource order. Addresses are assigned by PnP.
constexpr bool valid_resources(uint64_t hda, uint32_t hda_length,
                               uint64_t dsp, uint32_t dsp_length) noexcept {
    return hda_length == 0x4000u && dsp_length == 0x100000u &&
        hda != 0 && dsp != 0 &&
        (hda & 0x3fffu) == 0 && (dsp & 0xfffffu) == 0 &&
        hda <= UINT64_C(0x7fffffffffffffff) - hda_length &&
        dsp <= UINT64_C(0x7fffffffffffffff) - dsp_length &&
        (hda + hda_length <= dsp || dsp + dsp_length <= hda);
}
}
