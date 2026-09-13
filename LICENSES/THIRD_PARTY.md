# Third-party notices

The M0.1 compatibility declaration references the public interface layout from:

- `coolstar/sklhdaudbus`, BSD-3-Clause: https://github.com/coolstar/sklhdaudbus

No third-party binaries or proprietary `csaudiointcsof` source are included in this bootstrap.

Future use of `da7219`, `max98357a`, SOF, or Linux source must preserve the license applicable to each upstream component. Code should not be copied across incompatible licenses without an explicit licensing review.

M0.6's independent MIT byte parser uses published SOF/rimage layout facts, with
the exact source references in `docs/M06_FIRMWARE.md`. No upstream implementation
is vendored. CI optionally downloads one pinned `sof-bin` firmware solely as a
regression input. It is not redistributed in this repository or CI artifacts;
the manifest links to upstream `LICENCE.Intel`.
