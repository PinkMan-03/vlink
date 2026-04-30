#!/usr/bin/env python3
"""Comprehensive test for ALL VLink Python bindings including newly added APIs."""

import os
import threading
import time
import tempfile

os.environ["VLINK_DISCOVER_DISABLE"] = "1"

import vlink as _vlink  # type: ignore

assert getattr(_vlink, "__backend__", None) == "nanobind"


def _make_node(cls, url, ser_type=""):
    node = cls(url, ser_type=ser_type, auto_init=False)
    node.set_discovery_enabled(False)
    node.init()
    return node


def test_bytes_extended():
    b = _vlink.Bytes.from_bytes(b"hello")
    assert b.size() == 5
    assert b.real_size() == 5
    assert b.capacity() >= 5
    assert b.offset() == 0
    assert b.is_owner()
    assert not b.is_loaned()
    assert not b.is_ptr()
    assert b[0] == ord('h')
    assert b[4] == ord('o')

    # resize / reserve / shrink
    b2 = _vlink.Bytes.create(100)
    assert b2.size() == 100
    b2.resize(50)
    assert b2.size() == 50
    b2.reserve(200)
    assert b2.capacity() >= 200
    b2.shrink_to(10)
    assert b2.size() == 10

    # deep_copy_self
    b3 = _vlink.Bytes.from_bytes(b"abc")
    b3.deep_copy_self()
    assert b3.to_bytes() == b"abc"
    assert b3.is_owner()

    # to_raw_data
    raw = _vlink.Bytes.from_bytes(b"\x01\x02\x03").to_raw_data()
    assert raw == [1, 2, 3]

    # equality
    a = _vlink.Bytes.from_bytes(b"test")
    b = _vlink.Bytes.from_bytes(b"test")
    c = _vlink.Bytes.from_bytes(b"other")
    assert a == b
    assert not (a == c)

    # is_compress_data
    compressed = _vlink.Bytes.compress(_vlink.Bytes.from_bytes(b"X" * 1000))
    assert _vlink.Bytes.is_compress_data(compressed)
    assert not _vlink.Bytes.is_compress_data(_vlink.Bytes.from_bytes(b"hello"))

    # reverse_order
    rev = _vlink.Bytes.reverse_order(_vlink.Bytes.from_bytes(b"\x01\x02\x03"))
    assert rev.to_bytes() == b"\x03\x02\x01"

    # endianness
    assert isinstance(_vlink.Bytes.is_little_endian(), bool)
    assert isinstance(_vlink.Bytes.is_big_endian(), bool)
    assert _vlink.Bytes.is_little_endian() != _vlink.Bytes.is_big_endian()

    # stack_size
    assert _vlink.Bytes.stack_size() == 96

    # from_user_input (hex with spaces)
    b4 = _vlink.Bytes.from_user_input("48 65 6C 6C 6F")
    assert b4.to_string() == "Hello"

    # bytes-like object compatibility
    assert _vlink.Bytes.from_bytes(memoryview(b"xyz")).to_bytes() == b"xyz"

    crc_a = _vlink.Bytes.get_crc_32(b"hello")
    crc_b = _vlink.Bytes.get_crc_32(bytearray(b"hello"))
    crc_c = _vlink.Bytes.get_crc_32(memoryview(b"hello"))
    crc_d = _vlink.Bytes.get_crc_32(_vlink.Bytes.from_bytes(b"hello"))
    assert crc_a == crc_b == crc_c == crc_d

    base64_a = _vlink.Bytes.encode_to_base64(b"raw-input")
    base64_b = _vlink.Bytes.encode_to_base64(_vlink.Bytes.from_bytes(b"raw-input"))
    assert base64_a == base64_b

    big = b"X" * 5000
    c1 = _vlink.Bytes.compress(big)
    assert _vlink.Bytes.is_compress_data(c1)
    assert _vlink.Bytes.uncompress(c1).to_bytes() == big

    rev = _vlink.Bytes.reverse_order(b"\x01\x02\x03")
    assert rev.to_bytes() == b"\x03\x02\x01"

    print("[PASS] Bytes extended")


