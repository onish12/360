# M0.6.15H15C-LIVE-R2 signed package readiness

The signed package is produced only by the manual
`H15C-LIVE-R2 signed test package - MANUAL ONLY` workflow.

The runner creates an ephemeral self-signed Code Signing certificate with a
NonExportable private key. The artifact contains SYS, INF, CAT, the public CER,
`package_manifest.json`, hashes and the H15C-LIVE target scripts. No PFX, P12,
PVK, KEY or private key is distributed.

The certificate validity window is seven days while artifact retention is three
days.

## Target-side readiness

The checker is read-only. It validates exact target state, package hashes,
certificate identity and Code Signing EKU, and confirms that SYS and CAT carry
the same signer certificate.

A package is ready for the R2 transaction only when its exact signer
certificate is absent from both LocalMachine Root and TrustedPublisher. R2
deliberately rejects a pre-existing copy because a rollback could not prove that
the trust state returned to its original condition.

Do not manually import the certificate.

The preflight additionally requires TestSign already effective and WinRE
enabled. Only the transaction is allowed to add temporary trust.

## Trust ownership

The transaction owns the entire certificate lifecycle:

`absent before -> exact certificate trusted -> package verified Valid ->
filter capture -> filter removed -> Intel baseline proven -> exact certificate
removed -> absent after`.

If driver rollback is not proven, the signer certificate is retained for safety
and the result is a failed transaction, not a successful cleanup.

PCI_CONFIG_WRITE=FALSE
MMIO=FALSE
DSP_BOOT=FALSE
AUDIO_PLAYBACK=FALSE
PRIVATE_KEY_DISTRIBUTED=FALSE
PACKAGE_TRIGGER=MANUAL_ONLY
TARGET_TRUST_MANUAL_STEP=FALSE
