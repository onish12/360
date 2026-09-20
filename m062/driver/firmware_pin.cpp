// SPDX-License-Identifier: MIT
#include "firmware_pin.h"
#if defined(PHASER_KERNEL_BUILD)
#include <ntddk.h>
#pragma comment(lib,"cng.lib")
#else
#include <Windows.h>
#endif
#include <bcrypt.h>
namespace phaser360 { namespace windows {
bool MatchesFirmwarePin(const unsigned char* input,unsigned bytes) noexcept {
#if defined(PHASER_KERNEL_BUILD)
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return false;
#endif
    if(!input || bytes!=kPinnedImageBytes) return false;
    const unsigned char expected[32]={
        0x40,0x02,0x9b,0x5a,0x05,0x66,0x5f,0x19,0xa4,0x92,0xef,0x00,0xb8,0xc0,0xa2,0x4c,
        0x42,0xe9,0x0d,0x7c,0x00,0xfc,0x57,0x14,0x6e,0x07,0x94,0x7f,0xd1,0x40,0x7d,0x5c};
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    unsigned char digest[32]={};
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,MS_PRIMITIVE_PROVIDER,0)==0;
    if(ok) ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)==0;
    if(ok) ok=BCryptHashData(hash,const_cast<unsigned char*>(input),bytes,0)==0;
    if(ok) ok=BCryptFinishHash(hash,digest,sizeof(digest),0)==0;
    if(hash && BCryptDestroyHash(hash)!=0) ok=false;
    if(algorithm && BCryptCloseAlgorithmProvider(algorithm,0)!=0) ok=false;
    unsigned difference=0;
    for(unsigned i=0;i<32;++i) difference|=static_cast<unsigned>(digest[i]^expected[i]);
    return ok && difference==0;
}
}}