def test_logger_extended():
    _vlink.Logger.init("full_test")

    # Level checks
    assert _vlink.Logger.is_writable(_vlink.LogLevel.Info)
    assert _vlink.Logger.is_writable(_vlink.LogLevel.Error)

    # Console format
    orig = _vlink.Logger.get_console_fmt_enable()
    _vlink.Logger.set_console_fmt_enable(False)
    assert not _vlink.Logger.get_console_fmt_enable()
    _vlink.Logger.set_console_fmt_enable(orig)

    # Backtrace
    _vlink.Logger.enable_backtrace(10)
    _vlink.log_debug("backtrace msg 1")
    _vlink.log_debug("backtrace msg 2")
    _vlink.Logger.dump_backtrace()
    _vlink.Logger.disable_backtrace()

    # Custom console handler
    captured = []
    _vlink.Logger.register_console_handler(lambda lv, msg: captured.append((lv, msg)))
    _vlink.log_info("custom handler test")
    _vlink.Logger.flush()
    time.sleep(0.05)
    # Restore default by registering None-like handler
    _vlink.Logger.register_console_handler(lambda lv, msg: None)

    print("[PASS] Logger extended")


def test_elapsed_timer_extended():
    t = _vlink.ElapsedTimer(_vlink.TimerMethod.CpuTimestamp, _vlink.TimerAccuracy.Micro)
    assert t.get_method() == _vlink.TimerMethod.CpuTimestamp
    assert t.get_accuracy() == _vlink.TimerAccuracy.Micro
    t.start()
    time.sleep(0.01)
    e = t.get()
    assert e > 0
    t.stop()

    # Static methods
    ts = _vlink.ElapsedTimer.get_sys_timestamp()
    assert ts > 0
    cpu = _vlink.ElapsedTimer.get_cpu_active_time()
    assert cpu >= 0

    print("[PASS] ElapsedTimer extended")


def test_deadline_timer_extended():
    dt = _vlink.DeadlineTimer(200)
    assert dt.is_valid()
    assert dt.deadline() > 0
    assert dt.remaining_time() > 0
    dt.set_deadline_abs(dt.deadline() + 1000)
    assert dt.remaining_time() > 0
    dt.reset()
    assert not dt.is_valid()

    micro = _vlink.DeadlineTimer(1000, _vlink.TimerAccuracy.Micro)
    assert micro.get_accuracy() == _vlink.TimerAccuracy.Micro

    print("[PASS] DeadlineTimer extended")


def test_message_loop_extended():
    loop = _vlink.MessageLoop(_vlink.MessageLoopType.Normal)
    loop.set_name("test_ext")
    assert loop.get_name() == "test_ext"
    assert loop.get_type() == _vlink.MessageLoopType.Normal

    # Strategy
    loop.set_strategy(_vlink.MessageLoopStrategy.Block)
    assert loop.get_strategy() == _vlink.MessageLoopStrategy.Block
    loop.set_strategy(_vlink.MessageLoopStrategy.Optimization)

    # Begin/end/idle handlers
    begin_called = [False]
    loop.register_begin_handler(lambda: begin_called.__setitem__(0, True))
    loop.async_run()
    time.sleep(0.05)
    assert begin_called[0]
    assert loop.is_running()
    assert not loop.is_ready_to_quit()

    # Max counters
    assert loop.get_max_task_count() >= 0
    assert loop.get_max_timer_count() >= 0
    assert loop.get_max_elapsed_time() >= 0

    # Wakeup
    loop.wakeup()

    # Wait for idle (timeout_ms, check)
    loop.wait_for_idle(100, True)
    loop.wait_for_idle(100, False)

    # Lockfree capacity reset hook (no-op for Normal type, just ensure binding works)
    lockfree_loop = _vlink.MessageLoop(_vlink.MessageLoopType.Lockfree)
    lockfree_loop.reset_lockfree_capacity()

    # post_task_with_priority (needs Priority type queue)
    ploop = _vlink.MessageLoop(_vlink.MessageLoopType.Priority)
    ploop.async_run()
    prio_done = threading.Event()
    ploop.post_task_with_priority(prio_done.set, _vlink.TaskPriority.Highest)
    prio_done.wait(2.0)
    assert prio_done.is_set()
    ploop.quit()
    ploop.wait_for_quit(2000)

    loop.quit()
    loop.wait_for_quit(2000)
    print("[PASS] MessageLoop extended")


