# Architecture

```text
Windows Audio Engine / PortCls-WaveRT       (later milestones)
                |
        PHASER360 SOF backend
                |
        GUID_ADSP_BUS_INTERFACE v1
                |
           sklhdaudbus
                |
      Intel Gemini Lake DEV_3198
                |
      +---------+----------+
      |                    |
    SSP1                 SSP2 / PDM
      |                    |
 MAX98357A             DA7219 / mics
 speakers              headset
```

## Public bus contract

`sklhdaudbus` exposes an ADSP child device and a version-1 driver-defined interface identified by:

`{752A2CAE-3455-4D18-A184-8B34B22632CE}`

The public interface contains the standard `INTERFACE` header, `CtlrDevId`, and callbacks for resources, DSP power, interrupt registration, render/capture streams, DMA preparation/cleanup, trigger, stream position and SPIB.

M0.1 intentionally consumes only the standard header plus `CtlrDevId`. Function pointers are stored but never called.

## PHASER360 routing target

The verified target routing for later milestones is:

- SSP1 render -> MAX98357A -> internal speakers.
- SSP2 render/capture -> DA7219 -> headphone/headset.
- PDM/DMIC -> internal microphone path.

No routing code exists in M0.1.
