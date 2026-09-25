// SPDX-License-Identifier: MIT
// User-mode test shim ONLY. Never include this directory in a WDK target.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
using UCHAR = unsigned char;
using ULONG = uint32_t;
using USHORT = uint16_t;
using ULONGLONG = uint64_t;
using SIZE_T = size_t;
using NTSTATUS = int32_t;
struct PHYSICAL_ADDRESS { int64_t QuadPart; };
constexpr NTSTATUS STATUS_SUCCESS = 0;
constexpr NTSTATUS STATUS_INVALID_DEVICE_STATE = -1;
constexpr NTSTATUS STATUS_INVALID_PARAMETER = -2;
constexpr NTSTATUS STATUS_DEVICE_CONFIGURATION_ERROR = -3;
constexpr NTSTATUS STATUS_DEVICE_BUSY = -4;
constexpr NTSTATUS STATUS_INSUFFICIENT_RESOURCES = -5;
constexpr unsigned PASSIVE_LEVEL = 0;
constexpr ULONG MAXULONG = UINT32_MAX;
constexpr ULONG FILE_4096_BYTE_ALIGNMENT=0x0fffu;
#define _IRQL_requires_(x)
#define NT_SUCCESS(x) ((x) >= 0)
#define NT_ASSERT(x) assert(x)
inline void RtlCopyMemory(void* d,const void* s,size_t n) { std::memcpy(d,s,n); }
inline size_t RtlCompareMemory(const void* a,const void* b,size_t n) {
    const auto* x=static_cast<const unsigned char*>(a);
    const auto* y=static_cast<const unsigned char*>(b);
    size_t equal=0;
    while(equal<n && x[equal]==y[equal]) ++equal;
    return equal;
}
inline void RtlZeroMemory(void* d,size_t n) { std::memset(d,0,n); }
unsigned KeGetCurrentIrql();
void KeMemoryBarrier();
using ULONG_PTR=uintptr_t;
using LONGLONG=int64_t;
struct LARGE_INTEGER { LONGLONG QuadPart; };
constexpr unsigned KernelMode=0;
constexpr bool FALSE=false;
UCHAR READ_REGISTER_UCHAR(UCHAR*);
USHORT READ_REGISTER_USHORT(USHORT*);
ULONG READ_REGISTER_ULONG(ULONG*);
void WRITE_REGISTER_UCHAR(UCHAR*,UCHAR);
void WRITE_REGISTER_USHORT(USHORT*,USHORT);
void WRITE_REGISTER_ULONG(ULONG*,ULONG);
void KeStallExecutionProcessor(unsigned);
NTSTATUS KeDelayExecutionThread(unsigned,bool,LARGE_INTEGER*);
ULONGLONG KeQueryInterruptTime();

using BOOLEAN=unsigned char;
constexpr BOOLEAN TRUE=1;
struct GUID {
    uint32_t Data1; uint16_t Data2; uint16_t Data3; UCHAR Data4[8];
};
using LPCGUID=const GUID*;
using PINTERFACE_REFERENCE=void(*)(void*);
using PINTERFACE_DEREFERENCE=void(*)(void*);
struct INTERFACE {
    USHORT Size=0; USHORT Version=0; void* Context=nullptr;
    PINTERFACE_REFERENCE InterfaceReference=nullptr;
    PINTERFACE_DEREFERENCE InterfaceDereference=nullptr;
};
using PINTERFACE=INTERFACE*;
using PGET_SET_DEVICE_DATA=ULONG(*)(void*,ULONG,void*,ULONG,ULONG);
struct BUS_INTERFACE_STANDARD {
    USHORT Size=0; USHORT Version=0; void* Context=nullptr;
    PINTERFACE_REFERENCE InterfaceReference=nullptr;
    PINTERFACE_DEREFERENCE InterfaceDereference=nullptr;
    void* TranslateBusAddress=nullptr;
    void* GetDmaAdapter=nullptr;
    PGET_SET_DEVICE_DATA SetBusData=nullptr;
    PGET_SET_DEVICE_DATA GetBusData=nullptr;
};
constexpr ULONG PCI_WHICHSPACE_CONFIG=0;
constexpr UCHAR CmResourceTypeInterrupt=2;
struct CM_PARTIAL_RESOURCE_DESCRIPTOR {
    UCHAR Type;
    UCHAR ShareDisposition=0;
    USHORT Flags=0;
    union {
        struct { PHYSICAL_ADDRESS Start; ULONG Length; } Memory;
        struct { ULONG Level; ULONG Vector; ULONG_PTR Affinity; } Interrupt;
        struct {
            union {
                struct {
                    USHORT Group;
                    USHORT Reserved;
                    USHORT MessageCount;
                    ULONG Vector;
                    ULONG_PTR Affinity;
                } Raw;
                struct {
                    ULONG Level;
                    ULONG Vector;
                    ULONG_PTR Affinity;
                } Translated;
            };
        } MessageInterrupt;
    } u={};
};
constexpr UCHAR CmResourceTypeMemory=3,CmResourceTypeMemoryLarge=7;
constexpr USHORT CM_RESOURCE_MEMORY_READ_ONLY=1,CM_RESOURCE_MEMORY_WRITE_ONLY=2;
constexpr USHORT CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE=0x0000;
constexpr USHORT CM_RESOURCE_INTERRUPT_LATCHED=0x0001;
constexpr USHORT CM_RESOURCE_INTERRUPT_MESSAGE=0x0002;
constexpr USHORT CM_RESOURCE_INTERRUPT_SECONDARY_INTERRUPT=0x0010;
constexpr USHORT CM_RESOURCE_INTERRUPT_WAKE_HINT=0x0020;
constexpr ULONG PAGE_READWRITE=4,PAGE_NOCACHE=0x200;
#define UNREFERENCED_PARAMETER(x) (void)(x)
void* MmMapIoSpaceEx(PHYSICAL_ADDRESS,SIZE_T,ULONG);
void MmUnmapIoSpace(void*,SIZE_T);
using PCM_PARTIAL_RESOURCE_DESCRIPTOR=CM_PARTIAL_RESOURCE_DESCRIPTOR*;

// Deterministic callback model only; not a multithreaded atomic implementation.
using LONG=int32_t;
inline LONG InterlockedExchange(volatile LONG* p,LONG value) {
    const LONG old=*p; *p=value; return old;
}
inline LONG InterlockedCompareExchange(volatile LONG* p,LONG value,LONG compare) {
    const LONG old=*p; if(old==compare) *p=value; return old;
}

constexpr NTSTATUS STATUS_INVALID_IMAGE_HASH=-6;
constexpr NTSTATUS STATUS_NOT_SUPPORTED=-7;
constexpr NTSTATUS STATUS_DELETE_PENDING=-8;
