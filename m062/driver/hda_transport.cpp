// SPDX-License-Identifier: MIT
#include "hda_transport.h"
namespace phaser360 { namespace windows {
bool HdaTransport::Valid(ULONG o,unsigned w) const noexcept {
    return KeGetCurrentIrql()==PASSIVE_LEVEL && gate_ && gate_->Allowed() && base_ &&
        (w==1 || w==2 || w==4) && o%w==0 && o<=length_ && w<=length_-o;
}
bool HdaTransport::Read(void* p,ULONG o,unsigned w,ULONG* value) noexcept {
    auto& self=*static_cast<HdaTransport*>(p);
    if(!value || !self.Valid(o,w)) return false;
    if(w==1) *value=READ_REGISTER_UCHAR(self.base_+o);
    else if(w==2) *value=READ_REGISTER_USHORT(reinterpret_cast<USHORT*>(self.base_+o));
    else *value=READ_REGISTER_ULONG(reinterpret_cast<ULONG*>(self.base_+o));
    return true;
}
bool HdaTransport::Write(void* p,ULONG o,unsigned w,ULONG value) noexcept {
    auto& self=*static_cast<HdaTransport*>(p);
    if(!self.Valid(o,w)) return false;
    if(w==1) WRITE_REGISTER_UCHAR(self.base_+o,static_cast<UCHAR>(value));
    else if(w==2) WRITE_REGISTER_USHORT(reinterpret_cast<USHORT*>(self.base_+o),static_cast<USHORT>(value));
    else WRITE_REGISTER_ULONG(reinterpret_cast<ULONG*>(self.base_+o),value);
    return true;
}
void HdaTransport::Delay(void*,unsigned us) noexcept {
    if(us<=50) { KeStallExecutionProcessor(us); return; }
    LARGE_INTEGER interval; interval.QuadPart=-static_cast<LONGLONG>(us)*10;
    (void)KeDelayExecutionThread(KernelMode,FALSE,&interval);
}
bool HdaTransport::Verify(void* p) noexcept {
    return static_cast<HdaTransport*>(p)->stream_.IsDetached();
}
NTSTATUS HdaTransport::Prepare(WDFDEVICE device,UCHAR* base,ULONG length,
                              const UCHAR* payload,SIZE_T bytes) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || attempted_ || !gate_ ||
       !gate_->Allowed() || !dma_ || !dma_->HardwarePrepared())
        return STATUS_INVALID_DEVICE_STATE;
    if(!base || (reinterpret_cast<ULONG_PTR>(base)&3) || !device || !payload ||
       !bytes || bytes>sof::kMaxDmaBytes) return STATUS_INVALID_PARAMETER;
    attempted_=true; base_=base; length_=length;
    const auto pciStatus=pci_.Capture(device);
    if(!NT_SUCCESS(pciStatus)) return pciStatus;
    const sof::RegisterIo io={this,Read,Write,Delay,length_};
    controllerAttempted_=true;
    if(!controller_.Initialize(io)) return STATUS_DEVICE_CONFIGURATION_ERROR;
    if(!stream_.Select(io)) return STATUS_DEVICE_CONFIGURATION_ERROR;

    NTSTATUS status=dma_->Stage(payload,bytes);
    if(!NT_SUCCESS(status)) return status;
    allocated_=true;

    BootDmaView view={};
    status=dma_->Publish(&view);
    if(!NT_SUCCESS(status)) return status;
    published_=true;
    if(!stream_.Configure(view.bdlLogical,view.payloadBytes,view.lastValidIndex))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    // H15D: after HDA is cold/owned but before any DSP MMIO, apply only the
    // two SOF pre-fw PCI policy bits attested by H15C. Any failure is cleaned
    // by the mandatory Shutdown -> QuiesceController path.
    status=pciPolicy_.Apply(device,pci_.Snapshot(),*gate_);
    if(!NT_SUCCESS(status)) return status;
    return STATUS_SUCCESS;
}
bool HdaTransport::Start() noexcept {
    return KeGetCurrentIrql()==PASSIVE_LEVEL && controller_.Ready() &&
        pciPolicy_.Applied() && allocated_ && published_ && stream_.Start();
}
bool HdaTransport::StopAndRelease() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return false;
    if(!allocated_) return true;
    if(published_ && !stream_.StopDetach()) return false;
    if(!dma_ || !NT_SUCCESS(dma_->ReleaseSession(published_ ? Verify : nullptr,this)))
        return false;
    allocated_=false; published_=false;
    return true;
}
} }

bool phaser360::windows::HdaTransport::QuiesceController() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return false;

    bool controllerOk=true;
    if(controllerAttempted_) {
        controllerOk=controller_.Quiesce();
        if(controllerOk) controllerAttempted_=false;
    }

    // Restore H15D-owned PCI bits even when HDA quiesce reported failure.
    const bool pciOk=pciPolicy_.Restore();
    return controllerOk && pciOk;
}
