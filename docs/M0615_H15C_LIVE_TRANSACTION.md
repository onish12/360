# M0.6.15H15C-LIVE-R1 transactional install/capture/rollback gate

H15C-LIVE-R1 adds the controlled physical-use contract for the read-only upper
filter. CI still does not authorize or perform target installation.

## Preflight

Collect-H15cLiveInstallPreflight.ps1 is read-only. It requires:

- administrator x64 PowerShell;
- Windows build exactly 19044;
- exactly one healthy exact DEV_3198 REV_06 target;
- Intel IntcAudioBus still bound as the function service;
- no existing H15C-LIVE published extension package;
- Windows RE enabled;
- Code Integrity reporting CODEINTEGRITY_OPTION_TESTSIGN;
- a complete H15C-LIVE package whose SYS and CAT Authenticode status is Valid.

It records Secure Boot and HVCI but does not change either one. It does not
change BCD, trust stores, registry, drivers, devices or power state.

Microsoft documents SystemCodeIntegrityInformation option 0x02 as
CODEINTEGRITY_OPTION_TESTSIGN.

## Transaction

Run-H15cLiveTransaction.ps1 performs one bounded transaction:

1. repeat all exact-target/trust/WinRE gates;
2. export the currently bound Intel package with PnPUtil /export-driver;
3. add/install only the exact H15C-LIVE extension package;
4. discover and record the resulting PHASER360 oemNN.inf by its unique
   ExtensionId;
5. restart only the exact DEV_3198 instance;
6. require Phaser360H15cLive in CompoundUpperFilters while IntcAudioBus remains
   the function service;
7. execute the existing read-only PCI collector;
8. delete only the recorded PHASER360 extension package using
   PnPUtil /delete-driver oemNN.inf /uninstall /force;
9. restart only DEV_3198;
10. require exact restoration of service, base INF, version and provider and
    require the PHASER360 package/filter to be absent.

There is no /reboot flag.

## Failure behavior

Once installation has started, every failure enters a finally-block emergency
rollback. The script searches only for packages carrying the fixed PHASER360
ExtensionId, removes those packages, restarts only DEV_3198 and writes a
rollback log.

The transaction result is successful only if the Intel baseline identity is
exactly restored and no H15C-LIVE package/filter remains.

If Windows cannot boot, H15C_LIVE_WINRE_ROLLBACK.txt documents offline DISM
removal of only the recorded PHASER360 oemNN.inf. The Intel base package must
not be removed.

Microsoft documents AddFilter for device-specific filters on Windows 10 1903+,
PnPUtil /delete-driver /uninstall for package removal, /restart-device on
Windows 10 2004+, and offline DISM driver removal.

## Explicit holds

TARGET_INSTALL_EXECUTED_IN_CI=FALSE
TARGET_INSTALL_AUTHORIZED_BY_SOURCE_ALONE=FALSE
BCD_WRITE=FALSE
TRUST_STORE_WRITE=FALSE
SYSTEM_REBOOT=FALSE
PCI_CONFIG_WRITE=FALSE
MMIO=FALSE
DSP_BOOT=FALSE
AUDIO_PLAYBACK=FALSE
