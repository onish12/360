// SPDX-License-Identifier: MIT
#include "h15or_readonly_recovery_probe.h"
#pragma warning(disable:4996)

using namespace phaser360::windows;

namespace {
constexpr ULONGLONG kHdaPhys=0x00000000CEEE0000ull;
constexpr ULONGLONG kDspPhys=0x00000000CEF00000ull;
constexpr ULONG kHdaLen=0x4000u,kDspLen=0x100000u;
constexpr ULONG kGcap=0x0000u,kVmin=0x0002u,kVmaj=0x0003u,kGctl=0x0008u;
constexpr ULONG kCorbctl=0x004cu,kRirbctl=0x005cu,kStreamBase=0x0080u,kStreamStride=0x20u,kRunBit=0x2u;
constexpr ULONG kPpctl=0x0804u,kPpsts=0x0808u,kEm2=0x1030u;
constexpr ULONG kAdspcs=0x0004u,kAdspic=0x0008u,kAdspis=0x000cu,kHipci=0x0048u,kHipcie=0x004cu,kHipcctl=0x0050u,kRom=0x80000u;
constexpr ULONG kExpectedPg=0x00000010u,kExpectedCg=0x807b0dffu,kExpectedEm2=0x04007000u;

UNICODE_STRING gSym{};

void Complete(PIRP irp,NTSTATUS status,ULONG_PTR info=0) noexcept {
    irp->IoStatus.Status=status;irp->IoStatus.Information=info;IoCompleteRequest(irp,IO_NO_INCREMENT);
}
NTSTATUS CreateClose(PDEVICE_OBJECT,PIRP irp) noexcept {Complete(irp,STATUS_SUCCESS);return STATUS_SUCCESS;}

bool ReadObservation(UCHAR*hda,UCHAR*dsp,H15orObservation*o) noexcept {
    if(!hda||!dsp||!o)return false;RtlZeroMemory(o,sizeof(*o));
    o->hdaGcap=READ_REGISTER_USHORT(reinterpret_cast<volatile USHORT*>(hda+kGcap));
    o->hdaVmin=READ_REGISTER_UCHAR(hda+kVmin);o->hdaVmaj=READ_REGISTER_UCHAR(hda+kVmaj);
    o->hdaGctl=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(hda+kGctl));
    o->hdaCorbctl=READ_REGISTER_UCHAR(hda+kCorbctl);o->hdaRirbctl=READ_REGISTER_UCHAR(hda+kRirbctl);
    const ULONG total=((o->hdaGcap>>8)&0xfu)+((o->hdaGcap>>12)&0xfu)+((o->hdaGcap>>3)&0x1fu);
    if(total>31u)return false;o->totalStreams=static_cast<UCHAR>(total);
    for(ULONG i=0;i<total;++i){if(READ_REGISTER_UCHAR(hda+kStreamBase+i*kStreamStride)&kRunBit)o->streamRunMask|=1u<<i;}
    o->hdaIntelEm2=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(hda+kEm2));
    o->hdaPpctl=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(hda+kPpctl));
    o->hdaPpsts=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(hda+kPpsts));
    o->dspAdspcs=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(dsp+kAdspcs));
    o->dspAdspic=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(dsp+kAdspic));
    o->dspAdspis=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(dsp+kAdspis));
    o->dspHipci=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(dsp+kHipci));
    o->dspHipcie=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(dsp+kHipcie));
    o->dspHipcctl=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(dsp+kHipcctl));
    o->dspRomStatus=READ_REGISTER_ULONG(reinterpret_cast<volatile ULONG*>(dsp+kRom));
    return o->hdaGcap!=0xffffu&&o->hdaGctl!=0xffffffffu;
}

