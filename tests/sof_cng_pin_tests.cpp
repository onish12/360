// SPDX-License-Identifier: MIT
#include "../m062/driver/firmware_pin.h"
#include <vector>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstdlib>
using namespace phaser360::windows;
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
int main(int argc,char** argv) {
    CHECK(!MatchesFirmwarePin(nullptr,kPinnedImageBytes));
    std::vector<unsigned char> image(kPinnedImageBytes,0);
    CHECK(!MatchesFirmwarePin(image.data(),kPinnedImageBytes));
    CHECK(!MatchesFirmwarePin(image.data(),kPinnedImageBytes-1));
    if(argc==2) {
        std::ifstream input(argv[1],std::ios::binary); CHECK(input.good());
        image.assign(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
        CHECK(image.size()==kPinnedImageBytes && MatchesFirmwarePin(image.data(),kPinnedImageBytes));
        for(unsigned offset : {0u,767u,768u,kPinnedImageBytes-1}) {
            image[offset]^=1; CHECK(!MatchesFirmwarePin(image.data(),kPinnedImageBytes)); image[offset]^=1;
        }
        CHECK(MatchesFirmwarePin(image.data(),kPinnedImageBytes));
    }
    std::printf("SOF_CNG_PIN_TESTS=%u PASS; crypto=REAL_WINDOWS_CNG; official_fixture=%s; hardware=NONE\n",
                checks,argc==2?"CHECKED":"NOT_SUPPLIED");
}
