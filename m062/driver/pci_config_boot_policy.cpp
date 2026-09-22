// SPDX-License-Identifier: MIT
#include "pci_config_boot_policy.h"
#include <wdmguid.h>

namespace phaser360 { namespace windows {

namespace {
bool ReadConfigImage(BUS_INTERFACE_STANDARD& bus,UCHAR* config) noexcept {
    if(!config || !bus.GetBusData) return false;
    RtlZeroMemory(config,kPciConfigSnapshotBytes);
    return bus.GetBusData(
        bus.Context,PCI_WHICHSPACE_CONFIG,config,0,kPciConfigSnapshotBytes)
        ==kPciConfigSnapshotBytes;
}
}

bool PciConfigBootPolicy::ReadDword(
    BUS_INTERFACE_STANDARD& bus,ULONG offset,ULONG* value) noexcept {
    if(!value || !bus.GetBusData) return false;
    *value=0;
    return bus.GetBusData(
        bus.Context,PCI_WHICHSPACE_CONFIG,value,offset,sizeof(*value))==sizeof(*value);
}

bool PciConfigBootPolicy::WriteDword(
    BUS_INTERFACE_STANDARD& bus,ULONG offset,ULONG value) noexcept {
    if(!bus.SetBusData) return false;
    return bus.SetBusData(
        bus.Context,PCI_WHICHSPACE_CONFIG,&value,offset,sizeof(value))==sizeof(value);
}

bool PciConfigBootPolicy::RestoreWithBus(BUS_INTERFACE_STANDARD& bus) noexcept {
    if(!gate_ || !gate_->Allowed() || gate_->Removed()) return false;
    bool ok=true;

    // Match SOF post-fw policy ordering: clock gating first, then power gating.
    if(cgChanged_) {
        ULONG current=0;
        if(!gate_->Allowed() || gate_->Removed() ||
           !ReadDword(bus,kCgctlOffset,&current)) {
            ok=false;
        } else {
            const ULONG desired=(current&~kCgctlAdspDcge)|
                                (originalCgctl_&kCgctlAdspDcge);
            if(desired!=current &&
               (!gate_->Allowed() || gate_->Removed() ||
                !WriteDword(bus,kCgctlOffset,desired)))
                ok=false;
            ULONG verify=0;
            if(!gate_->Allowed() || gate_->Removed() ||
               !ReadDword(bus,kCgctlOffset,&verify) ||
               (verify&kCgctlAdspDcge)!=(originalCgctl_&kCgctlAdspDcge))
                ok=false;
            else
                cgChanged_=false;
        }
    }

    if(pgChanged_) {
        ULONG current=0;
        if(!gate_->Allowed() || gate_->Removed() ||
           !ReadDword(bus,kPgctlOffset,&current)) {
            ok=false;
        } else {
            const ULONG desired=(current&~kPgctlAdspPgd)|
                                (originalPgctl_&kPgctlAdspPgd);
            if(desired!=current &&
               (!gate_->Allowed() || gate_->Removed() ||
                !WriteDword(bus,kPgctlOffset,desired)))
                ok=false;
            ULONG verify=0;
            if(!gate_->Allowed() || gate_->Removed() ||
               !ReadDword(bus,kPgctlOffset,&verify) ||
               (verify&kPgctlAdspPgd)!=(originalPgctl_&kPgctlAdspPgd))
                ok=false;
            else
                pgChanged_=false;
        }
    }

    if(!pgChanged_ && !cgChanged_) applied_=false;
    return ok && !pgChanged_ && !cgChanged_;
}

NTSTATUS PciConfigBootPolicy::Apply(
    WDFDEVICE device,const PciConfigSnapshot& evidence,
    HardwareAccessGate& gate) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || attempted_ || !device ||
       !gate.Allowed() || gate.Removed())
        return STATUS_INVALID_DEVICE_STATE;

    attempted_=true;
    device_=device;
    gate_=&gate;

