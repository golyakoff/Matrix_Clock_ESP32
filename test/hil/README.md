# Hardware-in-the-loop tests

Tests against a real clock: the PC plays the phone app over BLE (the protocol of
[main/include/ble.h](../../main/include/ble.h)) and, optionally, reads the clock's log over USB. They
check what cannot be checked without the board: the BLE protocol on the real firmware, settings
storage, the time keeping, WiFi, the firmware update and the rollback.

## Setup

- A PC with Bluetooth LE. Linux with BlueZ is the main target (`bluetoothctl` must see the adapter);
  macOS and Windows work with [bleak](https://github.com/hbldh/bleak) too, but pairing and the
  serial reset are tested on Linux only.
- The clock with the firmware to test, preferably connected over USB (`--port`). The clock takes one
  BLE connection: close the app on the phone (or turn the phone's Bluetooth off) first.
- Python 3.10+:

```bash
python3 -m venv ~/.venvs/matrixclock-hil
~/.venvs/matrixclock-hil/bin/pip install -r test/hil/requirements.txt
```

## Running

```bash
cd test/hil
~/.venvs/matrixclock-hil/bin/pytest --port /dev/ttyUSB0
```

Close `idf.py monitor` first: the serial port can be opened only once.

Options:

| Option | Meaning |
|--------|---------|
| `--port PORT` | Serial port of the clock. Its log is checked for crashes and unexpected reboots after every test; needed for `--reboot` and the rollback test |
| `--clock ADDRESS` | BLE address of the clock; default: the first advertising Matrix Clock |
| `--reboot` | Also run the tests that reset the clock (settings, time and pairing survive a reset); needs `--port`, the clock has no reboot command |
| `--wifi` | Also run the WiFi tests: network scan; with WiFi configured in the app also time sync from the internet and the update check |
| `--internet-update` | With `--wifi`: install the latest release from the internet if it is newer than the clock's firmware |
| `--firmware FILE` | Also run the BLE upload tests and **install** `FILE` at the end: abort, not an image, a short image, disconnect, rollback (`--port`), then the real update. Use the application image: `build/release_4mb/tetris-clock.bin` or a release `*_fw.bin` |
| `--manual` | Also run the touch pad tests: a person touches the pad when asked. Run with `-s` |

Everything:

```bash
pytest -s --port /dev/ttyUSB0 --reboot --wifi --manual --firmware ../../build/release_4mb/tetris-clock.bin
```

One test: `pytest test_time.py::test_new_year --port /dev/ttyUSB0 -v`.

The rollback test needs a clock whose bootloader has the rollback (flashed over USB with v2.0.0 or
newer: the bootloader is not updated over the air) and the boot messages of the bootloader in the
log (`Loaded app from partition at offset ...`, on by default). The firmware reports errors only in
release builds; the debug build (`tools/build.sh debug_4mb`) logs everything, which helps to find out
why a test fails.

## Pairing

Writing the WiFi settings needs an encrypted link, so the tests pair with the clock ("Just Works", no
PIN) when they connect. The clock keeps a few bonds (NimBLE: 3 by default), the PC takes one of
them. To pair again from scratch: `bluetoothctl remove <address>`.

## What the tests change

The tests restore what they change: display on/off, brightness, color order, Animation Splash, hourly
brightness, alarms, aging offset, time zone. Exceptions:

- the clock's time is set to the PC's local time at the end of the time tests (as the app does);
- the WiFi settings are never changed: the tests write only invalid values, which the clock must
  ignore (`test_bond_survives_reset` writes them back unchanged);
- `--reboot` resets the clock;
- `--firmware` installs the given firmware, `--internet-update` the latest release.

## Files

- `matrixclock.py` - the protocol client (synchronous wrapper over bleak) and the serial log reader;
  usable from a Python shell for manual experiments:

  ```python
  import matrixclock as mc
  clock = mc.Clock(mc.find_clock()); clock.connect()
  clock.version(); clock.time_zone(); clock.wifi_status()
  clock.write(mc.MANUAL_BRIGHTNESS, bytes([15]))
  ```

- `conftest.py` - options and fixtures;
- `test_basics.py` - characteristics, version, flash size, temperature;
- `test_control.py` - display and RTC settings: round trips and invalid values;
- `test_time.py` - the clock's time, the date rollovers, the time zone must not shift it;
- `test_network.py` - time zone, WiFi settings validation, status values, commands with WiFi off;
- `test_wifi.py` - network scan, time sync, update check and install from the internet (`--wifi`);
- `test_reboot.py` - persistence over a reset (`--reboot`);
- `test_ota.py` - firmware upload over BLE and rollback (`--firmware`);
- `test_touch.py` - the touch pad (`--manual`).

Host tests of hardware independent code are in [../host](../host): `make -C test/host`.
