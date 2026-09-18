// SPDX-License-Identifier: MIT
#include "glk_boot.h"
namespace phaser360 { namespace windows {
bool GlkBoot::Valid(ULONG o) const noexcept {
    return KeGetCurrentIrql()==PASSIVE_LEVEL && dsp_ && !(o&3) && o<=length_ && length_-o>=4;
}
bool GlkBoot::Read(void* p,ULONG o,ULONG* v) noexcept {
    auto& self=*static_cast<GlkBoot*>(p);
    if(!v || !self.Valid(o)) return false;
    *v=READ_REGISTER_ULONG(reinterpret_cast<ULONG*>(self.dsp_+o)); return true;
}
bool GlkBoot::Write(void* p,ULONG o,ULONG v) noexcept {
    auto& self=*static_cast<GlkBoot*>(p);
    if(!self.Valid(o)) return false;
    WRITE_REGISTER_ULONG(reinterpret_cast<ULONG*>(self.dsp_+o),v); return true;
}
bool GlkBoot::Delay(void*,unsigned us) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || us>500) return false;
    LARGE_INTEGER interval; interval.QuadPart=-static_cast<LONGLONG>(us)*10;
    return KeDelayExecutionThread(KernelMode,FALSE,&interval)==STATUS_SUCCESS;
}
ULONGLONG GlkBoot::Now(void*) noexcept { return KeQueryInterruptTime()/10; }
NTSTATUS GlkBoot::Prepare(WDFDEVICE device,UCHAR* hda,ULONG hdaLength,UCHAR* dsp,ULONG dspLength,
                          const UCHAR* payload,SIZE_T bytes,
                          const UCHAR* xman,SIZE_T xmanBytes,USHORT maxAbiMinor) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || attempted_) return STATUS_INVALID_DEVICE_STATE;
    if(!dsp || (reinterpret_cast<ULONG_PTR>(dsp)&3)) return STATUS_INVALID_PARAMETER;
    attempted_=true; dsp_=dsp; length_=dspLength;
    const sof::RomIo io={this,Read,Write,Delay,Now,length_};
    if(!rom_.Bind(io)) { primaryError_=rom_.Error(); return STATUS_INVALID_PARAMETER; }
    if(!ipc_.Configure(io,xman,xmanBytes,maxAbiMinor)) return STATUS_INVALID_PARAMETER;
    const NTSTATUS status=hda_.Prepare(device,hda,hdaLength,payload,bytes);
    if(!NT_SUCCESS(status)) return status; // no DSP mutation on failed HDA prepare
    dspTouched_=true;
    // HDA prepare has verified cold streams and programmed RUN=0.
    if(!rom_.PowerDown() || !ipc_.Arm() || !rom_.Initialize(hda_.Tag())) {
        primaryError_=rom_.Error(); return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    prepared_=true; return STATUS_SUCCESS;
}
TransferResult GlkBoot::Transfer() noexcept {
    TransferResult result;
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !prepared_) return result;
    prepared_=false;
    result.started=hda_.Start();
    if(result.started) {
        result.firmwareEntered=rom_.WaitEntered();
        result.romError=rom_.Error(); primaryError_=result.romError;
    }
    // Even failed start/ROM wait needs stop. Preserve primary result separately.
    result.dmaReleased=hda_.StopAndRelease();
    if(result.firmwareEntered && result.dmaReleased) {
        result.ipcReady=ipc_.Receive(); ipcLive_=result.ipcReady; result.ipcError=ipc_.Error();
        if(result.ipcReady) {
            const sof::RomIo io={this,Read,Write,Delay,Now,length_};
            result.commandReady=commands_.Bind(io,ipc_);
        }
    }
    return result;
}
sof::CommandResult GlkBoot::Command(const UCHAR* request,SIZE_T bytes,ULONG expected,
                                    UCHAR* reply,SIZE_T capacity) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !ipcLive_) return {};
    return commands_.Exchange(request,bytes,expected,reply,capacity);
}
bool GlkBoot::Shutdown() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return false;
    prepared_=false; ipcLive_=false; commands_.Close();
    if(!hda_.StopAndRelease()) return false;
    // Do not overwrite the primary ROM failure with a cleanup result.
    return !dspTouched_ || rom_.PowerDown();
}
} }
