"""Settings survive a reset (the clock has no reboot command: the reset goes over the serial port).
Needs --reboot and --port."""

import time

import pytest

import matrixclock as mc
from conftest import reboot

pytestmark = pytest.mark.reboot

SETTINGS = (mc.AUTO_BRIGHTNESS, mc.MANUAL_BRIGHTNESS, mc.COLOR_ORDER, mc.ANIMATION_SPLASH,
            mc.HOURLY_BRIGHTNESS, mc.ON_ALARM, mc.OFF_ALARM, mc.AGING_OFFSET)


def test_reset_keeps_settings(clock, log, version):
    orig = {u: clock.read(u) for u in SETTINGS}
    count = clock.u8(mc.ANIMATION_COUNT)
    changed = {
        mc.AUTO_BRIGHTNESS: bytes([orig[mc.AUTO_BRIGHTNESS][0] ^ 1]),
        mc.MANUAL_BRIGHTNESS: bytes([3 if orig[mc.MANUAL_BRIGHTNESS][0] != 3 else 4]),
        mc.COLOR_ORDER: bytes([orig[mc.COLOR_ORDER][0] ^ 1]),
        mc.ANIMATION_SPLASH: mc.splash_value(2, 1, count - 1)
        if orig[mc.ANIMATION_SPLASH] != mc.splash_value(2, 1, count - 1) else mc.splash_value(1, 0, 0),
        mc.HOURLY_BRIGHTNESS: bytes((b + 1) % 16 for b in orig[mc.HOURLY_BRIGHTNESS]),
        mc.ON_ALARM: mc.alarm_value(6, 1, False) if orig[mc.ON_ALARM] != mc.alarm_value(6, 1, False)
        else mc.alarm_value(6, 2, False),
        mc.OFF_ALARM: mc.alarm_value(23, 1, False) if orig[mc.OFF_ALARM] != mc.alarm_value(23, 1, False)
        else mc.alarm_value(23, 2, False),
        mc.AGING_OFFSET: bytes([(orig[mc.AGING_OFFSET][0] + 1) & 0xFF]),
    }
    try:
        for u, v in changed.items():
            clock.write(u, v)
        # the hourly brightness table reaches NVS from the main loop
        time.sleep(1)
        reboot(clock, log)
        assert clock.version() == version
        assert {u: clock.read(u) for u in SETTINGS} == changed
    finally:
        for u, v in orig.items():
            clock.write(u, v)


def test_reset_keeps_network_settings(network, log, keep_time_zone):
    wifi = network.read(mc.WIFI_SETTINGS)
    tz = "CET-1CEST,M3.5.0,M10.5.0/3" if keep_time_zone != "CET-1CEST,M3.5.0,M10.5.0/3" else "MSK-3"
    network.write(mc.TIME_ZONE, tz.encode())
    time.sleep(1)
    reboot(network, log)
    assert network.time_zone() == tz
    assert network.read(mc.WIFI_SETTINGS) == wifi


def test_reset_keeps_time(clock, log, keep_time):
    """The DS3231 keeps the time; Time runs on from it after the reset"""
    start = mc.clock_seconds(2027, 6, 30, 12, 0, 0)
    clock.set_time(start)
    t0 = time.monotonic()
    reboot(clock, log)
    diff = clock.time() - start - (time.monotonic() - t0)
    assert abs(diff) <= 2, f"{diff:+.1f} s off after the reset"


def test_bond_survives_reset(network, log):
    """The pairing is stored: an encrypted write works after the reset without pairing again"""
    reboot(network, log)
    raw = network.read(mc.WIFI_SETTINGS)
    s = mc.parse_wifi_settings(raw)
    network.write(mc.WIFI_SETTINGS, mc.wifi_settings_write(s.mode, s.period, s.ssid))
    assert network.read(mc.WIFI_SETTINGS) == raw
