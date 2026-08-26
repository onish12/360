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
