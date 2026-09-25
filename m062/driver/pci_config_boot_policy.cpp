// SPDX-License-Identifier: MIT
#include "pci_config_boot_policy.h"
#include <wdmguid.h>

namespace phaser360 { namespace windows {

namespace {
constexpr ULONG kVendorOffset=0x00u;
constexpr ULONG kCommandOffset=0x04u;
constexpr ULONG kClassRevisionOffset=0x08u;
constexpr ULONG kHeaderTypeOffset=0x0eu;
constexpr ULONG kBarsOffset=0x10u;
constexpr ULONG kBarsBytes=0x18u;
constexpr ULONG kSubsystemOffset=0x2cu;
constexpr ULONG kFirstCapabilityOffset=0x34u;
constexpr ULONG kInterruptPinOffset=0x3du;
constexpr UCHAR kCapabilityMin=0x50u;
constexpr UCHAR kCapabilityMax=0xfcu;

USHORT Load16(const UCHAR* p) noexcept {
    return static_cast<USHORT>(
        static_cast<USHORT>(p[0]) |
        static_cast<USHORT>(static_cast<USHORT>(p[1])<<8));
}
ULONG Load32(const UCHAR* p) noexcept {
    return static_cast<ULONG>(p[0]) |
        (static_cast<ULONG>(p[1])<<8) |
        (static_cast<ULONG>(p[2])<<16) |
        (static_cast<ULONG>(p[3])<<24);
}
bool EqualRange(const UCHAR* a,const UCHAR* b,ULONG offset,ULONG bytes) noexcept {
    return a && b && bytes!=0 &&
        RtlCompareMemory(a+offset,b+offset,bytes)==bytes;
}
bool ReadConfigImage(BUS_INTERFACE_STANDARD& bus,UCHAR* config) noexcept {
    if(!config || !bus.GetBusData) return false;
    RtlZeroMemory(config,kPciConfigSnapshotBytes);
    return bus.GetBusData(
        bus.Context,PCI_WHICHSPACE_CONFIG,config,0,kPciConfigSnapshotBytes)
        ==kPciConfigSnapshotBytes;
}
bool IdentityMatches(const UCHAR* live,const PciConfigSnapshot& evidence) noexcept {
    return live &&
        Load16(live+kVendorOffset)==evidence.vendorId &&
        Load16(live+kVendorOffset+2)==evidence.deviceId &&
        (live[kHeaderTypeOffset]&0x7fu)==(evidence.headerType&0x7fu) &&
        live[kFirstCapabilityOffset]==evidence.firstCapability;
}
bool StableHeaderMatches(const UCHAR* live,const PciConfigSnapshot& evidence) noexcept {
    // Do not compare PCI Status or capability payload/status fields: those can
    // legitimately change after HDA reset/stream preparation. Keep the fields
    // that prove this is still the same function/resource contract.
    return live &&
        EqualRange(live,evidence.config,kCommandOffset,2) &&
        EqualRange(live,evidence.config,kClassRevisionOffset,4) &&
        EqualRange(live,evidence.config,kBarsOffset,kBarsBytes) &&
        EqualRange(live,evidence.config,kSubsystemOffset,4) &&
        live[kInterruptPinOffset]==evidence.config[kInterruptPinOffset];
}
bool CapabilityStructureMatches(const UCHAR* live,const PciConfigSnapshot& evidence) noexcept {
    if(!live || !evidence.firstCapability || evidence.capabilityCount==0) return false;
    UCHAR seen[64]={};
    UCHAR current=evidence.firstCapability;
    UCHAR count=0;
    for(unsigned n=0;n<48;++n) {
        if((current&3u)!=0 || current<kCapabilityMin || current>kCapabilityMax ||
           static_cast<ULONG>(current)+2u>kPciConfigSnapshotBytes)
            return false;
        const unsigned slot=current>>2;
        if(slot>=64 || seen[slot]) return false;
        seen[slot]=1;
        ++count;

        // Capability ID and next-link are structural. Capability control/status
        // payload bytes are intentionally not part of the live TOCTOU fence.
        if(live[current]==0xffu ||
           live[current]!=evidence.config[current] ||
           live[current+1]!=evidence.config[current+1])
            return false;
        const UCHAR next=live[current+1];
        if(!next) return count==evidence.capabilityCount;
        current=next;
    }
    return false;
}
bool OwnedDwordsMatch(const UCHAR* live,const PciConfigSnapshot& evidence) noexcept {
    return live &&
        Load32(live+PciConfigBootPolicy::PgctlOffset())==evidence.pgctl &&
        Load32(live+PciConfigBootPolicy::CgctlOffset())==evidence.cgctl;
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
    failure_=PciConfigBootFailure::None;
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || attempted_ || !device ||
       !gate.Allowed() || gate.Removed())
        return Fail(PciConfigBootFailure::State,STATUS_INVALID_DEVICE_STATE);

    attempted_=true;
    device_=device;
    gate_=&gate;

    if(evidence.vendorId!=0x8086 || evidence.deviceId!=0x3198 ||
       (evidence.headerType&0x7f)!=0 || evidence.firstCapability<0x50 ||
       (evidence.firstCapability&3)!=0 || evidence.capabilityCount==0)
        return Fail(PciConfigBootFailure::Evidence,STATUS_DEVICE_CONFIGURATION_ERROR);

