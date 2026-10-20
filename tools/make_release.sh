#!/usr/bin/env bash
# Packs PlatformIO build outputs into GitHub release assets and writes the
# release page text. Run from the repo root after `pio run` for every env:
#
#   tools/make_release.sh v1.9.0
#
# Produces in $OUT_DIR (default: release/), for each variant V:
#   tetris-clock_<tag>_V_bootloader.bin   0x1000   second-stage bootloader
#   tetris-clock_<tag>_V_pt.bin           0x8000   partition table
#   tetris-clock_<tag>_boot_app0.bin      0xe000   otadata: boot from app0 (shared)
#   tetris-clock_<tag>_V_fw.bin           0x10000  application (also used for BLE OTA)
#   tetris-clock_<tag>_V_full.bin         0x0      all of the above merged into one image
#   flash_V.bat                                    Windows flashing script
#   tetris-clock_<tag>_V.zip                       the separate images + flash_V.bat
# and the release page markdown in $NOTES_FILE (default: release_notes.md).
#
# Requires: python with esptool >= 5 (`pip install esptool`).
set -euo pipefail

TAG="${1:?usage: $0 <tag>}"
BUILD_DIR="${BUILD_DIR:-.pio/build}"
OUT="${OUT_DIR:-release}"
NOTES="${NOTES_FILE:-release_notes.md}"
PYTHON="${PYTHON:-python}"
PREFIX="tetris-clock_${TAG}"
BUILD_DATE="$(git log -1 --format=%cI 2>/dev/null || date -Iseconds)"

# boot_app0.bin ships with the Arduino-ESP32 framework package
BOOT_APP0="${BOOT_APP0:-$(find ~/.platformio/packages -path '*framework-arduinoespressif32*/tools/partitions/boot_app0.bin' 2>/dev/null | head -n1)}"
[ -f "$BOOT_APP0" ] || { echo "boot_app0.bin not found (set BOOT_APP0=...)" >&2; exit 1; }

# variant | PlatformIO env | flash size
VARIANTS=(
  "debug_4mb|esp32_4mb_debug|4MB"
  "release_4mb|esp32_4mb|4MB"
  "release_16mb|esp32_16mb|16MB"
)

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$BOOT_APP0" "$OUT/${PREFIX}_boot_app0.bin"

# Writes flash_<variant>.bat (CRLF line endings, cmd.exe syntax)
write_bat() {
  local v="$1" size="$2" bat="$OUT/flash_$1.bat"
  cat > "$bat" <<EOF
@echo off
rem Tetris Clock ${TAG} (${v}, ${size} flash) - flash an ESP32 over USB with esptool.
rem Usage: flash_${v}.bat [COMx]     (no port given = auto-detect)
rem Keep this file in the same folder as the .bin files.
cd /d "%~dp0"

set BAUD=921600
set PORTARG=
if not "%~1"=="" set PORTARG=--port %~1

python --version >nul 2>&1 || (
  echo Python not found. Install it from https://www.python.org/downloads/
  echo and tick "Add python.exe to PATH" in the installer.
  goto :fail
)
python -m esptool version >nul 2>&1 || (
  echo esptool not found, installing it with pip...
  python -m pip install --upgrade esptool || goto :fail
)

rem 0x1000 bootloader, 0x8000 partition table, 0xe000 otadata (boot app0), 0x10000 application.
rem The nvs partition (0x9000, clock settings) is not touched.
python -m esptool --chip esp32 %PORTARG% --baud %BAUD% write-flash --flash-mode dio --flash-size ${size} ^
  0x1000  ${PREFIX}_${v}_bootloader.bin ^
  0x8000  ${PREFIX}_${v}_pt.bin ^
  0xe000  ${PREFIX}_boot_app0.bin ^
  0x10000 ${PREFIX}_${v}_fw.bin
if errorlevel 1 goto :fail

echo.
echo === DONE: the clock restarts and waits for a BLE connection ===
pause
exit /b 0

:fail
echo.
echo === FLASH FAILED ===
echo - Stuck at "Connecting...": hold the BOOT button on the board until writing starts.
echo - Write errors: lower the speed, edit "set BAUD=460800" (or 115200) in this file.
echo - No COM port: install the USB-UART driver (CP210x or CH340) and pass the port, e.g. flash_${v}.bat COM4
pause
exit /b 1
EOF
  sed -i 's/$/\r/' "$bat"
}

for entry in "${VARIANTS[@]}"; do
  IFS='|' read -r v env size <<< "$entry"
  src="$BUILD_DIR/$env"
  for f in bootloader.bin partitions.bin firmware.bin; do
    [ -f "$src/$f" ] || { echo "missing $src/$f (run: pio run -e $env)" >&2; exit 1; }
  done

  cp "$src/bootloader.bin" "$OUT/${PREFIX}_${v}_bootloader.bin"
  cp "$src/partitions.bin" "$OUT/${PREFIX}_${v}_pt.bin"
  cp "$src/firmware.bin"   "$OUT/${PREFIX}_${v}_fw.bin"

  # Single image flashed at 0x0; gaps (incl. nvs) are filled with 0xFF
  "$PYTHON" -m esptool --chip esp32 merge-bin -o "$OUT/${PREFIX}_${v}_full.bin" \
    --flash-mode dio --flash-size "$size" \
    0x1000  "$OUT/${PREFIX}_${v}_bootloader.bin" \
    0x8000  "$OUT/${PREFIX}_${v}_pt.bin" \
    0xe000  "$OUT/${PREFIX}_boot_app0.bin" \
    0x10000 "$OUT/${PREFIX}_${v}_fw.bin"

  write_bat "$v" "$size"

  (cd "$OUT" && "$PYTHON" -m zipfile -c "${PREFIX}_${v}.zip" \
    "flash_${v}.bat" \
    "${PREFIX}_${v}_bootloader.bin" \
    "${PREFIX}_${v}_pt.bin" \
    "${PREFIX}_boot_app0.bin" \
    "${PREFIX}_${v}_fw.bin")
