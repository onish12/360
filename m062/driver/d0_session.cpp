// SPDX-License-Identifier: MIT
#include "d0_session.h"
#include <new>

namespace phaser360 { namespace windows {

NTSTATUS D0SessionOwner::Begin(
    WDFDEVICE device,IpcInterrupt& irq,HardwareAccessGate& gate) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || memory_ || session_ || !device ||
       !gate.Allowed())
        return STATUS_INVALID_DEVICE_STATE;

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject=device;

    void* storage=nullptr;
    const auto status=WdfMemoryCreate(
        &attributes,NonPagedPoolNx,0x35534850u,sizeof(Session),&memory_,&storage);
    if(!NT_SUCCESS(status)) return status;
    if(!storage || (reinterpret_cast<ULONG_PTR>(storage)&(alignof(Session)-1))!=0) {
        WdfObjectDelete(memory_); memory_=nullptr;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    session_=::new(storage) Session(irq,gate);
    if(session_->boot.AccessGate()!=&gate || !session_->boot.Fresh() ||
       !session_->boot.AccessAllowed()) {
        session_->~Session(); session_=nullptr;
        WdfObjectDelete(memory_); memory_=nullptr;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    ++generation_;
    return STATUS_SUCCESS;
}

GlkBoot* D0SessionOwner::Boot() noexcept {
    return session_?&session_->boot:nullptr;
}
ColdPower* D0SessionOwner::Power() noexcept {
    return session_?&session_->power:nullptr;
}

bool D0SessionOwner::Destroy() noexcept {
    if(!session_ || !memory_) return false;
    session_->~Session(); session_=nullptr;
    WdfObjectDelete(memory_); memory_=nullptr;
    return true;
}

bool D0SessionOwner::ReleaseUnused() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !session_ ||
       !session_->boot.Fresh() || session_->boot.CommandUsable())
        return false;
    return Destroy();
}

bool D0SessionOwner::ReleaseClean() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !session_ ||
       !session_->power.CanReleaseMappings() || session_->boot.CommandUsable())
        return false;
    return Destroy();
}

bool D0SessionOwner::AbandonRemoved(HardwareAccessGate& gate) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !session_ || !gate.Removed())
        return false;
    return Destroy();
}

} }
