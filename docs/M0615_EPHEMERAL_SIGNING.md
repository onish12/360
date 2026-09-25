# M0.6.15H14.3 certutil root-trust signing gate

H14.3 keeps the deterministic H14.1 tool paths. H14.1 timed out in Import-Certificate and H14.2 proved that direct X509Store.Add to the Root store also blocks on this hosted runner. H14.3 therefore uses the Windows certutil command-line path to add the ephemeral public certificate to the disposable runner machine Root store, then removes it by thumbprint before artifact staging.

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

A public CER copy is exported temporarily only for `certutil -f -addstore Root`. TrustedPublisher is no longer modified because SignTool `/pa` verification of the test-signed SYS/CAT requires a trusted root; no additional publisher-store mutation is needed for this CI gate.

This trust modification exists only inside the disposable CI runner. It is not
a target-side trust procedure.

## H14.1/H14.2 timeout evidence and H14.3 change

H14.1 completed deterministic tool discovery and certificate creation, then timed out in the certificate trust step. H14.2 narrowed this further: `H14_CERT_ROOT_ADD_BEGIN` was emitted and `H14_CERT_ROOT_ADD_END` was not, proving the block occurs in the Root-store add operation itself. H14.3 replaces both PowerShell PKI import and X509Store root insertion with `certutil -f -addstore Root`, and uses `certutil -delstore Root <thumbprint>` for explicit cleanup.

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
