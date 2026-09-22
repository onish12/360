// SPDX-License-Identifier: MIT
#include "pci_config_attestation.h"
#include <initguid.h>
#include <wdmguid.h>

namespace phaser360 { namespace windows {

namespace {
constexpr USHORT kIntelVendor=0x8086;
constexpr USHORT kGlkAudioDevice=0x3198;
constexpr ULONG kConfigBytes=256;
constexpr ULONG kVendorWindowStart=0x40;
constexpr ULONG kVendorWindowEnd=0x4f;
constexpr ULONG kPgctlOffset=0x44;
constexpr ULONG kCgctlOffset=0x48;
constexpr UCHAR kCapabilityMin=0x50;
constexpr UCHAR kCapabilityMax=0xfc;
}

USHORT PciConfigAttestation::Load16(const UCHAR* p) noexcept {
    return static_cast<USHORT>(
        static_cast<USHORT>(p[0]) |
        static_cast<USHORT>(static_cast<USHORT>(p[1])<<8));
}

ULONG PciConfigAttestation::Load32(const UCHAR* p) noexcept {
    return static_cast<ULONG>(p[0]) |
        (static_cast<ULONG>(p[1])<<8) |
        (static_cast<ULONG>(p[2])<<16) |
        (static_cast<ULONG>(p[3])<<24);
}

bool PciConfigAttestation::ValidateCapabilityChain(
    const UCHAR* config,SIZE_T bytes,UCHAR first,UCHAR* count) noexcept {
    if(!config || bytes<kConfigBytes || !count) return false;
    *count=0;
    if(!first || (first&3) || first<kCapabilityMin || first>kCapabilityMax)
        return false;

    UCHAR seen[64]={};
    UCHAR current=first;
    for(unsigned n=0;n<48;++n) {
        if((current&3) || current<kCapabilityMin || current>kCapabilityMax ||
           static_cast<SIZE_T>(current)+2>bytes)
            return false;
        const unsigned slot=current>>2;
        if(slot>=64 || seen[slot]) return false;
        seen[slot]=1;
        ++(*count);

        // 0xff is a floating/nonexistent config-space read, not a capability.
        if(config[current]==0xff) return false;
        const UCHAR next=config[current+1];
        if(!next) return true;
        current=next;
    }
    return false;
}

NTSTATUS PciConfigAttestation::Capture(WDFDEVICE device) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || attempted_)
        return STATUS_INVALID_DEVICE_STATE;
    if(!device) return STATUS_INVALID_PARAMETER;

    attempted_=true;
    BUS_INTERFACE_STANDARD bus={};
    const auto status=WdfFdoQueryForInterface(
        device,&GUID_BUS_INTERFACE_STANDARD,reinterpret_cast<PINTERFACE>(&bus),
        static_cast<USHORT>(sizeof(bus)),1,nullptr);
    if(!NT_SUCCESS(status)) return status;

    // A successful query carries a referenced interface. Always release it
    // after the single bounded GetBusData operation.
    if(!bus.GetBusData || !bus.InterfaceDereference) {
        if(bus.InterfaceDereference) bus.InterfaceDereference(bus.Context);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    UCHAR config[kConfigBytes]={};
    const ULONG read=bus.GetBusData(
        bus.Context,PCI_WHICHSPACE_CONFIG,config,0,kConfigBytes);
    bus.InterfaceDereference(bus.Context);
    if(read!=kConfigBytes) return STATUS_DEVICE_CONFIGURATION_ERROR;

    PciConfigSnapshot candidate{};
    candidate.vendorId=Load16(config+0x00);
    candidate.deviceId=Load16(config+0x02);
    candidate.headerType=config[0x0e];
    candidate.firstCapability=config[0x34];
    candidate.pgctl=Load32(config+kPgctlOffset);
    candidate.cgctl=Load32(config+kCgctlOffset);

    // Type 0 ends at 0x3f. Windows owns the header and every linked
    // capability. Require the complete conventional capability chain to stay
    // at/above 0x50, leaving 0x40..0x4f as the device-specific window.
    if(candidate.vendorId!=kIntelVendor ||
       candidate.deviceId!=kGlkAudioDevice ||
       (candidate.headerType&0x7f)!=0 ||
       !ValidateCapabilityChain(
           config,kConfigBytes,candidate.firstCapability,
           &candidate.capabilityCount))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    static_assert(kPgctlOffset>=kVendorWindowStart &&
                  kCgctlOffset<=kVendorWindowEnd,
                  "H15C offsets must remain inside attested vendor window");
    snapshot_=candidate;
    valid_=true;
    return STATUS_SUCCESS;
}

} }