    if(evidence.vendorId!=0x8086 || evidence.deviceId!=0x3198 ||
       (evidence.headerType&0x7f)!=0 || evidence.firstCapability<0x50 ||
       (evidence.firstCapability&3)!=0 || evidence.capabilityCount==0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    BUS_INTERFACE_STANDARD bus{};
    const auto status=WdfFdoQueryForInterface(
        device,&GUID_BUS_INTERFACE_STANDARD,reinterpret_cast<PINTERFACE>(&bus),
        static_cast<USHORT>(sizeof(bus)),1,nullptr);
    if(!NT_SUCCESS(status)) return status;

    if(!bus.GetBusData || !bus.SetBusData || !bus.InterfaceDereference) {
        if(bus.InterfaceDereference) bus.InterfaceDereference(bus.Context);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    UCHAR liveConfig[kPciConfigSnapshotBytes]={};
    const bool snapshotExact=
        ReadConfigImage(bus,liveConfig) &&
        RtlCompareMemory(liveConfig,evidence.config,kPciConfigSnapshotBytes)
            ==kPciConfigSnapshotBytes;
    if(!snapshotExact) {
        bus.InterfaceDereference(bus.Context);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    const ULONG pgctl=evidence.pgctl;
    const ULONG cgctl=evidence.cgctl;
    originalPgctl_=pgctl;
    originalCgctl_=cgctl;
    if(!gate.Allowed() || gate.Removed()) {
        bus.InterfaceDereference(bus.Context);
        return STATUS_INVALID_DEVICE_STATE;
    }

    // SOF hda_dsp_pre_fw_run(): disable ADSP clock gating first.
    const ULONG desiredCgctl=cgctl&~kCgctlAdspDcge;
    if(desiredCgctl!=cgctl) {
        // Set dirty before SetBusData: a short write may have changed bytes
        // even though the API reports fewer than four bytes transferred.
        cgChanged_=true;
        if(!gate.Allowed() || gate.Removed() ||
           !WriteDword(bus,kCgctlOffset,desiredCgctl)) {
            if(gate.Allowed() && !gate.Removed()) (void)RestoreWithBus(bus);
            bus.InterfaceDereference(bus.Context);
            return gate.Removed()?STATUS_DELETE_PENDING:STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        if(!gate.Allowed() || gate.Removed()) {
            bus.InterfaceDereference(bus.Context);
            return STATUS_DELETE_PENDING;
        }
        ULONG verify=0;
        if(!ReadDword(bus,kCgctlOffset,&verify) || verify!=desiredCgctl) {
            if(gate.Allowed() && !gate.Removed()) (void)RestoreWithBus(bus);
            bus.InterfaceDereference(bus.Context);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }

    // Then prevent opportunistic ADSP power gating while firmware boots.
    const ULONG desiredPgctl=pgctl|kPgctlAdspPgd;
    if(desiredPgctl!=pgctl) {
        pgChanged_=true;
        if(!gate.Allowed() || gate.Removed() ||
           !WriteDword(bus,kPgctlOffset,desiredPgctl)) {
            if(gate.Allowed() && !gate.Removed()) (void)RestoreWithBus(bus);
            bus.InterfaceDereference(bus.Context);
            return gate.Removed()?STATUS_DELETE_PENDING:STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        if(!gate.Allowed() || gate.Removed()) {
            bus.InterfaceDereference(bus.Context);
            return STATUS_DELETE_PENDING;
        }
        ULONG verify=0;
        if(!ReadDword(bus,kPgctlOffset,&verify) || verify!=desiredPgctl) {
            if(gate.Allowed() && !gate.Removed()) (void)RestoreWithBus(bus);
            bus.InterfaceDereference(bus.Context);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }

    applied_=true;
    bus.InterfaceDereference(bus.Context);
    return STATUS_SUCCESS;
}

bool PciConfigBootPolicy::Restore() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return false;
    if(!applied_ && !Dirty()) return true;
    if(!device_ || !gate_ || !gate_->Allowed() || gate_->Removed()) return false;

    BUS_INTERFACE_STANDARD bus{};
    const auto status=WdfFdoQueryForInterface(
        device_,&GUID_BUS_INTERFACE_STANDARD,reinterpret_cast<PINTERFACE>(&bus),
        static_cast<USHORT>(sizeof(bus)),1,nullptr);
    if(!NT_SUCCESS(status)) return false;
    if(!bus.GetBusData || !bus.SetBusData || !bus.InterfaceDereference) {
        if(bus.InterfaceDereference) bus.InterfaceDereference(bus.Context);
        return false;
    }

    const bool ok=RestoreWithBus(bus);
    bus.InterfaceDereference(bus.Context);
    return ok;
}

} }
