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
#define WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(t,n) inline t* n(WDFOBJECT h) { return static_cast<t*>(FakeWdfContext(static_cast<WDFINTERRUPT>(h))); }
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

using WDFDPC=FakeObject*;
using WDFWORKITEM=FakeObject*;
struct WDF_DPC_CONFIG { void(*EvtDpcFunc)(WDFDPC); BOOLEAN AutomaticSerialization; };
struct WDF_WORKITEM_CONFIG { void(*EvtWorkItem)(WDFWORKITEM); BOOLEAN AutomaticSerialization; };
inline void WDF_DPC_CONFIG_INIT(WDF_DPC_CONFIG* c,void(*f)(WDFDPC)) { *c={f,TRUE}; }
inline void WDF_WORKITEM_CONFIG_INIT(WDF_WORKITEM_CONFIG* c,void(*f)(WDFWORKITEM)) { *c={f,TRUE}; }
NTSTATUS WdfDpcCreate(WDF_DPC_CONFIG*,WDF_OBJECT_ATTRIBUTES*,WDFDPC*);
BOOLEAN WdfDpcEnqueue(WDFDPC);
BOOLEAN WdfDpcCancel(WDFDPC,BOOLEAN);
NTSTATUS WdfWorkItemCreate(WDF_WORKITEM_CONFIG*,WDF_OBJECT_ATTRIBUTES*,WDFWORKITEM*);
void WdfWorkItemEnqueue(WDFWORKITEM);
void WdfWorkItemFlush(WDFWORKITEM);

using WDFMEMORY=FakeObject*;
constexpr unsigned NonPagedPoolNx=512;
NTSTATUS WdfMemoryCreate(WDF_OBJECT_ATTRIBUTES*,unsigned,ULONG,SIZE_T,WDFMEMORY*,void**);

// PnP registration shim; callback execution is a host model, not KMDF.
struct FakeDeviceInit;
using PWDFDEVICE_INIT=FakeDeviceInit*;
struct FakeResourceList;
using WDFCMRESLIST=FakeResourceList*;
struct WDF_PNPPOWER_EVENT_CALLBACKS {
    NTSTATUS(*EvtDevicePrepareHardware)(WDFDEVICE,WDFCMRESLIST,WDFCMRESLIST);
    NTSTATUS(*EvtDeviceReleaseHardware)(WDFDEVICE,WDFCMRESLIST);
};
inline void WDF_PNPPOWER_EVENT_CALLBACKS_INIT(WDF_PNPPOWER_EVENT_CALLBACKS* c) { *c={}; }
void WdfDeviceInitSetPnpPowerEventCallbacks(PWDFDEVICE_INIT,WDF_PNPPOWER_EVENT_CALLBACKS*);
ULONG WdfCmResourceListGetCount(WDFCMRESLIST);
PCM_PARTIAL_RESOURCE_DESCRIPTOR WdfCmResourceListGetDescriptor(WDFCMRESLIST,ULONG);
