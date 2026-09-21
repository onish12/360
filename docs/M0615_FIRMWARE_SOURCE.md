# M0.6.15H7 build-time embedded firmware source contract

M0.6.15H7 defines a controlled firmware source without adding kernel runtime
file I/O and without committing or staging the firmware bytes in repository
artifacts.

It remains a non-installable milestone and DeviceAdd still does not auto-stage
firmware.

## Trust chain

The authorized source identity remains the already-reviewed GLK/APL fixture in
`tests/fixtures/sof_glk_reference.json`:

- upstream commit:
  `a973fdba7a3555899ba3e58f89be3c258cf2a166`;
- byte count: `287488`;
- SHA-256:
  `40029b5a05665f19a492ef00b8c0a24c42e90d7c00fc57146e07947fd1407d5c`.

H7 creates two independent authorization barriers.

### Barrier 1: build-time generation

`scripts/generate-embedded-firmware.py` performs no network access.

It accepts an existing fixture only when both the exact byte count and SHA-256
match `sof_glk_reference.json`. It then emits a temporary C++ translation unit
containing a read-only byte array and one provider function:

`GetEmbeddedFirmwareSource(FirmwareSourceView*)`.

The generator refuses a one-bit-mutated fixture before provider generation.

The generated source is written under the CI build directory. It is not added to
the repository and is not copied into the H7 development artifact.

### Barrier 2: runtime owned-copy pin

The embedded provider supplies only a transient kernel-owned view.

`StagePinnedFirmwareFromSource` requires the exact reviewed extent and calls
`PinnedFirmware::Load`.

`PinnedFirmware::Load`:

1. allocates its own nonpaged WDFMEMORY snapshot;
2. copies all 287488 bytes;
3. computes the existing SHA-256 pin with Windows CNG over that owned copy;
4. authorizes the snapshot only when the digest matches.

The source pointer is not retained after the synchronous handoff.

Therefore a build-time provider cannot bypass the runtime pin merely by
returning a pointer and claiming the correct identity.

## DeviceOwner API narrowing

H6 exposed a temporary generic
`StageFirmware(const UCHAR*, SIZE_T)` method.

H7 removes that arbitrary-buffer API.

`DeviceOwner` now exposes only:

`StageEmbeddedFirmware()`.

That method is hard-wired to the generated
`GetEmbeddedFirmwareSource` provider contract.

There is no public path on DeviceOwner for a caller-selected kernel buffer or a
filesystem path.

## No runtime firmware file I/O

H7 adds no:

- `ZwCreateFile`;
- `ZwReadFile`;
- `NtCreateFile`;
- `IoCreateFile`;
- `WdfIoTargetOpen`;
- synchronous WDF file-read path.

Consequently there is no runtime firmware filename, directory search, path
spoofing or file replacement race in the driver-side source contract.

## DeviceAdd remains fail-closed

`Phaser360EvtDeviceAdd` does not call `StageEmbeddedFirmware`.

This separation is intentional. H7 validates the source contract before a later
milestone connects it to the device initialization path.

Until that connection is explicitly reviewed, a real D0Entry through the H6
owner still reaches an unloaded `PinnedFirmware` and fails closed before DSP
boot.

## Official-fixture CI

The Windows offline workflow first performs the existing reference validation.
Only after that succeeds does H7:

1. run the offline generator on the verified fixture;
2. create a one-bit-mutated copy and require generator rejection;
3. reconfigure the existing CMake build with the generated provider path;
4. compile `phaser360_embedded_firmware_tests`;
5. call the generated provider;
6. verify the provider bytes using real Windows CNG;
7. stage those bytes through the production
   `StagePinnedFirmwareFromSource -> PinnedFirmware::Load` path;
8. require owned-copy load/release semantics.

The generated firmware C++ file and original firmware binary are not copied into
the downloadable development artifact. Only
`H7_EMBEDDED_FIRMWARE_REPORT.json` is staged.

## Static safety guards

The H7 WDK workflow also requires:

- runtime file-I/O APIs absent from the source contract and DeviceOwner;
- generator contains no network client;
- arbitrary DeviceOwner firmware-buffer API absent;
- DeviceAdd autostaging absent;
- no committed `.ri` or generated embedded-firmware source;
- WDK project remains `StaticLibrary`;
- `INSTALLABLE=FALSE`;
- `AUDIO_PLAYBACK=NOT_IMPLEMENTED`.

## Still absent

H7 does not add:

- provider autostaging from DeviceAdd;
- SYS linkage/package;
- INF binding;
- signing/install scripts;
- physical Lenovo DSP boot;
- SSP1/MAX98357A programming;
- SSP2/DA7219 programming;
- WaveRT/ACX audio endpoints;
- audio playback.

A later milestone may connect this already-verified source to DeviceAdd, but the
first physical package must still remain DSP-boot/IPC/shutdown only with all
audio output paths disabled.
