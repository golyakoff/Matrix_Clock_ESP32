"""
Matrix Clock test client: the BLE protocol of main/include/ble.h (as the phone app speaks it) and the
serial log of the clock.

The BLE side is a synchronous wrapper over bleak: an asyncio loop runs in a background thread, so
tests are plain functions. The clock answers an invalid write with success and ignores it (no ATT
errors), so tests check the value read back.
"""

import asyncio
import calendar
import os
import queue
import re
import struct
import threading
import time
from dataclasses import dataclass

from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

SERVICE = "5de498a1-e7a6-4f4a-b323-913741895ad0"
NAME = "Tetris Clock"

TIME = "d5bd8d18-bd9a-4ef4-b206-8c78ffbe2774"
TIME_STR = "aa063b0f-db36-47d0-8f19-a70fa97d86df"
OFF_ALARM = "84915734-bf86-46e7-b394-22e25b3f9007"
ON_ALARM = "6bdbd293-b623-411c-bb2a-f429eaf93cf1"
SHOW = "2e126c52-37b8-4a7d-9688-28e33104c0e1"
AUTO_BRIGHTNESS = "9b078810-99ab-4423-b3a8-6f2e86a09582"
MANUAL_BRIGHTNESS = "117ed80d-af6e-4e4d-b900-48f68725a7d3"
COLOR_ORDER = "36b8588d-6f99-4bdc-8b6f-13089f234978"
ANIMATION_SPLASH = "a3f1c2d4-5b6e-47a8-9c0d-1e2f3a4b5c6d"
HOURLY_BRIGHTNESS = "c2c5d9aa-4c0b-4a69-9e9b-9e1d8b7a2f31"
TEMPERATURE = "13be1932-508d-4beb-afbc-2c21d1397920"
AGING_OFFSET = "f89e201d-434f-4675-b60e-2cf682200c50"
FLASH_SIZE = "c9f0e1d2-3b4a-5c6d-7e8f-a0b1c2d3e4f5"
ANIMATION_COUNT = "c9f0e1d2-3b4a-5c6d-7e8f-a0b1c2d3e4f6"
TIME_ZONE = "c9f0e1d2-3b4a-5c6d-7e8f-a0b1c2d3e4f7"
WIFI_SETTINGS = "c9f0e1d2-3b4a-5c6d-7e8f-a0b1c2d3e4f8"
WIFI_STATUS = "c9f0e1d2-3b4a-5c6d-7e8f-a0b1c2d3e4f9"
WIFI_NETWORKS = "c9f0e1d2-3b4a-5c6d-7e8f-a0b1c2d3e4fa"
COMMAND = "c9f0e1d2-3b4a-5c6d-7e8f-a0b1c2d3e4fb"
UPDATE_INFO = "c9f0e1d2-3b4a-5c6d-7e8f-a0b1c2d3e4fc"
OTA_STATUS = "c9f0e1d2-3b4a-5c6d-7e8f-a0b1c2d3e4fd"
DEVICE_NAME = "c9f0e1d2-3b4a-5c6d-7e8f-a0b1c2d3e4fe"  # optional, after v2.0.0
VERSION = "beb5483e-36e1-4688-b7f5-ea07361b26a0"
OTA_CONTROL = "beb5483e-36e1-4688-b7f5-ea07361b26a1"
OTA_DATA = "beb5483e-36e1-4688-b7f5-ea07361b26a2"

BASE = (TIME, TIME_STR, OFF_ALARM, ON_ALARM, SHOW, AUTO_BRIGHTNESS, MANUAL_BRIGHTNESS, COLOR_ORDER,
        ANIMATION_SPLASH, HOURLY_BRIGHTNESS, TEMPERATURE, AGING_OFFSET, FLASH_SIZE, ANIMATION_COUNT,
        VERSION, OTA_CONTROL, OTA_DATA)
NETWORK = (TIME_ZONE, WIFI_SETTINGS, WIFI_STATUS, WIFI_NETWORKS, COMMAND, UPDATE_INFO, OTA_STATUS)
NOTIFYING = (TIME, TIME_STR, SHOW, VERSION, WIFI_STATUS, WIFI_NETWORKS, UPDATE_INFO, OTA_STATUS)

CMD_SYNC_TIME, CMD_CHECK_UPDATE, CMD_INSTALL_UPDATE, CMD_SCAN_WIFI = 1, 2, 3, 4

OTA_START, OTA_END, OTA_ABORT = 1, 2, 3

