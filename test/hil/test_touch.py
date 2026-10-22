"""Touch pad: a person touches it when asked. Needs --manual and -s (the prompts)."""

import time

import pytest

import matrixclock as mc

pytestmark = pytest.mark.manual


def ask(capsys, text):
    with capsys.disabled():
        print(f"\n>>> {text}", flush=True)


def test_touch_toggles_once(clock, capsys, keep_show):
    """Each touch toggles the display exactly once and is reported within a moment; holding the finger
    and letting go is one event, not two (the release used to toggle back)"""
    for n in range(1, 4):
        clock.drain(mc.SHOW)
        before = clock.u8(mc.SHOW)
        ask(capsys, f"[{n}/3] touch the pad once, hold the finger for a second, let go")
        first = clock.wait_notify(mc.SHOW, timeout=30)
        assert first[0] == before ^ 1, "the display toggles"
        more = clock.notifications(mc.SHOW, 2.5)
        assert not more, f"one touch, {1 + len(more)} toggles"
        assert clock.u8(mc.SHOW) == before ^ 1


def test_touch_latency(clock, capsys, keep_show):
    """The display reacts to the touch at once. A person judges it: the test asks"""
    ask(capsys, "touch the pad 5 times at a steady pace, watching the display")
    events = []
    clock.drain(mc.SHOW)
    deadline = time.monotonic() + 30
    while len(events) < 5 and time.monotonic() < deadline:
        try:
            clock.wait_notify(mc.SHOW, timeout=deadline - time.monotonic())
            events.append(time.monotonic())
        except TimeoutError:
            break
    assert len(events) == 5, f"5 touches, {len(events)} toggles"
    with capsys.disabled():
        answer = input(">>> did the display switch right at each touch? [y/n] ").strip().lower()
    assert answer.startswith("y"), "the display lags behind the touch"