    BUS_INTERFACE_STANDARD bus{};
    const auto status=WdfFdoQueryForInterface(
        device,&GUID_BUS_INTERFACE_STANDARD,reinterpret_cast<PINTERFACE>(&bus),
        static_cast<USHORT>(sizeof(bus)),1,nullptr);
    if(!NT_SUCCESS(status))
        return Fail(PciConfigBootFailure::BusInterfaceQuery,status);

    if(!bus.GetBusData || !bus.SetBusData || !bus.InterfaceDereference) {
        if(bus.InterfaceDereference) bus.InterfaceDereference(bus.Context);
        return Fail(PciConfigBootFailure::BusInterfaceInvalid,
                    STATUS_DEVICE_CONFIGURATION_ERROR);
    }

    UCHAR liveConfig[kPciConfigSnapshotBytes]={};
    if(!ReadConfigImage(bus,liveConfig)) {
        bus.InterfaceDereference(bus.Context);
        return Fail(PciConfigBootFailure::LiveRead,
                    STATUS_DEVICE_CONFIGURATION_ERROR);
    }
    if(!IdentityMatches(liveConfig,evidence)) {
        bus.InterfaceDereference(bus.Context);
        return Fail(PciConfigBootFailure::IdentityDrift,
                    STATUS_DEVICE_CONFIGURATION_ERROR);
    }
    if(!StableHeaderMatches(liveConfig,evidence)) {
        bus.InterfaceDereference(bus.Context);
        return Fail(PciConfigBootFailure::StableHeaderDrift,
                    STATUS_DEVICE_CONFIGURATION_ERROR);
    }
    if(!CapabilityStructureMatches(liveConfig,evidence)) {
        bus.InterfaceDereference(bus.Context);
        return Fail(PciConfigBootFailure::CapabilityStructureDrift,
                    STATUS_DEVICE_CONFIGURATION_ERROR);
    }
    if(!OwnedDwordsMatch(liveConfig,evidence)) {
        bus.InterfaceDereference(bus.Context);
        return Fail(PciConfigBootFailure::OwnedDwordDrift,
                    STATUS_DEVICE_CONFIGURATION_ERROR);
    }

    const ULONG pgctl=evidence.pgctl;
    const ULONG cgctl=evidence.cgctl;
    originalPgctl_=pgctl;
    originalCgctl_=cgctl;
    if(!gate.Allowed() || gate.Removed()) {
        bus.InterfaceDereference(bus.Context);
        return Fail(PciConfigBootFailure::GateClosed,STATUS_INVALID_DEVICE_STATE);
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
            const bool removed=gate.Removed();
            bus.InterfaceDereference(bus.Context);
            return Fail(removed?PciConfigBootFailure::CgctlGateLost:
                                PciConfigBootFailure::CgctlWrite,
                        removed?STATUS_DELETE_PENDING:STATUS_DEVICE_CONFIGURATION_ERROR);
        }
        if(!gate.Allowed() || gate.Removed()) {
            bus.InterfaceDereference(bus.Context);
            return Fail(PciConfigBootFailure::CgctlGateLost,STATUS_DELETE_PENDING);
        }
        ULONG verify=0;
        if(!ReadDword(bus,kCgctlOffset,&verify) || verify!=desiredCgctl) {
            if(gate.Allowed() && !gate.Removed()) (void)RestoreWithBus(bus);
            bus.InterfaceDereference(bus.Context);
            return Fail(PciConfigBootFailure::CgctlVerify,
                        STATUS_DEVICE_CONFIGURATION_ERROR);
        }
    }

    // Then prevent opportunistic ADSP power gating while firmware boots.
    const ULONG desiredPgctl=pgctl|kPgctlAdspPgd;
    if(desiredPgctl!=pgctl) {
        pgChanged_=true;
        if(!gate.Allowed() || gate.Removed() ||
           !WriteDword(bus,kPgctlOffset,desiredPgctl)) {
            if(gate.Allowed() && !gate.Removed()) (void)RestoreWithBus(bus);
            const bool removed=gate.Removed();
            bus.InterfaceDereference(bus.Context);
            return Fail(removed?PciConfigBootFailure::PgctlGateLost:
                                PciConfigBootFailure::PgctlWrite,
                        removed?STATUS_DELETE_PENDING:STATUS_DEVICE_CONFIGURATION_ERROR);
        }
        if(!gate.Allowed() || gate.Removed()) {
            bus.InterfaceDereference(bus.Context);
            return Fail(PciConfigBootFailure::PgctlGateLost,STATUS_DELETE_PENDING);
        }
        ULONG verify=0;
        if(!ReadDword(bus,kPgctlOffset,&verify) || verify!=desiredPgctl) {
            if(gate.Allowed() && !gate.Removed()) (void)RestoreWithBus(bus);
            bus.InterfaceDereference(bus.Context);
            return Fail(PciConfigBootFailure::PgctlVerify,
                        STATUS_DEVICE_CONFIGURATION_ERROR);
        }
    }

    applied_=true;
    bus.InterfaceDereference(bus.Context);
    failure_=PciConfigBootFailure::None;
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
