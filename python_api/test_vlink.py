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

    # CRC32
    crc = _vlink.Bytes.get_crc_32(b)
    assert isinstance(crc, int) and crc > 0

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
    val = getter.get()
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


if __name__ == "__main__":
    _vlink.Logger.init("py_test")
    print(f"VLink Python Bindings Test - v{_vlink.VERSION}")
    print("=" * 50)

    test_bytes()
    test_pubsub()
    test_rpc()
    test_field()
    test_message_loop()
    test_thread_pool()
    test_utils()
    test_qos()

    print("=" * 50)
    print("ALL TESTS PASSED!")
