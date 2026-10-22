"""Display and RTC settings: round trips. Every test restores what it changes."""

import time

import pytest

import matrixclock as mc


def roundtrip(clock, uuid, values, wait=0.0):
    orig = clock.read(uuid)
    try:
        for v in values:
            clock.write(uuid, v)
            if wait:
                time.sleep(wait)
            assert clock.read(uuid) == v, f"{uuid}: wrote {v.hex()}"
    finally:
        clock.write(uuid, orig)
    assert clock.read(uuid) == orig


def test_show(clock, keep_show):
    for v in (0, 1, 0, 1):
        clock.write(mc.SHOW, bytes([v]))
        assert clock.u8(mc.SHOW) == v


def test_show_rejects(clock, keep_show):
    clock.write(mc.SHOW, b"\x02")
    assert clock.u8(mc.SHOW) == keep_show


def test_auto_brightness(clock):
    roundtrip(clock, mc.AUTO_BRIGHTNESS, [b"\x00", b"\x01"])


def test_manual_brightness(clock):
    roundtrip(clock, mc.MANUAL_BRIGHTNESS, [bytes([v]) for v in (0, 15, 7)])


def test_color_order(clock):
    roundtrip(clock, mc.COLOR_ORDER, [b"\x01", b"\x00"])


def test_color_order_rejects(clock):
    orig = clock.read(mc.COLOR_ORDER)
    clock.write(mc.COLOR_ORDER, b"\x02")
    assert clock.read(mc.COLOR_ORDER) == orig


def test_animation_splash(clock):
    count = clock.u8(mc.ANIMATION_COUNT)
    values = [mc.splash_value(m, d, i) for m, d, i in ((0, 0, 0), (3, 3, count - 1), (1, 2, count // 2))]
    roundtrip(clock, mc.ANIMATION_SPLASH, values)


def test_animation_splash_one_byte(clock):
    """A write of one byte (apps before v1.8.0) means index bit 3 = 0"""
    count = clock.u8(mc.ANIMATION_COUNT)
    if count <= 8:
        pytest.skip("8 animations or less")
    orig = clock.read(mc.ANIMATION_SPLASH)
    try:
        clock.write(mc.ANIMATION_SPLASH, mc.splash_value(2, 1, 9))
        clock.write(mc.ANIMATION_SPLASH, mc.splash_value(2, 1, 9)[:1])
        assert mc.parse_splash(clock.read(mc.ANIMATION_SPLASH)) == (2, 1, 1)
    finally:
        clock.write(mc.ANIMATION_SPLASH, orig)


def test_animation_splash_preview_not_stored(clock):
    orig = clock.read(mc.ANIMATION_SPLASH)
    mode, duration, index = mc.parse_splash(orig)
    try:
        clock.write(mc.ANIMATION_SPLASH, mc.splash_value(mode, duration, index, preview=True))
        assert clock.read(mc.ANIMATION_SPLASH) == orig
    finally:
        clock.write(mc.ANIMATION_SPLASH, orig)


def test_hourly_brightness(clock):
    table = bytes(range(16)) + bytes(range(8))
    roundtrip(clock, mc.HOURLY_BRIGHTNESS, [table, bytes(reversed(table))])


def test_hourly_brightness_rejects(clock):
    orig = clock.read(mc.HOURLY_BRIGHTNESS)
    assert len(orig) == 24
    for bad in (orig[:23], orig + b"\x00"):
        clock.write(mc.HOURLY_BRIGHTNESS, bytes(15 - b for b in bad))
        assert clock.read(mc.HOURLY_BRIGHTNESS) == orig, f"{len(bad)} bytes"


@pytest.mark.parametrize("uuid", [mc.ON_ALARM, mc.OFF_ALARM], ids=["on", "off"])
def test_alarm(clock, uuid):
    values = [mc.alarm_value(h, m, a) for h, m, a in ((0, 0, False), (23, 59, True), (7, 30, False))]
    roundtrip(clock, uuid, values)
    h, m, _ = mc.parse_alarm(clock.read(uuid))
    assert 0 <= h <= 23 and 0 <= m <= 59


def test_alarms_independent(clock):
    on, off = clock.read(mc.ON_ALARM), clock.read(mc.OFF_ALARM)
    try:
        clock.write(mc.ON_ALARM, mc.alarm_value(6, 15, False))
        clock.write(mc.OFF_ALARM, mc.alarm_value(22, 45, False))
        assert mc.parse_alarm(clock.read(mc.ON_ALARM)) == (6, 15, False)
        assert mc.parse_alarm(clock.read(mc.OFF_ALARM)) == (22, 45, False)
    finally:
        clock.write(mc.ON_ALARM, on)
        clock.write(mc.OFF_ALARM, off)


def test_aging_offset(clock):
    roundtrip(clock, mc.AGING_OFFSET, [bytes([v & 0xFF]) for v in (-128, 127, -5, 0)])