WIFI_OFF, WIFI_PERIODIC = 0, 1
STATE_OFF, STATE_CONNECTING, STATE_CONNECTED, STATE_ERROR = 0, 1, 2, 3
SCAN_NONE, SCAN_RUNNING, SCAN_DONE, SCAN_ERROR = 0, 1, 2, 3
UPDATE_UNKNOWN, UPDATE_CHECKING, UPDATE_UP_TO_DATE, UPDATE_AVAILABLE, UPDATE_FAILED = 0, 1, 2, 3, 4
OTA_IDLE, OTA_RECEIVING, OTA_SUCCESS, OTA_ERROR, OTA_CHECKING, OTA_NO_UPDATE = 0, 1, 2, 3, 4, 5
OTA_ERR_BUSY, OTA_ERR_FLASH, OTA_ERR_SHA, OTA_ERR_DOWNLOAD = 1, 3, 5, 8

KEEP_PASSWORD = 0xFF
ALARM_ACTIVE = 0x0800

VERSION_RX = re.compile(r"^v?\d+\.\d+\.\d+")


################################################################################
# Binary helpers


class Reader:
    def __init__(self, data):
        self.data = bytes(data)
        self.pos = 0

    def u8(self):
        v = self.data[self.pos]
        self.pos += 1
        return v

    def i8(self):
        v = struct.unpack_from("<b", self.data, self.pos)[0]
        self.pos += 1
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return v

    def bytes(self, n):
        b = self.data[self.pos:self.pos + n]
        if len(b) != n:
            raise ValueError("value too short")
        self.pos += n
        return b

    def str8(self):
        return self.bytes(self.u8()).decode("utf-8")

    def end(self):
        if self.pos != len(self.data):
            raise ValueError(f"{len(self.data) - self.pos} extra bytes")


def str8(s):
    b = s.encode("utf-8") if isinstance(s, str) else bytes(s)
    return bytes([len(b)]) + b


def u32(v):
    return struct.pack("<I", v)


################################################################################
# Decoded values


@dataclass
class WifiSettings:
    mode: int
    period: int
    ssid: str
    password_stored: bool


@dataclass
class WifiStatus:
    state: int
    ip: bytes
    rssi: int
    synced_at: int


@dataclass
class Network:
    ssid: str
    rssi: int
    security: int
    channel: int


@dataclass
class UpdateInfo:
    state: int
    version: str
    checked_at: int


@dataclass
class OtaStatus:
    state: int
    error: int
    written: int
    total: int


def parse_wifi_settings(d):
    r = Reader(d)
    mode, period, ssid = r.u8(), r.u8(), r.str8()
    password = r.u8()
    r.end()
    assert password in (0, KEEP_PASSWORD), f"the password is never read back: {password}"
    return WifiSettings(mode, period, ssid, password == KEEP_PASSWORD)


def wifi_settings_write(mode, period, ssid, password=None):
    """password None: keep the stored one"""
    pw = bytes([KEEP_PASSWORD]) if password is None else str8(password)
    return bytes([mode, period]) + str8(ssid) + pw


def parse_wifi_status(d):
    r = Reader(d)
    v = WifiStatus(r.u8(), r.bytes(4), r.i8(), r.u32())
    r.end()
    return v


def parse_networks(d):
    """(state, total, first, [Network])"""
    r = Reader(d)
    state, total, first, n = r.u8(), r.u8(), r.u8(), r.u8()
    nets = [Network(r.str8(), r.i8(), r.u8(), r.u8()) for _ in range(n)]
    r.end()
    return state, total, first, nets


def parse_update_info(d):
    r = Reader(d)
    v = UpdateInfo(r.u8(), r.str8(), r.u32())
    r.end()
    return v


def parse_ota_status(d):
    r = Reader(d)
    v = OtaStatus(r.u8(), r.u8(), r.u32(), r.u32())
    r.end()
    return v


def alarm_value(hours, minutes, active):
    return struct.pack("<H", hours * 60 + minutes + (ALARM_ACTIVE if active else 0))


def parse_alarm(d):
    """(hours, minutes, active)"""
    v = struct.unpack("<H", d)[0]
    m = v & 0x07FF
    return m // 60, m % 60, bool(v & ALARM_ACTIVE)


def splash_value(mode, duration, index, preview=False):
    return bytes([(mode & 3) | (duration & 3) << 2 | (index & 7) << 4 | (0x80 if preview else 0),
                  index >> 3 & 1])


def parse_splash(d):
    """(mode, duration, index)"""
    assert len(d) == 2, d.hex()
    return d[0] & 3, d[0] >> 2 & 3, (d[0] >> 4 & 7) | (d[1] & 1) << 3


################################################################################
# The clock's time: its local calendar fields counted as UTC seconds (see MC_TIME_CHAR_UUID)


def clock_seconds(y, mo, d, h=0, mi=0, s=0):
    return calendar.timegm((y, mo, d, h, mi, s, 0, 0, 0))


def clock_fields(seconds):
    return time.gmtime(seconds)[:6]


