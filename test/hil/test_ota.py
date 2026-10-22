"""Firmware upload over BLE. Needs --firmware (installed at the end: use the build you want on the clock
afterwards). Tests run in file order: failures first (after each one the clock shows ERROR and reboots
in 10 s into the same firmware), then the rollback (with --port), the real update last."""

import time

import pytest

import matrixclock as mc

pytestmark = pytest.mark.ota

APP_SLOT = {4: 0x1E0000}  # partitions/4MB.csv


@pytest.fixture(scope="module")
def firmware(request, clock):
    with open(request.config.getoption("--firmware"), "rb") as f:
        data = f.read()
    assert data[0] == 0xE9, "not an ESP application image (use the app .bin, not the merged one)"
    # esp_app_desc_t follows the image header (24 bytes) and the first segment header (8 bytes)
    assert int.from_bytes(data[32:36], "little") == 0xABCD5432, "no application description"
    version = data[48:80].split(b"\0")[0].decode()
    slot = APP_SLOT.get(clock.u8(mc.FLASH_SIZE))
    assert slot is None or len(data) <= slot, "the image does not fit the app slot of this board"
    return data, version


def start(clock, size):
    clock.write(mc.OTA_CONTROL, bytes([mc.OTA_START]) + mc.u32(size))


def send(clock, data, limit=None):
    chunk = min(clock.mtu - 3, 512)
    end = len(data) if limit is None else min(limit, len(data))
    for pos in range(0, end, chunk):
        clock.write(mc.OTA_DATA, data[pos:min(pos + chunk, end)])


def expect_failed_reboot(clock, log, mark, version):
    """A failed upload: the clock reboots into the same firmware"""
    clock.wait_gone(timeout=30)
    if log:
        log.wait_for(r"OTA FAILED", mark, timeout=5)
        log.wait_for(log.BOOT, mark, timeout=10)
    clock.reconnect()
    assert clock.version() == version


def test_abort(clock, log, version, firmware):
    data, _ = firmware
    m = log.mark() if log else 0
    start(clock, len(data))
    send(clock, data, 64 * 1024)
    clock.write(mc.OTA_CONTROL, bytes([mc.OTA_ABORT]))
    expect_failed_reboot(clock, log, m, version)


def test_not_an_image(clock, log, version, firmware):
    data, _ = firmware
    garbage = bytes([0x00]) + data[1:]
    m = log.mark() if log else 0
    start(clock, len(garbage))
    try:
        send(clock, garbage, 16 * 1024)
    except Exception:  # noqa: BLE001 - the clock may already be rebooting
        pass
    expect_failed_reboot(clock, log, m, version)


def test_short_image(clock, log, version, firmware):
    data, _ = firmware
    m = log.mark() if log else 0
    start(clock, len(data))
    send(clock, data, len(data) // 4)
    clock.write(mc.OTA_CONTROL, bytes([mc.OTA_END]))
    expect_failed_reboot(clock, log, m, version)


def test_disconnect_aborts(clock, log, version, firmware):
    """The phone goes away mid-upload: the clock gives up and reboots instead of waiting with a frozen
    display (regression of 2.0.0 and older)"""
    data, _ = firmware
    m = log.mark() if log else 0
    start(clock, len(data))
    send(clock, data, 32 * 1024)
    clock.disconnect()
    if log:
        log.wait_for(log.BOOT, m, timeout=30)
    else:
        time.sleep(15)
    clock.reconnect()
    assert clock.version() == version


def upload(clock, data):
    t0 = time.monotonic()
    start(clock, len(data))
    send(clock, data)
    clock.write(mc.OTA_CONTROL, bytes([mc.OTA_END]))
    speed = len(data) / (time.monotonic() - t0) / 1024
    print(f"\nuploaded {len(data)} bytes, {speed:.1f} KB/s")
    clock.wait_gone(timeout=30)


def test_rollback(clock, log, version, firmware):
    """A new firmware reset before it is confirmed (30 s) is rolled back by the bootloader"""
    if not log:
        pytest.skip("needs --port: the reset and the boot partition")
    data, _ = firmware
    m = log.mark()
    log.reset()
    old = log.boot_partition(m)
    clock.reconnect()

    m = log.mark()
    upload(clock, data)
    new = log.boot_partition(m)
    assert new != old, "the update boots from the other app partition"
    time.sleep(3)
    m = log.mark()
    log.reset()
    assert log.boot_partition(m) == old, "rolled back to the previous firmware"
    clock.reconnect()
    assert clock.version() == version


def test_install(clock, log, firmware):
    """The real update; the clock keeps it (confirmed after 30 s)"""
    data, new_version = firmware
    m = log.mark() if log else 0
    upload(clock, data)
    clock.reconnect(wait_s=90)
    assert clock.version() == new_version
    if log:
        assert not log.crashes(m)
        partition = log.boot_partition(m)
        time.sleep(35)
        m = log.mark()
        log.reset()
        assert log.boot_partition(m) == partition, "the firmware was confirmed: no rollback"
        clock.reconnect()
        assert clock.version() == new_version