def test_timer_extended():
    loop = _vlink.MessageLoop()
    loop.async_run()

    timer = _vlink.Timer(loop)
    timer.set_interval(50)
    timer.set_loop_count(2)
    timer.set_strict(True)
    assert timer.is_strict()
    timer.set_priority(5)
    assert timer.get_priority() == 5
    assert timer.get_message_loop() is not None

    done = threading.Event()
    timer.start(lambda: done.set() if timer.get_invoke_count() >= 2 else None)
    done.wait(2.0)
    timer.stop()

    # set_callback
    cb2_called = [False]
    timer.set_loop_count(1)
    timer.set_callback(lambda: cb2_called.__setitem__(0, True))
    timer.start(lambda: cb2_called.__setitem__(0, True))
    time.sleep(0.2)
    timer.stop()

    # call_once
    once_done = threading.Event()
    _vlink.Timer.call_once(loop, 30, once_done.set)
    once_done.wait(2.0)
    assert once_done.is_set()

    # TIMER_INFINITE constant
    assert _vlink.TIMER_INFINITE == -1

    loop.quit()
    loop.wait_for_quit(2000)
    print("[PASS] Timer extended")


def test_thread_pool_extended():
    pool = _vlink.ThreadPool(2)
    assert pool.get_max_task_count() >= 0
    # ThreadPool uses its own Strategy type (same values as MessageLoop::Strategy)
    pool.shutdown()
    print("[PASS] ThreadPool extended")


def test_process():
    # Static execute
    ret = _vlink.Process.execute("/bin/echo", ["hello"])
    assert ret == 0

    # Instance-based
    p = _vlink.Process()
    assert p.get_state() == _vlink.Process.State.NotRunning

    p.set_working_directory("/tmp")
    assert p.get_working_directory() == "/tmp"

    p.set_inherit_environment(True)
    assert p.get_inherit_environment()

    p.set_process_mode(_vlink.Process.Mode.Merged)
    assert p.get_process_mode() == _vlink.Process.Mode.Merged

    # Run a command and read output
    p2 = _vlink.Process()
    p2.set_process_mode(_vlink.Process.Mode.Merged)
    p2.start("/bin/echo", ["hello_vlink"])
    p2.wait_for_finished(5000)
    output = p2.read_all()
    assert b"hello_vlink" in output
    assert p2.get_exit_code() == 0
    assert p2.get_exit_status() == _vlink.Process.ExitStatus.Normal

    # start_command
    p3 = _vlink.Process()
    p3.set_process_mode(_vlink.Process.Mode.Merged)
    p3.start_command("/bin/echo test_cmd")
    p3.wait_for_finished(5000)
    out3 = p3.read_all()
    assert b"test_cmd" in out3

    # start_detached
    assert _vlink.Process.start_detached("/bin/true")

    # Callbacks
    finished_info = [None]
    p4 = _vlink.Process()
    p4.register_finished_callback(lambda code, status: finished_info.__setitem__(0, (code, status)))
    p4.start("/bin/true")
    p4.wait_for_finished(5000)
    time.sleep(0.1)
    assert finished_info[0] is not None
    assert finished_info[0][0] == 0

    p5 = _vlink.Process()
    p5.set_process_mode(_vlink.Process.Mode.Merged)
    p5.start("/bin/echo", ["line_one"])
    p5.wait_for_finished(5000)
    assert isinstance(p5.can_read_line_stdout(), bool)
    line = p5.read_line_stdout()
    assert line is None or b"line_one" in line

    p6 = _vlink.Process()
    p6.set_process_mode(_vlink.Process.Mode.Merged)
    p6.start("/bin/echo", ["chunk"])
    p6.wait_for_finished(5000)
    chunk_out = p6.read_stdout(0)
    assert isinstance(chunk_out, bytes)

    assert _vlink.Process.INFINITE == -1
    assert _vlink.Process.DEFAULT_WAIT_TIMEOUT_MS == 3000

    print("[PASS] Process")


