# M0.6.15H14.1 deterministic ephemeral signing gate

H14.1 proves that the exact H13.1 package can be cryptographically signed and verified in CI without distributing a signed driver package or any private key. Tool discovery is deterministic: SignTool is supplied by the official `Microsoft.Windows.SDK.BuildTools` package pinned to 10.0.28000.2526, while Inf2Cat remains supplied by the matching `Microsoft.Windows.WDK.x64` package.

It does not establish production Windows kernel acceptance on the Lenovo and it
does not modify target trust.

## Ephemeral CI certificate

The H14 workflow creates one self-signed code-signing certificate in the
GitHub Actions runner CurrentUser certificate store with:

- type: CodeSigningCert;
- RSA 2048;
- SHA-256;
- one-day lifetime;
- KeyExportPolicy = NonExportable.

The private key is never exported. H14 contains no Export-PfxCertificate path
and no PFX/P12 payload is created.

A public CER copy is exported temporarily only so the same certificate can be
trusted in the runner CurrentUser Root and TrustedPublisher stores for local
Authenticode verification.

This trust modification exists only inside the disposable CI runner. It is not
a target-side trust procedure.

## Correct signing order

H14 rebuilds the KMDF 1.31 driver from the exact pinned embedded firmware and
uses this ordering:

1. create the ephemeral code-signing certificate;
2. embedded-sign phaser360_m1_boot.sys with SHA-256;
3. run Inf2Cat on the package containing the already-signed SYS;
4. sign the generated phaser360_m1_boot.cat with the same certificate;
5. verify SYS and CAT with SignTool Authenticode policy;
6. require Get-AuthenticodeSignature status Valid for both files.

Signing the SYS before Inf2Cat is mandatory because the catalog must contain the
hash of the final signed SYS image rather than the earlier unsigned bytes.

No network timestamp is used in H14.

## Package scope

The temporary package still uses the exact H13 INF:

- exact controller HWID only;
- Windows 10 build 19044 target contract;
- KMDF 1.31;
- demand-start kernel service;
- no filters;
- no audio endpoints;
- no MAX98357A/DA7219 configuration;
- no WaveRT/ACX/PortCls registration.

Inf2Cat is rerun for 10_VB_X64 after SYS signing.

## Deterministic WDK tools

H14.1 resolves SignTool from `Microsoft.Windows.SDK.BuildTools.10.0.28000.2526\\bin\\10.0.28000.0\\x64\\signtool.exe` and Inf2Cat from `Microsoft.Windows.WDK.x64.10.0.28000.2526\\c\\bin\\10.0.28000.0\\x86\\Inf2Cat.exe`. The workflow emits begin/end markers for discovery, certificate creation/trust, SYS signing, Inf2Cat, CAT signing, verification and cleanup so a timeout identifies the exact operation.

## Verification and evidence

H14 records only metadata:

- certificate subject;
- certificate thumbprint;
- certificate expiration;
- non-exportable key policy marker;
- signed SYS SHA-256;
- signed CAT SHA-256;
- Authenticode verification state;
- target-trust-not-modified marker;
- install-not-executed marker.

The signed SYS, INF, CAT, public CER, generated firmware provider and downloaded
firmware fixture are all temporary.

## Cleanup

Before artifact staging H14 deletes:

- the entire temporary signed package;
- the build-output SYS;
- generated firmware provider;
- firmware fixture;
- temporary public CER.

It removes the certificate thumbprint from all runner stores used by H14:

- CurrentUser\My;
- CurrentUser\Root;
- CurrentUser\TrustedPublisher.

The workflow then verifies those files and certificate entries no longer exist.

## Safety boundary

H14 proves signing mechanics only. It does not:

- export a private key;
- upload SYS, INF, CAT, CER, PFX or P12;
- add any certificate to the Lenovo;
- enable TESTSIGNING;
- disable driver-signature enforcement;
- stage/install a driver;
- bind/unbind or restart DEV_3198;
- execute MMIO on the Lenovo;
- boot the physical DSP;
- program SSP/PDM/codecs/amplifiers;
- expose or play audio.

The next physical-deployment milestone must separately decide the target trust
mechanism and must require the H12/H13 baseline backup and WinRE rollback gates
before installation.
