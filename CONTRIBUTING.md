# Contributing

Issues and pull requests are welcome. Please keep changes focused and explain
the hardware and firmware version used for testing.

Before opening a pull request:

1. Run `.\setup.ps1` once on Windows if the local uFBT environment is missing.
2. Run `.\build.ps1` and confirm the FAP passes `APPCHK` for target 7, API 88.2.
3. Run `ufbt format` and `ufbt lint`.
4. Describe any direct-UART or RS-485 hardware used during validation.

Do not claim DMX512 electrical or timing compliance from a finite logic-level
test. Never connect unprotected Flipper GPIOs directly to an RS-485 bus.