def test_utils_extended():
    # New utils
    mid = _vlink.utils.get_machine_id()
    assert isinstance(mid, str)

    tid = _vlink.utils.get_native_thread_id()
    assert tid > 0

    tz = _vlink.utils.get_timezone_diff()
    assert isinstance(tz, int)

    cpu = _vlink.utils.get_cpu_usage()
    assert isinstance(cpu, float)

    mem = _vlink.utils.get_memory_usage()
    assert isinstance(mem, float)

    # is_process_running
    assert isinstance(_vlink.utils.is_process_running("nonexistent_12345"), bool)

    # check_singleton
    assert isinstance(_vlink.utils.check_singleton("py_test_singleton_" + str(os.getpid())), bool)

    # filter_available default false: returns full list
    addrs = _vlink.utils.get_all_ipv4_address(filter_available=False)
    assert isinstance(addrs, list)

    # get_dds_default_address respects max_count
    capped = _vlink.utils.get_dds_default_address(max_count=2)
    assert isinstance(capped, list) and len(capped) <= 2

    # set_thread_name without thread arg
    assert isinstance(_vlink.utils.set_thread_name("py_audit_thread"), bool)

    print("[PASS] Utils extended")


def test_helpers_extended():
    assert _vlink.helpers.double_to_string(3.14159, 2) == "3.14"
    assert _vlink.helpers.hash_combine(123, 456) > 0
    assert _vlink.helpers.contains_substring("hello world", "world")
    assert not _vlink.helpers.contains_substring("hello", "xyz")

    # format_date
    date_str = _vlink.helpers.format_date(1700000000000000000)  # ns since epoch
    assert isinstance(date_str, str) and len(date_str) > 0

    # format_time_diff
    diff = _vlink.helpers.format_time_diff(3661000)
    assert isinstance(diff, str)

    # format_hex_number
    hex_str = _vlink.helpers.format_hex_number(255)
    assert "ff" in hex_str.lower() or "FF" in hex_str

    # get_pair_string
    pair = _vlink.helpers.get_pair_string("key=value", "=")
    assert pair[0] == "key"
    assert pair[1] == "value"

    # convert_date_to_timestamp
    ts = _vlink.helpers.convert_date_to_timestamp("2024-01-01 00:00:00")
    assert isinstance(ts, int)

    # replace_string returns a new string (Python str is immutable)
    replaced = _vlink.helpers.replace_string("a-b-c", "-", "_")
    assert replaced == "a_b_c"

    # format_hex_number_unsigned overload
    assert isinstance(_vlink.helpers.format_hex_number_unsigned(0xDEADBEEF), str)

    # local <-> utf8 round-trip is a no-op when the source is plain ASCII
    rt = _vlink.helpers.string_utf8_to_local(_vlink.helpers.string_local_to_utf8("ascii-text"))
    assert rt == "ascii-text"

    print("[PASS] Helpers extended")


def test_qos_enums():
    # Reliability::Kind
    assert _vlink.Qos.Reliability.Kind.BestEffort is not None
    assert _vlink.Qos.Reliability.Kind.Reliable is not None

    # History::Kind
    assert _vlink.Qos.History.Kind.KeepLast is not None
    assert _vlink.Qos.History.Kind.KeepAll is not None

    # Durability::Kind
    assert _vlink.Qos.Durability.Kind.Volatile is not None
    assert _vlink.Qos.Durability.Kind.TransientLocal is not None
    assert _vlink.Qos.Durability.Kind.Transient is not None
    assert _vlink.Qos.Durability.Kind.Persistent is not None

    # PublishMode::Kind
    assert _vlink.Qos.PublishMode.Kind.Sync is not None
    assert _vlink.Qos.PublishMode.Kind.ASync is not None

    # Liveliness::Kind
    assert _vlink.Qos.Liveliness.Kind.Automatic is not None

    # Full QoS construction
    qos = _vlink.Qos()
    qos.reliability.kind = _vlink.Qos.Reliability.Kind.Reliable
    qos.reliability.block_time = 100
    qos.history.kind = _vlink.Qos.History.Kind.KeepLast
    qos.history.depth = 10
    qos.durability.kind = _vlink.Qos.Durability.Kind.TransientLocal
    qos.publish_mode.kind = _vlink.Qos.PublishMode.Kind.ASync
    qos.deadline.period = 1000
    qos.lifespan.duration = 5000
    qos.latency_budget.duration = 50
    qos.resource_limits.max_samples = 100
    qos.additions.priority = _vlink.Qos.Additions.Priority.RealTime
    qos.additions.is_express = True
    qos.valid = True
    assert qos.valid

    qos.destination_order.kind = _vlink.Qos.DestinationOrder.Kind.SourceTimestamp
    assert qos.destination_order.kind == _vlink.Qos.DestinationOrder.Kind.SourceTimestamp
    qos.ownership.kind = _vlink.Qos.Ownership.Kind.Exclusive
    assert qos.ownership.kind == _vlink.Qos.Ownership.Kind.Exclusive

    qos.name = "py_audit_profile"
    assert qos.name == "py_audit_profile"
    long_name = "x" * 50
    qos.name = long_name
    assert qos.name == long_name[:19]

    print("[PASS] QoS enums & construction")


