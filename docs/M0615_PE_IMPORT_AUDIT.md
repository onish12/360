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
