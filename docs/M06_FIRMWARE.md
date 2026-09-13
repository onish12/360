# M0.6: SOF firmware parser — OFFLINE ONLY

This is real source for a prerequisite of the future SOF loader. It is **not an
audio driver or a loader**. No INF/SYS, signing certificate, installation script,
MMIO, DMA, DSP power transition, IPC, codec access or audio output is added here.
The M0.5 kernel source is unchanged. Its legacy installer is separately blocked
because it assumed the retired Intel SST 4883 / oem0.inf Windows baseline.

## What is implemented

`src/sof/firmware_image.cpp` is a C++17 byte parser without allocation, exceptions,
OS dependencies or hardware callbacks. It reads fields byte-by-byte rather than
dereferencing potentially unaligned packed structures. Errors clear the result.
Its offset/count arithmetic is bounded before it touches a field.

The supported **structural profile** is the legacy APL/GLK v1.8 container:

- optional unsigned `XMan` 1.x header and bounded, aligned metadata elements;
- `$CPD` v1 directory with three known entries, checksum and disjoint file ranges;
- type-4 CSS header, RSA-2048 field lengths and bounded CSS extensions;
- 96-byte ADSP metadata with the expected descriptor base;
- `$AM1` descriptor at payload offset `0x2000`, preload size and module bounds;
- `$AME` module/config tables, file-backed segment bounds and virtual-address overflow;
- distinction between file-backed text/rodata and BSS with no file contents.

Caps are 16 MiB per image, 64 KiB of extended metadata, 128 modules and 1,024
configuration records. Other formats (including `$AE1`, CSE v2.5/IPC4 profiles)
are deliberately rejected, not guessed. Unknown XMan/CSS element types are
bounds-checked but not semantically interpreted. The strict preload rule is
specific to this reviewed profile, not a universal SOF format rule.

### What PASS does NOT mean

The parser does **not** verify RSA signatures, signed component/module hashes,
metadata semantics, permitted DSP memory maps or module entry-point permissions.
It does not prove IPC ABI, topology, codec-clock or Windows compatibility.
It does not prove the target will boot the image. A byte mutation inside a code
segment can still pass the structural parser; the separate fixture SHA-256 check
will reject a changed regression image. A future loader needs its own reviewed
authentication and platform gates before consuming any of these offsets.

`sof_inspect` therefore always reports `signature_verification=NOT_PERFORMED`,
`hardware_access=NONE` and `installable=false`, including on success.

## Reproducible upstream input

`tests/fixtures/sof_glk_reference.json` fixes the URL, commit, byte count, SHA-256
and expected decoded fields. It is an **offline regression fixture**, not a claim
that this is the newest firmware or a deployment recommendation.

At sof-bin commit `a973fdba7a3555899ba3e58f89be3c258cf2a166`, directory
`v1.9.x/sof-v1.9.3` resolves `sof-glk.ri` through `intel-signed/sof-glk.ri` to
`intel-signed/sof-apl.ri`. The shared image is 287,488 bytes, with SHA-256
`40029b5a05665f19a492ef00b8c0a24c42e90d7c00fc57146e07947fd1407d5c`.
The directory version and embedded `$AM1` version differ: the latter is `1.9.0.1`.
The parser reports the embedded value rather than inventing one from a filename.

Expected output: 768 unsigned metadata bytes; 286,720 payload bytes; 70 preload
pages; two modules (`BRNGUP`, `BASEFW`); four file-backed segments. These values
were cross-checked against upstream `sof_ri_info.py` at commit
`e0386e19109c5ae0ff0614223b9cba111a9d926b`.

The firmware is fetched explicitly for CI tests, not committed or put in the
downloadable development artifacts. Its upstream license remains applicable.
No proprietary `csaudiointcsof` source or binary is used.

## Tests and developer commands

No administrator rights are needed. These commands run ordinary user-mode code.