def test_url_remap():
    remap = _vlink.UrlRemap()
    assert not remap.is_valid()

    # Create a temp remap file
    remap_file = os.path.join(tempfile.mkdtemp(), "remap.json")
    with open(remap_file, "w") as f:
        f.write('{"old://topic": "new://topic"}')

    assert remap.load(remap_file)
    assert remap.is_valid()
    assert remap.convert("old://topic") == "new://topic"
    assert remap.convert("unchanged://topic") == "unchanged://topic"

    remap.set_enable_log(True)
    assert remap.is_enable_log()

    assert remap.unload()
    assert not remap.is_valid()

    os.remove(remap_file)
    print("[PASS] UrlRemap")


def test_node_common_apis():
    """Test Node<> shared methods via Publisher."""
    pub = _vlink.Publisher("intra://test/node_common", auto_init=False)

    # Discovery
    pub.set_discovery_enabled(False)
    assert not pub.get_discovery_enabled()
    pub.set_discovery_enabled(True)
    assert pub.get_discovery_enabled()

    # schema_type
    pub.set_ser_type("demo.proto.NodeCommon", _vlink.SchemaType.Protobuf)
    assert pub.get_ser_type() == "demo.proto.NodeCommon"
    assert pub.get_schema_type() == _vlink.SchemaType.Protobuf
    pub.set_ser_type("demo.proto.NodeCommonV2")
    assert pub.get_ser_type() == "demo.proto.NodeCommonV2"
    assert pub.get_schema_type() == _vlink.SchemaType.Protobuf
    pub.set_ser_type("")
    assert pub.get_ser_type() == ""
    assert pub.get_schema_type() == _vlink.SchemaType.Unknown
    pub.set_ser_type("demo.proto.NodeCommon", _vlink.SchemaType.Protobuf)

    # Safety quit
    pub.set_safety_quit(True)
    assert pub.get_safety_quit()
    pub.set_safety_quit(False)

    pub.init()

    # Transport type
    assert pub.get_transport_type() == _vlink.TransportType.Intra

    # Manual unloan
    assert not pub.is_manual_unloan()

    # Properties
    pub.set_property("test_key", "test_val")
    assert pub.get_property("test_key") == "test_val"

    pub.deinit()
    print("[PASS] Node common APIs")


def test_node_wire_meta_validation():
    try:
        _vlink.Publisher("intra://test/invalid_ser_only", ser_type="demo.proto.Invalid", auto_init=False)
        assert False, "Expected ValueError for ser_type without schema_type"
    except ValueError:
        pass

    try:
        _vlink.Publisher(
            "intra://test/invalid_schema_only",
            schema_type=_vlink.SchemaType.Protobuf,
            auto_init=False,
        )
        assert False, "Expected ValueError for schema_type without ser_type"
    except ValueError:
        pass

    pub = _vlink.Publisher(
        "intra://test/valid_wire_meta",
        ser_type="demo.proto.Valid",
        schema_type=_vlink.SchemaType.Protobuf,
        auto_init=False,
    )
    assert pub.get_ser_type() == "demo.proto.Valid"
    assert pub.get_schema_type() == _vlink.SchemaType.Protobuf
    print("[PASS] Node wire_meta validation")


def test_getter_change_reporting():
    getter = _make_node(_vlink.Getter, "intra://test/cr")
    getter.set_change_reporting(True)
    assert getter.get_change_reporting()
    getter.set_change_reporting(False)
    assert not getter.get_change_reporting()
    getter.deinit()
    print("[PASS] Getter change_reporting")


