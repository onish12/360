# Safety policy

Audio DSP development can hang the OS and incorrect amplifier/clock programming can damage speakers. The repository therefore uses staged safety gates.

## M0.1 restrictions

The probe may query `GUID_ADSP_BUS_INTERFACE` only. It must not call any returned hardware callback.

The source has an explicit compile-time switch:

`PHASER_ENABLE_HARDWARE_CALLS=0`

M0.1 must fail compilation if changed to a non-zero value without source changes in the gated section.

## Future hardware-write milestones

Before enabling any MMIO/DSP/codec write:

1. capture the exact pre-test DriverStore/service state;
2. create offline WinRE disable instructions for the experimental driver;
3. require a manual build flag;
4. keep amplifier enable separate from DSP boot;
5. start with muted/zeroed output buffers;
6. cap initial playback duration and gain;
7. never automatically reboot after driver installation.


## Project-wide no-playback hold — 2026-09-20

This rule applies to every later milestone until it is explicitly replaced by a
reviewed speaker-output safety milestone.

- DSP boot, firmware, IPC, PnP/power, removal and recovery tests must remain
  non-playback tests. A successful DSP boot is not permission to produce sound.
- No code path may enable the speaker amplifier, intentionally unmute a render
  path, publish non-zero speaker PCM, or create an automatic playback test while
  this hold is active.
- The first physical M1 loader test must leave the speaker/amplifier path
  disabled. Headphone, speaker and microphone endpoint work remains later.
- Speaker enable must remain a separate, default-OFF gate from DSP boot. Unknown,
  missing or contradictory board/topology/clock/gain data must fail closed.
- Before any first speaker output, verify the exact amplifier identity and board
  wiring, enable/shutdown polarity, clock and sample format, channel mapping,
  speaker electrical limits, codec/amplifier power sequence, mute/unmute sequence
  and a conservative gain policy from primary hardware evidence. Do not guess
  numeric gain, duration or power limits.
- Before any first speaker output, add deterministic tests for wrong format,
  wrong clock, wrong channel map, failed mute, failed shutdown, partial power
  transitions, suspend/resume, surprise removal and recovery. Required Windows,
  Linux sanitizer and real-WDK builds must all be green for the exact source.
- The initial output buffer, when that later milestone is eventually authorized,
  must be explicitly zeroed/muted before any amplifier enable. Non-zero audio
  requires a distinct reviewed step after the mute/power state is confirmed.
- No automated retry may re-enable an amplifier after a fault, timeout, removal,
  resume anomaly or uncertain hardware state.
- A first physical output test is not performed merely because CI passes. It
  requires a separately reviewed hardware/recovery procedure and an explicit
  statement of the verified electrical and software limits being used.

These restrictions are intentionally stricter than the generic future-milestone
rules above because a software error in a digital amplifier path can damage the
internal speakers.