def local_now_seconds(tz=None):
    """Local time now as the clock counts it; tz: a POSIX TZ string (the clock's zone), None: the PC's"""
    now = time.time()
    if tz is None:
        t = time.localtime(now)
    else:
        old = os.environ.get("TZ")
        os.environ["TZ"] = tz
        time.tzset()
        try:
            t = time.localtime(now)
        finally:
            if old is None:
                del os.environ["TZ"]
            else:
                os.environ["TZ"] = old
            time.tzset()
    return calendar.timegm(t[:6] + (0, 0, 0)) + (now % 1)


def time_string(seconds):
    return "%04d-%02d-%02d %02d:%02d:%02d" % clock_fields(seconds)


################################################################################
# BLE client


class Clock:
    """Connected clock. All methods are synchronous."""

    def __init__(self, address, timeout=20.0):
        self.address = address
        self.timeout = timeout
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self.loop.run_forever, daemon=True)
        self.thread.start()
        self.client = None
        self.queues = {u: queue.Queue() for u in NOTIFYING}
        self.chars = set()

    def _run(self, coro, timeout=None):
        return asyncio.run_coroutine_threadsafe(coro, self.loop).result(timeout or self.timeout + 10)

    # connection

    def connect(self, pair=True):
        self._run(self._connect(pair), timeout=self.timeout * 3)

    async def _connect(self, pair):
        self.client = BleakClient(self.address, timeout=self.timeout)
        await self.client.connect()
        if pair:
            try:
                # "Just Works": the WiFi settings are written over an encrypted link only
                await self.client.pair()
            except (BleakError, NotImplementedError):
                # already bonded, or the platform pairs on the first encrypted access
                pass
        svc = self.client.services.get_service(SERVICE)
        if svc is None:
            raise RuntimeError("Matrix Clock service not found")
        # BlueZ does not report the MTU by itself (bleak assumes 23), ask for it
        backend = getattr(self.client, "_backend", None)
        if hasattr(backend, "_acquire_mtu"):
            try:
                await backend._acquire_mtu()
            except Exception:  # noqa: BLE001 - keep the default MTU
                pass
        self.chars = {c.uuid.lower() for c in svc.characteristics}
        for u in NOTIFYING:
            if u in self.chars:
                await self.client.start_notify(u, self._make_handler(u))

    def _make_handler(self, u):
        def handler(_, data):
            self.queues[u].put((time.monotonic(), bytes(data)))
        return handler

    def disconnect(self):
        if self.client:
            try:
                self._run(self.client.disconnect())
            except Exception:  # noqa: BLE001 - already gone
                pass

    def wait_gone(self, timeout=30.0):
        """Wait until the clock drops the connection (it reboots)"""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self.connected:
                return
            time.sleep(0.2)
        raise TimeoutError(f"the clock is still connected after {timeout} s")

    def reconnect(self, wait_s=60.0):
        """After a reboot: wait until the clock advertises again and connect"""
        self.disconnect()
        deadline = time.monotonic() + wait_s
        last = None
        while time.monotonic() < deadline:
            try:
                dev = self._run(BleakScanner.find_device_by_address(self.address, timeout=5.0))
                if dev:
                    self.connect(pair=False)
                    self.drain()
                    return
            except Exception as e:  # noqa: BLE001 - keep trying until the deadline
                last = e
            time.sleep(1)
        raise TimeoutError(f"the clock did not come back in {wait_s} s: {last}")

    @property
    def connected(self):
        return bool(self.client and self.client.is_connected)

    @property
    def mtu(self):
        return self.client.mtu_size

    def has(self, u):
        return u in self.chars

    @property
    def network(self):
        """Firmware v2.0.0+: WiFi and the update from the internet"""
        return all(u in self.chars for u in NETWORK)

    def close(self):
        self.disconnect()
        self.loop.call_soon_threadsafe(self.loop.stop)

    # raw access

    def read(self, u):
        return bytes(self._run(self.client.read_gatt_char(u)))

    def write(self, u, data):
        self._run(self.client.write_gatt_char(u, bytes(data), response=True))

    # notifications

    def drain(self, u=None):
        for n in ([u] if u else NOTIFYING):
            q = self.queues[n]
            while not q.empty():
                q.get_nowait()

    def wait_notify(self, u, pred=lambda d: True, timeout=10.0):
        """First notification of `u` matching pred(data); returns the data"""
        deadline = time.monotonic() + timeout
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                raise TimeoutError(f"no matching notification of {u} in {timeout} s")
            try:
                _, data = self.queues[u].get(timeout=left)
            except queue.Empty:
                continue
            if pred(data):
                return data

    def notifications(self, u, wait):
        """All notifications of `u` in the next `wait` seconds"""
        end = time.monotonic() + wait
        result = []
        while True:
            left = end - time.monotonic()
            if left <= 0:
                return result
            try:
                result.append(self.queues[u].get(timeout=left)[1])
            except queue.Empty:
                return result

    # decoded helpers

    def u8(self, u):
        d = self.read(u)
        assert len(d) == 1, f"{u}: {d.hex()}"
        return d[0]

    def i8(self, u):
        return struct.unpack("<b", self.read(u))[0]

    def version(self):
        return self.read(VERSION).decode("utf-8")

    def time(self):
        """The clock's time (see clock_seconds()) from the next Time notification"""
        self.drain(TIME)
        return struct.unpack("<I", self.wait_notify(TIME, timeout=3))[0]

    def set_time(self, seconds):
        self.write(TIME, u32(int(seconds)))

    def time_zone(self):
        return self.read(TIME_ZONE).decode("utf-8")

    def wifi_settings(self):
        return parse_wifi_settings(self.read(WIFI_SETTINGS))

    def wifi_status(self):
        return parse_wifi_status(self.read(WIFI_STATUS))

    def update_info(self):
        return parse_update_info(self.read(UPDATE_INFO))

    def ota_status(self):
        return parse_ota_status(self.read(OTA_STATUS))

    def command(self, cmd):
        self.write(COMMAND, bytes([cmd]))

    def networks(self):
        """Whole WiFi networks list, page by page: (state, [Network])"""
        self.write(WIFI_NETWORKS, b"\x00")
        state, total, first, nets = parse_networks(self.read(WIFI_NETWORKS))
        assert first == 0
        result = list(nets)
        while len(result) < total:
            assert nets, "empty page before the end of the list"
            self.write(WIFI_NETWORKS, bytes([len(result)]))
            state, total, first, nets = parse_networks(self.read(WIFI_NETWORKS))
            assert first == len(result)
            result += nets
        return state, result


