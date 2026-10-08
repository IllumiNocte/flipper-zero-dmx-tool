# Flipper Zero DMX Tool

[![FAP build](https://github.com/IllumiNocte/flipper-zero-dmx-tool/actions/workflows/build.yml/badge.svg)](https://github.com/IllumiNocte/flipper-zero-dmx-tool/actions/workflows/build.yml)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD--3--Clause-blue.svg)](LICENSE)

A portable DMX512 sender, receiver, and timing analyzer for Flipper Zero.

The project currently targets Unleashed firmware `unlshd-090` / API 88.2. It
started as a direct 3.3 V UART companion for the IllumiNocte RP2040 DMX test
fixture and is being developed toward a protected RS-485 interface for real
DMX networks.

> [!WARNING]
> Never connect a Flipper GPIO directly to an RS-485 DMX line. Direct wiring in
> this repository is only for 3.3 V logic-level test fixtures. Real DMX needs an
> appropriate RS-485 transceiver and protection circuitry.

## Features

### Sender

- continuous DMX output on Flipper pin 13 at 40 frames per second;
- 512 stored channel values with selectable four- or six-fader banks;
- freely adjustable bank start address and transmitted slot count;
- long Left/Right bank navigation;
- non-destructive blackout and clear-all action;
- persistent channel values, bank position, width, and frame size;
- independently timed output thread with RP2040-verified Break, MAB, and frame period.

### Receiver

- USART1 input on pin 14 at 250 kbaud, 8N2;
- start code, slot count, and all received channels in four-channel banks;
- double-buffered reception for full 512-slot frames;
- noise, overrun, and parity-error counters.

### Analyzer

- frames per second and frame counter;
- Break, Mark-After-Break, and frame-period measurements;
- short-Break and short-MAB counters;
- separate UART error count.

Timing analysis uses the same logic signal on pin 2 / PA7 while pin 14 handles
UART decoding. This is intentionally a compact field analyzer, not calibrated
laboratory or standards-conformance equipment.

## Controls

Use Left/Right to move between `TX`, `RX`, `Ana`, and `Setup`.

### TX

- short OK: select the next fader;
- Up/Down: change the selected value; hold for steps of five;
- long OK: start or stop continuous output;
- long Left/Right: move to the previous or next channel bank.

The title shows the current address range and `ON`, `OFF`, or `BO` for blackout.

### RX

- Up/Down: move through received channels in banks of four.

### Setup

- short OK: select the next setting;
- Up/Down: edit start address, bank width, frame size, or blackout;
- on `Clear all 512 values`, short Up or Down clears every stored value.

Settings and channel values are saved when the app exits normally. TX output
and blackout intentionally start disabled after reopening the app.

## Direct-UART test wiring

### Receive and analyze an RP2040 signal

| Flipper Zero | RP2040 tester |
|---|---|
| Pin 14 / USART RX | Port 1 TX / GPIO0 or port 2 TX / GPIO4 |
| Pin 2 / PA7 | Same selected TX signal |
| GND | GND |

Pin 2 is optional when only UART decoding is needed.

### Test Flipper output

| Flipper Zero | RP2040 tester |
|---|---|
| Pin 13 / USART TX | Port 1 RX / GPIO1 or port 2 RX / GPIO5 |
| GND | GND |

Do not connect two active TX outputs together.

## Building

Python 3.8 or newer is required. On Windows PowerShell:

```powershell
.\setup.ps1
.\build.ps1
```

The resulting app is written to `dist/dmx_tool.fap`. To build, upload, and
launch it on a USB-connected Flipper:

```powershell
$env:UFBT_HOME = Join-Path (Get-Location) ".ufbt"
.\.venv\Scripts\ufbt.exe launch
```

The setup script uses a project-local virtual environment and pins the SDK to
the Unleashed 090 release so the generated FAP matches firmware API 88.2.

GitHub Actions performs the same build and publishes the FAP as a workflow
artifact for every push and pull request.

## Validation

The sender has been tested by the IllumiNocte RP2040 DMX analyzer using its
independent 4 MHz PIO/DMA capture. A representative four-slot run produced:

- 400 complete frames in 10 seconds;
- 24.932–25.068 ms frame periods;
- 110.75–111.00 us Breaks;
- 15.00–15.25 us MABs;
- zero framing errors and zero short MABs across ten capture windows.

These are finite digital logic-level measurements. They do not qualify an
RS-485 transmitter, isolation, termination, protection, or behavior on a live
DMX installation.

## Status and roadmap

This is an early development release. The current direct-UART implementation
is useful for development and fixture testing. Planned work includes:

- an external protected RS-485 interface with direction control;
- richer receiver navigation and changed-channel highlighting;
- analyzer min/average/max statistics, pause, and reset;
- optional scenes and RDM support.

## License

BSD 3-Clause License. See [LICENSE](LICENSE).

Copyright © 2026 IllumiNocte.
