"""Network characteristics (firmware v2.0.0+) without using the radio: time zone, WiFi settings
validation, status values, commands while WiFi is off. The WiFi settings are never changed: only
invalid writes, which the clock must ignore."""

import ipaddress
import re
import time

import pytest

import matrixclock as mc


def test_time_zone(network, keep_time_zone):
    for tz in ("CET-1CEST,M3.5.0,M10.5.0/3", "<+0530>-5:30", "UTC0", "X" * 47):
        network.write(mc.TIME_ZONE, tz.encode())
        assert network.time_zone() == tz


def test_time_zone_rejects(network, keep_time_zone):
    network.write(mc.TIME_ZONE, b"X" * 48)
    assert network.time_zone() == keep_time_zone


DEFAULT_NAME = re.compile(r"^Tetris Clock [0-9A-F]{4}$")


@pytest.fixture
def keep_device_name(network):
    if not network.has(mc.DEVICE_NAME):
        pytest.skip("no device name characteristic")
    name = network.read(mc.DEVICE_NAME)
    yield name.decode("utf-8")
    # the default name is not stored: write it back only if a custom one was set
    network.write(mc.DEVICE_NAME, b"" if DEFAULT_NAME.match(name.decode("utf-8")) else name)


def test_device_name(network, keep_device_name):
    for name in ("Kitchen", "Часы кухня", "N" * 20):
        network.write(mc.DEVICE_NAME, name.encode())
        assert network.read(mc.DEVICE_NAME).decode("utf-8") == name
    network.write(mc.DEVICE_NAME, b"")
    assert DEFAULT_NAME.match(network.read(mc.DEVICE_NAME).decode("utf-8"))


def test_device_name_rejects(network, keep_device_name):
    for bad in (b"N" * 21, b"Tab\there", b"Del\x7f"):
        network.write(mc.DEVICE_NAME, bad)
        assert network.read(mc.DEVICE_NAME).decode("utf-8") == keep_device_name, f"{bad!r} was accepted"


def test_wifi_settings_value(network):
    s = network.wifi_settings()
    assert s.mode in (mc.WIFI_OFF, mc.WIFI_PERIODIC)
    assert 1 <= s.period <= 24
    assert len(s.ssid.encode()) <= 32
    if s.mode != mc.WIFI_OFF:
        assert s.ssid


def test_wifi_settings_rejects(network):
    orig_raw = network.read(mc.WIFI_SETTINGS)
    s = mc.parse_wifi_settings(orig_raw)
    ssid = s.ssid or "x"
    bad = {
        "mode 2": mc.wifi_settings_write(2, s.period, ssid),
        "period 0": mc.wifi_settings_write(s.mode, 0, ssid),
        "period 25": mc.wifi_settings_write(s.mode, 25, ssid),
        "periodic without SSID": mc.wifi_settings_write(mc.WIFI_PERIODIC, s.period, ""),
        "SSID of 33 bytes": mc.wifi_settings_write(s.mode, s.period, "S" * 33),
        "password of 65 bytes": mc.wifi_settings_write(s.mode, s.period, ssid, "p" * 65),
        "no password byte": bytes([s.mode, s.period]) + mc.str8(ssid),
        "short password": bytes([s.mode, s.period]) + mc.str8(ssid) + b"\x08abc",
        "bytes after keep": mc.wifi_settings_write(s.mode, s.period, ssid) + b"\x00",
        "SSID past the end": bytes([s.mode, s.period, 20]) + b"abc",
        "3 bytes": bytes([s.mode, s.period, 0]),
    }
    try:
        for name, value in bad.items():
            network.write(mc.WIFI_SETTINGS, value)
            assert network.read(mc.WIFI_SETTINGS) == orig_raw, f"{name} was accepted"
    finally:
        if network.read(mc.WIFI_SETTINGS) != orig_raw:
            # the stored password stays: restore the rest
            network.write(mc.WIFI_SETTINGS, mc.wifi_settings_write(s.mode, s.period, s.ssid))


def test_status_values(network):
    st = network.wifi_status()
    assert st.state in (mc.STATE_OFF, mc.STATE_CONNECTING, mc.STATE_CONNECTED, mc.STATE_ERROR)
    if st.state == mc.STATE_CONNECTED:
        assert ipaddress.IPv4Address(st.ip).is_private
        assert -127 <= st.rssi < 0
    else:
        assert st.ip == b"\0\0\0\0" and st.rssi == 0
    if st.synced_at:
        assert st.synced_at > mc.clock_seconds(2026, 1, 1)

    info = network.update_info()
    assert info.state in range(5)
    if info.state in (mc.UPDATE_UP_TO_DATE, mc.UPDATE_AVAILABLE):
        assert mc.VERSION_RX.match(info.version), info.version
        assert info.checked_at > mc.clock_seconds(2026, 1, 1)

    ota = network.ota_status()
    assert ota.state in range(6)
    assert ota.written <= ota.total or ota.total == 0

    state, total, first, nets = mc.parse_networks(network.read(mc.WIFI_NETWORKS))
    assert state in range(4)
    if state != mc.SCAN_DONE:
        assert (total, nets) == (0, [])


@pytest.fixture
def wifi_off(network):
    if network.wifi_settings().mode != mc.WIFI_OFF:
        pytest.skip("WiFi is configured: see test_wifi.py")
    return network


def test_check_without_wifi(wifi_off):
    """Command 0x02 with WiFi off fails right away"""
    wifi_off.drain(mc.UPDATE_INFO)
    wifi_off.command(mc.CMD_CHECK_UPDATE)
    time.sleep(1)
    assert wifi_off.update_info().state == mc.UPDATE_FAILED
    assert wifi_off.wifi_status().state == mc.STATE_OFF


def test_install_without_wifi(wifi_off, version):
    """Command 0x03 with WiFi off: OTA error 8, nothing else happens"""
    wifi_off.drain(mc.OTA_STATUS)
    wifi_off.command(mc.CMD_INSTALL_UPDATE)
    st = mc.parse_ota_status(wifi_off.wait_notify(mc.OTA_STATUS, timeout=5))
    assert (st.state, st.error) == (mc.OTA_ERROR, mc.OTA_ERR_DOWNLOAD)
    time.sleep(2)
    assert wifi_off.connected
    assert wifi_off.version() == version


def test_sync_without_wifi(wifi_off):
    wifi_off.drain(mc.WIFI_STATUS)
    wifi_off.command(mc.CMD_SYNC_TIME)
    assert not wifi_off.notifications(mc.WIFI_STATUS, 3), "WiFi stays off"
    assert wifi_off.wifi_status().state == mc.STATE_OFF


def test_unknown_command(network, version):
    network.command(0x7F)
    time.sleep(1)
    assert network.connected
    assert network.version() == version
