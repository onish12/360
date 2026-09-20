# M0.6.12 owned, SHA-256-pinned firmware snapshot

PinnedFirmware copies the complete reference image into device-parented
NonPagedPoolNx memory, hashes that copy with Windows CNG, and exposes it only
through a synchronous ColdPower entry method. A rejected input never reaches
that entry method. No caller-selected manifest/payload split or ABI limit is
accepted by this new path.

## Primary sources reviewed before implementation

- [Microsoft: WdfMemoryCreate and ownership](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfmemory/nf-wdfmemory-wdfmemorycreate)
- [Microsoft: BCryptOpenAlgorithmProvider](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptopenalgorithmprovider)
- [Microsoft: BCryptCreateHash](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptcreatehash)
- [Microsoft: BCryptHashData](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcrypthashdata)
- [Microsoft: BCryptFinishHash](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptfinishhash)
- [Microsoft: BCryptDestroyHash](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptdestroyhash)

The implementation uses Microsoft's SHA-256 primitive provider and the
Open/Create/HashData/Finish/Destroy/Close sequence at PASSIVE_LEVEL. CNG owns
its hash-object allocation. Kernel consumers link Cng.lib; native test programs
link Bcrypt.lib. The same hashing source compiles against real WDK headers and
runs against Windows user-mode CNG in CI. No custom cryptographic algorithm is
implemented, and no injected hash callback exists in the production API.

## Exact identity policy

The pin matches the existing regression reference in
`tests/fixtures/sof_glk_reference.json`:

- Upstream commit: `a973fdba7a3555899ba3e58f89be3c258cf2a166`.
- Path: `v1.9.x/sof-v1.9.3/intel-signed/sof-apl.ri`.
- Whole image: 287,488 bytes.
- SHA-256: `40029b5a05665f19a492ef00b8c0a24c42e90d7c00fc57146e07947fd1407d5c`.
- XMan: first 768 bytes; payload: following 286,720 bytes; maximum ABI minor: 20.

Those fixed boundaries come from the existing parsed reference. The entire
image hash binds them; the kernel path does not parse arbitrary unpinned images.
The offline parser continues to validate the actual upstream fixture in CI.
[Upstream license](https://github.com/thesofproject/sof-bin/blob/a973fdba7a3555899ba3e58f89be3c258cf2a166/LICENCE.Intel).
Firmware bytes are not included in the development artifact.

This is an exact file-identity allowlist, not RSA signature-chain verification,
revocation policy or approval to deploy this firmware. The reference remains
untested on the Lenovo. A new image requires an explicit source/pin review;
there is no runtime configuration that substitutes another hash.

## Lifetime and entry contract

Load requires a valid readable kernel buffer, an alive WDF device and serialized
PASSIVE execution. It is not a user-pointer probing or IOCTL implementation.
It allocates and fills a private snapshot before hashing. Invalid size,
allocation failure, CNG failure or digest mismatch leaves no usable image.
There are no publicly exposed snapshot pointers or memory handles.

Enter requires the same WDF device used by Load. It derives both input ranges
from the owned image and calls ColdPower.Enter. Reentrant release is refused
while that call is executing. Callers must serialize all operations; the
entering flag is not a substitute for thread synchronization. The original
caller buffer can change after Load without changing the verified snapshot.

After Enter returns, the existing boot code has copied firmware into owned DMA
memory and parsed IPC windows into value fields. It retains neither input
pointer. Release can therefore free the snapshot independently of later DMA
cleanup. It does not stop or free DMA. A retained snapshot may be used for later
sessions after the existing ColdPower.NextD0/teardown requirements are met.

Release invalidates entry and deletes the memory object. It must precede device
parent destruction; no automatic destructor attempts WDF cleanup. Underlying
low-level ColdPower/GlkBoot APIs still retain their caller-trust contract. The
future PnP driver must select this verified path; no PnP entry point is registered
by this milestone.

## Verification scope

Ownership fault tests use a simulated hash result to cover failure and lifetime
branches. They check allocation/hash rejection, wrong device/IRQL, duplicate
load, source mutation, reentrant release, boot-failure propagation and release.
Local GCC ASan/UBSan: 30 assertions pass; LeakSanitizer disabled for ptrace.

Separate Windows tests execute the real production CNG sequence against the
upstream fixture and reject mutations in XMan and payload. A combined test
executes the real hash and snapshot owner, changes the original input, verifies
the entry still receives the pinned owned copy and rejects a corrupted load.
Its boot callback and WDF allocation remain simulated. Neither real CNG testing
nor WDK compilation is kernel execution or hardware compatibility validation.

PnP/removal recovery, platform IRQ routing, machine/codec configuration, stream
DMA and WaveRT remain incomplete. No installer or audio output is supplied.
