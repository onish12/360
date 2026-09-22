# M0.6.15H15C-LIVE-R2 package readiness

R1 validated the extension-filter install, read-only capture and driver rollback
transaction, but it intentionally produced no signed installable package.

R2 adds a workflow-dispatch-only package producer. The CI runner creates a
short-lived self-signed Code Signing certificate whose private key is marked
non-exportable. The SYS is signed first, Inf2Cat then hashes that signed SYS,
and the CAT is signed with the same certificate.

The downloadable artifact contains SYS, INF, CAT, the public CER,
package_manifest.json, SHA256SUMS.txt and the H15C-LIVE scripts. It never
contains PFX, P12, PVK, KEY or any private signing key.

The target-side checker is read-only. It validates exact target state, package
hashes, certificate identity and Code Signing EKU, verifies that SYS and CAT
carry the same signer certificate, and reports whether the exact CER is already
present in LocalMachine Root and TrustedPublisher. It does not install a
driver, change trust, modify BCD or restart a device.

Microsoft requires the test computer to trust the test certificate used for a
test-signed PnP package. Therefore certificate installation is an explicit
operator action outside the checker. Only after the checker reports
H15C_LIVE_R2_PACKAGE_TRUST_READY and both signatures are Valid may the existing
R1 preflight and transaction be used.

The test certificate is short-lived and should be removed from both machine
stores immediately after the filter capture/rollback completes.

PCI_CONFIG_WRITE=FALSE
MMIO=FALSE
DSP_BOOT=FALSE
AUDIO_PLAYBACK=FALSE
PRIVATE_KEY_DISTRIBUTED=FALSE
PACKAGE_TRIGGER=MANUAL_ONLY
