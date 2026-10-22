"""The clock's time: the Time and Time string characteristics. The clock keeps local time; the Time
value is its calendar fields counted as UTC seconds, independently of the clock's time zone.
Every test sets the clock to the PC's local time at the end."""

import struct
import time

import pytest

import matrixclock as mc


def set_and_follow(clock, seconds, follow_s):
    """Set the time, return (seconds since the write, Time value) of the notifications for follow_s"""
    clock.drain(mc.TIME)
    t0 = time.monotonic()
    clock.set_time(seconds)
    result = []
    end = t0 + follow_s
    while time.monotonic() < end:
        try:
            d = clock.wait_notify(mc.TIME, timeout=max(0.1, end - time.monotonic()))
        except TimeoutError:
            break
        result.append((time.monotonic() - t0, struct.unpack("<I", d)[0]))
    return result


def assert_runs_from(values, start):
    """The clock counts from `start` second by second"""
    assert values, "no Time notifications (they come every second while connected)"
    for elapsed, v in values:
        assert -1 <= v - start - elapsed <= 1.5, \
            f"{elapsed:.1f} s after setting {mc.time_string(start)} the clock is at {mc.time_string(v)}"


def test_time_notifies_every_second(clock):
    values = [struct.unpack("<I", d)[0] for d in clock.notifications(mc.TIME, 3.5)]
    assert len(values) >= 3
    assert all(b - a == 1 for a, b in zip(values, values[1:])), values


def test_set_time(clock, keep_time):
    start = mc.clock_seconds(2027, 3, 14, 15, 9, 26)
    assert_runs_from(set_and_follow(clock, start, 3), start)


def test_time_string(clock, keep_time):
    start = mc.clock_seconds(2027, 3, 14, 15, 9, 26)
    clock.set_time(start)
    clock.drain(mc.TIME_STR)
    clock.drain(mc.TIME)
    s = clock.wait_notify(mc.TIME_STR, timeout=3).rstrip(b"\0").decode()
    v = struct.unpack("<I", clock.wait_notify(mc.TIME, timeout=3))[0]
    # the month is 1-based (0-based before firmware v2.0.0)
    assert s.startswith("2027-03-14 15:09:"), s
    assert abs(int(s[-2:]) - mc.clock_fields(v)[5]) <= 1


def test_new_year(clock, keep_time):
    start = mc.clock_seconds(2029, 12, 31, 23, 59, 58)
    values = set_and_follow(clock, start, 4)
    assert_runs_from(values, start)
    assert mc.clock_fields(values[-1][1])[:3] == (2030, 1, 1)


def test_leap_day(clock, keep_time):
    start = mc.clock_seconds(2028, 2, 28, 23, 59, 58)
    values = set_and_follow(clock, start, 4)
    assert_runs_from(values, start)
    assert mc.clock_fields(values[-1][1])[:3] == (2028, 2, 29)


@pytest.mark.parametrize("tz", ["MSK-3", "CET-1CEST,M3.5.0,M10.5.0/3", "EST5EDT,M3.2.0,M11.1.0"])
def test_time_zone_does_not_shift_time(network, keep_time_zone, keep_time, tz):
    """The time zone converts only the time from the internet; the clock's time and its Time value stay
    as written (regression of 2.0.0: mktime() shifted both by the zone and by summer time)"""
    network.write(mc.TIME_ZONE, tz.encode())
    for start in (mc.clock_seconds(2026, 7, 15, 11, 59, 57),   # summer time
                  mc.clock_seconds(2026, 1, 15, 11, 59, 57),   # winter time
                  mc.clock_seconds(2026, 3, 29, 1, 59, 58),    # the hour CET springs over
                  mc.clock_seconds(2026, 10, 25, 2, 59, 58)):  # the hour CET repeats
        values = set_and_follow(network, start, 3.5)
        assert_runs_from(values, start)
