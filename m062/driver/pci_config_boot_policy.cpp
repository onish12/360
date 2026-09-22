// SPDX-License-Identifier: MIT
#include "pci_config_boot_policy.h"
#include <wdmguid.h>

namespace phaser360 { namespace windows {

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
    bool ok=true;

    // Match SOF post-fw policy ordering: clock gating first, then power gating.
    if(cgChanged_) {
        ULONG current=0;
        if(!ReadDword(bus,kCgctlOffset,&current)) {
            ok=false;
        } else {
            const ULONG desired=(current&~kCgctlAdspDcge)|
                                (originalCgctl_&kCgctlAdspDcge);
            if(desired!=current && !WriteDword(bus,kCgctlOffset,desired))
                ok=false;
            ULONG verify=0;
            if(!ReadDword(bus,kCgctlOffset,&verify) ||
               (verify&kCgctlAdspDcge)!=(originalCgctl_&kCgctlAdspDcge))
                ok=false;
            else
                cgChanged_=false;
        }
    }

    if(pgChanged_) {
        ULONG current=0;
        if(!ReadDword(bus,kPgctlOffset,&current)) {
            ok=false;
        } else {
            const ULONG desired=(current&~kPgctlAdspPgd)|
                                (originalPgctl_&kPgctlAdspPgd);
            if(desired!=current && !WriteDword(bus,kPgctlOffset,desired))
                ok=false;
            ULONG verify=0;
            if(!ReadDword(bus,kPgctlOffset,&verify) ||
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

    ULONG pgctl=0,cgctl=0;
    bool success=ReadDword(bus,kPgctlOffset,&pgctl) &&
                 ReadDword(bus,kCgctlOffset,&cgctl) &&
                 pgctl==evidence.pgctl && cgctl==evidence.cgctl;
    if(!success) {
        bus.InterfaceDereference(bus.Context);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    originalPgctl_=pgctl;
    originalCgctl_=cgctl;

    // SOF hda_dsp_pre_fw_run(): disable ADSP clock gating first.
    const ULONG desiredCgctl=cgctl&~kCgctlAdspDcge;
    if(desiredCgctl!=cgctl) {
        if(!WriteDword(bus,kCgctlOffset,desiredCgctl)) {
            (void)RestoreWithBus(bus);
            bus.InterfaceDereference(bus.Context);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        cgChanged_=true;
        ULONG verify=0;
        if(!ReadDword(bus,kCgctlOffset,&verify) || verify!=desiredCgctl) {
            (void)RestoreWithBus(bus);
            bus.InterfaceDereference(bus.Context);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }

    // Then prevent opportunistic ADSP power gating while firmware boots.
    const ULONG desiredPgctl=pgctl|kPgctlAdspPgd;
    if(desiredPgctl!=pgctl) {
        if(!WriteDword(bus,kPgctlOffset,desiredPgctl)) {
            (void)RestoreWithBus(bus);
            bus.InterfaceDereference(bus.Context);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        pgChanged_=true;
        ULONG verify=0;
        if(!ReadDword(bus,kPgctlOffset,&verify) || verify!=desiredPgctl) {
            (void)RestoreWithBus(bus);
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
