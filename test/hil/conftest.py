"""
Hardware-in-the-loop tests: a real clock, the PC plays the phone over BLE.
See README.md in this directory.
"""

import time

import pytest

import matrixclock as mc


def pytest_addoption(parser):
    g = parser.getgroup("matrixclock")
    g.addoption("--clock", help="BLE address of the clock (default: the first advertising Matrix Clock)")
    g.addoption("--port", help="serial port of the clock, e.g. /dev/ttyUSB0: crash and reboot checks, "
                               "resets for --reboot and the rollback test")
    g.addoption("--firmware", help="firmware .bin for the BLE upload tests, installed at the end "
                                   "(build/release_4mb/tetris-clock.bin or a release *_fw.bin)")
    g.addoption("--reboot", action="store_true", help="also run the tests that reset the clock (needs --port)")
    g.addoption("--wifi", action="store_true", help="also run the tests that use WiFi: scan, and with WiFi "
                                                    "configured in the app, time sync and update check")
    g.addoption("--internet-update", action="store_true",
                help="also install the latest release from the internet if it is newer (needs --wifi)")
    g.addoption("--manual", action="store_true", help="also run the tests that need a person at the clock "
                                                      "(touch pad); run with -s")
    g.addoption("--pairing", action="store_true", help="also run the pairing window tests: they make the PC "
                                                       "forget its pairing and pair again (needs --port)")
    g.addoption("--slow", action="store_true", help="also run the tests that wait for the idle timeout (5 min each)")


MARKS = {
    "reboot": "resets the clock (--reboot, --port)",
    "ota": "uploads firmware over BLE (--firmware)",
    "wifi": "uses WiFi (--wifi)",
    "internet_update": "installs a release from the internet (--internet-update)",
    "manual": "needs a person at the clock (--manual)",
    "pairing": "forgets and redoes the PC's pairing (--pairing, --port)",
    "slow": "waits minutes (--slow)",
}


def pytest_configure(config):
    for name, text in MARKS.items():
        config.addinivalue_line("markers", f"{name}: {text}")


def pytest_collection_modifyitems(config, items):
    port = config.getoption("--port")
    skip = {
        "reboot": None if config.getoption("--reboot") and port else "needs --reboot and --port",
        "ota": None if config.getoption("--firmware") else "needs --firmware",
        "wifi": None if config.getoption("--wifi") else "needs --wifi",
        "internet_update": None if config.getoption("--internet-update") else "needs --internet-update",
        "manual": None if config.getoption("--manual") else "needs --manual",
        "pairing": None if config.getoption("--pairing") and port else "needs --pairing and --port",
        "slow": None if config.getoption("--slow") else "needs --slow",
    }
    for item in items:
        for mark, reason in skip.items():
            if reason and mark in item.keywords:
                item.add_marker(pytest.mark.skip(reason=reason))


@pytest.fixture(scope="session")
def log(request):
    """Serial log of the clock, None without --port"""
    port = request.config.getoption("--port")
    if not port:
        yield None
        return
    s = mc.SerialLog(port)
    yield s
    s.close()


@pytest.fixture(scope="session")
def clock(request, log):
    address = request.config.getoption("--clock")
    if not address:
        address = mc.find_clock()
        if not address:
            pytest.exit("no advertising Matrix Clock found (is the phone app connected? the clock takes one "
                        "connection); pass --clock ADDRESS", returncode=2)
    c = mc.Clock(address)
    # not subscribed yet: the subscriptions fail on a link the clock does not let in
    c.connect(subscribe=False)
    if c.let_in():
        c.subscribe()
    elif log:
        # Firmware v2.1.0+ pairs a new phone only while its pairing window is open
        pair_in_window(c, log)
    else:
        c.close()
        pytest.exit("the clock does not let the PC in: open its pairing window (hold the touch pad for "
                    "5 s, or power it on) and run the tests within 2 minutes; or pass --port", returncode=2)
    yield c
    c.close()


def pair_in_window(clock, log):
    """Reset the clock, which opens its pairing window, and pair again from scratch"""
    clock.disconnect()
    clock.unpair()
    m = log.mark()
    log.reset()
    log.wait_for(log.BOOT, m, timeout=10)
    clock.reconnect(pair=True)
    assert clock.let_in(), "the clock does not let the PC in although its pairing window is open"


@pytest.fixture(scope="session")
def version(clock):
    return clock.version()


@pytest.fixture
def network(clock):
    """The clock with the network characteristics (firmware v2.0.0+)"""
    if not clock.network:
        pytest.skip("no network characteristics (firmware < 2.0.0)")
    return clock


@pytest.fixture
def keep_time_zone(network):
    tz = network.time_zone()
    yield tz
    network.write(mc.TIME_ZONE, tz.encode())


@pytest.fixture
def keep_time(clock):
    """Set the clock to the PC's local time after the test, as the phone app does"""
    yield
    clock.set_time(mc.local_now_seconds())


@pytest.fixture
def keep_show(clock):
    show = clock.u8(mc.SHOW)
    yield show
    clock.write(mc.SHOW, bytes([show]))


def reboot(clock, log):
    """Reset the clock over the serial port (no BLE command for it) and connect again"""
    m = log.mark()
    log.reset()
    log.wait_for(log.BOOT, m, timeout=10)
    clock.reconnect()
    assert not log.crashes(m)
    return m


@pytest.fixture(autouse=True)
def no_crash(request, log):
    """Fail a test during which the firmware crashed or rebooted unexpectedly"""
    if not log:
        yield
        return
    m = log.mark()
    yield
    time.sleep(0.2)
    crashes = log.crashes(m)
    assert not crashes, "firmware crash:\n" + "\n".join(crashes)
    if not ({"reboot", "ota", "internet_update", "pairing"} & set(request.keywords)):
        boots = log.find(log.BOOT, m)
        assert not boots, "unexpected reboot:\n" + "\n".join(boots)
