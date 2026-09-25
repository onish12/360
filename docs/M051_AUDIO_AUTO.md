# PHASER360 AUDIO AUTO 1.1.1

For the completed 2026-09-16 21:12:27 snapshot with failed cleanup, run
**REPAIR_AUDIO.cmd** as administrator following START_AICI.txt. This reuses the
existing journal and does not load a driver or require F7. See the
[hardware report and repair](M051_HARDWARE_20260916.md).

RUN_AUDIO.cmd is the separate probe entry point. The package includes the compiled x64 M0.5.1 read-only kernel
(0.5.1.0) and the x64 device-binding helper. No compiler or WDK is needed on the
laptop. Audio playback is not implemented.

| Observed state | Action |
| --- | --- |
| Exact target, reviewed Intel 4883 INF, signing permits test | Verify package/export/service; perform the controlled handoff |
| Reviewed Intel, signing blocked/unknown | Export and report prerequisite; no device changes |
| Different Intel bytes, other OEM package or untyped metadata | Preserve package and report; no automatic replacement |
| Unbound code 28 without retained Intel | Existing M0.5.1 transaction |
| Unbound Problem 0 or 28 with retained Intel | Stop; use the existing snapshot rather than repeating the probe |
| Previous probe package or service | Require its recorded recovery cleanup |
| Changed state, failed backup, failed cleanup | Explicit failure; no false success |

The handoff verifies all three Intel file hashes from the supplied 2026-09-16
report and the loaded SYS. BootCritical and service Start are recorded as
different properties. Intel package deletion is never inferred from Start=3.

See [M051_HANDOFF.md](M051_HANDOFF.md) for the exact API scope, tests, official
references and recovery boundary. The successful handoff intentionally leaves
the controller unbound with Intel files preserved. It does not restore the
initial binding or provide sound. It does not guarantee that Windows cannot
select Intel again later.

The exact live OEM name selects exports; source/export paths, lengths and
SHA-256 hashes must match. Reparse entries, missing/extra files, changed source
files and changed bindings are rejected. The report includes the PnP tree,
service metadata, WMI-reported bindings, audio/codec/DSP devices and recovery
configuration. WMI alone is not proof of exclusive package use. Recovery keys
are not collected.

Report files are under PHASER360_AUDIO on the Windows volume. DriverBackup is
outside RESULT_AUDIO_*.zip. Nothing is uploaded automatically. WinRE status
does not prove boot/access, and export is not a Windows image backup.

CI runs Windows PowerShell 5.1 tests, x64 SDK/managed ABI checks, compiled-helper
loading, C++ resource tests and WDK build/signing. The downloaded artifact's
manifest is checked again. The first real snapshot succeeded in version 1.1;
cleanup failed and prompted this correction. Physical repair and audio playback
are not established by CI success.