done

# ---------------------------------------------------------------- release page
P="$PREFIX"
cat > "$NOTES" <<EOF
# Tetris Clock ESP32 Firmware

**Version:** ${TAG}
**Build date:** ${BUILD_DATE}

## Which files do I need?

| Board flash | Build | Download |
|---|---|---|
| 4MB  | 🚀 Release (optimized)    | \`${P}_release_4mb.zip\` |
| 16MB | 🚀 Release (optimized)    | \`${P}_release_16mb.zip\` |
| 4MB  | 🐞 Debug (serial logging) | \`${P}_debug_4mb.zip\` |

Each zip contains everything needed for a first install over USB: the four flash
images and a ready-to-run \`flash_<variant>.bat\`. Not sure about the flash size?
\`python -m esptool flash-id\` prints it (\`Detected flash size: ...\`).

Already running the clock? Update over BLE from the Android app instead — it uses
the \`*_fw.bin\` file and keeps your settings.

## First install on Windows (from scratch)

1. **Install Python 3** from https://www.python.org/downloads/ — in the installer tick
   **"Add python.exe to PATH"**.
2. **Install esptool** — open a command prompt (\`Win+R\` → \`cmd\`) and run:
   \`\`\`bat
   python -m pip install --upgrade esptool
   python -m esptool version
   \`\`\`
   The second command must print the esptool version (5.x).
3. **Connect the board** with a USB data cable. It should appear in Device Manager
   under *Ports (COM & LPT)*, e.g. *Silicon Labs CP210x USB to UART Bridge (COM4)*.
   No port → install the CP210x or CH340 driver (depends on the USB chip on your board).
4. **Download and unpack** the zip for your board, e.g. \`${P}_release_16mb.zip\`.
5. **Run** \`flash_release_16mb.bat\` (double-click), or from the command prompt pass the
   port explicitly: \`flash_release_16mb.bat COM4\`.
   If it hangs at \`Connecting...\`, hold the **BOOT** button on the board until writing starts.

After \`=== DONE ===\` the clock restarts and waits for a BLE connection from the app.

This is the \`flash_release_16mb.bat\` from the 16MB zip (the other zips contain the
same script with their own file names and flash size):

\`\`\`bat
$(tr -d '\r' < "$OUT/flash_release_16mb.bat")
\`\`\`

## Linux / macOS

\`\`\`bash
python3 -m pip install --upgrade esptool
python3 -m esptool --chip esp32 --baud 921600 write-flash --flash-mode dio --flash-size 16MB \\
  0x1000  ${P}_release_16mb_bootloader.bin \\
  0x8000  ${P}_release_16mb_pt.bin \\
  0xe000  ${P}_boot_app0.bin \\
  0x10000 ${P}_release_16mb_fw.bin
\`\`\`

For a 4MB board use the \`release_4mb\` (or \`debug_4mb\`) files and \`--flash-size 4MB\`.
Add \`--port /dev/ttyUSB0\` (Linux) or \`--port /dev/cu.usbserial-XXXX\` (macOS) if the
port is not detected automatically.

## Single-file image

\`${P}_<variant>_full.bin\` is all four images merged into one, written at address **0x0**:

\`\`\`bash
python -m esptool --chip esp32 --baud 921600 write-flash 0x0 ${P}_release_16mb_full.bin
\`\`\`

It also works with browser-based flashers such as https://espressif.github.io/esptool-js/
(offset \`0x0\`). ⚠️ The merged image overwrites the \`nvs\` partition, so the clock's
settings are reset — use it for a clean install, not for updates.

## Flash map

| Address | File | Content |
|---|---|---|
| \`0x1000\`  | \`*_bootloader.bin\` | Second-stage bootloader |
| \`0x8000\`  | \`*_pt.bin\`         | Partition table |
| \`0x9000\`  | —                    | \`nvs\`: clock settings (not written by the .bat) |
| \`0xe000\`  | \`*_boot_app0.bin\`  | \`otadata\`: boot from \`app0\` |
| \`0x10000\` | \`*_fw.bin\`         | Application (\`app0\`) |

\`*_fw.bin\` is the application only — never write it at \`0x1000\`. If the serial log
(115200 baud) shows \`invalid header: 0xffffffff\` in a loop, the bootloader is missing:
reflash with the \`.bat\` (or the full image).

## Notes
- **Debug versions** include serial logging and debug features
- **Production versions** are optimized for size and performance
- Use the files of one variant together — don't mix 4MB and 16MB images
EOF

echo "=== Release assets in $OUT ==="
ls -la "$OUT"
echo "=== Release notes: $NOTES ==="