def find_clock(timeout=10.0):
    """Address of the first advertising clock (by the service UUID), None if not found"""
    async def scan():
        devices = await BleakScanner.discover(timeout=timeout, service_uuids=[SERVICE])
        return devices[0].address if devices else None
    return asyncio.run(scan())


################################################################################
# Serial log


class SerialLog:
    """Reads the clock's log in a background thread; resets the board like esptool does.

    Release builds log errors only (ESP_LOGE), the debug build everything; the bootloader always logs
    the boot partition and the ROM the reset reason."""

    CRASH = re.compile(r"Guru Meditation|abort\(\) was called|Backtrace:|"
                       r"Task watchdog got triggered|stack overflow|assert failed")
    BOOT = r"rst:0x"
    PARTITION = re.compile(r"Loaded app from partition at offset (0x[0-9a-fA-F]+)")

    def __init__(self, port, baud=115200):
        import serial  # pyserial, only needed with --port
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = baud
        self.ser.timeout = 0.2
        # do not reset the board when opening the port (DevKit auto-reset circuit)
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        self.lines = []
        self.lock = threading.Lock()
        self.stop = False
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self):
        buf = b""
        while not self.stop:
            try:
                chunk = self.ser.read(256)
            except Exception:  # noqa: BLE001 - the port is gone
                break
            if not chunk:
                continue
            buf += chunk
            *done, buf = buf.split(b"\n")
            with self.lock:
                for line in done:
                    text = re.sub(r"\x1b\[[0-9;]*m", "", line.decode("utf-8", "replace")).rstrip("\r")
                    self.lines.append((time.monotonic(), text))

    def mark(self):
        """Position to search from"""
        with self.lock:
            return len(self.lines)

    def since(self, mark=0):
        with self.lock:
            return [t for _, t in self.lines[mark:]]

    def find(self, pattern, mark=0):
        rx = re.compile(pattern)
        return [line for line in self.since(mark) if rx.search(line)]

    def wait_for(self, pattern, mark=0, timeout=30.0):
        rx = re.compile(pattern)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.since(mark):
                m = rx.search(line)
                if m:
                    return m
            time.sleep(0.1)
        raise TimeoutError(f"'{pattern}' not in the log in {timeout} s")

    def crashes(self, mark=0):
        return [line for line in self.since(mark) if self.CRASH.search(line)]

    def boot_partition(self, mark=0, timeout=30.0):
        """Offset of the app partition the bootloader started after `mark`"""
        return self.wait_for(self.PARTITION.pattern, mark, timeout).group(1).lower()

    def reset(self):
        """Hardware reset via EN (RTS), boot from flash (IO0 high)"""
        self.ser.dtr = False
        self.ser.rts = True
        time.sleep(0.2)
        self.ser.rts = False

    def close(self):
        self.stop = True
        self.thread.join(timeout=2)
        self.ser.close()
