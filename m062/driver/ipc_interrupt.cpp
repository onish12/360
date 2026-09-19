// SPDX-License-Identifier: MIT
#include "ipc_interrupt.h"
namespace phaser360 { namespace windows {
struct IpcIrqContext { IpcInterrupt* owner; };
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(IpcIrqContext,GetIpcIrqContext)
bool IpcInterrupt::Read(ULONG off,ULONG& value) noexcept {
    if(!dsp_ || off>0x50 || (off&3)) return false;
    value=READ_REGISTER_ULONG(reinterpret_cast<ULONG*>(dsp_+off));
    return value!=MAXULONG;
}
bool IpcInterrupt::Bits(ULONG off,ULONG mask,ULONG value) noexcept {
    ULONG old=0,check=0;
    if(!Read(off,old)) return false;
    WRITE_REGISTER_ULONG(reinterpret_cast<ULONG*>(dsp_+off),(old&~mask)|(value&mask));
    return Read(off,check) && (check&mask)==value;
}
bool IpcInterrupt::Mask() noexcept {
    armed_=false;
    // Attempt both masks, even if one register is inaccessible.
    const bool global=Bits(8,1,0),local=Bits(0x50,3,0);
    return global && local;
}
bool IpcInterrupt::Unmask() noexcept {
    if(!Bits(0x50,3,1) || !Bits(8,1,1)) {
        fault_=true; (void)Mask(); return false;
    }
    armed_=true;
    return true; // notifications only; DONE remains polled
}
NTSTATUS IpcInterrupt::Create(WDFDEVICE device,PCM_PARTIAL_RESOURCE_DESCRIPTOR raw,
                              PCM_PARTIAL_RESOURCE_DESCRIPTOR translated,GlkBoot* boot,
                              UCHAR* dsp,ULONG length) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || created_) return STATUS_INVALID_DEVICE_STATE;
    if(!device || !raw || !translated || raw->Type!=CmResourceTypeInterrupt ||
       translated->Type!=CmResourceTypeInterrupt || !boot || !dsp ||
       (reinterpret_cast<ULONG_PTR>(dsp)&3) || length<0x54 || length>0x100000) return STATUS_INVALID_PARAMETER;
    created_=true; boot_=boot; dsp_=dsp;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes); attributes.ParentObject=device;
    NTSTATUS status=WdfWaitLockCreate(&attributes,&serial_);
    if(!NT_SUCCESS(status)) return status;
    WDF_WORKITEM_CONFIG workConfig;
    WDF_WORKITEM_CONFIG_INIT(&workConfig,Work); workConfig.AutomaticSerialization=FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,IpcIrqContext); attributes.ParentObject=device;
    status=WdfWorkItemCreate(&workConfig,&attributes,&work_);
    if(!NT_SUCCESS(status)) { WdfObjectDelete(serial_); serial_=nullptr; return status; }
    GetIpcIrqContext(work_)->owner=this;
    WDF_DPC_CONFIG dpcConfig;
    WDF_DPC_CONFIG_INIT(&dpcConfig,Deferred); dpcConfig.AutomaticSerialization=FALSE;
    status=WdfDpcCreate(&dpcConfig,&attributes,&dpc_);
    if(!NT_SUCCESS(status)) {
        WdfObjectDelete(work_); work_=nullptr; WdfObjectDelete(serial_); serial_=nullptr; return status;
    }
    GetIpcIrqContext(dpc_)->owner=this;
    WDF_INTERRUPT_CONFIG config;
    WDF_INTERRUPT_CONFIG_INIT(&config,Isr,nullptr);
    config.PassiveHandling=FALSE; config.AutomaticSerialization=FALSE;
    config.InterruptRaw=raw; config.InterruptTranslated=translated;
    config.EvtInterruptEnable=Enable; config.EvtInterruptDisable=Disable;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,IpcIrqContext);
    status=WdfInterruptCreate(device,&config,&attributes,&interrupt_);
    if(!NT_SUCCESS(status)) {
        WdfObjectDelete(dpc_); dpc_=nullptr; WdfObjectDelete(work_); work_=nullptr;
        WdfObjectDelete(serial_); serial_=nullptr; return status;
    }
    GetIpcIrqContext(interrupt_)->owner=this;
    return STATUS_SUCCESS;
}
BOOLEAN IpcInterrupt::Synchronized(WDFINTERRUPT,WDFCONTEXT context) {
    auto& req=*static_cast<SyncRequest*>(context); auto& self=*req.self;
    if(req.operation==Operation::Stop) {
        self.ready_=false; self.stopped_=true;
        if(!self.dsp_) return TRUE;
        if(!self.Mask()) { self.fault_=true; return FALSE; }
        self.dsp_=nullptr; return TRUE;
    }
    if(req.operation==Operation::Fault) {
        self.ready_=false; self.fault_=true;
        return self.dsp_ && self.Mask()?TRUE:FALSE;
    }
    if(!self.enabled_ || self.stopped_ || self.fault_) return FALSE;
    if(req.operation==Operation::Arm) self.ready_=true;
    if(!self.ready_) return FALSE;
    if(req.operation==Operation::Begin) {
        if(!self.Mask()) { self.fault_=true; return FALSE; }
        return TRUE;
    }
    if(req.operation==Operation::Check) return TRUE;
    return self.Unmask()?TRUE:FALSE;
}
bool IpcInterrupt::Sync(Operation operation) noexcept {
    SyncRequest request={this,operation};
    return WdfInterruptSynchronize(interrupt_,Synchronized,&request)!=FALSE;
}
bool IpcInterrupt::CanStartBeforeEnable() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    const bool result=!enableSeen_ && !closed_;
    WdfWaitLockRelease(serial_); return result;
}
bool IpcInterrupt::CancelBeforeEnable() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    // Caller serializes against framework Enable; no ISR has ever been enabled.
    const bool result=!enableSeen_;
    if(result) { stopped_=true; ready_=false; armed_=false; dsp_=nullptr; closed_=true; drained_=true; }
    WdfWaitLockRelease(serial_); return result;
}
bool IpcInterrupt::RebindStopped(GlkBoot* boot,UCHAR* dsp,ULONG length) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_ || !boot || !dsp ||
       (reinterpret_cast<ULONG_PTR>(dsp)&3) || length<0x54 || length>0x100000) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    // PnP caller serializes against Enable/Disable. No IRQ lock/synchronization
    // while disconnected. The wait lock excludes an executing old work item.
    const bool result=closed_ && drained_ && disableSeen_ &&
        InterlockedCompareExchange(&pendingWork_,0,0)==0;
    if(result) {
        boot_=boot; dsp_=dsp; closed_=false; drained_=false; stopped_=false; ready_=false;
        armed_=false; enabled_=false; fault_=false; enableSeen_=false; disableSeen_=false;
    }
    WdfWaitLockRelease(serial_); return result;
}
bool IpcInterrupt::Running() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    const bool result=!closed_ && boot_->CommandUsable() && Sync(Operation::Check);
    WdfWaitLockRelease(serial_); return result;
}
bool IpcInterrupt::Arm() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    const bool result=!closed_ && boot_->CommandUsable() && Sync(Operation::Arm);
    WdfWaitLockRelease(serial_); return result;
}
bool IpcInterrupt::Stop() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    const bool result=closed_ || Sync(Operation::Stop);
    if(result) closed_=true;
    WdfWaitLockRelease(serial_); return result;
}
bool IpcInterrupt::DrainStopped() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    const bool closed=closed_,done=drained_;
    WdfWaitLockRelease(serial_);
    if(!closed) return false;
    if(done) return true;
    // Stop already synchronized with ISR and prevents all new DPC enqueue.
    // Never hold the wait lock: a running/queued worker needs it to return.
    (void)WdfDpcCancel(dpc_,TRUE);
    WdfWorkItemFlush(work_);
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    InterlockedExchange(&pendingWork_,0); drained_=true;
    WdfWaitLockRelease(serial_); return true;
}
BOOLEAN IpcInterrupt::Isr(WDFINTERRUPT interrupt,ULONG) {
    auto& self=*GetIpcIrqContext(interrupt)->owner;
    if(!self.enabled_ || !self.armed_ || !self.ready_ || self.stopped_ || self.fault_) return FALSE;
    ULONG status=0,busy=0;
    if(!self.Read(0xc,status) || !self.Read(0x40,busy)) {
        self.fault_=true; (void)self.Mask(); return FALSE;
    }
    if(!(status&1) || !(busy&0x80000000u)) return FALSE;
    if(!self.Mask()) { self.fault_=true; return TRUE; }
    // No mailbox read, wait, allocation or ACK at DIRQL. Framework coalesces work.
    InterlockedExchange(&self.pendingWork_,1);
    (void)WdfDpcEnqueue(self.dpc_);
    return TRUE;
}
NTSTATUS IpcInterrupt::Enable(WDFINTERRUPT interrupt,WDFDEVICE) {
    auto& self=*GetIpcIrqContext(interrupt)->owner;
    self.disableSeen_=false; self.enableSeen_=true; self.enabled_=false; self.ready_=false;
    if(self.stopped_) return STATUS_SUCCESS;
    if(!self.Mask()) { self.fault_=true; return STATUS_DEVICE_CONFIGURATION_ERROR; }
    self.enabled_=true; return STATUS_SUCCESS;
}
NTSTATUS IpcInterrupt::Disable(WDFINTERRUPT interrupt,WDFDEVICE) {
    auto& self=*GetIpcIrqContext(interrupt)->owner;
    self.disableSeen_=true;
    self.enabled_=false; self.ready_=false;
    if(!self.dsp_) return STATUS_SUCCESS;
    if(!self.Mask()) { self.fault_=true; return STATUS_DEVICE_CONFIGURATION_ERROR; }
    return STATUS_SUCCESS;
}
void IpcInterrupt::Deferred(WDFDPC dpc) {
    auto& self=*GetIpcIrqContext(dpc)->owner;
    // DISPATCH_LEVEL: only queue work, no mailbox or blocking operations.
    WdfWorkItemEnqueue(self.work_);
}
void IpcInterrupt::Work(WDFWORKITEM work) {
    auto& self=*GetIpcIrqContext(work)->owner;
    if(WdfWaitLockAcquire(self.serial_,nullptr)!=STATUS_SUCCESS) return;
    InterlockedExchange(&self.pendingWork_,0);
    if(!self.closed_ && self.Sync(Operation::Begin)) {
        const auto status=self.boot_->PollNotifications();
        if(status==sof::CommandStatus::Ok) (void)self.Sync(Operation::Rearm);
        else (void)self.Sync(Operation::Fault);
    }
    WdfWaitLockRelease(self.serial_);
}
sof::CommandResult IpcInterrupt::Command(const UCHAR* request,SIZE_T bytes,ULONG expected,
                                         UCHAR* reply,SIZE_T capacity) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return {};
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return {};
    sof::CommandResult result;
    if(!closed_ && boot_->CommandUsable() && Sync(Operation::Begin)) {
        result=boot_->Command(request,bytes,expected,reply,capacity);
        if(boot_->CommandUsable()) (void)Sync(Operation::Rearm);
        else (void)Sync(Operation::Fault);
    }
    WdfWaitLockRelease(serial_); return result;
}
bool IpcInterrupt::Pop(sof::IpcNotification* event) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    // After Stop no boot access: caller may already be tearing the boot owner down.
    const bool result=!closed_ && boot_->PopNotification(event);
    WdfWaitLockRelease(serial_); return result;
}
}}
