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

// Interrupt ABI shim: compiled only into host tests.
using WDFINTERRUPT=FakeObject*;
using WDFWAITLOCK=FakeObject*;
using WDFOBJECT=void*;
using WDFCONTEXT=void*;
using PFN_WDF_INTERRUPT_ISR=BOOLEAN(*)(WDFINTERRUPT,ULONG);
using PFN_WDF_INTERRUPT_SYNCHRONIZE=BOOLEAN(*)(WDFINTERRUPT,WDFCONTEXT);
struct WDF_OBJECT_ATTRIBUTES { WDFOBJECT ParentObject; size_t contextSize; };
inline void WDF_OBJECT_ATTRIBUTES_INIT(WDF_OBJECT_ATTRIBUTES* a) { *a={}; }
#define WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(a,t) do { *(a)={}; (a)->contextSize=sizeof(t); } while(0)
void* FakeWdfContext(WDFINTERRUPT);
#define WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(t,n) inline t* n(WDFINTERRUPT h) { return static_cast<t*>(FakeWdfContext(h)); }
struct WDF_INTERRUPT_CONFIG {
    PFN_WDF_INTERRUPT_ISR EvtInterruptIsr;
    void* EvtInterruptDpc;
    NTSTATUS(*EvtInterruptEnable)(WDFINTERRUPT,WDFDEVICE);
    NTSTATUS(*EvtInterruptDisable)(WDFINTERRUPT,WDFDEVICE);
    void(*EvtInterruptWorkItem)(WDFINTERRUPT,WDFOBJECT);
    PCM_PARTIAL_RESOURCE_DESCRIPTOR InterruptRaw,InterruptTranslated;
    BOOLEAN PassiveHandling,AutomaticSerialization;
};
inline void WDF_INTERRUPT_CONFIG_INIT(WDF_INTERRUPT_CONFIG* c,PFN_WDF_INTERRUPT_ISR i,void* d) { *c={}; c->EvtInterruptIsr=i; c->EvtInterruptDpc=d; }
NTSTATUS WdfWaitLockCreate(WDF_OBJECT_ATTRIBUTES*,WDFWAITLOCK*);
NTSTATUS WdfWaitLockAcquire(WDFWAITLOCK,LONGLONG*);
void WdfWaitLockRelease(WDFWAITLOCK);
NTSTATUS WdfInterruptCreate(WDFDEVICE,WDF_INTERRUPT_CONFIG*,WDF_OBJECT_ATTRIBUTES*,WDFINTERRUPT*);
BOOLEAN WdfInterruptSynchronize(WDFINTERRUPT,PFN_WDF_INTERRUPT_SYNCHRONIZE,WDFCONTEXT);
BOOLEAN WdfInterruptQueueWorkItemForIsr(WDFINTERRUPT);
