"""WiFi: network scan (Command 0x04) and, with WiFi configured in the app, time sync (0x01), update check
(0x02) and, with --internet-update, the installation from the internet (0x03). Needs --wifi."""

import json
import time
import urllib.error
import urllib.request

import pytest

import matrixclock as mc

pytestmark = pytest.mark.wifi

MANIFEST = "https://github.com/golyakoff/Matrix_Clock_ESP32/releases/latest/download/manifest.json"


def scan(clock, timeout=20.0):
    clock.drain(mc.WIFI_NETWORKS)
    clock.command(mc.CMD_SCAN_WIFI)
    start = clock.wait_notify(mc.WIFI_NETWORKS)
    # a repeated command while scanning is accepted and does nothing
    clock.command(mc.CMD_SCAN_WIFI)
    end = clock.wait_notify(mc.WIFI_NETWORKS, lambda d: d[0] in (mc.SCAN_DONE, mc.SCAN_ERROR), timeout)
    return start, end


def test_scan(network):
    mode = network.wifi_settings().mode
    start, end = scan(network)
    assert mc.parse_networks(start) == (mc.SCAN_RUNNING, 0, 0, []), "header only, scanning"
    state, total, first, nets = mc.parse_networks(end)
    assert state == mc.SCAN_DONE, "scan failed (during a WiFi session: retry)"
    assert (first, nets) == (0, []), "notifications carry the header only"

    state, networks = network.networks()
    assert state == mc.SCAN_DONE
    assert len(networks) == total
    assert 0 < total <= 64, "no networks found at all"

    ssids = [n.ssid for n in networks]
    assert all(ssids), "no hidden networks"
    assert len(set(ssids)) == len(ssids), "unique SSIDs"
    rssi = [n.rssi for n in networks]
    assert rssi == sorted(rssi, reverse=True), "strongest first"
    for n in networks:
        assert -127 <= n.rssi < 0, n
        assert n.security in (0, 1, 2, 3), n
        assert 1 <= n.channel <= 14, f"2.4 GHz channels only: {n}"

    if mode == mc.WIFI_OFF:
        assert network.wifi_status().state == mc.STATE_OFF, "the scan does not connect"


def test_scan_paging(network):
    _, end = scan(network)
    if end[0] != mc.SCAN_DONE:
        pytest.skip("scan failed")
    total = end[1]
    # past the end: an empty page
    network.write(mc.WIFI_NETWORKS, bytes([255]))
    assert mc.parse_networks(network.read(mc.WIFI_NETWORKS)) == (mc.SCAN_DONE, total, total, [])
    # every page fits one read
    network.write(mc.WIFI_NETWORKS, b"\x00")
    assert len(network.read(mc.WIFI_NETWORKS)) <= 512
    if total > 1:
        network.write(mc.WIFI_NETWORKS, bytes([total - 1]))
        state, t, first, nets = mc.parse_networks(network.read(mc.WIFI_NETWORKS))
        assert (first, len(nets)) == (total - 1, 1)


@pytest.fixture
def wifi(network):
    s = network.wifi_settings()
    if s.mode == mc.WIFI_OFF:
        pytest.skip("WiFi is off: configure it in the app first")
    return network


def session(clock, command, timeout=90.0):
    """Run a WiFi session by a command; returns the WiFi states it went through"""
    clock.drain(mc.WIFI_STATUS)
    clock.command(command)
    states = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            st = mc.parse_wifi_status(clock.wait_notify(mc.WIFI_STATUS, timeout=deadline - time.monotonic()))
        except TimeoutError:
            break
        states.append(st.state)
        if st.state in (mc.STATE_OFF, mc.STATE_ERROR) and mc.STATE_CONNECTING in states:
            return states
    raise AssertionError(f"the session did not end in {timeout} s: {states}")


def test_sync_time(wifi, keep_time):
    """The time from the internet in the clock's time zone (the PC's clock must be synced too)"""
    tz = wifi.time_zone()
    # off by a day: the sync must fix it
    wifi.set_time(mc.local_now_seconds(tz) - 86400)
    before = time.time()
    states = session(wifi, mc.CMD_SYNC_TIME)
    assert mc.STATE_CONNECTED in states, f"could not connect: {states} (password? the network in range?)"
    st = wifi.wifi_status()
    assert st.synced_at >= int(before) - 1, "no time sync in the session"
    assert abs(st.synced_at - time.time()) < 120
    # the RTC is written at the next second boundary
    time.sleep(1.5)
    diff = wifi.time() - mc.local_now_seconds(tz)
    assert abs(diff) <= 2, f"the clock is {diff:+.1f} s off the PC in {tz}"


def released_version():
    """The version of manifest.json of the latest release, None if the release has none"""
    try:
        with urllib.request.urlopen(MANIFEST, timeout=20) as r:
            return json.load(r)["version"]
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise


def test_check_update(wifi, version):
    wifi.drain(mc.UPDATE_INFO)
    wifi.command(mc.CMD_CHECK_UPDATE)
    deadline = time.monotonic() + 90
    info = None
    while time.monotonic() < deadline:
        info = mc.parse_update_info(wifi.wait_notify(mc.UPDATE_INFO, timeout=deadline - time.monotonic()))
        if info.state not in (mc.UPDATE_UNKNOWN, mc.UPDATE_CHECKING):
            break
    released = released_version()
    if released is None:
        assert info.state == mc.UPDATE_FAILED, "the latest release has no manifest.json"
        return
    assert info.state in (mc.UPDATE_UP_TO_DATE, mc.UPDATE_AVAILABLE), info
    assert info.version == released
    assert abs(info.checked_at - time.time()) < 300
    newer = tuple(map(int, released.lstrip("v").split("."))) > tuple(map(int, version.lstrip("v").split(".")))
    assert info.state == (mc.UPDATE_AVAILABLE if newer else mc.UPDATE_UP_TO_DATE)


@pytest.mark.internet_update
def test_install_from_internet(wifi, log, version):
    """Installs the latest release if it is newer; the clock reboots into it"""
    wifi.drain(mc.OTA_STATUS)
    m = log.mark() if log else 0
    wifi.command(mc.CMD_INSTALL_UPDATE)
    progress = []
    deadline = time.monotonic() + 600
    while time.monotonic() < deadline:
        st = mc.parse_ota_status(wifi.wait_notify(mc.OTA_STATUS, timeout=deadline - time.monotonic()))
        progress.append(st)
        if st.state in (mc.OTA_SUCCESS, mc.OTA_ERROR, mc.OTA_NO_UPDATE):
            break
    last = progress[-1]
    if last.state == mc.OTA_NO_UPDATE:
        assert wifi.update_info().state == mc.UPDATE_UP_TO_DATE
        pytest.skip(f"{version} is the latest release")
    assert last.state == mc.OTA_SUCCESS, f"installation failed: {last}"
    written = [p.written for p in progress if p.state == mc.OTA_RECEIVING]
    assert written == sorted(written), "progress goes forward"
    assert last.written == last.total > 0
    released = wifi.update_info().version
    wifi.wait_gone()
    wifi.reconnect(wait_s=90)
    assert wifi.version() == released
    if log:
        assert not log.crashes(m)
