"""Paired phones: the list, the name the app sends and forgetting (firmware v2.1.3+)"""

import time

import pytest

import matrixclock as mc
from conftest import reboot


def test_bonded_phones_present(clock):
    assert clock.has(mc.BONDED_PHONES), "the paired phones characteristic is missing; the firmware is older than 2.1.3"


def test_bonded_phones(clock):
    capacity, phones = clock.bonded_phones()
    assert capacity == 5
    assert 1 <= len(phones) <= capacity
    assert sum(p.current for p in phones) == 1, "exactly one entry is this connection"
    for phone in phones:
        assert phone.type in (0, 1, 2, 3)
        assert len(phone.address) == 6
        assert len(phone.name.encode("utf-8")) <= 48


def test_set_bond_name(clock):
    _, before = clock.bonded_phones()
    me = next(p for p in before if p.current)

    clock.set_bond_name("HIL test phone")
    _, after = clock.bonded_phones()
    assert next(p for p in after if p.current).name == "HIL test phone"

    # put back what was there (an empty name removes it)
    clock.set_bond_name(me.name)
    _, restored = clock.bonded_phones()
    assert next(p for p in restored if p.current).name == me.name


def test_set_bond_name_rejects_controls(clock):
    _, before = clock.bonded_phones()
    clock.set_bond_name("a\x01b")
    _, after = clock.bonded_phones()
    assert [p.name for p in after] == [p.name for p in before]


def test_set_bond_name_rejects_too_long(clock):
    _, before = clock.bonded_phones()
    clock.set_bond_name("X" * 49)
    _, after = clock.bonded_phones()
    assert [p.name for p in after] == [p.name for p in before]


def test_forget_current_phone_rejected(clock):
    _, before = clock.bonded_phones()
    me = next(p for p in before if p.current)
    clock.forget_bond(me)
    _, after = clock.bonded_phones()
    assert len(after) == len(before)
    assert next(p for p in after if p.current).mac == me.mac


def test_forget_unknown_phone_rejected(clock):
    _, before = clock.bonded_phones()
    unknown = mc.BondedPhone(0, b"\x01\x02\x03\x04\x05\x06", False, "")
    clock.forget_bond(unknown)
    _, after = clock.bonded_phones()
    assert [p.mac for p in after] == [p.mac for p in before]


def test_forget_bond_needs_the_address(clock):
    _, before = clock.bonded_phones()
    clock.write(mc.COMMAND, bytes([mc.CMD_FORGET_BOND, 0, 1]))
    _, after = clock.bonded_phones()
    assert [p.mac for p in after] == [p.mac for p in before]


@pytest.mark.reboot
def test_bond_name_survives_reset(clock, log):
    _, before = clock.bonded_phones()
    me = next(p for p in before if p.current)
    new_name = "HIL persistent name" if me.name != "HIL persistent name" else "HIL persistent name 2"
    try:
        clock.set_bond_name(new_name)
        time.sleep(1)
        reboot(clock, log)
        _, after = clock.bonded_phones()
        assert next(p for p in after if p.current).name == new_name
    finally:
        clock.set_bond_name(me.name)
        time.sleep(1)
