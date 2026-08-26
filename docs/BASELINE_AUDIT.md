# Target baseline audit

This document records the project facts used to constrain development.

## Stable pre-experiment state

The earlier Windows audio baseline used Intel SST `9.22.0.4832` for the controller and OED. Windows booted, but the OED failed to start and there was no working internal audio path.

## Rejected Intel experiment

A later Intel SST `9.22.0.4883` set became bound to the controller/OED. DA7219 and MAX98357A still had no usable Windows codec binding and no internal audio endpoint was produced. When the Intel audio kernel services were disabled offline, Windows booted again. Therefore 4883 is rejected as a development base for this project.

## Development constraint

The free implementation must not depend on proprietary `csaudiointcsof`. The allowed reference boundary is the publicly documented/open-source `sklhdaudbus` interface plus public SOF/Linux/codec sources.
