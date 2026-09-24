// SPDX-License-Identifier: MIT
#include "ipc_interrupt.h"
#include "stage_trace.h"
namespace phaser360 { namespace windows {
struct IpcIrqContext { IpcInterrupt* owner; };
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(IpcIrqContext,GetIpcIrqContext)
bool IpcInterrupt::Read(ULONG off,ULONG& value) noexcept {
    if(!boot_ || !boot_->AccessAllowed() || !dsp_ || off>0x50 || (off&3)) return false;
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
    const bool global=Bits(8,1,0);
    StageTraceStatus(global?L"I20_MASK_GLOBAL_OK":L"I20_MASK_GLOBAL_FAIL",
                     global?STATUS_SUCCESS:STATUS_DEVICE_CONFIGURATION_ERROR);
    const bool local=Bits(0x50,3,0);
    StageTraceStatus(local?L"I30_MASK_LOCAL_OK":L"I30_MASK_LOCAL_FAIL",
                     local?STATUS_SUCCESS:STATUS_DEVICE_CONFIGURATION_ERROR);
    return global && local;
}
bool IpcInterrupt::Unmask() noexcept {
    if(!Bits(0x50,3,1) || !Bits(8,1,1)) {
        fault_=true; (void)Mask(); return false;
    }
    armed_=true;
    return true; // notifications only; DONE remains polled
}
NTSTATUS IpcInterrupt::CreateDormant(WDFDEVICE device) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || created_) return STATUS_INVALID_DEVICE_STATE;
    if(!device) return STATUS_INVALID_PARAMETER;
    created_=true;
    deviceLifetimeShell_=true;
    bootStartAllowed_=false;
    hardwareEnableAllowed_=false;
    return CreateObjects(device,nullptr,nullptr);
}
bool IpcInterrupt::BindDormant(const PnpDormantInterruptBinding& binding,
                               GlkBoot* boot) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_ || !created_ ||
       !deviceLifetimeShell_ || bootStartAllowed_ || hardwareEnableAllowed_ ||
       !boot || !binding.gate || !binding.gate->Allowed() ||
       !binding.dsp || (reinterpret_cast<ULONG_PTR>(binding.dsp)&3) ||
       binding.dspLength!=0x100000 || !binding.raw || !binding.translated ||
       binding.raw->Type!=CmResourceTypeInterrupt ||
       binding.translated->Type!=CmResourceTypeInterrupt)
        return false;

    const bool rawMessage=(binding.raw->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)!=0;
    const bool translatedMessage=(binding.translated->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)!=0;
    if(rawMessage!=translatedMessage) return false;
    if(binding.kind==PnpInterruptKind::MessageSignaled) {
        if(!rawMessage || binding.messageCount!=1) return false;
    } else if(rawMessage || binding.messageCount!=0) {
        return false;
    }

    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    bool result=!boot_ && !dsp_ && !enableSeen_ && !enabled_ && !everArmed_ &&
                !stopped_ && !fault_ && !admissionClosed_ && boot->Fresh();
    if(result) {
        result=boot->BindAccessGate(binding.gate) &&
               boot->AccessGate()==binding.gate && boot->AccessAllowed() &&
               binding.gate->Allowed();
    }
    if(result) { boot_=boot; dsp_=binding.dsp; }
    WdfWaitLockRelease(serial_);
    return result;
}

bool IpcInterrupt::UnbindDormant() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_ || !created_ ||
       !deviceLifetimeShell_ || hardwareEnableAllowed_)
        return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    const bool disconnected=!enabled_ && (!enableSeen_ || disableSeen_);
    const bool result=boot_ && dsp_ && disconnected && !everArmed_ &&
        !ready_ && InterlockedCompareExchange(&pendingWork_,0,0)==0;
    if(result) {
        boot_=nullptr; dsp_=nullptr;
        bootStartAllowed_=false;
        admissionClosed_=false; closed_=false; disconnectedSeen_=false;
        enableFailed_=false; disableMasked_=false; drained_=false;
        enableSeen_=false; disableSeen_=false;
        armed_=false; enabled_=false; ready_=false; fault_=false; stopped_=false;
    }
    WdfWaitLockRelease(serial_);
    return result;
}