NTSTATUS Ioctl(PDEVICE_OBJECT,PIRP irp) noexcept {
    auto*sp=IoGetCurrentIrpStackLocation(irp);
    if(!sp||sp->Parameters.DeviceIoControl.IoControlCode!=IOCTL_PHASER360_H15OR_SNAPSHOT){
        Complete(irp,STATUS_INVALID_DEVICE_REQUEST);return STATUS_INVALID_DEVICE_REQUEST;
    }
    if(sp->Parameters.DeviceIoControl.InputBufferLength!=sizeof(H15orRequestV1)||
       sp->Parameters.DeviceIoControl.OutputBufferLength<sizeof(H15orResultV1)||
       !irp->AssociatedIrp.SystemBuffer){
        Complete(irp,STATUS_BUFFER_TOO_SMALL);return STATUS_BUFFER_TOO_SMALL;
    }
    const auto req=*reinterpret_cast<H15orRequestV1*>(irp->AssociatedIrp.SystemBuffer);
    H15orResultV1 r{};r.version=1u;r.size=sizeof(r);r.busNumber=req.busNumber;r.slotNumber=req.slotNumber;
    r.hdaPhysical=kHdaPhys;r.dspPhysical=kDspPhys;r.hdaLength=kHdaLen;r.dspLength=kDspLen;
    if(req.version!=1u||req.size!=sizeof(H15orRequestV1)||req.busNumber>255u||req.slotNumber>255u){
        r.status=STATUS_INVALID_PARAMETER;RtlCopyMemory(irp->AssociatedIrp.SystemBuffer,&r,sizeof(r));Complete(irp,STATUS_SUCCESS,sizeof(r));return STATUS_SUCCESS;
    }

    UCHAR cfg[256]{};r.pciBytesRead=HalGetBusDataByOffset(PCIConfiguration,req.busNumber,req.slotNumber,cfg,0,sizeof(cfg));
    if(r.pciBytesRead>=0x4cu){
        r.flags|=H15orPciRead;
        r.vendorId=*reinterpret_cast<UNALIGNED USHORT*>(cfg+0);
        r.deviceId=*reinterpret_cast<UNALIGNED USHORT*>(cfg+2);
        RtlCopyMemory(&r.pgctl,cfg+0x44,sizeof(ULONG));RtlCopyMemory(&r.cgctl,cfg+0x48,sizeof(ULONG));
        if(r.vendorId==0x8086u&&r.deviceId==0x3198u)r.flags|=H15orPciIdentityExact;
        if(r.pgctl==kExpectedPg&&r.cgctl==kExpectedCg)r.flags|=H15orPciPolicyBaseline;
    }

    PHYSICAL_ADDRESS hp{};hp.QuadPart=kHdaPhys;PHYSICAL_ADDRESS dp{};dp.QuadPart=kDspPhys;
    auto*h=static_cast<UCHAR*>(MmMapIoSpaceEx(hp,kHdaLen,PAGE_READONLY|PAGE_NOCACHE));
    auto*d=static_cast<UCHAR*>(MmMapIoSpaceEx(dp,kDspLen,PAGE_READONLY|PAGE_NOCACHE));
    if(h&&d){
        r.flags|=H15orMmioMapped;
        if(ReadObservation(h,d,&r.obs)){
            r.flags|=H15orObservationCaptured;
            if(r.obs.hdaGctl==0&&r.obs.hdaIntelEm2==kExpectedEm2)r.flags|=H15orHdaWritableBaseline;
            if(r.obs.hdaPpctl==0)r.flags|=H15orPpctlZero;
            if((r.obs.hdaCorbctl&kRunBit)==0&&(r.obs.hdaRirbctl&kRunBit)==0&&r.obs.totalStreams==13u&&r.obs.streamRunMask==0)r.flags|=H15orHdaTransportIdle;
            if(r.obs.dspAdspcs==0x001d003cu&&r.obs.dspHipci==0&&r.obs.dspHipcie==0x00420000u&&r.obs.dspHipcctl==0&&r.obs.dspRomStatus==0x01006701u)r.flags|=H15orDspStableReadable;
        }
    }
    if(d)MmUnmapIoSpace(d,kDspLen);if(h)MmUnmapIoSpace(h,kHdaLen);

    constexpr ULONG safe=H15orPciRead|H15orPciIdentityExact|H15orMmioMapped|H15orObservationCaptured|
        H15orPciPolicyBaseline|H15orHdaWritableBaseline|H15orDspStableReadable|H15orPpctlZero|H15orHdaTransportIdle;
    if((r.flags&safe)==safe){r.flags|=H15orExactSafeForHandoff;r.status=STATUS_SUCCESS;}
    else r.status=STATUS_DEVICE_CONFIGURATION_ERROR;

    RtlCopyMemory(irp->AssociatedIrp.SystemBuffer,&r,sizeof(r));Complete(irp,STATUS_SUCCESS,sizeof(r));return STATUS_SUCCESS;
}

void Unload(PDRIVER_OBJECT d) noexcept {
    if(gSym.Buffer)IoDeleteSymbolicLink(&gSym);
    if(d&&d->DeviceObject)IoDeleteDevice(d->DeviceObject);
}
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT d,PUNICODE_STRING) {
    if(!d||KeGetCurrentIrql()!=PASSIVE_LEVEL)return STATUS_INVALID_DEVICE_STATE;
    UNICODE_STRING dev;RtlInitUnicodeString(&dev,L"\\Device\\Phaser360H15or");
    RtlInitUnicodeString(&gSym,L"\\DosDevices\\Phaser360H15or");
    PDEVICE_OBJECT obj=nullptr;auto st=IoCreateDevice(d,0,&dev,kH15orDeviceType,FILE_DEVICE_SECURE_OPEN,FALSE,&obj);
    if(!NT_SUCCESS(st))return st;
    obj->Flags|=DO_BUFFERED_IO;
    d->MajorFunction[IRP_MJ_CREATE]=CreateClose;d->MajorFunction[IRP_MJ_CLOSE]=CreateClose;d->MajorFunction[IRP_MJ_DEVICE_CONTROL]=Ioctl;d->DriverUnload=Unload;
    st=IoCreateSymbolicLink(&gSym,&dev);if(!NT_SUCCESS(st)){IoDeleteDevice(obj);return st;}
    obj->Flags&=~DO_DEVICE_INITIALIZING;return STATUS_SUCCESS;
}
