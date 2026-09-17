// SPDX-License-Identifier: MIT
#pragma once
#include "ntddk.h"
struct FakeObject;
using WDFDEVICE = void*;
using WDFDMAENABLER = FakeObject*;
using WDFCOMMONBUFFER = FakeObject*;
constexpr void* WDF_NO_OBJECT_ATTRIBUTES = nullptr;
constexpr unsigned WdfDmaProfileScatterGather = 1;
struct WDF_DMA_ENABLER_CONFIG { unsigned profile; size_t maximum; };
struct WDF_COMMON_BUFFER_CONFIG { unsigned alignment; };
inline void WDF_DMA_ENABLER_CONFIG_INIT(WDF_DMA_ENABLER_CONFIG* c,unsigned p,size_t n) { *c={p,n}; }
inline void WDF_COMMON_BUFFER_CONFIG_INIT(WDF_COMMON_BUFFER_CONFIG* c,unsigned a) { *c={a}; }
NTSTATUS WdfDmaEnablerCreate(WDFDEVICE,WDF_DMA_ENABLER_CONFIG*,void*,WDFDMAENABLER*);
NTSTATUS WdfCommonBufferCreateWithConfig(WDFDMAENABLER,size_t,WDF_COMMON_BUFFER_CONFIG*,void*,WDFCOMMONBUFFER*);
PHYSICAL_ADDRESS WdfCommonBufferGetAlignedLogicalAddress(WDFCOMMONBUFFER);
void* WdfCommonBufferGetAlignedVirtualAddress(WDFCOMMONBUFFER);
void WdfObjectDelete(FakeObject*);
