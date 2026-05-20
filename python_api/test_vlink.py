#!/usr/bin/env python3
"""
VLink Python bindings self-test.

Usage:
    cd build_python/output/lib
    LD_LIBRARY_PATH=. python3 ../../../python_api/test_vlink.py
"""

import os
import threading
import time

os.environ["VLINK_DISCOVER_DISABLE"] = "1"

import vlink as _vlink  # type: ignore

assert getattr(_vlink, "__backend__", None) == "nanobind"


def _make_node(cls, url, ser_type=""):
    node = cls(url, ser_type=ser_type, auto_init=False)
    node.set_discovery_enabled(False)
    node.init()
    return node


def test_bytes():
    """Test Bytes class."""
    b = _vlink.Bytes.from_bytes(b"hello world")
    assert b.to_bytes() == b"hello world"
    assert len(b) == 11
    assert bool(b)
    assert not bool(_vlink.Bytes())
    assert b.to_string() == "hello world"

    # Base64
    encoded = _vlink.Bytes.encode_to_base64(b)
    decoded = _vlink.Bytes.decode_from_base64(encoded)
    assert decoded.to_bytes() == b"hello world"

    # CRC-32 (CRC-32/ISO-HDLC)
    crc32 = _vlink.Bytes.get_crc_32(b)
    assert isinstance(crc32, int) and crc32 > 0

    # CRC-64 (CRC-64/ECMA-182)
    crc64 = _vlink.Bytes.get_crc_64(b)
    assert isinstance(crc64, int) and crc64 > 0

    # Compression
    big = _vlink.Bytes.from_bytes(b"A" * 10000)
    compressed = _vlink.Bytes.compress(big)
    decompressed = _vlink.Bytes.uncompress(compressed)
    assert decompressed.to_bytes() == b"A" * 10000
    assert compressed.size() < big.size()

    # Buffer protocol
    mv = memoryview(_vlink.Bytes.from_bytes(b"\x01\x02\x03"))
    assert mv[0] == 1 and mv[2] == 3

    # Bytes-like input support
    ba = bytearray(b"abc")
    from_bytearray = _vlink.Bytes.from_bytes(ba)
    assert from_bytearray.to_bytes() == b"abc"

    print("[PASS] Bytes")


def test_pubsub():
    """Test Publisher / Subscriber (Event model)."""
    received = []
    event = threading.Event()

    sub = _make_node(_vlink.Subscriber, "intra://py_test/pubsub")
    sub.listen(lambda data: (received.append(data), event.set() if len(received) >= 3 else None))

    pub = _make_node(_vlink.Publisher, "intra://py_test/pubsub")
    pub.wait_for_subscribers(timeout_ms=2000)
    assert pub.has_subscribers()

    pub.publish(b"a")
    pub.publish(b"b")
    pub.publish(b"c")
    event.wait(timeout=2.0)

    assert received == [b"a", b"b", b"c"], f"Got: {received}"
    pub.deinit()
    sub.deinit()
    print("[PASS] Publisher/Subscriber")


def test_rpc():
    """Test Server / Client (Method model)."""
    srv = _make_node(_vlink.Server, "intra://py_test/rpc")
    srv.listen(lambda req: b"re:" + req)

    cli = _make_node(_vlink.Client, "intra://py_test/rpc")
    cli.wait_for_connected(timeout_ms=2000)
    assert cli.is_connected()

    assert cli.invoke(b"hello") == b"re:hello"
    assert cli.invoke(b"world") == b"re:world"

    # Async invoke
    result = [None]
    ev = threading.Event()
    cli.invoke_async(b"async", lambda r: (result.__setitem__(0, r), ev.set()))
    ev.wait(timeout=2.0)
    assert result[0] == b"re:async"

    cli.deinit()
    srv.deinit()
    print("[PASS] Server/Client RPC")


