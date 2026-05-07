"""
VLink nanobind wrapper.
"""

from _vlink_nanobind import *  # noqa: F401,F403
from _vlink_nanobind import (
    ImplType, TransportType, InitType, ActionType, SchemaType, LogLevel, StatusType,
    TimerMethod, TimerAccuracy, MessageLoopType, MessageLoopStrategy, TaskPriority,
    Bytes, Version, SampleLostInfo,
    Logger, ElapsedTimer, DeadlineTimer, MessageLoop, MultiLoop, Timer, WheelTimer,
    ThreadPool, SpinLock, CpuProfiler, CpuProfilerGuard, MemoryPool,
    Process, UrlRemap,
    Qos, SslOptions, Security,
    Publisher, Subscriber, Server, Client, Setter, Getter,
    DiscoveryViewer, BagWriter, BagReader,
    utils, helpers, QosProfile,
    log_trace, log_debug, log_info, log_warn, log_error, log_fatal,
    VERSION, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH,
)

try:
    from _vlink_nanobind import MemoryResource  # noqa: F401
    _HAS_MEMORY_RESOURCE = True
except ImportError:
    _HAS_MEMORY_RESOURCE = False

__backend__ = "nanobind"
__version__ = VERSION
__all__ = [
    "ImplType", "TransportType", "InitType", "ActionType", "SchemaType", "LogLevel", "StatusType",
    "TimerMethod", "TimerAccuracy", "MessageLoopType", "MessageLoopStrategy", "TaskPriority",
    "Bytes", "Version", "SampleLostInfo",
    "Logger", "ElapsedTimer", "DeadlineTimer", "MessageLoop", "MultiLoop", "Timer", "WheelTimer",
    "ThreadPool", "SpinLock", "CpuProfiler", "CpuProfilerGuard", "MemoryPool",
    "Process", "UrlRemap",
    "Qos", "SslOptions", "Security",
    "Publisher", "Subscriber", "Server", "Client", "Setter", "Getter",
    "DiscoveryViewer", "BagWriter", "BagReader",
    "utils", "helpers", "QosProfile",
    "log_trace", "log_debug", "log_info", "log_warn", "log_error", "log_fatal",
    "VERSION", "VERSION_MAJOR", "VERSION_MINOR", "VERSION_PATCH",
    "__backend__",
]
if _HAS_MEMORY_RESOURCE:
    __all__.insert(__all__.index("MemoryPool") + 1, "MemoryResource")