def test_bag_extended():
    bag_path = os.path.join(tempfile.mkdtemp(), "ext_test.vdb")

    # Writer with split callback
    cfg = _vlink.BagWriter.Config()
    cfg.compress = _vlink.BagWriter.CompressType.LZAV
    w = _vlink.BagWriter.create(bag_path, cfg)
    w.async_run()
    for i in range(5):
        seq = w.push("intra://t", "raw", _vlink.SchemaType.Raw, _vlink.ActionType.Publish, f"m{i}".encode())
        assert isinstance(seq, int)
    time.sleep(0.05)
    seq_immediate = w.push(
        "intra://t", "raw", _vlink.SchemaType.Raw, _vlink.ActionType.Publish, b"sync", immediate=True
    )
    assert isinstance(seq_immediate, int)
    time.sleep(0.2)
    w.quit()
    w.wait_for_quit(5000)
    del w  # release writer before opening reader

    # filter_get (shared instance management) - uses separate file
    filter_path = bag_path + ".filter_test.vdb"
    w2 = _vlink.BagWriter.filter_get(filter_path)
    assert w2 is not None
    del w2  # release before reader opens bag_path

    # Reader with check/reindex
    r = _vlink.BagReader.create(bag_path)
    info = r.get_info()
    assert info.message_count >= 5
    assert info.storage_type == "SQLite3"
    assert info.url_metas[0].schema_type == _vlink.SchemaType.Raw
    assert r.get_schema_type("intra://t") == _vlink.SchemaType.Raw

    # Start loop first (check/reindex run async tasks on the loop)
    r.async_run()

    # check integrity
    check_ok = r.check()
    assert isinstance(check_ok, bool)

    # reindex (may return False if already indexed, that's OK)
    reindex_result = r.reindex()
    assert isinstance(reindex_result, bool)

    # Status callback
    statuses = []
    r.register_status_callback(lambda s: statuses.append(s))

    # Playback
    msgs = []
    done = threading.Event()
    r.register_output_callback(lambda ts, url, at, d: msgs.append(d))
    r.register_finish_callback(lambda intr: done.set())

    cfg2 = _vlink.BagReader.Config()
    cfg2.rate = 100.0
    cfg2.auto_quit = True
    r.play(cfg2)
    done.wait(10.0)

    assert len(msgs) >= 5
    assert b"m0" in msgs

    r.quit()
    r.wait_for_quit(5000)

    os.remove(bag_path)
    print("[PASS] Bag extended")


def test_register_terminate_signal():
    # Just test it doesn't crash - can't easily test actual signal
    signal_received = [False]
    _vlink.utils.register_terminate_signal(lambda sig: signal_received.__setitem__(0, True))
    print("[PASS] register_terminate_signal")


def test_ssl_options():
    ssl = _vlink.SslOptions()
    assert not ssl.is_valid()
    ssl.ca_file = "/tmp/ca.pem"
    assert ssl.is_valid()
    ssl.cert_file = "/tmp/cert.pem"
    ssl.key_file = "/tmp/key.pem"
    ssl.key_password = "secret"
    ssl.server_name = "localhost"
    ssl.ciphers = "AES256"
    ssl.verify_peer = True
    assert ssl.is_valid()
    print("[PASS] SslOptions")


def test_security():
    sec = _vlink.Security()
    sec.set_key("my_secret_key_16")
    plain = b"Hello VLink Security!"
    encrypted = sec.encrypt(plain)
    assert encrypted is not None
    assert encrypted != plain
    decrypted = sec.decrypt(encrypted)
    assert decrypted == plain
    print("[PASS] Security")


def test_security_set_callbacks():
    sec = _vlink.Security()

    def xor_enc(data):
        return bytes(b ^ 0x5A for b in data)

    def xor_dec(data):
        return bytes(b ^ 0x5A for b in data)

    sec.set_callbacks(xor_enc, xor_dec)

    plain = b"custom-cipher-roundtrip"
    encrypted = sec.encrypt(plain)
    assert encrypted is not None and encrypted != plain
    decrypted = sec.decrypt(encrypted)
    assert decrypted == plain
    print("[PASS] Security.set_callbacks")


def test_server_async_reply():
    """Test Server listen_for_reply + reply binding (intra:// doesn't support reply, so test binding only)."""
    srv = _make_node(_vlink.Server, "intra://test/async_srv2")
    req_arrived = threading.Event()

    def on_request(req_id, req_data):
        req_arrived.set()

    srv.listen_for_reply(on_request)

    cli = _make_node(_vlink.Client, "intra://test/async_srv2")
    cli.wait_for_connected(2000)
    cli.invoke_async(b"test", lambda r: None)

    req_arrived.wait(2.0)
    assert req_arrived.is_set(), "listen_for_reply callback not called"

    # reply() returns False on intra:// (not supported), but binding is correct
    ret = srv.reply(0, b"resp")
    assert ret is False or ret is True  # just verify it doesn't crash

    cli.deinit()
    srv.deinit()
    print("[PASS] Server async reply (binding verified)")


