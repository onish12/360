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


## Verified CI evidence (2026-09-21)

Final H7 source/guard commit:
`6702caa5876a9865db0ce14f3de1a07c6c341011`.
Tree: `488da6b23db4fa300a1b092e5cac2731d715ca73`.

Two intermediate H7 commits exposed guard-script defects only:

- `f6450650`: the guard scanned build-tree `.bin` scratch files and falsely
  treated CMake's compiler ABI probe as committed firmware;
- `3db367f4`: an incremental edit corrupted the PowerShell guard syntax.

Neither failure occurred in the WDK build, source contract, generated provider,
official fixture, CNG pin, PnP lifecycle or boot model. The guard was rewritten
cleanly and restricted to Git-tracked paths.

Final verification:

- WDK/KMDF run `35623749183`, job `106412883362`: real WDK
  compilation passes and all 10 selected host tests pass.
- PnP remains `SOF_PNP_RESOURCES_TESTS=516 PASS`.
- Repeated-D0 integration remains
  `SOF_GLK_BOOT_TESTS=327105 PASS; hardware=NONE`.
- Pinned owner remains `SOF_PINNED_OWNER_TESTS=34 PASS`.
- F4 read-only collector guards remain green.
- H6 owner guard remains green with arbitrary-buffer staging absent.
- H7 static guard reports:
  `H7_FIRMWARE_SOURCE_STATIC_TESTS=PASS;
  build_time_embedded=YES; runtime_file_io=NO;
  arbitrary_buffer_api=NO; deviceadd_autostage=NO;
  firmware_bytes_committed=NO; installable=NO; playback=NO`.
- Windows/Linux offline run `35623754867`: Windows job
  `106412900981` passes all 14 existing tests and Linux job
  `106412900560` passes all 12 tests.
- Windows official reference validation remains PASS:
  `SOF_CNG_PIN_TESTS=10 PASS` and
  `SOF_PINNED_REFERENCE_TESTS=13 PASS`.
- H7 generated-provider report:
  input SHA-256
  `40029b5a05665f19a492ef00b8c0a24c42e90d7c00fc57146e07947fd1407d5c`;
  generated C++ source SHA-256
  `3071e8694b79226ea2837f055097c686773801bea8fa5cf0f0621eda6e61ad75`;
  runtime file I/O `NONE`.
- A one-bit-mutated fixture is rejected by the generator with
  `firmware SHA-256 mismatch; refusing provider generation`.
- The generated provider -> production staging -> owned-copy CNG test reports
  `H7_EMBEDDED_FIRMWARE_TESTS=19 PASS;
  source=GENERATED_BUILD_TIME; runtime_file_io=NONE;
  crypto=REAL_WINDOWS_CNG; hardware=NONE`.
- H7 development artifact
  `PHASER360_M0615H7_EMBEDDED_FIRMWARE_SOURCE`: ID
  `10651555161`, 468,784 bytes; GitHub archive SHA-256
  `72584ad0f562892ae9dbbda2e894c55a8418e937601a96641d3be253c0f0eba2`.
- Windows offline artifact ID `10650465631` contains the H7 metadata report
  but not the generated firmware source or firmware binary; archive SHA-256
  `49a069659e4aba59adadc72cf7ae63840b91a0ec1ef06b1ddcd062312fd12af0`.

H7 is still non-installable and DeviceAdd still does not stage firmware.
No physical Lenovo execution or audio path is authorized.
