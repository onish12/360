# M0.6.15H10 reproducible KMDF binary audit

H10 verifies that the temporary H8/H9 KMDF boot driver is reproducible from the
same reviewed inputs.

The SYS remains CI-only and is deleted before artifact staging.

## Why this milestone exists

H8 and H9 linked the same production driver logic but reported different SYS
SHA-256 values across separate CI runs.

A changing binary hash is not acceptable evidence for a future physical package
unless the difference is understood or eliminated.

H10 therefore requires deterministic linking before any installable M1 package
is prepared.

## Linker reproducibility mode

The real KMDF Driver project adds the MSVC linker option:

`/Brepro`

The PE/COFF format defines `IMAGE_DEBUG_TYPE_REPRO` as the debug-directory
marker for an image built reproducibly. When this marker is present, PE
date/time fields may contain hash-derived values instead of wall-clock
timestamps.

H10 does not trust the option string alone. It audits the final PE image.

## Independent PE parser

`scripts/audit-pe-repro.py` is a small fail-closed parser with no external
packages.

It validates:

- DOS MZ signature;
- PE signature;
- AMD64 machine value 0x8664;
- PE32+ optional-header magic;
- debug data-directory bounds;
- section-backed RVA mapping;
- complete 28-byte IMAGE_DEBUG_DIRECTORY entries;
- at least one entry with Type=16
  (`IMAGE_DEBUG_TYPE_REPRO`).

The parser emits a JSON report containing image byte count, COFF timestamp field,
debug-directory entries and the REPRO result.

## Two clean links

Within the same pinned WDK job and checkout, H10:

1. performs the existing H8/H9 real driver link;
2. audits x64/Native/imports;
3. hashes the first SYS;
4. audits its IMAGE_DEBUG_TYPE_REPRO marker;
5. deletes the first SYS;
6. invokes MSBuild `/t:Rebuild` again with the same:
   - checkout;
   - project;
   - generated embedded firmware source path;
   - WDK/KMDF versions;
   - x64 Release configuration;
   - SignMode=Off;
7. hashes the second SYS;
8. audits its IMAGE_DEBUG_TYPE_REPRO marker;
9. requires identical file size;
10. requires byte-identical SHA-256.

A matching size without a matching hash is failure.

A matching hash without the PE REPRO marker is also failure.

## Artifact boundary

Only the following reproducibility evidence may be staged:

- first PE REPRO JSON report;
- second PE REPRO JSON report;
- H8 link metadata;
- H9 PE header/import text reports;
- source/docs/static library artifacts already allowed by prior milestones.

The second SYS is deleted before staging.

The recursive artifact guard continues to reject any `.sys` or `.ri`.

## Safety boundary

H10 changes no hardware behavior.

It adds no:

- INF;
- catalog;
- signing package;
- install script;
- pnputil/devcon action;
- controller bind/restart;
- physical Lenovo execution;
- codec/amplifier programming;
- SSP/PDM endpoint;
- Windows audio stack dependency;
- playback.

A future physical M1 package must use an audited reproducible binary and will
still be limited to DSP boot -> FW_READY/IPC -> shutdown with all audio outputs
disabled.
