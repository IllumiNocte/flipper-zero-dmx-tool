# Changelog

All notable changes to this project are documented here.

## [0.5.0] - 2026-10-08

### Added

- 512-channel sender workspace with freely selectable start address.
- Four- and six-fader bank layouts.
- Configurable transmitted slot count.
- Non-destructive blackout and clear-all controls.
- Persistent sender settings and channel values.

## [0.4.2] - 2026-10-08

### Fixed

- Long OK no longer advances repeatedly through selected TX channels.

## [0.4.1] - 2026-10-08

### Changed

- Moved DMX output into an independent 25 ms timing thread.
- Reduced measured MAB from about 58 us to about 15 us.
- Stabilized output at 40 frames per second.

## [0.4.0] - 2026-10-08

### Added

- Four-channel graphical sender.
- Full 512-channel receive buffering and bank navigation.

## [0.3.0] - 2026-10-08

### Added

- Break, MAB, and frame-period measurement on pin 2 / PA7.
- Timing-error counters.

## [0.2.1] - 2026-10-08

### Added

- Live DMX UART reception, four-channel display, and FPS measurement.
- Unleashed 090 / firmware API 88.2 compatibility.
