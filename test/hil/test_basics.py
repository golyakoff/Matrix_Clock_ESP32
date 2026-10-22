"""Characteristics and read-only values"""

import matrixclock as mc


def test_all_characteristics_present(clock):
    missing = sorted(set(mc.BASE) - clock.chars)
    assert not missing


def test_network_characteristics(clock, version):
    """Firmware v2.0.0+ has all of them, older none"""
    present = sorted(set(mc.NETWORK) & clock.chars)
    major = int(mc.VERSION_RX.match(version).group(0).lstrip("v").split(".")[0])
    if major >= 2:
        assert len(present) == len(mc.NETWORK)
    else:
        assert not present


def test_version(version):
    assert mc.VERSION_RX.match(version), version


def test_flash_size(clock):
    assert clock.u8(mc.FLASH_SIZE) in (4, 8, 16)


def test_animation_count(clock):
    assert 1 <= clock.u8(mc.ANIMATION_COUNT) <= 16


def test_temperature(clock):
    # DS3231, whole degrees: a room
    assert 0 <= clock.i8(mc.TEMPERATURE) <= 60


def test_mtu(clock):
    # the clock asks for 515; the OTA chunks are MTU - 3 bytes
    assert clock.mtu >= 23
