# M0.6.15H9 PE/import audit

H9 audits the actual temporary KMDF SYS produced by H8 before the CI runner is
allowed to delete it.

This milestone does not distribute the SYS and does not add an INF, installer,
certificate or physical-hardware step.

## Why audit the linked binary

Source-level guards and a successful final link do not by themselves prove that
the produced image has the expected PE target or import surface.

H9 therefore inspects the exact linked `phaser360_m1_boot.sys` while it still
exists on the WDK runner.

The audit runs before the H8 destruction rule.

## PE header audit

CI runs:

- `dumpbin /headers <sys>`;
- `dumpbin /imports <sys>`.

The full textual reports are saved as:

- `H9_PE_HEADERS.txt`;
- `H9_PE_IMPORTS.txt`.

The headers must identify:

- machine `x64` / PE machine 0x8664;
- subsystem `Native`.

A different architecture or non-native subsystem fails the workflow.

## CNG import requirement

H8 proved that the real Driver project requires explicit `cng.lib`.

H9 now verifies that the final import table contains the BCrypt functions used
by the production owned-copy firmware identity gate:

- BCryptOpenAlgorithmProvider;
- BCryptCloseAlgorithmProvider;
- BCryptCreateHash;
- BCryptHashData;
- BCryptFinishHash;
- BCryptDestroyHash.

This verifies the final linked image did not accidentally lose or replace the
runtime CNG identity check.

## Forbidden user-mode imports

The linked SYS must not import user-mode runtime libraries including:

- KERNEL32.dll;
- USER32.dll;
- NTDLL.dll;
- ADVAPI32.dll;
- BCRYPT.dll;
- WINMM.dll;
- MMDEVAPI.dll;
- AUDIOSES.dll.

The production hash path is kernel CNG, not user-mode BCRYPT.dll.

## Forbidden audio-stack imports

H9 also fails if the import report names:

- PORTCLS.SYS;
- KS.SYS;
- DRMK.SYS;
- ACX01000.SYS.

The purpose is to prove that this boot-only image has not silently acquired a
Windows audio endpoint/class-driver dependency.

Codec/amplifier source names remain forbidden by the source guards as well.

## Destruction and artifact boundary

Only after PE/import validation succeeds does the workflow:

1. calculate and record the temporary SYS SHA-256 and size;
2. delete the SYS;
3. delete the generated embedded-firmware C++ source;
4. delete the local SOF fixture;
5. prove each deletion succeeded.

The H9 artifact contains the text header/import reports and H8 metadata, but the
recursive artifact guard still rejects `.sys` and `.ri`.

Thus the binary surface can be audited without distributing an installable
driver.

## Still not a physical package

H9 does not add:

- INF binding;
- catalog;
- test certificate;
- install command;
- controller restart;
- recovery script;
- Lenovo execution;
- MAX98357A/DA7219 programming;
- SSP/PDM endpoint setup;
- WaveRT/ACX/PortCls audio endpoint;
- playback.

Physical M1 remains blocked until the binary audit is green and a separate,
reviewed recovery + target-identity package exists.


## Verified CI evidence (2026-09-21)

Final H9 source/runner-fix commit:
`0b8a0b2f5fc4a5182c6a689d0b37a7a30de9fc81`.
Tree: `cb2ba8057f5d3c3e2e69011d6d9fc3dbe4517d0b`.

The first H9 source commit `ea0695d0` failed only because the workflow
invoked `dumpbin` by PATH name on the hosted runner. The final commit resolves
the x64 Hostx64/x64 dumpbin explicitly through the installed Visual Studio
toolchain. No driver production source changed in that correction.

Final verification:

- WDK/KMDF run `35629003161`, job `106430333102`: real WDK
  static-library compilation passes and all 10 selected host regressions pass.
- PnP remains `SOF_PNP_RESOURCES_TESTS=516 PASS`.
- Repeated-D0 model remains `SOF_GLK_BOOT_TESTS=327105 PASS`.
- Pinned firmware owner remains `SOF_PINNED_OWNER_TESTS=34 PASS`.
- H6/H7/H8 static guards remain green.
- Temporary real KMDF SYS:
  - 338,944 bytes;
  - SHA-256
    `1e34a821fb7444b8bb270cddd7c1c043281d82897fb67f5d636061ac419275fb`;
  - PE machine x64;
  - subsystem Native.
- H9 runtime import audit reports
  `H9_PE_AUDIT=PASS; machine=X64; subsystem=NATIVE;
  cng_imports=PRESENT; usermode_imports=NONE; audio_imports=NONE;
  sys_uploaded=FALSE`.
- The final image contains all required BCrypt imports used by the runtime
  firmware identity check and no PortCls/KS/DRMK/ACX import.
- H9 static guard reports
  `H9_PE_AUDIT_STATIC_TESTS=PASS; headers=REQUIRED; imports=REQUIRED;
  cng_import=REQUIRED; usermode_imports=FORBIDDEN;
  audio_imports=FORBIDDEN; sys_upload=NO; inf=ABSENT; playback=NO`.
- Windows/Linux offline run `35629009717`: Windows job
  `106430355542` passes 14/14 plus the generated-provider
  `H7_EMBEDDED_FIRMWARE_TESTS=19 PASS`; Linux job
  `106430355308` passes 12/12.
- Official Windows fixture remains
  `SOF_CNG_PIN_TESTS=10 PASS` and
  `SOF_PINNED_REFERENCE_TESTS=13 PASS`.
- Report-only artifact `PHASER360_M0615H9_PE_AUDIT_REPORT`: ID
  `10653831424`, 480,967 bytes; GitHub archive SHA-256
  `f5369342f335309812dba1b59dc8cdd968f9a6eb24abb3d45fb36a7b4e895853`.

The SYS and firmware fixture are deleted before artifact upload. H9 authorizes
no physical Lenovo execution and no audio path.
