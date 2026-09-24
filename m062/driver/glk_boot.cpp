// SPDX-License-Identifier: MIT
#include "glk_boot.h"
#include "stage_trace.h"
namespace phaser360 { namespace windows {
bool GlkBoot::Valid(ULONG o) const noexcept {
    return KeGetCurrentIrql()==PASSIVE_LEVEL && AccessAllowed() && dsp_ &&
        !(o&3) && o<=length_ && length_-o>=4;
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
    StageTrace(L"G10_GLK_PREPARE_ENTER");
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || attempted_ || !AccessAllowed())
        return STATUS_INVALID_DEVICE_STATE;
    if(!dsp || (reinterpret_cast<ULONG_PTR>(dsp)&3)) return STATUS_INVALID_PARAMETER;
    attempted_=true; dsp_=dsp; length_=dspLength;
    const sof::RomIo io={this,Read,Write,Delay,Now,length_};
    if(!rom_.Bind(io)) {
        primaryError_=rom_.Error();
        StageTraceStatus(L"G20_ROM_BIND_FAIL",STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    StageTrace(L"G20_ROM_BIND_OK");
    if(!ipc_.Configure(io,xman,xmanBytes,maxAbiMinor)) {
        StageTraceStatus(L"G30_IPC_CONFIG_FAIL",STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    StageTrace(L"G30_IPC_CONFIG_OK");
    const NTSTATUS status=hda_.Prepare(device,hda,hdaLength,payload,bytes);
    if(!NT_SUCCESS(status)) {
        StageTraceStatus(L"G40_HDA_PREPARE_FAIL",status);
        return status; // no DSP mutation on failed HDA prepare
    }
    StageTrace(L"G40_HDA_PREPARE_OK");
    dspTouched_=true;
    // HDA prepare has verified cold streams and programmed RUN=0.
    if(!rom_.PowerDown()) {
        primaryError_=rom_.Error();
        StageTraceStatus(L"G50_ROM_POWERDOWN_FAIL",STATUS_DEVICE_CONFIGURATION_ERROR);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    StageTrace(L"G50_ROM_POWERDOWN_OK");
    if(!ipc_.Arm()) {
        primaryError_=rom_.Error();
        StageTraceStatus(L"G60_IPC_ARM_FAIL",STATUS_DEVICE_CONFIGURATION_ERROR);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    StageTrace(L"G60_IPC_ARM_OK");
    if(!rom_.Initialize(hda_.Tag())) {
        primaryError_=rom_.Error();
        StageTraceStatus(L"G70_ROM_INITIALIZE_FAIL",STATUS_DEVICE_CONFIGURATION_ERROR);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    StageTrace(L"G70_ROM_INITIALIZE_OK");
    prepared_=true;
    StageTrace(L"G80_GLK_PREPARE_OK");
    return STATUS_SUCCESS;
}
TransferResult GlkBoot::Transfer() noexcept {
    StageTrace(L"T10_TRANSFER_ENTER");
    TransferResult result;
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !prepared_) return result;
    prepared_=false;
    result.started=hda_.Start();
    StageTraceStatus(result.started?L"T20_HDA_START_OK":L"T20_HDA_START_FAIL",
                     result.started?STATUS_SUCCESS:STATUS_DEVICE_CONFIGURATION_ERROR);
    if(result.started) {
        result.firmwareEntered=rom_.WaitEntered();
        result.romError=rom_.Error(); primaryError_=result.romError;
        StageTraceStatus(result.firmwareEntered?L"T30_ROM_ENTERED_OK":L"T30_ROM_ENTERED_FAIL",
                         result.firmwareEntered?STATUS_SUCCESS:STATUS_DEVICE_CONFIGURATION_ERROR);
    }
    // Even failed start/ROM wait needs stop. Preserve primary result separately.
    result.dmaReleased=hda_.StopAndRelease();
    StageTraceStatus(result.dmaReleased?L"T40_DMA_RELEASE_OK":L"T40_DMA_RELEASE_FAIL",
                     result.dmaReleased?STATUS_SUCCESS:STATUS_DEVICE_CONFIGURATION_ERROR);
    if(result.firmwareEntered && result.dmaReleased) {
        result.ipcReady=ipc_.Receive(); ipcLive_=result.ipcReady; result.ipcError=ipc_.Error();
        StageTraceStatus(result.ipcReady?L"T50_IPC_READY_OK":L"T50_IPC_READY_FAIL",
                         result.ipcReady?STATUS_SUCCESS:STATUS_DEVICE_CONFIGURATION_ERROR);
        if(result.ipcReady) {
            const sof::RomIo io={this,Read,Write,Delay,Now,length_};
            result.commandReady=commands_.Bind(io,ipc_);
            StageTraceStatus(result.commandReady?L"T60_COMMAND_READY_OK":L"T60_COMMAND_READY_FAIL",
                             result.commandReady?STATUS_SUCCESS:STATUS_DEVICE_CONFIGURATION_ERROR);
        }
    }
    return result;
}
sof::CommandResult GlkBoot::Command(const UCHAR* request,SIZE_T bytes,ULONG expected,
                                    UCHAR* reply,SIZE_T capacity) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !ipcLive_) return {};
    return commands_.Exchange(request,bytes,expected,reply,capacity);
}
sof::CommandStatus GlkBoot::PollNotifications() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !ipcLive_) return sof::CommandStatus::State;
    return commands_.PollNotifications();
}
bool GlkBoot::PopNotification(sof::IpcNotification* event) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !ipcLive_) return false;
    return commands_.PopNotification(event);
}
bool GlkBoot::Shutdown() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return false;
    prepared_=false; ipcLive_=false; commands_.Close();
    if(!hda_.StopAndRelease()) return false;
    // Keep HDA global processing alive after firmware DMA has been detached:
    // IPC/FW runtime still owns the DSP. Quiesce HDA only after DSP power-down.
    if(dspTouched_ && !rom_.PowerDown()) return false;
    return hda_.QuiesceController();
}
} }
