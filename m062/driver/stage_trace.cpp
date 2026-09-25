// SPDX-License-Identifier: MIT
#include "stage_trace.h"

#if defined(PHASER_KERNEL_BUILD)

namespace phaser360 { namespace windows {

const GUID kStageTraceProviderGuid={
    0xebaed0db,0xf9db,0x42ea,{0xa1,0x62,0x5f,0x41,0x11,0x38,0x40,0x51}
};

namespace {
REGHANDLE gStageTraceHandle=0;

void Append(WCHAR* buffer,SIZE_T capacity,SIZE_T* used,PCWSTR text) noexcept {
    if(!buffer || !used || !text || capacity==0) return;
    while(*text && *used+1<capacity) buffer[(*used)++]=*text++;
}

void AppendHex32(WCHAR* buffer,SIZE_T capacity,SIZE_T* used,ULONG value) noexcept {
    static const WCHAR hex[]=L"0123456789ABCDEF";
    if(!buffer || !used || capacity==0) return;
    for(int shift=28;shift>=0 && *used+1<capacity;shift-=4)
        buffer[(*used)++]=hex[(value>>shift)&0xFu];
}
}

NTSTATUS StageTraceRegister() noexcept {
    if(gStageTraceHandle!=0) return STATUS_INVALID_DEVICE_STATE;
    REGHANDLE handle=0;
    const auto status=EtwRegister(
        &kStageTraceProviderGuid,nullptr,nullptr,&handle);
    if(NT_SUCCESS(status)) gStageTraceHandle=handle;
    return status;
}

void StageTraceUnregister() noexcept {
    if(gStageTraceHandle==0) return;
    const auto handle=gStageTraceHandle;
    gStageTraceHandle=0;
    (void)EtwUnregister(handle);
}

void StageTraceStatus(const wchar_t* stage,NTSTATUS status) noexcept {
    const auto handle=gStageTraceHandle;
    if(handle==0 || !stage) return;

    // Stack memory is nonpaged, so this remains valid even when called by the
    // DIRQL interrupt Enable path. EtwWriteString is documented for any IRQL
    // when its data is resident in system space.
    WCHAR message[160]={};
    SIZE_T used=0;
    Append(message,RTL_NUMBER_OF(message),&used,L"PHASER360_R7 ");
    Append(message,RTL_NUMBER_OF(message),&used,stage);
    Append(message,RTL_NUMBER_OF(message),&used,L" status=0x");
    AppendHex32(message,RTL_NUMBER_OF(message),&used,
                static_cast<ULONG>(status));
    message[(used<RTL_NUMBER_OF(message))?used:RTL_NUMBER_OF(message)-1]=L'\0';

    (void)EtwWriteString(
        handle,4u,1ull,nullptr,message);
}

void StageTraceStatusValue(const wchar_t* stage,NTSTATUS status,ULONG value) noexcept {
    const auto handle=gStageTraceHandle;
    if(handle==0 || !stage) return;

    WCHAR message[192]={};
    SIZE_T used=0;
    Append(message,RTL_NUMBER_OF(message),&used,L"PHASER360_R7 ");
    Append(message,RTL_NUMBER_OF(message),&used,stage);
    Append(message,RTL_NUMBER_OF(message),&used,L" status=0x");
    AppendHex32(message,RTL_NUMBER_OF(message),&used,
                static_cast<ULONG>(status));
    Append(message,RTL_NUMBER_OF(message),&used,L" value=0x");
    AppendHex32(message,RTL_NUMBER_OF(message),&used,value);
    message[(used<RTL_NUMBER_OF(message))?used:RTL_NUMBER_OF(message)-1]=L'\0';

    (void)EtwWriteString(
        handle,4u,1ull,nullptr,message);
}

} }

#endif // PHASER_KERNEL_BUILD
