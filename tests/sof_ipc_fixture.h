// SPDX-License-Identifier: MIT
#pragma once
#include <vector>
#include <cstdint>
inline void IpcPut(std::vector<uint8_t>& b,size_t off,uint32_t v) {
    for(unsigned j=0;j<4;++j) b[off+j]=static_cast<uint8_t>(v>>(j*8));
}
inline std::vector<uint8_t> IpcXman() {
    std::vector<uint8_t> b(432,0);
    IpcPut(b,0,0x6e614d58); IpcPut(b,4,432); IpcPut(b,8,16); IpcPut(b,12,0x1000000);
    IpcPut(b,16,1); IpcPut(b,20,416); IpcPut(b,24,400); IpcPut(b,28,0x70000000);
    IpcPut(b,32,1); IpcPut(b,36,7);
    const uint32_t rows[7][4]={{5,0,4096,0},{1,0,4096,4096},{0,1,8192,0},
        {3,2,2048,0},{6,2,2048,2048},{4,2,4096,4096},{2,3,8192,0}};
    for(size_t i=0;i<7;++i) {
        const size_t o=40+24*i;
        IpcPut(b,o+4,rows[i][0]); IpcPut(b,o+8,rows[i][1]);
        IpcPut(b,o+16,rows[i][2]); IpcPut(b,o+20,rows[i][3]);
    }
    return b;
}
inline void IpcReadyBytes(std::vector<uint8_t>& b,size_t off) {
    IpcPut(b,off,108); IpcPut(b,off+4,0x70000000); IpcPut(b,off+24,60);
    IpcPut(b,off+28,0x90001); IpcPut(b,off+64,0x3014000);
}
