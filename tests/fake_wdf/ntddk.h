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
#define _IRQL_requires_(x)
#define NT_SUCCESS(x) ((x) >= 0)
#define NT_ASSERT(x) assert(x)
inline void RtlCopyMemory(void* d,const void* s,size_t n) { std::memcpy(d,s,n); }
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
constexpr UCHAR CmResourceTypeInterrupt=2;
struct CM_PARTIAL_RESOURCE_DESCRIPTOR { UCHAR Type; };
using PCM_PARTIAL_RESOURCE_DESCRIPTOR=CM_PARTIAL_RESOURCE_DESCRIPTOR*;