```sh
cmake -S . -B /path/outside/repo/m06 -DCMAKE_BUILD_TYPE=Debug
cmake --build /path/outside/repo/m06 --config Debug
ctest --test-dir /path/outside/repo/m06 -C Debug --output-on-failure
```

An out-of-tree directory outside the repository also avoids inheriting the
separate WDK `Directory.Build.props/targets` from an existing driver build.
On GCC/Clang, add `-DPHASER_SOF_SANITIZERS=ON` for ASan/UBSan. CMake's Windows
multi-config build puts the executable under `Debug/`.

```sh
python scripts/verify-sof-reference.py --inspector /path/to/phaser360_sof_inspect --fixture /path/to/sof-apl.ri --download
```

Without `--download`, no network request is made. Existing fixture files are
never silently replaced. The hash is checked before the inspector executes.

Unit tests cover all truncated prefixes of a synthetic image, malformed sizes,
checksums, entry overlaps, unknown formats, configuration bounds, BSS handling,
unaligned input, error-result clearing and 20,000 deterministic mutations.
These tests are not an emulator of the DSP or proof of Windows driver safety.
The workflow builds with warnings as errors on Linux/GCC and Windows/MSVC;
Linux additionally runs ASan/UBSan. Local ptrace-based environments may require
`ASAN_OPTIONS=detect_leaks=0` for execution; CI does not disable leak checking.

## Next hardware boundary

Known target remains PHASER360 / PCI `8086:3198`, DA7219 on SSP2, MAX98357A on
SSP1 and PDM microphones. Reinstalling Windows does not turn a CI result into a
hardware result. No M0.5 runtime snapshot from the reinstalled system has been
supplied in this continuation. The old M0.5 installer must NOT be used to obtain
one: it requires Intel 9.22.0.4883 / oem0.inf and its rollback restores that stack.
New M0.5 builds stop at both runtime entry points before any system operation.
A separately reviewed fresh-Windows runtime/recovery path is required before
collecting a new snapshot and before approving a hardware-write milestone. Do
not reinstall Intel SST 9.22.0.4883 or disable signature checks for the old package.

Still missing for sound: reviewed DSP boot/firmware authentication and transport,
IPC3 handshake, board topology/clock programming, safe amplifier gating, Windows
WaveRT endpoints, and physical runtime/recovery tests. M0.6 does not waive any
rule in `docs/SAFETY.md` and contains no command that disables signature checks.

## Primary layout references

- [Linux v6.12 extended manifest definitions](https://github.com/torvalds/linux/blob/v6.12/include/sound/sof/ext_manifest.h)
- [Linux Gemini Lake firmware selection](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/pci-apl.c)
- [rimage CSE directory definitions](https://github.com/thesofproject/rimage/blob/9e0796058424e7b4491fa174a7d2f1b258c8e30b/src/include/rimage/cse.h)
- [rimage manifest v1.8 offset](https://github.com/thesofproject/rimage/blob/9e0796058424e7b4491fa174a7d2f1b258c8e30b/src/include/rimage/manifest.h)
- [rimage module, segment and firmware header definitions](https://github.com/thesofproject/rimage/blob/9e0796058424e7b4491fa174a7d2f1b258c8e30b/src/include/rimage/sof/user/manifest.h)
- [SOF reference inspector](https://github.com/thesofproject/sof/blob/e0386e19109c5ae0ff0614223b9cba111a9d926b/tools/sof_ri_info/sof_ri_info.py)
- [SOF platform coverage warning](https://github.com/thesofproject/sof-bin/blob/a973fdba7a3555899ba3e58f89be3c258cf2a166/README.md)
- [Upstream firmware license](https://github.com/thesofproject/sof-bin/blob/a973fdba7a3555899ba3e58f89be3c258cf2a166/LICENCE.Intel)

The new parser is an independent MIT-licensed implementation of these published
byte layouts; upstream implementation code and binaries are not vendored.
