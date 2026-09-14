#include "../m051/driver/resource_contract.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>

int main() {
    unsigned checks = 0;
    const auto check = [&checks](bool ok) {
        ++checks;
        if (!ok) { std::cerr << "resource check failed: " << checks << '\n'; std::exit(1); }
    };
    using phaser360::valid_resources;
    check(valid_resources(0xceee0000, 0x4000, 0xcef00000, 0x100000));
    check(valid_resources(0xd1000000, 0x4000, 0xd2000000, 0x100000));
    check(valid_resources(0x100004000, 0x4000, 0x100100000, 0x100000));
    check(valid_resources(0xd2100000, 0x4000, 0xd2000000, 0x100000));
    check(!valid_resources(0, 0x4000, 0xcef00000, 0x100000));
    check(!valid_resources(0xceee0000, 0x4000, 0, 0x100000));
    check(!valid_resources(0xcef00000, 0x4000, 0xcef00000, 0x100000));
    check(!valid_resources(0xcef04000, 0x4000, 0xcef00000, 0x100000));
    check(!valid_resources(UINT64_C(0xffffffffffffc000), 0x4000, 0xcef00000, 0x100000));
    check(!valid_resources(0xceee0000, 0x4000, UINT64_C(0x7ffffffffff00000), 0x100000));
    check(!valid_resources(0xceee0000, 0x100000, 0xcef00000, 0x4000));
    for (uint32_t offset = 1; offset < 0x4000; ++offset)
        check(!valid_resources(UINT64_C(0xceee0000) + offset, 0x4000, 0xcef00000, 0x100000));
    for (uint32_t offset = 1; offset < 0x100000; offset += 0x1000)
        check(!valid_resources(0xceee0000, 0x4000, UINT64_C(0xcef00000) + offset, 0x100000));
    std::cout << "M051_RESOURCE_CONTRACT=PASS; checks=" << checks << '\n';
}