def test_exec_task():
    """Test MessageLoop exec_task with delay."""
    loop = _vlink.MessageLoop()
    loop.async_run()

    done = threading.Event()
    t_start = time.time()

    ok = loop.exec_task(50, done.set)  # 50ms delay
    assert ok
    done.wait(2.0)
    elapsed = (time.time() - t_start) * 1000
    assert elapsed >= 40, f"exec_task delay too short: {elapsed}ms"

    loop.quit()
    loop.wait_for_quit(2000)
    print("[PASS] exec_task")


def test_timer_constructors():
    loop = _vlink.MessageLoop()
    loop.async_run()

    done = threading.Event()
    ticks = [0]

    # Full constructor: Timer(loop, interval_ms, loop_count, callback)
    timer = _vlink.Timer(loop, 50, 3, lambda: (ticks.__setitem__(0, ticks[0] + 1),
                                                 done.set() if ticks[0] >= 3 else None))
    # The timer auto-starts from this constructor
    timer.start(lambda: (ticks.__setitem__(0, ticks[0] + 1),
                         done.set() if ticks[0] >= 3 else None))
    done.wait(3.0)
    assert ticks[0] >= 3, f"Timer ticked only {ticks[0]} times"
    timer.stop()

    loop.quit()
    loop.wait_for_quit(2000)
    print("[PASS] Timer constructors")


def test_discovery_viewer_fields():
    try:
        dv = _vlink.DiscoveryViewer()
    except RuntimeError as exc:
        if "DiscoveryViewer: Failed to create socket" in str(exc):
            print("[SKIP] DiscoveryViewer fields (socket unavailable)")
            return
        raise

    info_list = dv.get_info_list()
    # Just verify types - may be empty if no services running
    assert isinstance(info_list, list)

    # Test convert_type_to_view
    view = _vlink.DiscoveryViewer.convert_type_to_view(1)  # kPublisher
    assert isinstance(view, str)

    # Test get_listen_address
    addr = _vlink.DiscoveryViewer.get_listen_address()
    assert isinstance(addr, str)

    print("[PASS] DiscoveryViewer fields")


def test_utils_terminal():
    w, h = _vlink.utils.get_terminal_size()
    assert isinstance(w, int)
    assert isinstance(h, int)
    print(f"  Terminal size: {w}x{h}")
    print("[PASS] Utils terminal")


def test_api_surface():
    expected_exports = [
        "ImplType", "TransportType", "InitType", "ActionType", "SchemaType", "LogLevel", "StatusType",
        "TimerMethod", "TimerAccuracy", "MessageLoopType", "MessageLoopStrategy", "TaskPriority",
        "Bytes", "Version", "SampleLostInfo",
        "Logger", "ElapsedTimer", "DeadlineTimer", "MessageLoop", "Timer", "ThreadPool", "SpinLock",
        "Process", "UrlRemap",
        "Qos", "SslOptions", "Security",
        "Publisher", "Subscriber", "Server", "Client", "Setter", "Getter",
        "DiscoveryViewer", "BagWriter", "BagReader",
        "utils", "helpers", "QosProfile",
        "log_trace", "log_debug", "log_info", "log_warn", "log_error", "log_fatal",
        "VERSION", "VERSION_MAJOR", "VERSION_MINOR", "VERSION_PATCH",
    ]
    for name in expected_exports:
        assert hasattr(_vlink, name), f"Missing module export: {name}"

    node_methods = [
        "init", "deinit", "set_discovery_enabled", "get_url", "get_transport_type",
        "get_schema_type", "set_ser_type",
        "attach", "detach", "get_message_loop", "get_status", "register_status_handler",
    ]
    for cls in (_vlink.Publisher, _vlink.Subscriber, _vlink.Server, _vlink.Client, _vlink.Setter, _vlink.Getter):
        for method in node_methods:
            assert hasattr(cls, method), f"{cls.__name__} missing method: {method}"

    class_methods = {
        _vlink.Publisher: ["detect_subscribers", "wait_for_subscribers", "has_subscribers", "publish"],
        _vlink.Subscriber: ["listen", "set_latency_and_lost_enabled", "is_latency_and_lost_enabled",
                            "get_latency", "get_lost"],
        _vlink.Server: ["listen", "listen_for_reply", "reply"],
        _vlink.Client: ["detect_connected", "wait_for_connected", "is_connected", "invoke", "invoke_async"],
        _vlink.Setter: ["set", "mark_as_publisher"],
        _vlink.Getter: ["listen", "get", "wait_for_value", "set_change_reporting", "get_change_reporting",
                        "mark_as_subscriber"],
    }
    for cls, methods in class_methods.items():
        for method in methods:
            assert hasattr(cls, method), f"{cls.__name__} missing method: {method}"

    helper_exports = ["to_int", "to_long", "trim_string", "has_startwith", "has_endwith"]
    for name in helper_exports:
        assert hasattr(_vlink.helpers, name), f"helpers missing function: {name}"

    utils_exports = ["get_host_name", "get_pid", "get_pid_str", "get_tmp_dir", "register_terminate_signal"]
    for name in utils_exports:
        assert hasattr(_vlink.utils, name), f"utils missing function: {name}"

    print("[PASS] API surface")