bool IpcInterrupt::GrantBootStart() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_ || !deviceLifetimeShell_)
        return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    const bool result=boot_ && dsp_ && boot_->Fresh() && boot_->AccessAllowed() &&
        !bootStartAllowed_ && !hardwareEnableAllowed_ && !enableSeen_ &&
        !enabled_ && !everArmed_ && !stopped_ && !fault_ && !admissionClosed_;
    if(result) bootStartAllowed_=true;
    WdfWaitLockRelease(serial_);
    return result;
}

bool IpcInterrupt::GrantFrameworkEnableAfterBoot() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_ || !deviceLifetimeShell_)
        return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    const bool result=bootStartAllowed_ && !hardwareEnableAllowed_ &&
        boot_ && dsp_ && boot_->AccessAllowed() && boot_->CommandUsable() &&
        !enableSeen_ && !enabled_ && !everArmed_ && !stopped_ &&
        !fault_ && !admissionClosed_;
    if(result) hardwareEnableAllowed_=true;
    WdfWaitLockRelease(serial_);
    return result;
}

bool IpcInterrupt::ResetDormantClosedSession() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_ || !deviceLifetimeShell_)
        return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    const bool frameworkDone=!enabled_ && (!enableSeen_ || disableSeen_);
    const bool result=boot_ && bootStartAllowed_ && frameworkDone &&
        closed_ && drained_ && stopped_ && !ready_ && !armed_ && !dsp_ &&
        !boot_->CommandUsable() &&
        InterlockedCompareExchange(&pendingWork_,0,0)==0;
    if(result) {
        boot_=nullptr; dsp_=nullptr;
        bootStartAllowed_=false; hardwareEnableAllowed_=false;
        admissionClosed_=false; closed_=false; disconnectedSeen_=false;
        enableFailed_=false; disableMasked_=false; drained_=false;
        enableSeen_=false; disableSeen_=false;
        armed_=false; enabled_=false; ready_=false; fault_=false; stopped_=false;
        everArmed_=false;
    }
    WdfWaitLockRelease(serial_);
    return result;
}

bool IpcInterrupt::ResetDormantRemovedSession() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_ || !deviceLifetimeShell_)
        return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    const bool removed=boot_ && boot_->AccessGate() && boot_->AccessGate()->Removed();
    const bool frameworkDone=!enabled_ && (!enableSeen_ || disableSeen_);
    const bool neverEnabled=!enableSeen_;
    const bool softwareDrained=neverEnabled || drained_;
    const bool result=removed && frameworkDone && admissionClosed_ && softwareDrained &&
        !ready_ && !armed_ && InterlockedCompareExchange(&pendingWork_,0,0)==0;
    if(result) {
        boot_=nullptr; dsp_=nullptr;
        bootStartAllowed_=false; hardwareEnableAllowed_=false;
        admissionClosed_=false; closed_=false; disconnectedSeen_=false;
        enableFailed_=false; disableMasked_=false; drained_=false;
        enableSeen_=false; disableSeen_=false;
        armed_=false; enabled_=false; ready_=false; fault_=false; stopped_=false;
        everArmed_=false;
    }
    WdfWaitLockRelease(serial_);
    return result;
}

