"""Who the clock lets in (firmware v2.1.0+) and which connections it drops (v2.1.1+).

Only a bonded phone gets past the firmware version; a new phone pairs only while the pairing window is
open (PAIRING_WINDOW_S after power on or after a 5 s touch of the pad). The pairing tests make the PC
forget its pairing and pair again (--pairing, --port: a reset opens the window); they leave the PC
paired, as it was."""

import time

import pytest

import matrixclock as mc
from conftest import pair_in_window


def other_connection(clock, pair, subscribe):
    """A second client to the same clock; the session one is disconnected first (one connection), and
    unsubscribed, as the app does: the PC's address and keys are the same, so the clock would restore
    the session's subscriptions on the other connection"""
    if clock.connected:
        clock.unsubscribe()
    clock.disconnect()
    c = mc.Clock(clock.address)
    c.connect(pair=pair, subscribe=subscribe)
    return c


def back_to_session(clock, other):
    other.close()
    clock.reconnect()


def reset_and_wait_closed_window(log):
    """Reset the clock and wait until the pairing window it opens at power on is closed"""
    m = log.mark()
    log.reset()
    log.wait_for(log.BOOT, m, timeout=10)
    time.sleep(mc.PAIRING_WINDOW_S + 10)


def expect_log(log, pattern, mark, timeout=15):
    """Check the clock's message, if it logs info messages: release builds log errors only"""
    if log and log.verbose(mark):
        log.wait_for(pattern, mark, timeout=timeout)


def test_version_is_open(clock, version):
    """The one characteristic open without pairing: the app reads it to know whether to pair"""
    assert mc.VERSION_RX.match(version)


@pytest.mark.slow
def test_subscribed_connection_kept(clock):
    """An app waiting for notifications is not idle: the clock keeps the link past the idle timeout"""
    time.sleep(mc.IDLE_TIMEOUT_S + 20)
    assert clock.connected
    assert clock.let_in()


@pytest.mark.slow
def test_idle_connection_dropped(clock, log):
    """A connection with no reads, writes or subscriptions is dropped, so the clock advertises again
    (Android keeps the link to a bonded clock open after the app is gone). It is the bonded PC, so the
    connection is let in: the idle check is not the pairing timeout"""
    m = log.mark() if log else 0
    idle = other_connection(clock, pair=False, subscribe=False)
    try:
        assert idle.let_in()
        idle.wait_gone(timeout=mc.IDLE_TIMEOUT_S + 30)
        expect_log(log, r"Connection idle for \d+ s, disconnecting", m)
        assert mc.find_clock() is not None, "the clock advertises again"
    finally:
        back_to_session(clock, idle)


@pytest.mark.pairing
def test_unpaired_phone_dropped(clock, log):
    """A phone that does not pair is dropped after AUTH_TIMEOUT_S; until then it reads the version only"""
    clock.disconnect()
    clock.unpair()
    m = log.mark()
    stranger = other_connection(clock, pair=False, subscribe=False)
    try:
        assert mc.VERSION_RX.match(stranger.version())
        stranger.wait_gone(timeout=mc.AUTH_TIMEOUT_S + 15)
        expect_log(log, r"did not pair in \d+ s, disconnecting", m)
    finally:
        stranger.close()
        pair_in_window(clock, log)


@pytest.mark.pairing
def test_pairing_rejected_outside_window(clock, log):
    """Outside the window the clock drops a new phone's keys and the phone. The PC can only forget its
    keys, not its address, so the clock still has a bond for it: the case of a bonded phone that forgot
    its keys and pairs anew, which the clock tells from the old bond by the key"""
    clock.disconnect()
    reset_and_wait_closed_window(log)
    clock.unpair()
    m = log.mark()
    stranger = mc.Clock(clock.address)
    try:
        try:
            stranger.connect(pair=True, subscribe=False)
        except Exception:  # noqa: BLE001 - the clock may drop the link during the pairing
            pass
        assert not stranger.let_in()
        expect_log(log, r"Pairing rejected, the pairing window is closed", m)
    finally:
        stranger.close()
        pair_in_window(clock, log)


@pytest.mark.pairing
def test_pairing_inside_window(clock, log):
    """Within the window after power on a new phone pairs, and the clock says so"""
    m = log.mark()
    pair_in_window(clock, log)
    expect_log(log, r"New phone paired", m)


@pytest.mark.manual
def test_long_touch_opens_window(clock, log, capsys, keep_show):
    """Holding the pad for 5 s opens the pairing window and says so on the display; it does not toggle
    the display (a short touch does, on release)"""
    clock.drain(mc.SHOW)
    show = clock.u8(mc.SHOW)
    m = log.mark() if log else 0
    with capsys.disabled():
        print("\n>>> hold the touch pad for 6 seconds, then let go", flush=True)
    time.sleep(10)
    assert clock.u8(mc.SHOW) == show, "a long touch must not toggle the display"
    expect_log(log, r"Touch: pairing window opened", m)
    expect_log(log, r"Pairing window is open", m)
    with capsys.disabled():
        answer = input(">>> does the clock show 'Bluetooth pairing / 2 min'? [y/n] ").strip().lower()
    assert answer.startswith("y")
