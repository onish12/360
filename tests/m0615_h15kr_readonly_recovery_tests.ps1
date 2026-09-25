$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content (Join-Path $root 'm062\h15kr\h15kr_readonly_recovery_probe.h') -Raw
$src=Get-Content (Join-Path $root 'm062\h15kr\h15kr_readonly_recovery_probe.cpp') -Raw
$proj=Get-Content (Join-Path $root 'm062\h15kr\phaser360_h15kr_readonly_recovery_probe.vcxproj') -Raw
$inf=Get-Content (Join-Path $root 'm062\h15kr\phaser360_h15kr_readonly_recovery_probe.inf') -Raw
$doc=Get-Content (Join-Path $root 'docs\M0615_H15KR_READONLY_RECOVERY.md') -Raw

foreach($x in @('0x833e6478u','sizeof(H15krSnapshotV1)==136u','kH15krRequiredLiveFlags=0x1fffu')){
 if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15KR_HDR_MISSING: $x"}
}
foreach($x in @(
 'MmMapIoSpaceEx(a[0],l[0],PAGE_READONLY|PAGE_NOCACHE)',
 'MmMapIoSpaceEx(a[1],l[1],PAGE_READONLY|PAGE_NOCACHE)',
 'READ_REGISTER_USHORT','READ_REGISTER_UCHAR','READ_REGISTER_ULONG',
 'kDspAdspcs=0x0004u','kDspAdspic=0x0008u','kDspHipcctl=0x0050u',
 'kCorbctl=0x004cu','kRirbctl=0x005cu','kStreamBase=0x0080u')){
 if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15KR_SRC_MISSING: $x"}
}
foreach($x in @('WRITE_REGISTER_','PAGE_READWRITE','GetBusData','SetBusData',
 'WdfInterruptCreate','WdfDma','WdfCommonBuffer','PinnedFirmware','GlkBoot','ColdPower')){
 if(($src+$hdr+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15KR_FORBIDDEN: $x"}
}
foreach($x in @('AddService=Phaser360H15kr,0x00000002',
 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06','KmdfLibraryVersion=1.31')){
 if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15KR_INF_MISSING: $x"}
}
foreach($x in @('PAGE_READONLY','no MMIO write','0x001D003C')){
 if($doc.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15KR_DOC_MISSING: $x"}
}
Write-Host 'H15KR_STATIC_TESTS=PASS; mapping=BOTH_BARS_READ_ONLY; mmio_write=NO; pci_write=NO; dma=NO; irq=NO; firmware=NO; playback=NO'
