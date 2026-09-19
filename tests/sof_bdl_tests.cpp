// SPDX-License-Identifier: MIT
#include "../src/sof/hda_bdl.h"
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace phaser360::sof;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
static uint32_t Get32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
int main() {
    std::array<uint8_t,kBdlBytes + 2> storage{};
    auto* b = storage.data() + 1; // Deliberately unaligned CPU output.
    uint16_t n = 99;
    for (size_t bytes : {size_t(1),size_t(4095),size_t(4096),size_t(4097),
                         size_t(286720),kMaxDmaBytes}) {
        storage.fill(0xaa);
        CHECK(BuildBootBdl(0x100000000ull,bytes,true,b,kBdlBytes,&n));
        CHECK(n == (bytes + 4095) / 4096);
        size_t sum = 0;
        for (size_t i = 0; i < n; ++i) {
            CHECK(Get32(b+i*16) == i*4096);
            CHECK(Get32(b+i*16+4) == 1);
            const auto len = Get32(b+i*16+8);
            CHECK(len > 0 && len <= 4096);
            CHECK(len == (bytes-sum < 4096 ? bytes-sum : 4096));
            CHECK(Get32(b+i*16+12) == 0);
            sum += len;
        }
        CHECK(sum == bytes);
        for (size_t i = size_t(n)*16; i < kBdlBytes; ++i) CHECK(b[i] == 0);
        CHECK(storage.front() == 0xaa && storage.back() == 0xaa);
    }
    struct Bad { uint64_t address; size_t bytes; bool wide; size_t cap; };
    for (const auto& t : {
        Bad{0,0,false,kBdlBytes}, Bad{0,kMaxDmaBytes+1,false,kBdlBytes},
        Bad{128,4096,false,kBdlBytes}, Bad{0,4096,false,kBdlBytes-1},
        Bad{0x100000000ull,1,false,kBdlBytes}, Bad{0xfffff000ull,4097,false,kBdlBytes},
        Bad{UINT64_MAX-4095,4097,true,kBdlBytes}
    }) {
        storage.fill(0xaa); n=99;
        CHECK(!BuildBootBdl(t.address,t.bytes,t.wide,b,t.cap,&n));
        CHECK(n == 0);
        for (auto value : storage) CHECK(value == 0xaa);
    }
    CHECK(BuildBootBdl(0xfffff000ull,4096,false,b,kBdlBytes,&n));
    CHECK(n == 1 && Get32(b) == 0xfffff000u && Get32(b+4) == 0);
    CHECK(BuildBootBdl(UINT64_MAX-4095,4096,true,b,kBdlBytes,&n));
    CHECK(!BuildBootBdl(0,1,false,nullptr,kBdlBytes,&n) && n == 0);
    CHECK(!BuildBootBdl(0,1,false,b,kBdlBytes,nullptr));
    std::printf("SOF_BDL_TESTS=%u PASS\n",checks);
}