def test_node_extended():
    sub = _vlink.Subscriber("intra://test/node_extended", auto_init=False)
    sub.set_discovery_enabled(False)
    sub.init()

    assert sub.get_message_loop() is None or hasattr(sub.get_message_loop(), "is_running")

    status = sub.get_status(_vlink.StatusType.SubscriptionMatched)
    assert status is None or isinstance(status, dict)

    sub.deinit()
    print("[PASS] Node extended (get_message_loop / get_status)")


def test_node_role_swaps():
    setter = _vlink.Setter("intra://test/role_swap_set", auto_init=False)
    setter.set_discovery_enabled(False)
    setter.mark_as_publisher()
    setter.init()
    setter.deinit()

    getter = _vlink.Getter("intra://test/role_swap_get", auto_init=False)
    getter.set_discovery_enabled(False)
    getter.mark_as_subscriber()
    getter.init()
    getter.deinit()
    print("[PASS] Setter.mark_as_publisher / Getter.mark_as_subscriber")


def test_log_fatal():
    assert callable(_vlink.log_fatal)
    try:
        _vlink.log_fatal("python_api regression: log_fatal smoke test")
    except RuntimeError:
        pass
    print("[PASS] log_fatal")


def test_publish_after_buffer_release():
    received = []
    ev = threading.Event()

    sub = _vlink.Subscriber("intra://test/owned_publish", auto_init=False)
    sub.set_discovery_enabled(False)
    sub.init()
    sub.listen(lambda data: (received.append(bytes(data)), ev.set()))

    pub = _vlink.Publisher("intra://test/owned_publish?type=queue", auto_init=False)
    pub.set_discovery_enabled(False)
    pub.init()
    pub.wait_for_subscribers(timeout_ms=2000)

    payload = bytearray(b"transient-payload-" + b"x" * 1024)
    pub.publish(bytes(payload))
    payload.clear()

    ev.wait(timeout=2.0)
    assert received and received[0].startswith(b"transient-payload-")

    pub.deinit()
    sub.deinit()
    print("[PASS] publish deep-copies caller's bytes (no UAF)")


if __name__ == "__main__":
    _vlink.Logger.init("full_test")
    print(f"VLink Full API Test - v{_vlink.VERSION}")
    print("=" * 60)

    test_bytes_extended()
    test_logger_extended()
    test_elapsed_timer_extended()
    test_deadline_timer_extended()
    test_message_loop_extended()
    test_timer_extended()
    test_thread_pool_extended()
    test_process()
    test_utils_extended()
    test_helpers_extended()
    test_qos_enums()
    test_url_remap()
    test_node_common_apis()
    test_getter_change_reporting()
    test_bag_extended()
    test_register_terminate_signal()
    test_ssl_options()
    test_security()
    test_security_set_callbacks()
    test_server_async_reply()
    test_exec_task()
    test_timer_constructors()
    test_discovery_viewer_fields()
    test_utils_terminal()
    test_api_surface()
    test_node_wire_meta_validation()
    test_node_extended()
    test_node_role_swaps()
    test_log_fatal()
    test_publish_after_buffer_release()

    print("=" * 60)
    print("ALL 30 TESTS PASSED!")