def test_field():
    """Test Setter / Getter (Field model)."""
    values = []
    ev = threading.Event()

    getter = _make_node(_vlink.Getter, "intra://py_test/field")
    getter.listen(lambda data: (values.append(data), ev.set()))

    setter = _make_node(_vlink.Setter, "intra://py_test/field")
    time.sleep(0.05)

    setter.set(b"v1")
    ev.wait(timeout=2.0)
    assert values[-1] == b"v1"

    ev.clear()
    setter.set(b"v2")
    ev.wait(timeout=2.0)
    assert values[-1] == b"v2"

    # Polling
    deadline = time.time() + 2.0
    val = None
    while time.time() < deadline:
        val = getter.get()
        if val == b"v2":
            break
        time.sleep(0.01)
    assert val == b"v2"

    setter.deinit()
    getter.deinit()
    print("[PASS] Setter/Getter")


def test_message_loop():
    """Test MessageLoop + Timer."""
    loop = _vlink.MessageLoop()
    loop.async_run()
    assert loop.is_running()

    done = threading.Event()
    loop.post_task(done.set)
    done.wait(timeout=2.0)
    assert done.is_set()

    # Timer
    ticks = [0]
    timer_done = threading.Event()
    timer = _vlink.Timer(loop)
    timer.set_interval(30)
    timer.set_loop_count(3)

    def on_tick():
        ticks[0] += 1
        if ticks[0] >= 3:
            timer_done.set()

    timer.start(on_tick)
    timer_done.wait(timeout=2.0)
    timer.stop()
    assert ticks[0] >= 3

    loop.quit()
    loop.wait_for_quit(timeout_ms=2000)
    print("[PASS] MessageLoop + Timer")


def test_thread_pool():
    """Test ThreadPool."""
    pool = _vlink.ThreadPool(2)
    count = [0]
    ev = threading.Event()
    lock = threading.Lock()

    def task():
        with lock:
            count[0] += 1
            if count[0] >= 4:
                ev.set()

    for _ in range(4):
        pool.post_task(task)
    ev.wait(timeout=2.0)
    assert count[0] >= 4
    pool.shutdown()
    print("[PASS] ThreadPool")


def test_utils():
    """Test utility functions."""
    assert _vlink.utils.get_host_name()
    assert _vlink.utils.get_pid() > 0
    assert _vlink.utils.get_tmp_dir()
    assert _vlink.helpers.has_startwith("hello", "hel")
    assert _vlink.helpers.has_endwith("hello", "llo")
    assert _vlink.helpers.trim_string("  x  ") == "x"
    print("[PASS] Utils + Helpers")


def test_qos():
    """Test QoS profiles."""
    qos_map = _vlink.QosProfile.get_available_qos_map()
    assert len(qos_map) > 0
    assert "event" in qos_map
    assert "method" in qos_map
    assert "field" in qos_map
    print(f"[PASS] QosProfile ({len(qos_map)} profiles)")


