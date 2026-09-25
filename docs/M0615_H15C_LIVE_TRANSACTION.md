# M0.6.15H15C-LIVE-R2 transactional trust and rollback

R2 uses the existing workflow-dispatch-only signed package producer and closes
the target-side trust gap left by R1. The H15C-LIVE filter remains read-only.

## Signed package

The manual workflow `.github/workflows/h15c-live-r2-package.yml` creates a
short-lived self-signed Code Signing certificate on a disposable runner. Its
private key is NonExportable and never enters the artifact. The workflow
exports only the public CER, signs the SYS, runs Inf2Cat over the already signed
SYS, signs the CAT, records exact hashes and signer identity in
`package_manifest.json`, removes the runner private certificate and uploads
the signed package for three days.

R2 keeps certificate validity longer than artifact retention so an artifact
cannot remain downloadable after its signer certificate has already expired.

## Read-only target gates

Before any target mutation:

- Windows must be exactly build 19044;
- the exact REV_06 DEV_3198 must be healthy;
- IntcAudioBus must remain the function service;
- no H15C-LIVE package/filter may already be present;
- TestSign must already be effective;
- WinRE must be enabled;
- package hashes, certificate identity, Code Signing EKU and SYS/CAT signer
  thumbprints must match the manifest;
- the exact package certificate must be absent from both LocalMachine Root and
  LocalMachine TrustedPublisher.

The package checker and preflight are read-only. They do not install the
certificate, driver, change BCD, restart the device or mutate registry state.

## Transaction

`Run-H15cLiveTransaction.ps1` performs one bounded transaction:

1. export the currently bound Intel package;
2. add only the package's exact public certificate to LocalMachine Root;
3. add the same exact certificate to LocalMachine TrustedPublisher;
4. require SYS and CAT Authenticode status to become Valid;
5. install only the exact H15C-LIVE Extension upper-filter package;
6. restart only the exact DEV_3198;
7. require IntcAudioBus to remain the function service and require the H15C
   filter in CompoundUpperFilters;
8. capture PCI configuration using the read-only filter;
9. uninstall only the recorded PHASER360 published INF;
10. restart only DEV_3198 back to the Intel baseline;
11. prove exact restoration of service, base INF, version and provider and prove
    the PHASER360 package/filter absent;
12. delete only the exact package-certificate thumbprint from
    TrustedPublisher and Root;
13. prove both certificate stores returned to their pre-transaction absence.

Success requires capture completed, Intel baseline restored and trust restored.

## Failure ordering

If anything fails after mutation begins, the finally path attempts driver
rollback first. The signer certificate is removed only when package/filter
absence is proven and the target exactly matches the recorded Intel baseline:
InstanceId, service, published INF, driver version and provider must all match,
the exact DEV_3198 REV_06 hardware ID must still be present, and the H15C upper
filter must be absent. Merely returning to an OK IntcAudioBus device is not
sufficient. If that exact proof is unavailable, R2 intentionally retains the
signer trust and writes `TRUST_RETAINED_FOR_SAFETY=TRUE`. This avoids making a
possibly still installed test-signed filter untrusted on a subsequent device
start.

If Windows cannot boot, WinRE instructions remove only the recorded PHASER360
H15C-LIVE published driver. Offline certificate-store edits are not attempted.
After Windows boots healthy on IntcAudioBus, the exact recorded certificate
thumbprint can be removed from TrustedPublisher and Root.

## Persistent-state accounting

Unlike R1, R2 does not claim RegistryWrite=NO. PnP package operations and
machine certificate stores are persistent Windows state.

R2 reports:

- `RegistryWrite=PNP_AND_CERT_STORES_TRANSACTIONAL`
- `TrustChange=TEMPORARY_LOCALMACHINE_ROOT_AND_TRUSTEDPUBLISHER`
- `SystemReboot=NO`
- `BcdWrite=NO`
- `PciConfigWrite=NO`
- `Mmio=NO`
- `DspBoot=NO`
- `AudioPlayback=NO`

## Primary references

- Microsoft Learn: Installing Test Certificates.
- Microsoft Learn: Test Signing.
- Microsoft Learn: Test-Signing Driver Packages.
- Microsoft Learn: CertUtil command syntax.
- Microsoft Learn: PnPUtil command syntax.
- Microsoft Learn: AddFilter directive and device filter ordering.