NTSTATUS IpcInterrupt::Create(WDFDEVICE device,PCM_PARTIAL_RESOURCE_DESCRIPTOR raw,
                              PCM_PARTIAL_RESOURCE_DESCRIPTOR translated,GlkBoot* boot,
                              UCHAR* dsp,ULONG length) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || created_) return STATUS_INVALID_DEVICE_STATE;
    if(!device || !raw || !translated || raw->Type!=CmResourceTypeInterrupt ||
       translated->Type!=CmResourceTypeInterrupt || !boot || !boot->AccessAllowed() || !dsp ||
       (reinterpret_cast<ULONG_PTR>(dsp)&3) || length<0x54 || length>0x100000) return STATUS_INVALID_PARAMETER;
    created_=true; deviceLifetimeShell_=false; boot_=boot; dsp_=dsp;
    bootStartAllowed_=true; hardwareEnableAllowed_=true;
    return CreateObjects(device,raw,translated);
}
NTSTATUS IpcInterrupt::CreateObjects(WDFDEVICE device,PCM_PARTIAL_RESOURCE_DESCRIPTOR raw,
                                     PCM_PARTIAL_RESOURCE_DESCRIPTOR translated) noexcept {
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
    if(req.operation==Operation::Arm) { self.ready_=true; self.everArmed_=true; }
    if(!self.ready_) return FALSE;
    if(req.operation==Operation::Begin) {
        if(!self.Mask()) { self.fault_=true; return FALSE; }
        return TRUE;
    }
    if(req.operation==Operation::Check) return TRUE;
    return self.Unmask()?TRUE:FALSE;
}
bool IpcInterrupt::Sync(Operation operation) noexcept {
    // Lifecycle callers serialize against Enable/Disable. Workers are excluded
    // by Stop's admission gate before framework Disable, even on mask failure.
    if(!hardwareEnableAllowed_ || !boot_ || !boot_->AccessAllowed() ||
       !enableSeen_ || enableFailed_ || disableSeen_ || disconnectedSeen_)
        return false;
    SyncRequest request={this,operation};
    return WdfInterruptSynchronize(interrupt_,Synchronized,&request)!=FALSE;
}
bool IpcInterrupt::CanStartBeforeEnable() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    const bool result=bootStartAllowed_ && boot_ && boot_->AccessAllowed() &&
        !enableSeen_ && !admissionClosed_;
    WdfWaitLockRelease(serial_); return result;
}
bool IpcInterrupt::CancelBeforeEnable() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    // Caller serializes against framework Enable; no ISR has ever been enabled.
    const bool result=!enableSeen_;
    if(result) { stopped_=true; ready_=false; armed_=false; dsp_=nullptr; admissionClosed_=true; closed_=true; drained_=true; }
    WdfWaitLockRelease(serial_); return result;
}
bool IpcInterrupt::RebindStopped(GlkBoot* boot,UCHAR* dsp,ULONG length) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_ || deviceLifetimeShell_ ||
       !boot || !boot->AccessAllowed() || !dsp ||
       (reinterpret_cast<ULONG_PTR>(dsp)&3) || length<0x54 || length>0x100000) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    // PnP caller serializes against Enable/Disable. No IRQ lock/synchronization
    // while disconnected. The wait lock excludes an executing old work item.
    const bool result=closed_ && drained_ && disableSeen_ &&
        InterlockedCompareExchange(&pendingWork_,0,0)==0;
    if(result) {
        boot_=boot; dsp_=dsp; bootStartAllowed_=true; hardwareEnableAllowed_=true;
        admissionClosed_=false; closed_=false; drained_=false; stopped_=false; ready_=false;
        armed_=false; enabled_=false; fault_=false; enableSeen_=false; disableSeen_=false;
        disableMasked_=false; everArmed_=false; enableFailed_=false; disconnectedSeen_=false;
    }
    WdfWaitLockRelease(serial_); return result;
}
bool IpcInterrupt::Running() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    const bool result=!admissionClosed_ && boot_ && boot_->CommandUsable() && Sync(Operation::Check);
    WdfWaitLockRelease(serial_); return result;
}
bool IpcInterrupt::Arm() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    const bool result=!admissionClosed_ && boot_ && boot_->CommandUsable() && Sync(Operation::Arm);
    WdfWaitLockRelease(serial_); return result;
}
bool IpcInterrupt::Stop() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    bool result=closed_;
    if(!result && enableSeen_ && !disableSeen_) {
        // Acquire serial before closing admission: any current worker/command
        // finishes first. Future workers must return without IRQ synchronization.
        admissionClosed_=true;
        result=Sync(Operation::Stop);
    }
    if(result) { admissionClosed_=true; closed_=true; }
    WdfWaitLockRelease(serial_); return result;
}
bool IpcInterrupt::FenceForSurpriseRemoval() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    // The shared hardware gate must already be terminal. Closing software
    // admission is safe without claiming that the device was masked/disconnected.
    const bool removed=boot_ && boot_->AccessGate() && boot_->AccessGate()->Removed();
    // Surprise removal is not synchronized with framework Enable/Disable.
    // Do not mutate IRQ-lock-owned state here. The terminal access gate prevents
    // later MMIO; this wait-lock-owned admission bit only blocks PASSIVE clients.
    if(removed) admissionClosed_=true;
    WdfWaitLockRelease(serial_);
    return removed;
}
bool IpcInterrupt::StopAfterDisconnect() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    // This entry's caller contract is completed framework disconnect in D0Exit,
    // not merely a failed Enable return. No surprise removal or late BAR release.
    // Failed/missing Enable can omit Disable: no ISR work was armed, but masking
    // still requires readback. Do that directly at PASSIVE with no IRQ consumer.
    const bool noEnable=!enableSeen_ || enableFailed_;
    const bool eligible=(disableSeen_ || noEnable) && (admissionClosed_ || !everArmed_);
    if(eligible) {
        admissionClosed_=true; disconnectedSeen_=true;
        stopped_=true; enabled_=false; ready_=false;
        const bool masked=closed_ || (disableSeen_?disableMasked_:Mask());
        if(masked) { closed_=true; dsp_=nullptr; }
    }
    const bool result=eligible && closed_;
    WdfWaitLockRelease(serial_); return result;
}
bool IpcInterrupt::DrainStopped() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !interrupt_) return false;
    if(WdfWaitLockAcquire(serial_,nullptr)!=STATUS_SUCCESS) return false;
    const bool closed=closed_ || (admissionClosed_ && (disableSeen_ || disconnectedSeen_)),done=drained_;
    WdfWaitLockRelease(serial_);
    if(!closed) return false;
    if(done) return true;
    // Either Stop confirmed the mask or completed disconnect/Disable excludes
    // new ISR delivery with admission already closed. Queue drain does NOT prove a
    // hardware mask, DSP shutdown or DMA quiescence.
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
    StageTrace(L"I10_INTERRUPT_ENABLE_ENTER");
    auto& self=*GetIpcIrqContext(interrupt)->owner;
    self.disableSeen_=false; self.disableMasked_=false; self.enableFailed_=false; self.enableSeen_=true; self.enabled_=false; self.ready_=false;
    if(self.stopped_) {
        StageTrace(L"I40_INTERRUPT_ENABLE_STOPPED");
        return STATUS_SUCCESS;
    }
    // DeviceAdd shell remains framework-connectable but hardware-inert until a
    // later reviewed D0Entry binding explicitly permits register masking.
    if(!self.hardwareEnableAllowed_) {
        self.enabled_=true;
        StageTrace(L"I40_INTERRUPT_ENABLE_INERT_OK");
        return STATUS_SUCCESS;
    }
    if(!self.boot_ || !self.boot_->AccessAllowed() || !self.dsp_ ||
       !self.Mask()) {
        self.enableFailed_=true; self.fault_=true;
        StageTraceStatus(L"I40_INTERRUPT_ENABLE_FAIL",STATUS_DEVICE_CONFIGURATION_ERROR);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    self.enabled_=true;
    StageTrace(L"I40_INTERRUPT_ENABLE_OK");
    return STATUS_SUCCESS;
}
NTSTATUS IpcInterrupt::Disable(WDFINTERRUPT interrupt,WDFDEVICE) {
    auto& self=*GetIpcIrqContext(interrupt)->owner;
    self.disableSeen_=true;
    self.enabled_=false; self.ready_=false;
    if(!self.hardwareEnableAllowed_) { self.disableMasked_=true; return STATUS_SUCCESS; }
    self.disableMasked_=!self.dsp_ || self.Mask();
    if(!self.disableMasked_) { self.fault_=true; return STATUS_DEVICE_CONFIGURATION_ERROR; }
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
    if(!self.admissionClosed_ && self.Sync(Operation::Begin)) {
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
    if(!admissionClosed_ && boot_ && boot_->CommandUsable() && Sync(Operation::Begin)) {
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
    const bool result=!admissionClosed_ && boot_ && boot_->AccessAllowed() &&
        boot_->PopNotification(event);
    WdfWaitLockRelease(serial_); return result;
}
}}