def test_uuid():
    """Test Uuid class (RFC 4122 128-bit UUID + v4 random + random_hex/random_bytes)."""

    # ---- constants ----
    assert _vlink.Uuid.BYTE_SIZE == 16
    assert _vlink.Uuid.STRING_SIZE == 36

    # ---- default-constructed = nil ----
    nil = _vlink.Uuid()
    assert nil.is_nil()
    assert nil.to_string() == "00000000-0000-0000-0000-000000000000"
    assert nil.to_compact_string() == "00000000000000000000000000000000"
    assert nil.variant() == _vlink.Uuid.Variant.Ncs
    assert nil.version() == _vlink.Uuid.Version.None_

    # ---- construct from bytes (16-byte payload) ----
    raw = bytes.fromhex("47ac10b858cc4a3c8c5b0e778899aabb")
    fixed = _vlink.Uuid(raw)
    assert not fixed.is_nil()
    assert fixed.bytes() == raw
    assert fixed.to_string() == "47ac10b8-58cc-4a3c-8c5b-0e778899aabb"
    assert fixed.to_compact_string() == "47ac10b858cc4a3c8c5b0e778899aabb"
    assert str(fixed) == fixed.to_string()
    assert "47ac10b8" in repr(fixed)

    # wrong-size payload -> ValueError
    try:
        _vlink.Uuid(b"\x00" * 8)
        assert False, "expected ValueError for 8-byte payload"
    except ValueError:
        pass

    # ---- generate_random produces v4 RFC ----
    a = _vlink.Uuid.generate_random()
    b = _vlink.Uuid.generate_random()
    assert not a.is_nil()
    assert a != b
    assert a.variant() == _vlink.Uuid.Variant.Rfc
    assert a.version() == _vlink.Uuid.Version.RandomBased

    # ---- comparison + hash ----
    a_copy = _vlink.Uuid(a.bytes())
    assert a == a_copy
    assert hash(a) == hash(a_copy)
    assert (a < b) or (b < a)

    # usable as dict key
    table = {a: 1, b: 2}
    assert table[a] == 1 and table[b] == 2

    # ---- parsing ----
    canonical = "47ac10b8-58cc-4a3c-8c5b-0e778899aabb"
    parsed = _vlink.Uuid.from_string(canonical)
    assert parsed is not None and parsed.to_string() == canonical

    # mixed-case
    assert _vlink.Uuid.from_string("47AC10B8-58CC-4A3C-8C5B-0E778899AABB").to_string() == canonical

    # braced
    assert _vlink.Uuid.from_string("{" + canonical + "}").to_string() == canonical

    # compact 32-char
    compact = canonical.replace("-", "")
    assert _vlink.Uuid.from_string(compact).to_string() == canonical

    # malformed -> None
    assert _vlink.Uuid.from_string("not-a-uuid") is None
    assert _vlink.Uuid.from_string("") is None
    assert _vlink.Uuid.from_string("{abc") is None

    # ---- is_valid ----
    assert _vlink.Uuid.is_valid(canonical)
    assert _vlink.Uuid.is_valid("{" + canonical + "}")
    assert _vlink.Uuid.is_valid(compact)
    assert not _vlink.Uuid.is_valid("")
    assert not _vlink.Uuid.is_valid("not-a-uuid")

    # ---- random_bytes ----
    buf = _vlink.Uuid.random_bytes(32)
    assert isinstance(buf, bytes) and len(buf) == 32
    assert _vlink.Uuid.random_bytes(0) == b""
    # variability
    assert _vlink.Uuid.random_bytes(32) != _vlink.Uuid.random_bytes(32)

    # ---- random_hex ----
    hex16 = _vlink.Uuid.random_hex(16)
    assert isinstance(hex16, str) and len(hex16) == 32
    assert all(c in "0123456789abcdef" for c in hex16)
    assert _vlink.Uuid.random_hex() == _vlink.Uuid.random_hex().__class__("") or True  # tautology guard
    assert len(_vlink.Uuid.random_hex()) == 32
    assert _vlink.Uuid.random_hex(0) == ""
    assert len(_vlink.Uuid.random_hex(5)) == 10
    assert _vlink.Uuid.random_hex(16) != _vlink.Uuid.random_hex(16)

    # ---- random_hex round-trips through from_string for 16-byte width ----
    hex_token = _vlink.Uuid.random_hex(16)
    parsed = _vlink.Uuid.from_string(hex_token)
    assert parsed is not None
    assert parsed.to_compact_string() == hex_token

    # ---- unique set of 100 randoms ----
    bucket = {_vlink.Uuid.generate_random() for _ in range(100)}
    assert len(bucket) == 100

    print("[PASS] Uuid")


if __name__ == "__main__":
    _vlink.Logger.init("py_test")
    print(f"VLink Python Bindings Test - v{_vlink.VERSION}")
    print("=" * 50)

    test_bytes()
    test_uuid()
    test_pubsub()
    test_rpc()
    test_field()
    test_message_loop()
    test_thread_pool()
    test_utils()
    test_qos()

    print("=" * 50)
    print("ALL TESTS PASSED!")
