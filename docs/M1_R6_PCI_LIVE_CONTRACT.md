# M1 R6 — bounded live PCI contract after physical R5 H80 failure

## Physical R5 evidence — Lenovo Onix12/Onyx12, 2026-09-25

Physical result archive:
`RESULT_M1_FAST_SAFE_20260925_223039_437edc5f.zip`

Archive SHA-256:
`54b5b116675b70dc2c2e23fa5b927f859b156c1170cde2b5d1f094a0ae645e52`

All 23 entries listed in `SHA256SUMS.txt` independently re-hashed
successfully.

The one-shot transaction bound `Phaser360M1` as `oem29.inf`,
version `0.6.15.134`, without a reboot request. The device subsequently
reported Code 10 because D0 entry failed. Automatic rollback returned the
target exactly to `IntcAudioBus` / `oem14.inf` / `9.22.0.4832`,
Status OK / ProblemCode 0, and removed the temporary PHASER360 trust.

ETW captured six identical start attempts. Every attempt reached:

- H20 PCI capture OK;
- H30 HDA controller initialization OK;
- H40 boot stream selection OK;
- H50 DMA stage OK;
- H60 DMA publish OK;
- H70 stream configuration OK;
- H80 PCI policy FAIL with `STATUS_DEVICE_CONFIGURATION_ERROR
  (0xC0000182)`.

Therefore R5 physically closes the previous R4 HDA-capability blocker.
The first unresolved stage is the pre-firmware PCI policy gate.

## R5 policy defect

H20 captures and validates the exact conventional 256-byte PCI configuration
image. R5 then initializes HDA, resets the controller, discovers/enables the
Processing Pipe, prepares a stream and programs the boot-stream transport.
Immediately before the bounded 0x44/0x48 policy writes, R5 required the entire
live 256-byte PCI image to remain byte-identical to the H20 snapshot.

That is stronger than the ownership requirement. PCI status bits and
capability control/status payloads are not owned by this policy and can change
without changing the identity of the PCI function or the two dwords the policy
is about to modify. Treating any such unrelated change as identity drift can
reject an otherwise valid live device.

## R6 live contract

R6 retains fail-closed validation immediately before the first PCI write, but
narrows it to the state needed to prove the same target and the same owned
write contract:

1. vendor/device, type-0 header and first capability pointer must match;
2. PCI command, class/revision, BARs, subsystem identity and interrupt pin
   must match the H20 evidence;
3. the conventional capability chain must have the same capability IDs and
   next-link structure;
4. PGCTL at 0x44 and CGCTL at 0x48 must match the H20 snapshot exactly;
5. only CGCTL bit 1 is cleared and PGCTL bit 2 is set;
6. each owned write is read back;
7. dirty state is set before every SetBusData call so a short/partial write is
   conservatively restored;
8. shutdown still restores only the owned bits to their captured values.

Changes to capability IDs/links, resource identity, or either owned dword still
fail before any policy write.

## R6 ETW diagnostics

The shared PCI policy object remains trace-provider agnostic because it is also
linked by the older H15D/H15H drivers. It exposes a bounded failure enum to its
caller. Only the M1 HDA transport translates that diagnostic into ETW.

R6 keeps `H80_PCI_POLICY_ENTER` / `H80_PCI_POLICY_OK|FAIL` and adds one
specific failure event before H80 FAIL:

- H81 state/gate precondition;
- H82 evidence validation;
- H83 BUS_INTERFACE_STANDARD query;
- H84 BUS_INTERFACE_STANDARD shape;
- H85 live config read;
- H86 identity drift;
- H87 stable-header drift;
- H88 capability-structure drift;
- H89 owned-dword drift;
- H8A gate closed before writes;
- H8B/H8C/H8D CGCTL write/gate/readback failure;
- H8E/H8F/H8G PGCTL write/gate/readback failure.

This preserves the isolated H15 driver link contract while making the next
physical M1 failure directly classifiable.

No audio playback, codec programming, speaker enable, BCD write or automatic
reboot is introduced by R6.
