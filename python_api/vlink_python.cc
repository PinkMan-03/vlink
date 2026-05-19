/*
 * Copyright (C) 2026 by Thun Lu. All rights reserved.
 * Author: Thun Lu <thun.lu@zohomail.cn>
 * Repo:   https://github.com/thun-res/vlink
 *  _    __   __      _           __
 * | |  / /  / /     (_) ____    / /__
 * | | / /  / /     / / / __ \  / //_/
 * | |/ /  / /___  / / / / / / / ,<
 * |___/  /_____/ /_/ /_/ /_/ /_/|_|
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file vlink_python.cc
 * @brief Comprehensive nanobind bindings for VLink communication middleware.
 *
 * @details
 * This file exposes the full VLink Python surface while factoring the six
 * template communication primitives through reusable nanobind registration
 * helpers.
 */

#include <nanobind/nanobind.h>
#include <nanobind/stl/chrono.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/vector.h>
#include <vlink/base/cpu_profiler.h>
#include <vlink/base/cpu_profiler_guard.h>
#include <vlink/base/deadline_timer.h>
#include <vlink/base/elapsed_timer.h>
#include <vlink/base/helpers.h>
#include <vlink/base/memory_pool.h>
#include <vlink/base/memory_resource.h>
#include <vlink/base/multi_loop.h>
#include <vlink/base/process.h>
#include <vlink/base/spin_lock.h>
#include <vlink/base/thread_pool.h>
#include <vlink/base/timer.h>
#include <vlink/base/wheel_timer.h>
#include <vlink/extension/bag_reader.h>
#include <vlink/extension/bag_writer.h>
#include <vlink/extension/discovery_viewer.h>
#include <vlink/extension/qos_profile.h>
#include <vlink/extension/status_detail.h>
#include <vlink/extension/url_remap.h>
#include <vlink/vlink.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace nb = nanobind;
using namespace nb::literals;  // NOLINT

namespace {

struct GilSafePyFunction {
  nb::callable fn;
  explicit GilSafePyFunction(nb::callable f) : fn(std::move(f)) {}
  ~GilSafePyFunction() {
    nb::handle leaked = fn.release();
    if (leaked.is_valid() && Py_IsInitialized()) {
      nb::gil_scoped_acquire gil;
      leaked.dec_ref();
    }
  }
};

struct GilSafePyObject {
  nb::object obj;
  explicit GilSafePyObject(nb::object o) : obj(std::move(o)) {}
  ~GilSafePyObject() {
    nb::handle leaked = obj.release();
    if (leaked.is_valid() && Py_IsInitialized()) {
      nb::gil_scoped_acquire gil;
      leaked.dec_ref();
    }
  }
};

inline void report_current_exception(const char* context) noexcept {
  try {
    throw;
  } catch (nb::python_error& e) {
    e.discard_as_unraisable(context);
  } catch (const std::exception& e) {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    PyErr_WriteUnraisable(nb::str(context).ptr());
  } catch (...) {
    PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
    PyErr_WriteUnraisable(nb::str(context).ptr());
  }
}

inline nb::str wide_string_to_python_str(const std::wstring& value) {
  PyObject* obj = PyUnicode_FromWideChar(value.c_str(), static_cast<Py_ssize_t>(value.size()));
  if (!obj) {
    throw nb::python_error();
  }
  return nb::steal<nb::str>(obj);
}

struct PyWideStringDeleter {
  void operator()(wchar_t* ptr) const noexcept {
    if (ptr) {
      PyMem_Free(ptr);
    }
  }
};

inline std::wstring python_str_to_wide_string(nb::str input) {
  Py_ssize_t size = 0;
  wchar_t* raw = PyUnicode_AsWideCharString(input.ptr(), &size);
  if (!raw) {
    throw nb::python_error();
  }

  std::unique_ptr<wchar_t, PyWideStringDeleter> wide(raw);
  return std::wstring(wide.get(), static_cast<size_t>(size));
}

class PythonBufferView {
 public:
  explicit PythonBufferView(nb::handle handle, int flags = PyBUF_SIMPLE) {
    if (PyObject_GetBuffer(handle.ptr(), &view_, flags) != 0) {
      throw nb::python_error();
    }
    valid_ = true;
  }

  PythonBufferView(const PythonBufferView&) = delete;
  PythonBufferView& operator=(const PythonBufferView&) = delete;

  ~PythonBufferView() {
    if (valid_) {
      PyBuffer_Release(&view_);
    }
  }

  [[nodiscard]] const uint8_t* data() const { return static_cast<const uint8_t*>(view_.buf); }
  [[nodiscard]] size_t size() const { return static_cast<size_t>(view_.len); }

 private:
  Py_buffer view_{};
  bool valid_ = false;
};

template <typename T, typename Enable = void>
struct PythonCodec {
  static T from_python(nb::handle handle) { return nb::cast<T>(handle); }
  static T from_python_owned(nb::handle handle) { return nb::cast<T>(handle); }
  static nb::object to_python(const T& value) { return nb::cast(value); }
};

template <>
struct PythonCodec<vlink::Bytes> {
  static vlink::Bytes from_python(nb::handle handle) {
    PythonBufferView view(handle);
    return vlink::Bytes::shallow_copy(view.data(), view.size());
  }

  static vlink::Bytes from_python_owned(nb::handle handle) {
    PythonBufferView view(handle);
    return vlink::Bytes::deep_copy(view.data(), view.size());
  }

  static nb::object to_python(const vlink::Bytes& value) { return nb::bytes(value.data(), value.size()); }
};

template <typename MsgT, typename Codec = PythonCodec<MsgT>>
auto make_value_callback(nb::callable py_cb, const char* context) {
  auto cb = std::make_shared<GilSafePyFunction>(std::move(py_cb));

  return [cb = std::move(cb), context](const MsgT& value) {
    if (!Py_IsInitialized()) {
      return;
    }

    nb::gil_scoped_acquire gil;

    try {
      cb->fn(Codec::to_python(value));
    } catch (std::exception&) {
      report_current_exception(context);
    }
  };
}

inline auto make_connect_callback(nb::callable py_cb, const char* context) {
  auto cb = std::make_shared<GilSafePyFunction>(std::move(py_cb));

  return [cb = std::move(cb), context](bool connected) {
    if (!Py_IsInitialized()) {
      return;
    }

    nb::gil_scoped_acquire gil;

    try {
      cb->fn(connected);
    } catch (std::exception&) {
      report_current_exception(context);
    }
  };
}

inline auto make_void_callback(nb::callable py_cb, const char* context) {
  auto cb = std::make_shared<GilSafePyFunction>(std::move(py_cb));

  return [cb = std::move(cb), context]() {
    if (!Py_IsInitialized()) {
      return;
    }

    nb::gil_scoped_acquire gil;

    try {
      cb->fn();
    } catch (std::exception&) {
      report_current_exception(context);
    }
  };
}

inline auto make_security_callback(nb::callable py_cb, const char* context) {
  auto cb = std::make_shared<GilSafePyFunction>(std::move(py_cb));

  return [cb = std::move(cb), context](const vlink::Bytes& in, vlink::Bytes& out) -> bool {
    if (!Py_IsInitialized()) {
      return false;
    }

    nb::gil_scoped_acquire gil;

    try {
      nb::object result = cb->fn(PythonCodec<vlink::Bytes>::to_python(in));
      if (result.is_none()) {
        return false;
      }
      out = PythonCodec<vlink::Bytes>::from_python_owned(result);
      return true;
    } catch (std::exception&) {
      report_current_exception(context);
      return false;
    }
  };
}

inline nb::dict status_to_dict(const vlink::Status::BasePtr& status) {
  nb::dict d;

  if (!status) {
    return d;
  }

  const auto type = status->get_type();
  d["type"] = static_cast<int>(type);
  d["status_type"] = type;
  d["description"] = status->get_string();

  auto put_handle = [&d](const char* key, vlink::Status::InstanceHandle handle) {
    if (handle == nullptr) {
      d[key] = nb::none();
    } else {
      d[key] = reinterpret_cast<uintptr_t>(handle);
    }
  };

  if (auto s = std::dynamic_pointer_cast<vlink::Status::PublicationMatched>(status)) {
    d["total_count"] = s->total_count;
    d["total_count_change"] = s->total_count_change;
    d["current_count"] = s->current_count;
    d["current_count_change"] = s->current_count_change;
    put_handle("last_subscription_handle", s->last_subscription_handle);
  } else if (auto s = std::dynamic_pointer_cast<vlink::Status::OfferedDeadlineMissed>(status)) {
    d["total_count"] = s->total_count;
    d["total_count_change"] = s->total_count_change;
    put_handle("last_instance_handle", s->last_instance_handle);
  } else if (auto s = std::dynamic_pointer_cast<vlink::Status::OfferedIncompatibleQos>(status)) {
    d["total_count"] = s->total_count;
    d["total_count_change"] = s->total_count_change;
    d["last_policy_id"] = s->last_policy_id;
  } else if (auto s = std::dynamic_pointer_cast<vlink::Status::LivelinessLost>(status)) {
    d["total_count"] = s->total_count;
    d["total_count_change"] = s->total_count_change;
  } else if (auto s = std::dynamic_pointer_cast<vlink::Status::SubscriptionMatched>(status)) {
    d["total_count"] = s->total_count;
    d["total_count_change"] = s->total_count_change;
    d["current_count"] = s->current_count;
    d["current_count_change"] = s->current_count_change;
    put_handle("last_publication_handle", s->last_publication_handle);
  } else if (auto s = std::dynamic_pointer_cast<vlink::Status::RequestedDeadlineMissed>(status)) {
    d["total_count"] = s->total_count;
    d["total_count_change"] = s->total_count_change;
    put_handle("last_instance_handle", s->last_instance_handle);
  } else if (auto s = std::dynamic_pointer_cast<vlink::Status::LivelinessChanged>(status)) {
    d["alive_count"] = s->alive_count;
    d["not_alive_count"] = s->not_alive_count;
    d["alive_count_change"] = s->alive_count_change;
    d["not_alive_count_change"] = s->not_alive_count_change;
    put_handle("last_publication_handle", s->last_publication_handle);
  } else if (auto s = std::dynamic_pointer_cast<vlink::Status::SampleRejected>(status)) {
    d["total_count"] = s->total_count;
    d["total_count_change"] = s->total_count_change;
    d["last_reason"] = static_cast<int>(s->last_reason);
    put_handle("last_instance_handle", s->last_instance_handle);
  } else if (auto s = std::dynamic_pointer_cast<vlink::Status::RequestedIncompatibleQos>(status)) {
    d["total_count"] = s->total_count;
    d["total_count_change"] = s->total_count_change;
    d["last_policy_id"] = s->last_policy_id;
  } else if (auto s = std::dynamic_pointer_cast<vlink::Status::SampleLost>(status)) {
    d["total_count"] = s->total_count;
    d["total_count_change"] = s->total_count_change;
  }

  return d;
}

int bytes_getbuffer(PyObject* obj, Py_buffer* view, int flags) {
  auto* bytes = nb::inst_ptr<vlink::Bytes>(nb::handle(obj));
  return PyBuffer_FillInfo(view, obj, bytes->data(), static_cast<Py_ssize_t>(bytes->size()), 0, flags);
}

void bytes_releasebuffer(PyObject*, Py_buffer*) {}

PyType_Slot bytes_type_slots[] = {
    {Py_bf_getbuffer, reinterpret_cast<void*>(bytes_getbuffer)},
    {Py_bf_releasebuffer, reinterpret_cast<void*>(bytes_releasebuffer)},
    {0, nullptr},
};

template <typename NodeT>
NodeT* make_url_node(const std::string& url, const std::string& ser_type, vlink::SchemaType schema_type,
                     bool auto_init) {
  const bool has_ser_type = !ser_type.empty();
  const bool has_schema_type = schema_type != vlink::SchemaType::kUnknown;

  if VUNLIKELY (!has_ser_type && has_schema_type) {
    throw nb::value_error("schema_type requires ser_type");
  }

  auto node = std::make_unique<NodeT>(url, vlink::InitType::kWithoutInit);

  if (has_ser_type) {
    node->set_ser_type(ser_type, schema_type);
  }

  if (auto_init) {
    node->init();
  }
  return node.release();
}

template <typename NodeT>
NodeT* make_url_security_node(const std::string& url, vlink::Security::Config sec_cfg, const std::string& ser_type,
                              vlink::SchemaType schema_type, bool auto_init) {
  const bool has_ser_type = !ser_type.empty();
  const bool has_schema_type = schema_type != vlink::SchemaType::kUnknown;

  if VUNLIKELY (!has_ser_type && has_schema_type) {
    throw nb::value_error("schema_type requires ser_type");
  }

  // SecurityXxx installs Security in its constructor, before this helper can call set_ser_type().
  if (has_ser_type && sec_cfg.advanced.aad_context.empty()) {
    const auto resolved_schema_type = has_schema_type ? schema_type : vlink::SchemaData::infer_ser_type(ser_type);
    sec_cfg.advanced.aad_context = url;
    sec_cfg.advanced.aad_context += "|";
    sec_cfg.advanced.aad_context += ser_type;
    sec_cfg.advanced.aad_context += "|";
    sec_cfg.advanced.aad_context += std::to_string(static_cast<uint32_t>(resolved_schema_type));
  }

  auto node = std::make_unique<NodeT>(url, std::move(sec_cfg), vlink::InitType::kWithoutInit);

  if (has_ser_type) {
    node->set_ser_type(ser_type, schema_type);
  }

  if (auto_init) {
    node->init();
  }
  return node.release();
}

template <typename Class, typename NodeT>
void bind_node_common(Class& cls) {
  cls.def("init", &NodeT::init)
      .def("deinit", &NodeT::deinit)
      .def("interrupt", &NodeT::interrupt)
      .def("has_inited", &NodeT::has_inited)
      .def("get_url", &NodeT::get_url)
      .def("get_ser_type", &NodeT::get_ser_type)
      .def("set_ser_type", &NodeT::set_ser_type, "ser_type"_a, "schema_type"_a = vlink::SchemaType::kUnknown)
      .def("get_schema_type", &NodeT::get_schema_type)
      .def("get_transport_type", &NodeT::get_transport_type)
      .def("set_property", &NodeT::set_property, "key"_a, "value"_a)
      .def("get_property", &NodeT::get_property, "key"_a)
      .def("set_discovery_enabled", &NodeT::set_discovery_enabled, "enable"_a)
      .def("get_discovery_enabled", &NodeT::get_discovery_enabled)
      .def("set_record_path", &NodeT::set_record_path, "path"_a)
      .def("set_ssl_options", &NodeT::set_ssl_options, "options"_a)
      .def("set_safety_quit", &NodeT::set_safety_quit, "enable"_a)
      .def("get_safety_quit", &NodeT::get_safety_quit)
      .def("is_support_loan", &NodeT::is_support_loan)
      .def(
          "loan", [](NodeT& self, int64_t size) { return self.loan(size); }, "size"_a)
      .def("return_loan", &NodeT::return_loan, "bytes"_a)
      .def("set_manual_unloan", &NodeT::set_manual_unloan, "enable"_a)
      .def("is_manual_unloan", &NodeT::is_manual_unloan)
      .def("suspend", &NodeT::suspend)
      .def("resume", &NodeT::resume)
      .def("is_suspend", &NodeT::is_suspend)
      .def("attach", &NodeT::attach, "loop"_a, nb::keep_alive<1, 2>())
      .def("detach", &NodeT::detach)
      .def("get_message_loop", &NodeT::get_message_loop, nb::rv_policy::reference)
      .def(
          "get_abstract_node",
          [](const NodeT& self) -> nb::object {
            const auto* node = self.get_abstract_node();
            if (node == nullptr) {
              return nb::none();
            }
            return nb::int_(reinterpret_cast<uintptr_t>(node));
          },
          "Return the non-owning AbstractNode address, or None if unavailable.")
      .def("get_cpu_usage", &NodeT::get_cpu_usage)
      .def(
          "get_status",
          [](NodeT& self, vlink::Status::Type type) -> nb::object {
            auto status = self.get_status(type);
            if (!status) return nb::none();
            return nb::object(status_to_dict(status));
          },
          "type"_a)
      .def(
          "register_status_handler",
          [](NodeT& self, nb::callable callback) {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            self.register_status_handler([cb](const vlink::Status::BasePtr& status) {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                cb->fn(status_to_dict(status));
              } catch (std::exception&) {
                report_current_exception("vlink::register_status_handler");
              }
            });
          },
          "callback"_a);
}

template <typename Class, typename NodeT>
void bind_node_security_ctor(Class& cls) {
  cls.def(nb::new_([](const std::string& url, vlink::Security::Config sec_cfg, const std::string& ser_type,
                      vlink::SchemaType schema_type, bool auto_init) {
            return make_url_security_node<NodeT>(url, std::move(sec_cfg), ser_type, schema_type, auto_init);
          }),
          "url"_a, "sec_cfg"_a, "ser_type"_a = "", "schema_type"_a = vlink::SchemaType::kUnknown, "auto_init"_a = true);
}

template <typename PubT, typename MsgT, typename Codec = PythonCodec<MsgT>, bool SecurityNode = false>
void bind_publisher(nb::module_& m, const char* name, const char* doc) {
  auto cls = nb::class_<PubT>(m, name, doc);
  if constexpr (SecurityNode) {
    bind_node_security_ctor<decltype(cls), PubT>(cls);
  } else {
    cls.def(nb::new_([](const std::string& url, const std::string& ser_type, vlink::SchemaType schema_type,
                        bool auto_init) { return make_url_node<PubT>(url, ser_type, schema_type, auto_init); }),
            "url"_a, "ser_type"_a = "", "schema_type"_a = vlink::SchemaType::kUnknown, "auto_init"_a = true);
  }
  bind_node_common<decltype(cls), PubT>(cls);
  cls.def(
         "detect_subscribers",
         [](PubT& self, nb::callable callback) {
           self.detect_subscribers(make_connect_callback(std::move(callback), "vlink::Publisher.detect_subscribers"));
         },
         "callback"_a)
      .def(
          "wait_for_subscribers",
          [](PubT& self, int timeout_ms) {
            nb::gil_scoped_release release;
            return self.wait_for_subscribers(std::chrono::milliseconds(timeout_ms));
          },
          "timeout_ms"_a = 5000)
      .def("has_subscribers", &PubT::has_subscribers)
      .def(
          "publish",
          [](PubT& self, nb::handle data, bool force) {
            auto value = Codec::from_python_owned(data);
            nb::gil_scoped_release release;
            return self.publish(value, force);
          },
          "data"_a, "force"_a = false)
      .def(
          "publish_fbb",
          [](PubT& self, nb::handle data, bool force) {
            auto value = Codec::from_python_owned(data);
            nb::gil_scoped_release release;
            return self.publish(value, force);
          },
          "data"_a, "force"_a = false, "Publish a finished FlatBuffers byte buffer.")
      .def("mark_as_setter", &PubT::mark_as_setter)
      .def("__repr__", [](const PubT& self) { return "Publisher(url='" + self.get_url() + "')"; });
}

template <typename SubT, typename MsgT, typename Codec = PythonCodec<MsgT>, bool SecurityNode = false>
void bind_subscriber(nb::module_& m, const char* name, const char* doc) {
  auto cls = nb::class_<SubT>(m, name, doc);
  if constexpr (SecurityNode) {
    bind_node_security_ctor<decltype(cls), SubT>(cls);
  } else {
    cls.def(nb::new_([](const std::string& url, const std::string& ser_type, vlink::SchemaType schema_type,
                        bool auto_init) { return make_url_node<SubT>(url, ser_type, schema_type, auto_init); }),
            "url"_a, "ser_type"_a = "", "schema_type"_a = vlink::SchemaType::kUnknown, "auto_init"_a = true);
  }
  bind_node_common<decltype(cls), SubT>(cls);
  cls.def(
         "listen",
         [](SubT& self, nb::callable callback) {
           return self.listen(make_value_callback<MsgT, Codec>(std::move(callback), "vlink::Subscriber.listen"));
         },
         "callback"_a)
      .def("set_latency_and_lost_enabled", &SubT::set_latency_and_lost_enabled, "enable"_a)
      .def("is_latency_and_lost_enabled", &SubT::is_latency_and_lost_enabled)
      .def("get_latency", &SubT::get_latency)
      .def("get_lost", &SubT::get_lost)
      .def("mark_as_getter", &SubT::mark_as_getter)
      .def("__repr__", [](const SubT& self) { return "Subscriber(url='" + self.get_url() + "')"; });
}

template <typename ServerT, typename ReqT, typename RespT, typename ReqCodec = PythonCodec<ReqT>,
          typename RespCodec = PythonCodec<RespT>, bool SecurityNode = false>
void bind_server(nb::module_& m, const char* name, const char* doc) {
  auto cls = nb::class_<ServerT>(m, name, doc);
  if constexpr (SecurityNode) {
    bind_node_security_ctor<decltype(cls), ServerT>(cls);
  } else {
    cls.def(nb::new_([](const std::string& url, const std::string& ser_type, vlink::SchemaType schema_type,
                        bool auto_init) { return make_url_node<ServerT>(url, ser_type, schema_type, auto_init); }),
            "url"_a, "ser_type"_a = "", "schema_type"_a = vlink::SchemaType::kUnknown, "auto_init"_a = true);
  }
  bind_node_common<decltype(cls), ServerT>(cls);
  cls.def(
         "listen",
         [](ServerT& self, nb::callable callback) {
           auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
           return self.listen([cb](const ReqT& req, RespT& resp) {
             if (!Py_IsInitialized()) return;
             nb::gil_scoped_acquire gil;
             try {
               nb::object result = cb->fn(ReqCodec::to_python(req));
               if (!result.is_none()) {
                 resp = RespCodec::from_python_owned(result);
               }
             } catch (std::exception&) {
               report_current_exception("vlink::Server.listen");
             }
           });
         },
         "callback"_a, "callback(request) -> response or None")
      .def(
          "listen_for_reply",
          [](ServerT& self, nb::callable callback) {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            return self.listen_for_reply([cb](uint64_t req_id, const ReqT& req) {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                cb->fn(req_id, ReqCodec::to_python(req));
              } catch (std::exception&) {
                report_current_exception("vlink::Server.listen_for_reply");
              }
            });
          },
          "callback"_a, "callback(req_id, request). Call reply(req_id, response) later")
      .def(
          "reply",
          [](ServerT& self, uint64_t req_id, nb::handle data) {
            return self.reply(req_id, RespCodec::from_python_owned(data));
          },
          "req_id"_a, "data"_a)
      .def("__repr__", [](const ServerT& self) { return "Server(url='" + self.get_url() + "')"; });
}

template <typename ServerT, typename ReqT, typename ReqCodec = PythonCodec<ReqT>, bool SecurityNode = false>
void bind_fire_forget_server(nb::module_& m, const char* name, const char* doc) {
  auto cls = nb::class_<ServerT>(m, name, doc);
  if constexpr (SecurityNode) {
    bind_node_security_ctor<decltype(cls), ServerT>(cls);
  } else {
    cls.def(nb::new_([](const std::string& url, const std::string& ser_type, vlink::SchemaType schema_type,
                        bool auto_init) { return make_url_node<ServerT>(url, ser_type, schema_type, auto_init); }),
            "url"_a, "ser_type"_a = "", "schema_type"_a = vlink::SchemaType::kUnknown, "auto_init"_a = true);
  }
  bind_node_common<decltype(cls), ServerT>(cls);
  cls.def(
         "listen",
         [](ServerT& self, nb::callable callback) {
           auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
           return self.listen([cb](const ReqT& req) {
             if (!Py_IsInitialized()) return;
             nb::gil_scoped_acquire gil;
             try {
               cb->fn(ReqCodec::to_python(req));
             } catch (std::exception&) {
               report_current_exception("vlink::FireForgetServer.listen");
             }
           });
         },
         "callback"_a, "callback(request) -> None")
      .def("__repr__", [](const ServerT& self) { return "FireForgetServer(url='" + self.get_url() + "')"; });
}

template <typename ClientT, typename ReqT, typename RespT, typename ReqCodec = PythonCodec<ReqT>,
          typename RespCodec = PythonCodec<RespT>, bool SecurityNode = false>
void bind_client(nb::module_& m, const char* name, const char* doc) {
  auto cls = nb::class_<ClientT>(m, name, doc);
  if constexpr (SecurityNode) {
    bind_node_security_ctor<decltype(cls), ClientT>(cls);
  } else {
    cls.def(nb::new_([](const std::string& url, const std::string& ser_type, vlink::SchemaType schema_type,
                        bool auto_init) { return make_url_node<ClientT>(url, ser_type, schema_type, auto_init); }),
            "url"_a, "ser_type"_a = "", "schema_type"_a = vlink::SchemaType::kUnknown, "auto_init"_a = true);
  }
  bind_node_common<decltype(cls), ClientT>(cls);
  cls.def(
         "detect_connected",
         [](ClientT& self, nb::callable callback) {
           self.detect_connected(make_connect_callback(std::move(callback), "vlink::Client.detect_connected"));
         },
         "callback"_a)
      .def(
          "wait_for_connected",
          [](ClientT& self, int timeout_ms) {
            nb::gil_scoped_release release;
            return self.wait_for_connected(std::chrono::milliseconds(timeout_ms));
          },
          "timeout_ms"_a = 5000)
      .def("is_connected", &ClientT::is_connected)
      .def(
          "invoke",
          [](ClientT& self, nb::handle data, int timeout_ms) -> nb::object {
            auto req = ReqCodec::from_python_owned(data);
            std::optional<RespT> res;
            {
              nb::gil_scoped_release release;
              res = self.invoke(req, std::chrono::milliseconds(timeout_ms));
            }
            if (res.has_value()) {
              return RespCodec::to_python(*res);
            }
            return nb::none();
          },
          "data"_a, "timeout_ms"_a = 5000)
      .def(
          "invoke_async",
          [](ClientT& self, nb::handle data, nb::callable callback) {
            auto req = ReqCodec::from_python_owned(data);
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            return self.invoke(req, [cb](const RespT& resp) {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                cb->fn(RespCodec::to_python(resp));
              } catch (std::exception&) {
                report_current_exception("vlink::Client.invoke_async");
              }
            });
          },
          "data"_a, "callback"_a)
      .def(
          "async_invoke",
          [](ClientT& self, nb::handle data) {
            auto req = ReqCodec::from_python_owned(data);
            nb::object py_future = nb::module_::import_("concurrent.futures").attr("Future")();
            auto future_ref = std::make_shared<GilSafePyObject>(nb::object(py_future));
            const bool accepted = self.invoke(req, [future_ref](const RespT& resp) {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                future_ref->obj.attr("set_result")(RespCodec::to_python(resp));
              } catch (std::exception&) {
                report_current_exception("vlink::Client.async_invoke.set_result");
              }
            });

            if (!accepted) {
              nb::object exc =
                  nb::module_::import_("builtins").attr("RuntimeError")("VLink async_invoke failed to submit request");
              py_future.attr("set_exception")(exc);
            }

            return py_future;
          },
          "data"_a, "Return a concurrent.futures.Future resolved with the response bytes.")
      .def("__repr__", [](const ClientT& self) {
        return "Client(url='" + self.get_url() + "', connected=" + (self.is_connected() ? "True" : "False") + ")";
      });
}

template <typename ClientT, typename ReqT, typename ReqCodec = PythonCodec<ReqT>, bool SecurityNode = false>
void bind_fire_forget_client(nb::module_& m, const char* name, const char* doc) {
  auto cls = nb::class_<ClientT>(m, name, doc);
  if constexpr (SecurityNode) {
    bind_node_security_ctor<decltype(cls), ClientT>(cls);
  } else {
    cls.def(nb::new_([](const std::string& url, const std::string& ser_type, vlink::SchemaType schema_type,
                        bool auto_init) { return make_url_node<ClientT>(url, ser_type, schema_type, auto_init); }),
            "url"_a, "ser_type"_a = "", "schema_type"_a = vlink::SchemaType::kUnknown, "auto_init"_a = true);
  }
  bind_node_common<decltype(cls), ClientT>(cls);
  cls.def(
         "detect_connected",
         [](ClientT& self, nb::callable callback) {
           self.detect_connected(
               make_connect_callback(std::move(callback), "vlink::FireForgetClient.detect_connected"));
         },
         "callback"_a)
      .def(
          "wait_for_connected",
          [](ClientT& self, int timeout_ms) {
            nb::gil_scoped_release release;
            return self.wait_for_connected(std::chrono::milliseconds(timeout_ms));
          },
          "timeout_ms"_a = 5000)
      .def("is_connected", &ClientT::is_connected)
      .def(
          "send",
          [](ClientT& self, nb::handle data) {
            auto req = ReqCodec::from_python_owned(data);
            nb::gil_scoped_release release;
            return self.send(req);
          },
          "data"_a)
      .def("__repr__", [](const ClientT& self) {
        return "FireForgetClient(url='" + self.get_url() + "', connected=" + (self.is_connected() ? "True" : "False") +
               ")";
      });
}

template <typename SetterT, typename ValueT, typename Codec = PythonCodec<ValueT>, bool SecurityNode = false>
void bind_setter(nb::module_& m, const char* name, const char* doc) {
  auto cls = nb::class_<SetterT>(m, name, doc);
  if constexpr (SecurityNode) {
    bind_node_security_ctor<decltype(cls), SetterT>(cls);
  } else {
    cls.def(nb::new_([](const std::string& url, const std::string& ser_type, vlink::SchemaType schema_type,
                        bool auto_init) { return make_url_node<SetterT>(url, ser_type, schema_type, auto_init); }),
            "url"_a, "ser_type"_a = "", "schema_type"_a = vlink::SchemaType::kUnknown, "auto_init"_a = true);
  }
  bind_node_common<decltype(cls), SetterT>(cls);
  cls.def(
         "set", [](SetterT& self, nb::handle data) { self.set(Codec::from_python_owned(data)); }, "data"_a)
      .def("mark_as_publisher", &SetterT::mark_as_publisher)
      .def("__repr__", [](const SetterT& self) { return "Setter(url='" + self.get_url() + "')"; });
}

template <typename GetterT, typename ValueT, typename Codec = PythonCodec<ValueT>, bool SecurityNode = false>
void bind_getter(nb::module_& m, const char* name, const char* doc) {
  auto cls = nb::class_<GetterT>(m, name, doc);
  if constexpr (SecurityNode) {
    bind_node_security_ctor<decltype(cls), GetterT>(cls);
  } else {
    cls.def(nb::new_([](const std::string& url, const std::string& ser_type, vlink::SchemaType schema_type,
                        bool auto_init) { return make_url_node<GetterT>(url, ser_type, schema_type, auto_init); }),
            "url"_a, "ser_type"_a = "", "schema_type"_a = vlink::SchemaType::kUnknown, "auto_init"_a = true);
  }
  bind_node_common<decltype(cls), GetterT>(cls);
  cls.def(
         "listen",
         [](GetterT& self, nb::callable callback) {
           return self.listen(make_value_callback<ValueT, Codec>(std::move(callback), "vlink::Getter.listen"));
         },
         "callback"_a)
      .def("get",
           [](GetterT& self) -> nb::object {
             auto result = self.get();
             if (result.has_value()) {
               return Codec::to_python(*result);
             }
             return nb::none();
           })
      .def(
          "wait_for_value",
          [](GetterT& self, int timeout_ms) {
            nb::gil_scoped_release release;
            return self.wait_for_value(std::chrono::milliseconds(timeout_ms));
          },
          "timeout_ms"_a = 5000)
      .def("set_change_reporting", &GetterT::set_change_reporting, "enable"_a)
      .def("get_change_reporting", &GetterT::get_change_reporting)
      .def("set_latency_and_lost_enabled", &GetterT::set_latency_and_lost_enabled, "enable"_a)
      .def("is_latency_and_lost_enabled", &GetterT::is_latency_and_lost_enabled)
      .def("get_latency", &GetterT::get_latency)
      .def("get_lost", &GetterT::get_lost)
      .def("mark_as_subscriber", &GetterT::mark_as_subscriber)
      .def("__repr__", [](const GetterT& self) { return "Getter(url='" + self.get_url() + "')"; });
}

}  // namespace

NB_MODULE(_vlink_nanobind, m) {
  m.doc() = "VLink: Transport-agnostic pub/sub, field, and RPC communication middleware";

  nb::enum_<vlink::ImplType>(m, "ImplType", "Node role type")
      .value("Unknown", vlink::kUnknownImplType)
      .value("Publisher", vlink::kPublisher)
      .value("Subscriber", vlink::kSubscriber)
      .value("Setter", vlink::kSetter)
      .value("Getter", vlink::kGetter)
      .value("Server", vlink::kServer)
      .value("Client", vlink::kClient);

  nb::enum_<vlink::TransportType>(m, "TransportType", "Transport backend")
      .value("Unknown", vlink::TransportType::kUnknown)
      .value("Intra", vlink::TransportType::kIntra)
      .value("Shm", vlink::TransportType::kShm)
      .value("Shm2", vlink::TransportType::kShm2)
      .value("Zenoh", vlink::TransportType::kZenoh)
      .value("Dds", vlink::TransportType::kDds)
      .value("Ddsc", vlink::TransportType::kDdsc)
      .value("Ddsr", vlink::TransportType::kDdsr)
      .value("Ddst", vlink::TransportType::kDdst)
      .value("Someip", vlink::TransportType::kSomeip)
      .value("Mqtt", vlink::TransportType::kMqtt)
      .value("Fdbus", vlink::TransportType::kFdbus)
      .value("Qnx", vlink::TransportType::kQnx);

  nb::enum_<vlink::InitType>(m, "InitType")
      .value("WithoutInit", vlink::InitType::kWithoutInit)
      .value("WithInit", vlink::InitType::kWithInit);

  nb::enum_<vlink::SecurityType>(m, "SecurityType")
      .value("WithoutSecurity", vlink::SecurityType::kWithoutSecurity)
      .value("WithSecurity", vlink::SecurityType::kWithSecurity);

  nb::enum_<vlink::ActionType>(m, "ActionType", "Message recording action")
      .value("Unknown", vlink::ActionType::kUnknownAction)
      .value("ClientRequest", vlink::ActionType::kClientRequest)
      .value("ClientResponse", vlink::ActionType::kClientResponse)
      .value("ServerRequest", vlink::ActionType::kServerRequest)
      .value("ServerResponse", vlink::ActionType::kServerResponse)
      .value("Publish", vlink::ActionType::kPublish)
      .value("Subscribe", vlink::ActionType::kSubscribe)
      .value("Set", vlink::ActionType::kSet)
      .value("Get", vlink::ActionType::kGet);

  nb::enum_<vlink::SchemaType>(m, "SchemaType", "Coarse schema family")
      .value("Unknown", vlink::SchemaType::kUnknown)
      .value("Protobuf", vlink::SchemaType::kProtobuf)
      .value("Flatbuffers", vlink::SchemaType::kFlatbuffers)
      .value("Raw", vlink::SchemaType::kRaw)
      .value("ZeroCopy", vlink::SchemaType::kZeroCopy);

  nb::class_<vlink::Bytes>(m, "Bytes", nb::type_slots(bytes_type_slots), "Versatile byte buffer")
      .def(nb::init<>())
      .def_static("init_memory_pool", &vlink::Bytes::init_memory_pool)
      .def_static("release_memory_pool", &vlink::Bytes::release_memory_pool)
      .def_static(
          "create", [](size_t size, uint8_t offset) { return vlink::Bytes::create(size, offset); }, "size"_a,
          "offset"_a = static_cast<uint8_t>(0))
      .def_static(
          "from_bytes",
          [](nb::handle data, uint8_t offset) {
            PythonBufferView view(data);
            return vlink::Bytes::deep_copy(view.data(), view.size(), offset);
          },
          "data"_a, "offset"_a = static_cast<uint8_t>(0))
      .def_static(
          "from_string", [](const std::string& s, uint8_t offset) { return vlink::Bytes::from_string(s, offset); },
          "s"_a, "offset"_a = static_cast<uint8_t>(0))
      .def_static(
          "from_user_input",
          [](const std::string& s) {
            bool ok = false;
            auto bytes = vlink::Bytes::from_user_input(s, &ok);
            if (!ok) throw std::runtime_error("Invalid hex/binary input");
            return bytes;
          },
          "hex_or_bin"_a)
      .def_static(
          "encode_to_base64",
          [](nb::handle data) {
            PythonBufferView view(data);
            auto b = vlink::Bytes::shallow_copy(view.data(), view.size());
            return vlink::Bytes::encode_to_base64(b);
          },
          "bytes"_a)
      .def_static("decode_from_base64", &vlink::Bytes::decode_from_base64, "str"_a)
      .def_static(
          "get_crc_32",
          [](nb::handle data) {
            PythonBufferView view(data);
            auto b = vlink::Bytes::shallow_copy(view.data(), view.size());
            return vlink::Bytes::get_crc_32(b);
          },
          "bytes"_a)
      .def_static(
          "get_crc_64",
          [](nb::handle data) {
            PythonBufferView view(data);
            auto b = vlink::Bytes::shallow_copy(view.data(), view.size());
            return vlink::Bytes::get_crc_64(b);
          },
          "bytes"_a)
      .def_static(
          "compress",
          [](nb::handle data, bool high_ratio) {
            PythonBufferView view(data);
            return vlink::Bytes::compress_data(view.data(), view.size(), high_ratio);
          },
          "bytes"_a, "high_ratio"_a = false)
      .def_static(
          "uncompress",
          [](nb::handle data, bool check_valid) {
            PythonBufferView view(data);
            return vlink::Bytes::uncompress_data(view.data(), view.size(), check_valid);
          },
          "bytes"_a, "check_valid"_a = true)
      .def_static(
          "is_compress_data",
          [](nb::handle data) {
            PythonBufferView view(data);
            return vlink::Bytes::is_compress_data(view.data(), view.size());
          },
          "bytes"_a)
      .def_static(
          "reverse_order",
          [](nb::handle data) {
            PythonBufferView view(data);
            auto b = vlink::Bytes::shallow_copy(view.data(), view.size());
            return vlink::Bytes::reverse_order(b);
          },
          "bytes"_a)
      .def_static(
          "convert_to_hex_str",
          [](nb::handle data) {
            PythonBufferView view(data);
            return vlink::Bytes::convert_to_hex_str(view.data(), view.size());
          },
          "bytes"_a)
      .def_static("is_little_endian", &vlink::Bytes::is_little_endian)
      .def_static("is_big_endian", &vlink::Bytes::is_big_endian)
      .def_static("stack_size", &vlink::Bytes::stack_size)
      .def("size", &vlink::Bytes::size)
      .def("real_size", &vlink::Bytes::real_size)
      .def("capacity", &vlink::Bytes::capacity)
      .def("offset", &vlink::Bytes::offset)
      .def("empty", &vlink::Bytes::empty)
      .def("is_owner", &vlink::Bytes::is_owner)
      .def("is_loaned", &vlink::Bytes::is_loaned)
      .def("is_ptr", &vlink::Bytes::is_ptr)
      .def("clear", &vlink::Bytes::clear)
      .def("resize", &vlink::Bytes::resize, "size"_a)
      .def("reserve", &vlink::Bytes::reserve, "capacity"_a)
      .def("shrink_to", &vlink::Bytes::shrink_to, "size"_a)
      .def("deep_copy_self", &vlink::Bytes::deep_copy_self)
      .def("to_bytes", [](const vlink::Bytes& self) { return nb::bytes(self.data(), self.size()); })
      .def("to_string", [](const vlink::Bytes& self) { return self.to_string(); })
      .def("to_raw_data", [](const vlink::Bytes& self) { return self.to_raw_data(); })
      .def("hex", [](const vlink::Bytes& self) { return vlink::Bytes::convert_to_hex_str(self.data(), self.size()); })
      .def("__len__", [](const vlink::Bytes& self) { return self.size(); })
      .def("__bool__", [](const vlink::Bytes& self) { return !self.empty(); })
      .def("__bytes__", [](const vlink::Bytes& self) { return nb::bytes(self.data(), self.size()); })
      .def("__eq__", [](const vlink::Bytes& a, const vlink::Bytes& b) { return a == b; })
      .def("__ne__", [](const vlink::Bytes& a, const vlink::Bytes& b) { return a != b; })
      .def("__getitem__",
           [](const vlink::Bytes& self, size_t i) -> uint8_t {
             if (i >= self.size()) throw nb::index_error();
             return self.data()[i];
           })
      .def("__repr__", [](const vlink::Bytes& self) {
        std::string repr = "Bytes(size=" + std::to_string(self.size());
        if (self.is_owner()) {
          repr += ", owned";
        } else if (self.is_loaned()) {
          repr += ", loaned";
        } else {
          repr += ", shallow";
        }
        return repr + ")";
      });

  nb::class_<vlink::Version>(m, "Version", "Semantic versioning")
      .def(nb::init<>())
      .def(nb::init<uint8_t, uint8_t, uint8_t>(), "major"_a, "minor"_a, "patch"_a)
      .def_static("from_string", &vlink::Version::from_string, "str"_a)
      .def("to_string", &vlink::Version::to_string)
      .def("is_valid", &vlink::Version::is_valid)
      .def_rw("major", &vlink::Version::major)
      .def_rw("minor", &vlink::Version::minor)
      .def_rw("patch", &vlink::Version::patch)
      .def("__eq__", [](const vlink::Version& a, const vlink::Version& b) { return a == b; })
      .def("__ne__", [](const vlink::Version& a, const vlink::Version& b) { return a != b; })
      .def("__lt__", [](const vlink::Version& a, const vlink::Version& b) { return a < b; })
      .def("__gt__", [](const vlink::Version& a, const vlink::Version& b) { return a > b; })
      .def("__repr__", [](const vlink::Version& self) { return "Version(" + self.to_string() + ")"; });

  nb::class_<vlink::SchemaData>(m, "SchemaData", "Runtime schema descriptor")
      .def(nb::init<>())
      .def_rw("name", &vlink::SchemaData::name)
      .def_rw("encoding", &vlink::SchemaData::encoding)
      .def_rw("schema_type", &vlink::SchemaData::schema_type)
      .def_rw("data", &vlink::SchemaData::data)
      .def_static("is_valid_type", &vlink::SchemaData::is_valid_type, "schema_type"_a)
      .def_static("is_real_type", &vlink::SchemaData::is_real_type, "schema_type"_a)
      .def_static(
          "convert_type",
          [](vlink::SchemaType schema_type) { return std::string(vlink::SchemaData::convert_type(schema_type)); },
          "schema_type"_a)
      .def_static("convert_encoding", &vlink::SchemaData::convert_encoding, "encoding"_a)
      .def_static(
          "infer_ser_type", [](const std::string& ser_type) { return vlink::SchemaData::infer_ser_type(ser_type); },
          "ser_type"_a)
      .def_static(
          "resolve_type",
          [](vlink::SchemaType schema_type, const std::string& ser_type, const std::string& encoding) {
            return vlink::SchemaData::resolve_type(schema_type, ser_type, encoding);
          },
          "schema_type"_a, "ser_type"_a = "", "encoding"_a = "")
      .def("__bool__",
           [](const vlink::SchemaData& self) {
             return !self.name.empty() || !self.encoding.empty() || self.schema_type != vlink::SchemaType::kUnknown ||
                    !self.data.empty();
           })
      .def("__repr__", [](const vlink::SchemaData& self) {
        return "SchemaData(name='" + self.name + "', encoding='" + self.encoding +
               "', size=" + std::to_string(self.data.size()) + ")";
      });

  nb::class_<vlink::SampleLostInfo>(m, "SampleLostInfo")
      .def(nb::init<>())
      .def_rw("total", &vlink::SampleLostInfo::total)
      .def_rw("lost", &vlink::SampleLostInfo::lost)
      .def("__repr__", [](const vlink::SampleLostInfo& self) {
        return "SampleLostInfo(total=" + std::to_string(self.total) + ", lost=" + std::to_string(self.lost) + ")";
      });

  nb::enum_<vlink::Logger::Level>(m, "LogLevel")
      .value("Trace", vlink::Logger::kTrace)
      .value("Debug", vlink::Logger::kDebug)
      .value("Info", vlink::Logger::kInfo)
      .value("Warn", vlink::Logger::kWarn)
      .value("Error", vlink::Logger::kError)
      .value("Fatal", vlink::Logger::kFatal)
      .value("Off", vlink::Logger::kOff);

  nb::class_<vlink::Logger>(m, "Logger")
      .def_static("init", &vlink::Logger::init, "app_name"_a = "", "log_path"_a = "")
      .def_static("get", &vlink::Logger::get, nb::rv_policy::reference)
      .def_static("flush", &vlink::Logger::flush)
      .def_static("set_console_level", &vlink::Logger::set_console_level, "level"_a)
      .def_static("get_console_level", &vlink::Logger::get_console_level)
      .def_static("set_file_level", &vlink::Logger::set_file_level, "level"_a)
      .def_static("get_file_level", &vlink::Logger::get_file_level)
      .def_static("set_console_fmt_enable", &vlink::Logger::set_console_fmt_enable, "enable"_a)
      .def_static("get_console_fmt_enable", &vlink::Logger::get_console_fmt_enable)
      .def_static(
          "set_stream_flag",
          [](int flags) { vlink::Logger::set_stream_flag(static_cast<std::ios_base::fmtflags>(flags)); }, "flags"_a)
      .def_static("get_stream_flag", []() { return static_cast<int>(vlink::Logger::get_stream_flag()); })
      .def_static("set_stream_precision", &vlink::Logger::set_stream_precision, "precision"_a)
      .def_static("get_stream_precision", &vlink::Logger::get_stream_precision)
      .def_static("set_stream_width", &vlink::Logger::set_stream_width, "width"_a)
      .def_static("get_stream_width", &vlink::Logger::get_stream_width)
      .def_static("is_busy", &vlink::Logger::is_busy)
      .def_static("is_writable", &vlink::Logger::is_writable, "level"_a)
      .def_static("enable_backtrace", &vlink::Logger::enable_backtrace, "size"_a)
      .def_static("disable_backtrace", &vlink::Logger::disable_backtrace)
      .def_static("dump_backtrace", &vlink::Logger::dump_backtrace)
      .def_static(
          "register_console_handler",
          [](nb::callable callback) {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            vlink::Logger::register_console_handler([cb](vlink::Logger::Level level, std::string_view msg) {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                cb->fn(level, std::string(msg));
              } catch (std::exception&) {
                report_current_exception("vlink::Logger.register_console_handler");
              }
            });
          },
          "callback"_a)
      .def_static(
          "register_file_handler",
          [](nb::callable callback) {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            vlink::Logger::register_file_handler([cb](vlink::Logger::Level level, std::string_view msg) {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                cb->fn(level, std::string(msg));
              } catch (std::exception&) {
                report_current_exception("vlink::Logger.register_file_handler");
              }
            });
          },
          "callback"_a);

  m.def("log_trace", [](const std::string& msg) { vlink::Logger::print<vlink::Logger::kTrace>(msg); }, "msg"_a);
  m.def("log_debug", [](const std::string& msg) { vlink::Logger::print<vlink::Logger::kDebug>(msg); }, "msg"_a);
  m.def("log_info", [](const std::string& msg) { vlink::Logger::print<vlink::Logger::kInfo>(msg); }, "msg"_a);
  m.def("log_warn", [](const std::string& msg) { vlink::Logger::print<vlink::Logger::kWarn>(msg); }, "msg"_a);
  m.def("log_error", [](const std::string& msg) { vlink::Logger::print<vlink::Logger::kError>(msg); }, "msg"_a);
  m.def("log_fatal", [](const std::string& msg) { vlink::Logger::print<vlink::Logger::kFatal>(msg); }, "msg"_a);

  nb::enum_<vlink::ElapsedTimer::Method>(m, "TimerMethod")
      .value("CpuTimestamp", vlink::ElapsedTimer::kCpuTimestamp)
      .value("CpuActiveTime", vlink::ElapsedTimer::kCpuActiveTime);
  nb::enum_<vlink::ElapsedTimer::Accuracy>(m, "TimerAccuracy")
      .value("Milli", vlink::ElapsedTimer::kMilli)
      .value("Micro", vlink::ElapsedTimer::kMicro)
      .value("Nano", vlink::ElapsedTimer::kNano);

  nb::class_<vlink::ElapsedTimer>(m, "ElapsedTimer")
      .def(nb::init<>())
      .def(nb::init<vlink::ElapsedTimer::Method>(), "method"_a)
      .def(nb::init<vlink::ElapsedTimer::Accuracy>(), "accuracy"_a)
      .def(nb::init<vlink::ElapsedTimer::Method, vlink::ElapsedTimer::Accuracy>(), "method"_a, "accuracy"_a)
      .def_static("get_sys_timestamp", &vlink::ElapsedTimer::get_sys_timestamp,
                  "accuracy"_a = vlink::ElapsedTimer::kMilli, "high_resolution"_a = true)
      .def_static("get_cpu_timestamp", &vlink::ElapsedTimer::get_cpu_timestamp,
                  "accuracy"_a = vlink::ElapsedTimer::kMilli, "high_resolution"_a = true)
      .def_static("get_cpu_active_time", &vlink::ElapsedTimer::get_cpu_active_time,
                  "accuracy"_a = vlink::ElapsedTimer::kMilli)
      .def("get_method", &vlink::ElapsedTimer::get_method)
      .def("get_accuracy", &vlink::ElapsedTimer::get_accuracy)
      .def("start", &vlink::ElapsedTimer::start)
      .def("stop", &vlink::ElapsedTimer::stop)
      .def("restart", &vlink::ElapsedTimer::restart)
      .def("is_active", &vlink::ElapsedTimer::is_active)
      .def("get", &vlink::ElapsedTimer::get);

  nb::class_<vlink::DeadlineTimer>(m, "DeadlineTimer")
      .def(nb::init<>())
      .def(nb::init<int64_t>(), "interval_ms"_a)
      .def(nb::init<int64_t, vlink::ElapsedTimer::Accuracy>(), "interval"_a, "accuracy"_a)
      .def("set_deadline", &vlink::DeadlineTimer::set_deadline, "interval"_a)
      .def("set_deadline_abs", &vlink::DeadlineTimer::set_deadline_abs, "abs_deadline"_a)
      .def("reset", &vlink::DeadlineTimer::reset)
      .def("deadline", &vlink::DeadlineTimer::deadline)
      .def("remaining_time", &vlink::DeadlineTimer::remaining_time)
      .def("has_expired", &vlink::DeadlineTimer::has_expired)
      .def("is_valid", &vlink::DeadlineTimer::is_valid)
      .def("get_accuracy", &vlink::DeadlineTimer::get_accuracy);

  nb::enum_<vlink::MessageLoop::Type>(m, "MessageLoopType")
      .value("Normal", vlink::MessageLoop::kNormalType)
      .value("Lockfree", vlink::MessageLoop::kLockfreeType)
      .value("Priority", vlink::MessageLoop::kPriorityType);
  nb::enum_<vlink::MessageLoop::Strategy>(m, "MessageLoopStrategy")
      .value("Optimization", vlink::MessageLoop::kOptimizationStrategy)
      .value("Pop", vlink::MessageLoop::kPopStrategy)
      .value("Block", vlink::MessageLoop::kBlockStrategy);
  nb::enum_<vlink::MessageLoop::Priority>(m, "TaskPriority")
      .value("No", vlink::MessageLoop::kNoPriority)
      .value("Lowest", vlink::MessageLoop::kLowestPriority)
      .value("Timer", vlink::MessageLoop::kTimerPriority)
      .value("Normal", vlink::MessageLoop::kNormalPriority)
      .value("Highest", vlink::MessageLoop::kHighestPriority);

  nb::enum_<vlink::ThreadPool::Type>(m, "ThreadPoolType")
      .value("Normal", vlink::ThreadPool::kNormalType)
      .value("Lockfree", vlink::ThreadPool::kLockfreeType);
  nb::enum_<vlink::ThreadPool::Strategy>(m, "ThreadPoolStrategy")
      .value("Optimization", vlink::ThreadPool::kOptimizationStrategy)
      .value("Pop", vlink::ThreadPool::kPopStrategy)
      .value("Block", vlink::ThreadPool::kBlockStrategy);

  nb::class_<vlink::MessageLoop>(m, "MessageLoop", "Single-threaded event loop")
      .def(nb::init<>())
      .def(nb::init<vlink::MessageLoop::Type>(), "type"_a)
      .def("set_name", &vlink::MessageLoop::set_name, "name"_a)
      .def("get_name", &vlink::MessageLoop::get_name)
      .def("get_type", &vlink::MessageLoop::get_type)
      .def("get_strategy", &vlink::MessageLoop::get_strategy)
      .def("set_strategy", &vlink::MessageLoop::set_strategy, "strategy"_a)
      .def(
          "post_task",
          [](vlink::MessageLoop& self, nb::callable callback) {
            return self.post_task(make_void_callback(std::move(callback), "vlink::MessageLoop.post_task"));
          },
          "callback"_a)
      .def(
          "post_task_with_priority",
          [](vlink::MessageLoop& self, nb::callable callback, vlink::MessageLoop::Priority priority) {
            return self.post_task_with_priority(
                make_void_callback(std::move(callback), "vlink::MessageLoop.post_task_with_priority"),
                static_cast<uint16_t>(priority));
          },
          "callback"_a, "priority"_a)
      .def(
          "register_begin_handler",
          [](vlink::MessageLoop& self, nb::callable callback) {
            self.register_begin_handler(
                make_void_callback(std::move(callback), "vlink::MessageLoop.register_begin_handler"));
          },
          "callback"_a)
      .def(
          "register_end_handler",
          [](vlink::MessageLoop& self, nb::callable callback) {
            self.register_end_handler(
                make_void_callback(std::move(callback), "vlink::MessageLoop.register_end_handler"));
          },
          "callback"_a)
      .def(
          "register_idle_handler",
          [](vlink::MessageLoop& self, nb::callable callback) {
            self.register_idle_handler(
                make_void_callback(std::move(callback), "vlink::MessageLoop.register_idle_handler"));
          },
          "callback"_a)
      .def("run",
           [](vlink::MessageLoop& self) {
             nb::gil_scoped_release release;
             return self.run();
           })
      .def("async_run",
           [](vlink::MessageLoop& self) {
             nb::gil_scoped_release release;
             return self.async_run();
           })
      .def("spin",
           [](vlink::MessageLoop& self) {
             nb::gil_scoped_release release;
             return self.spin();
           })
      .def(
          "spin_once",
          [](vlink::MessageLoop& self, bool block) {
            nb::gil_scoped_release release;
            return self.spin_once(block);
          },
          "block"_a = true)
      .def(
          "exec_task",
          [](vlink::MessageLoop& self, uint32_t delay_ms, nb::callable callback, uint16_t priority,
             uint32_t schedule_timeout_ms, uint32_t execution_timeout_ms) {
            vlink::Schedule::Config cfg(delay_ms, priority, schedule_timeout_ms, execution_timeout_ms);
            auto status = self.exec_task(cfg, make_void_callback(std::move(callback), "vlink::MessageLoop.exec_task"));
            return status.is_valid();
          },
          "delay_ms"_a, "callback"_a, "priority"_a = 0, "schedule_timeout_ms"_a = 0, "execution_timeout_ms"_a = 0,
          "Post a delayed/scheduled task. Returns True if posted successfully")
      .def(
          "quit",
          [](vlink::MessageLoop& self, bool force) {
            nb::gil_scoped_release release;
            return self.quit(force);
          },
          "force"_a = false)
      .def(
          "wait_for_quit",
          [](vlink::MessageLoop& self, int timeout_ms, bool check) {
            nb::gil_scoped_release release;
            return self.wait_for_quit(timeout_ms, check);
          },
          "timeout_ms"_a = -1, "check"_a = true)
      .def(
          "wait_for_idle",
          [](vlink::MessageLoop& self, int timeout_ms, bool check) {
            nb::gil_scoped_release release;
            return self.wait_for_idle(timeout_ms, check);
          },
          "timeout_ms"_a = -1, "check"_a = true)
      .def("wakeup", &vlink::MessageLoop::wakeup)
      .def("reset_lockfree_capacity", &vlink::MessageLoop::reset_lockfree_capacity)
      .def("is_running", &vlink::MessageLoop::is_running)
      .def("is_busy", &vlink::MessageLoop::is_busy)
      .def("is_ready_to_quit", &vlink::MessageLoop::is_ready_to_quit)
      .def("is_in_same_thread", &vlink::MessageLoop::is_in_same_thread)
      .def("get_task_count", &vlink::MessageLoop::get_task_count)
      .def("get_max_task_count", &vlink::MessageLoop::get_max_task_count)
      .def("get_max_timer_count", &vlink::MessageLoop::get_max_timer_count)
      .def("get_max_elapsed_time", &vlink::MessageLoop::get_max_elapsed_time);

  nb::class_<vlink::Timer>(m, "Timer", "Event-loop-driven periodic timer")
      .def(nb::init<>())
      .def(nb::init<vlink::MessageLoop*>(), "loop"_a, nb::keep_alive<1, 2>())
      .def(
          "__init__",
          [](nb::pointer_and_handle<vlink::Timer> v, vlink::MessageLoop* loop, uint32_t interval_ms,
             int32_t loop_count) { new (static_cast<vlink::Timer*>(v.p)) vlink::Timer(loop, interval_ms, loop_count); },
          "loop"_a, "interval_ms"_a, "loop_count"_a = vlink::Timer::kInfinite, nb::keep_alive<1, 2>())
      .def(
          "__init__",
          [](nb::pointer_and_handle<vlink::Timer> v, vlink::MessageLoop* loop, uint32_t interval_ms, int32_t loop_count,
             nb::callable callback) {
            new (static_cast<vlink::Timer*>(v.p)) vlink::Timer(
                loop, interval_ms, loop_count, make_void_callback(std::move(callback), "vlink::Timer.__init__"));
          },
          "loop"_a, "interval_ms"_a, "loop_count"_a, "callback"_a, nb::keep_alive<1, 2>())
      .def(
          "__init__",
          [](nb::pointer_and_handle<vlink::Timer> v, uint32_t interval_ms, int32_t loop_count) {
            new (static_cast<vlink::Timer*>(v.p)) vlink::Timer(interval_ms, loop_count);
          },
          "interval_ms"_a, "loop_count"_a = vlink::Timer::kInfinite)
      .def(
          "__init__",
          [](nb::pointer_and_handle<vlink::Timer> v, uint32_t interval_ms, nb::callable callback) {
            new (static_cast<vlink::Timer*>(v.p)) vlink::Timer(
                interval_ms, vlink::Timer::kInfinite, make_void_callback(std::move(callback), "vlink::Timer.__init__"));
          },
          "interval_ms"_a, "callback"_a)
      .def(
          "__init__",
          [](nb::pointer_and_handle<vlink::Timer> v, uint32_t interval_ms, int32_t loop_count, nb::callable callback) {
            new (static_cast<vlink::Timer*>(v.p))
                vlink::Timer(interval_ms, loop_count, make_void_callback(std::move(callback), "vlink::Timer.__init__"));
          },
          "interval_ms"_a, "loop_count"_a, "callback"_a)
      .def("attach", &vlink::Timer::attach, "loop"_a, nb::keep_alive<1, 2>())
      .def("detach", &vlink::Timer::detach)
      .def(
          "start",
          [](vlink::Timer& self, nb::object callback) {
            if (callback.is_none()) {
              self.start();
              return;
            }
            self.start(make_void_callback(nb::cast<nb::callable>(callback), "vlink::Timer.start"));
          },
          "callback"_a = nb::none())
      .def(
          "set_callback",
          [](vlink::Timer& self, nb::callable callback) {
            self.set_callback(make_void_callback(std::move(callback), "vlink::Timer.set_callback"));
          },
          "callback"_a)
      .def("restart", &vlink::Timer::restart)
      .def("stop", &vlink::Timer::stop)
      .def("is_active", &vlink::Timer::is_active)
      .def("is_strict", &vlink::Timer::is_strict)
      .def("get_interval", &vlink::Timer::get_interval)
      .def("get_loop_count", &vlink::Timer::get_loop_count)
      .def("get_remain_loop_count", &vlink::Timer::get_remain_loop_count)
      .def("get_invoke_count", &vlink::Timer::get_invoke_count)
      .def("get_priority", &vlink::Timer::get_priority)
      .def("set_interval", &vlink::Timer::set_interval, "interval_ms"_a)
      .def("set_loop_count", &vlink::Timer::set_loop_count, "count"_a)
      .def("set_strict", &vlink::Timer::set_strict, "strict"_a)
      .def("set_priority", &vlink::Timer::set_priority, "priority"_a)
      .def("get_message_loop", &vlink::Timer::get_message_loop, nb::rv_policy::reference)
      .def_static(
          "call_once",
          [](vlink::MessageLoop* loop, uint32_t interval_ms, nb::callable callback, uint16_t priority) {
            return vlink::Timer::call_once(loop, interval_ms,
                                           make_void_callback(std::move(callback), "vlink::Timer.call_once"), priority);
          },
          "loop"_a, "interval_ms"_a, "callback"_a, "priority"_a = 0);
  m.attr("TIMER_INFINITE") = vlink::Timer::kInfinite;

  nb::class_<vlink::ThreadPool>(m, "ThreadPool")
      .def(nb::init<size_t>(), "thread_count"_a = static_cast<size_t>(4))
      .def(nb::init<size_t, vlink::ThreadPool::Type>(), "thread_count"_a, "type"_a)
      .def("set_name", &vlink::ThreadPool::set_name, "name"_a)
      .def("get_name", &vlink::ThreadPool::get_name)
      .def("get_type", &vlink::ThreadPool::get_type)
      .def("get_strategy", &vlink::ThreadPool::get_strategy)
      .def("set_strategy", &vlink::ThreadPool::set_strategy, "strategy"_a)
      .def(
          "post_task",
          [](vlink::ThreadPool& self, nb::callable callback) {
            auto cb = make_void_callback(std::move(callback), "vlink::ThreadPool.post_task");
            nb::gil_scoped_release release;
            return self.post_task(std::move(cb));
          },
          "callback"_a)
      .def("shutdown",
           [](vlink::ThreadPool& self) {
             nb::gil_scoped_release release;
             return self.shutdown();
           })
      .def("get_task_count", &vlink::ThreadPool::get_task_count)
      .def("get_max_task_count", &vlink::ThreadPool::get_max_task_count)
      .def("is_in_work_thread", &vlink::ThreadPool::is_in_work_thread);

  nb::class_<vlink::SpinLock>(m, "SpinLock")
      .def(nb::init<>())
      .def("lock", &vlink::SpinLock::lock)
      .def("try_lock", &vlink::SpinLock::try_lock)
      .def("unlock", &vlink::SpinLock::unlock)
      .def("__enter__",
           [](vlink::SpinLock& self) -> vlink::SpinLock& {
             self.lock();
             return self;
           })
      .def("__exit__", [](vlink::SpinLock& self, nb::object, nb::object, nb::object) { self.unlock(); });

  nb::class_<vlink::CpuProfiler>(m, "CpuProfiler", "CPU active-time profiler")
      .def(nb::init<>())
      .def_static("is_global_enabled", &vlink::CpuProfiler::is_global_enabled)
      .def("begin", &vlink::CpuProfiler::begin)
      .def("end", &vlink::CpuProfiler::end)
      .def("get", &vlink::CpuProfiler::get)
      .def("restart", &vlink::CpuProfiler::restart)
      .def("__enter__",
           [](vlink::CpuProfiler& self) -> vlink::CpuProfiler& {
             self.begin();
             return self;
           })
      .def("__exit__", [](vlink::CpuProfiler& self, nb::object, nb::object, nb::object) { self.end(); });

  nb::class_<vlink::CpuProfilerGuard>(m, "CpuProfilerGuard", "RAII guard that brackets CpuProfiler.begin/end")
      .def(nb::init<vlink::CpuProfiler*>(), "profiler"_a, nb::keep_alive<1, 2>());

  nb::class_<vlink::MultiLoop, vlink::MessageLoop>(m, "MultiLoop", "Thread-pool backed MessageLoop")
      .def(nb::init<size_t>(), "thread_num"_a = static_cast<size_t>(4))
      .def(nb::init<size_t, vlink::MessageLoop::Type>(), "thread_num"_a, "type"_a);

  nb::class_<vlink::WheelTimer>(m, "WheelTimer", "Hierarchical timing wheel")
      .def(nb::init<uint32_t, uint32_t>(), "slots"_a, "interval_ms"_a)
      .def("start", &vlink::WheelTimer::start)
      .def("stop", &vlink::WheelTimer::stop)
      .def("pause", &vlink::WheelTimer::pause)
      .def("resume", &vlink::WheelTimer::resume)
      .def("wakeup", &vlink::WheelTimer::wakeup)
      .def("is_running", &vlink::WheelTimer::is_running)
      .def(
          "add",
          [](vlink::WheelTimer& self, uint32_t timeout_ms, nb::callable callback,
             uint32_t repeat_ms) -> vlink::WheelTimer::Key {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            return self.add(
                timeout_ms,
                [cb](vlink::WheelTimer::Key key) {
                  if (!Py_IsInitialized()) return;
                  nb::gil_scoped_acquire gil;
                  try {
                    cb->fn(key);
                  } catch (std::exception&) {
                    report_current_exception("vlink::WheelTimer.add");
                  }
                },
                repeat_ms);
          },
          "timeout_ms"_a, "callback"_a, "repeat_ms"_a = 0,
          "Schedule callback(key) once after timeout_ms; if repeat_ms>0 reschedule that interval. Returns Key.")
      .def("remove", &vlink::WheelTimer::remove, "key"_a)
      .def("get_remaining_time", &vlink::WheelTimer::get_remaining_time, "key"_a)
      .def("set_catchup_limit", &vlink::WheelTimer::set_catchup_limit, "max_slots_to_catch_up"_a);

  auto mp_cls = nb::class_<vlink::MemoryPool>(m, "MemoryPool", "Tiered (pyramid) memory pool with per-tier statistics");

  nb::class_<vlink::MemoryPool::Tier>(mp_cls, "Tier")
      .def(nb::init<>())
      .def(
          "__init__",
          [](vlink::MemoryPool::Tier* self, size_t max_size, size_t blocks_per_chunk) {
            new (self) vlink::MemoryPool::Tier{max_size, blocks_per_chunk};
          },
          "max_size"_a, "blocks_per_chunk"_a)
      .def_rw("max_size", &vlink::MemoryPool::Tier::max_size)
      .def_rw("blocks_per_chunk", &vlink::MemoryPool::Tier::blocks_per_chunk);

  nb::class_<vlink::MemoryPool::Config>(mp_cls, "Config")
      .def(nb::init<>())
      .def_rw("tiers", &vlink::MemoryPool::Config::tiers)
      .def_rw("prealloc", &vlink::MemoryPool::Config::prealloc);

  nb::class_<vlink::MemoryPool::TierStats>(mp_cls, "TierStats")
      .def_ro("max_size", &vlink::MemoryPool::TierStats::max_size)
      .def_ro("blocks_per_chunk", &vlink::MemoryPool::TierStats::blocks_per_chunk)
      .def_ro("block_size", &vlink::MemoryPool::TierStats::block_size)
      .def_ro("hit_count", &vlink::MemoryPool::TierStats::hit_count)
      .def_ro("deallocate_count", &vlink::MemoryPool::TierStats::deallocate_count)
      .def_ro("in_use_blocks", &vlink::MemoryPool::TierStats::in_use_blocks)
      .def_ro("chunk_count", &vlink::MemoryPool::TierStats::chunk_count)
      .def_ro("upstream_alloc_count", &vlink::MemoryPool::TierStats::upstream_alloc_count)
      .def_ro("upstream_alloc_bytes", &vlink::MemoryPool::TierStats::upstream_alloc_bytes);

  nb::class_<vlink::MemoryPool::OversizedStats>(mp_cls, "OversizedStats")
      .def_ro("alloc_count", &vlink::MemoryPool::OversizedStats::alloc_count)
      .def_ro("alloc_bytes", &vlink::MemoryPool::OversizedStats::alloc_bytes)
      .def_ro("dealloc_count", &vlink::MemoryPool::OversizedStats::dealloc_count);

  mp_cls.def(nb::init<>())
      .def(nb::init<int, bool>(), "level"_a, "prealloc"_a = false)
      .def(nb::init<const vlink::MemoryPool::Config&>(), "config"_a)
      .def("get_tier_count", &vlink::MemoryPool::get_tier_count)
      .def("get_stats", &vlink::MemoryPool::get_stats)
      .def("get_oversized_stats", &vlink::MemoryPool::get_oversized_stats)
      .def("reset_stats", &vlink::MemoryPool::reset_stats)
      .def("clear", &vlink::MemoryPool::clear)
      .def("trim", &vlink::MemoryPool::trim)
      .def_static("get_default_config", &vlink::MemoryPool::get_default_config)
      .def_static("global_instance", &vlink::MemoryPool::global_instance, "use_env_level"_a = true,
                  nb::rv_policy::reference);
  mp_cls.attr("kBlockAlignment") = vlink::MemoryPool::kBlockAlignment;

#ifdef VLINK_ENABLE_BASE_MEMORY_RESOURCE
  nb::class_<vlink::MemoryResource>(m, "MemoryResource",
                                    "std::pmr::memory_resource adapter delegating to a vlink::MemoryPool")
      .def(nb::init<>())
      .def(nb::init<int, bool>(), "level"_a, "prealloc"_a = false)
      .def(nb::init<const vlink::MemoryPool::Config&>(), "config"_a)
      .def("get_memory_pool", &vlink::MemoryResource::get_memory_pool, nb::rv_policy::reference)
      .def("trim", &vlink::MemoryResource::trim)
      .def_static("global_instance", &vlink::MemoryResource::global_instance, "use_env_level"_a = true,
                  nb::rv_policy::reference);
#endif

  nb::class_<vlink::Process> proc(m, "Process", "Child process management");

  nb::enum_<vlink::Process::State>(proc, "State")
      .value("NotRunning", vlink::Process::kNotRunningState)
      .value("Starting", vlink::Process::kStartingState)
      .value("Running", vlink::Process::kRunningState);
  nb::enum_<vlink::Process::ExitStatus>(proc, "ExitStatus")
      .value("Normal", vlink::Process::kNormalExitStatus)
      .value("Crash", vlink::Process::kCrashExitStatus);
  nb::enum_<vlink::Process::Error>(proc, "Error")
      .value("NoError", vlink::Process::kNoError)
      .value("UnknownError", vlink::Process::kUnknownError)
      .value("StartError", vlink::Process::kStartError)
      .value("CrashedError", vlink::Process::kCrashedError)
      .value("TimedOutError", vlink::Process::kTimedOutError)
      .value("WriteError", vlink::Process::kWriteError)
      .value("ReadError", vlink::Process::kReadError)
      .value("BufferOverflowError", vlink::Process::kBufferOverflowError);
  nb::enum_<vlink::Process::Mode>(proc, "Mode")
      .value("Separate", vlink::Process::kSeparateMode)
      .value("Merged", vlink::Process::kMergedMode)
      .value("Forwarded", vlink::Process::kForwardedMode)
      .value("ForwardedOutput", vlink::Process::kForwardedOutputMode)
      .value("ForwardedError", vlink::Process::kForwardedErrorMode);

  proc.def(nb::init<>())
      .def("get_state", &vlink::Process::get_state)
      .def("get_error", &vlink::Process::get_error)
      .def("get_exit_code", &vlink::Process::get_exit_code)
      .def("get_exit_status", &vlink::Process::get_exit_status)
      .def("is_running", &vlink::Process::is_running)
      .def("get_process_id", &vlink::Process::get_process_id)
      .def("set_max_buffer_size", &vlink::Process::set_max_buffer_size, "size"_a)
      .def("get_max_buffer_size", &vlink::Process::get_max_buffer_size)
      .def("set_process_mode", &vlink::Process::set_process_mode, "mode"_a)
      .def("get_process_mode", &vlink::Process::get_process_mode)
      .def("set_inherit_environment", &vlink::Process::set_inherit_environment, "inherit"_a)
      .def("get_inherit_environment", &vlink::Process::get_inherit_environment)
      .def("set_working_directory", &vlink::Process::set_working_directory, "dir"_a)
      .def("get_working_directory", &vlink::Process::get_working_directory)
      .def("set_environment", &vlink::Process::set_environment, "env_map"_a)
      .def("get_environment", &vlink::Process::get_environment)
      .def(
          "register_error_callback",
          [](vlink::Process& self, nb::callable callback) {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            self.register_error_callback([cb](vlink::Process::Error error) {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                cb->fn(error);
              } catch (std::exception&) {
                report_current_exception("vlink::Process.register_error_callback");
              }
            });
          },
          "callback"_a)
      .def(
          "register_finished_callback",
          [](vlink::Process& self, nb::callable callback) {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            self.register_finished_callback([cb](int code, vlink::Process::ExitStatus status) {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                cb->fn(code, status);
              } catch (std::exception&) {
                report_current_exception("vlink::Process.register_finished_callback");
              }
            });
          },
          "callback"_a)
      .def(
          "register_state_changed_callback",
          [](vlink::Process& self, nb::callable callback) {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            self.register_state_changed_callback([cb](vlink::Process::State state) {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                cb->fn(state);
              } catch (std::exception&) {
                report_current_exception("vlink::Process.register_state_changed_callback");
              }
            });
          },
          "callback"_a)
      .def(
          "register_ready_read_stdout_callback",
          [](vlink::Process& self, nb::callable callback) {
            self.register_ready_read_stdout_callback(
                make_void_callback(std::move(callback), "vlink::Process.register_ready_read_stdout_callback"));
          },
          "callback"_a)
      .def(
          "register_ready_read_stderr_callback",
          [](vlink::Process& self, nb::callable callback) {
            self.register_ready_read_stderr_callback(
                make_void_callback(std::move(callback), "vlink::Process.register_ready_read_stderr_callback"));
          },
          "callback"_a)
      .def("start", &vlink::Process::start, "program"_a, "arguments"_a = std::vector<std::string>{})
      .def("start_command", &vlink::Process::start_command, "command"_a)
      .def(
          "wait_for_started",
          [](vlink::Process& self, int timeout_ms) {
            nb::gil_scoped_release release;
            return self.wait_for_started(timeout_ms);
          },
          "timeout_ms"_a = 3000)
      .def(
          "wait_for_finished",
          [](vlink::Process& self, int timeout_ms) {
            nb::gil_scoped_release release;
            return self.wait_for_finished(timeout_ms);
          },
          "timeout_ms"_a = 3000)
      .def(
          "wait_for_ready_read",
          [](vlink::Process& self, int timeout_ms) {
            nb::gil_scoped_release release;
            return self.wait_for_ready_read(timeout_ms);
          },
          "timeout_ms"_a = 3000)
      .def("terminate", &vlink::Process::terminate)
      .def("kill", &vlink::Process::kill)
      .def("close", &vlink::Process::close, "force_kill_on_timeout"_a = false)
      .def("bytes_available_stdout", &vlink::Process::bytes_available_stdout)
      .def("bytes_available_stderr", &vlink::Process::bytes_available_stderr)
      .def("can_read_line_stdout", &vlink::Process::can_read_line_stdout)
      .def("can_read_line_stderr", &vlink::Process::can_read_line_stderr)
      .def("read_line_stdout",
           [](vlink::Process& self) -> nb::object {
             std::string line;
             if (!self.read_line_stdout(line)) return nb::none();
             return nb::object(nb::bytes(line.data(), line.size()));
           })
      .def("read_line_stderr",
           [](vlink::Process& self) -> nb::object {
             std::string line;
             if (!self.read_line_stderr(line)) return nb::none();
             return nb::object(nb::bytes(line.data(), line.size()));
           })
      .def(
          "read_stdout",
          [](vlink::Process& self, size_t max_size) {
            std::vector<uint8_t> buffer;
            self.read_stdout(buffer, max_size);
            return nb::bytes(buffer.empty() ? nullptr : reinterpret_cast<const char*>(buffer.data()), buffer.size());
          },
          "max_size"_a = static_cast<size_t>(0))
      .def(
          "read_stderr",
          [](vlink::Process& self, size_t max_size) {
            std::vector<uint8_t> buffer;
            self.read_stderr(buffer, max_size);
            return nb::bytes(buffer.empty() ? nullptr : reinterpret_cast<const char*>(buffer.data()), buffer.size());
          },
          "max_size"_a = static_cast<size_t>(0))
      .def("read_all_output",
           [](vlink::Process& self) {
             std::string result;
             self.read_all_output(result);
             return nb::bytes(result.data(), result.size());
           })
      .def("read_all_error",
           [](vlink::Process& self) {
             std::string result;
             self.read_all_error(result);
             return nb::bytes(result.data(), result.size());
           })
      .def("read_all",
           [](vlink::Process& self) {
             std::string result;
             self.read_all(result);
             return nb::bytes(result.data(), result.size());
           })
      .def(
          "write",
          [](vlink::Process& self, const std::string& data, int timeout_ms) { return self.write(data, timeout_ms); },
          "data"_a, "timeout_ms"_a = 5000)
      .def(
          "write",
          [](vlink::Process& self, nb::handle data, int timeout_ms) {
            PythonBufferView view(data);
            std::vector<uint8_t> buffer(view.data(), view.data() + view.size());
            return self.write(buffer, timeout_ms);
          },
          "data"_a, "timeout_ms"_a = 5000)
      .def("close_write_channel", &vlink::Process::close_write_channel)
      .def_static("execute", &vlink::Process::execute, "program"_a, "arguments"_a = std::vector<std::string>{},
                  "timeout_ms"_a = 30000)
      .def_static("start_detached", &vlink::Process::start_detached, "program"_a,
                  "arguments"_a = std::vector<std::string>{});

  proc.attr("INFINITE") = vlink::Process::kInfinite;
  proc.attr("DEFAULT_WAIT_TIMEOUT_MS") = vlink::Process::kDefaultWaitTimeoutMs;
  proc.attr("DEFAULT_WRITE_TIMEOUT_MS") = vlink::Process::kDefaultWriteTimeoutMs;
  proc.attr("DEFAULT_EXECUTE_TIMEOUT_MS") = vlink::Process::kDefaultExecuteTimeoutMs;
  proc.attr("DESTRUCTOR_WAIT_TIMEOUT_MS") = vlink::Process::kDestructorWaitTimeoutMs;

  auto utils = m.def_submodule("utils", "System utilities");
  utils.def("get_app_path", &vlink::Utils::get_app_path);
  utils.def("get_app_dir", &vlink::Utils::get_app_dir);
  utils.def("get_app_name", &vlink::Utils::get_app_name);
  utils.def("get_host_name", &vlink::Utils::get_host_name);
  utils.def("get_pid", &vlink::Utils::get_pid);
  utils.def("get_pid_str", &vlink::Utils::get_pid_str);
  utils.def("get_tmp_dir", &vlink::Utils::get_tmp_dir);
  utils.def("get_machine_id", &vlink::Utils::get_machine_id);
  utils.def("get_env", &vlink::Utils::get_env, "key"_a, "default_value"_a = "");
  utils.def("set_env", &vlink::Utils::set_env, "key"_a, "value"_a, "force"_a = true);
  utils.def("unset_env", &vlink::Utils::unset_env, "key"_a);
  utils.def("wait_for_device", &vlink::Utils::wait_for_device, "path"_a, "timeout_ms"_a, "poll_ms"_a = 50);
  utils.def("get_all_ipv4_address", &vlink::Utils::get_all_ipv4_address, "filter_available"_a = false);
  utils.def("get_all_ipv6_address", &vlink::Utils::get_all_ipv6_address, "filter_available"_a = false);
  utils.def("get_interface_name_by_ipv4", &vlink::Utils::get_interface_name_by_ipv4, "addr"_a);
  utils.def("get_interface_name_by_ipv6", &vlink::Utils::get_interface_name_by_ipv6, "addr"_a);
  utils.def("set_thread_name", [](const std::string& name) { return vlink::Utils::set_thread_name(name); }, "name"_a);
  utils.def(
      "set_thread_priority",
      [](int priority_level, int policy) { return vlink::Utils::set_thread_priority(priority_level, policy); },
      "priority_level"_a, "policy"_a = -1);
  utils.def(
      "set_thread_stick", [](uint32_t core_mask) { return vlink::Utils::set_thread_stick(core_mask); }, "core_mask"_a);
  utils.def("set_console_utf8_output", &vlink::Utils::set_console_utf8_output);
  utils.def("get_dds_default_address", &vlink::Utils::get_dds_default_address, "filter_available"_a = false,
            "max_count"_a = 5);
  utils.def("get_native_thread_id", &vlink::Utils::get_native_thread_id);
  utils.def("get_cpu_usage", &vlink::Utils::get_cpu_usage);
  utils.def("get_memory_usage", &vlink::Utils::get_memory_usage);
  utils.def("is_process_running", &vlink::Utils::is_process_running, "name"_a);
  utils.def("try_release_sys_memory", &vlink::Utils::try_release_sys_memory);
  utils.def("get_timezone_diff", &vlink::Utils::get_timezone_diff);
  utils.def("yield_cpu", &vlink::Utils::yield_cpu);
  utils.def("check_singleton", &vlink::Utils::check_singleton, "name"_a);
  utils.def(
      "get_terminal_size",
      []() {
        auto [w, h] = vlink::Utils::get_terminal_size();
        return nb::make_tuple(w, h);
      },
      "Returns (width, height) of the terminal");
  utils.def(
      "start_detect_keyboard",
      [](nb::callable callback, int poll_ms) {
        auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
        vlink::Utils::start_detect_keyboard(
            [cb](const std::string& key) {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                cb->fn(key);
              } catch (std::exception&) {
                report_current_exception("vlink::Utils.start_detect_keyboard");
              }
            },
            poll_ms);
      },
      "callback"_a, "poll_ms"_a = 100, "Start keyboard input detection: callback(key: str)");
  utils.def("stop_detect_keyboard", &vlink::Utils::stop_detect_keyboard);
  utils.def(
      "register_crash_signal",
      [](nb::callable callback) {
        auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
        vlink::Utils::register_crash_signal([cb](int sig) {
          if (!Py_IsInitialized()) return;
          nb::gil_scoped_acquire gil;
          try {
            cb->fn(sig);
          } catch (std::exception&) {
            report_current_exception("vlink::Utils.register_crash_signal");
          }
        });
      },
      "callback"_a, "Register handler for crash signals (SIGSEGV, SIGABRT, etc.)");
  utils.def(
      "register_terminate_signal",
      [](nb::callable callback, bool async_mode, bool passthrough) {
        auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
        vlink::Utils::register_terminate_signal(
            [cb](int sig) {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                cb->fn(sig);
              } catch (std::exception&) {
                report_current_exception("vlink::Utils.register_terminate_signal");
              }
            },
            async_mode, passthrough);
      },
      "callback"_a, "is_async"_a = false, "pass_through"_a = false);

  auto helpers = m.def_submodule("helpers", "String and formatting utilities");
  helpers.def("to_int", &vlink::Helpers::to_int, "str"_a, "default_value"_a = 0);
  helpers.def("to_long", &vlink::Helpers::to_long, "str"_a, "default_value"_a = 0, "offset"_a = 0);
  helpers.def("double_to_string", &vlink::Helpers::double_to_string, "value"_a, "precision"_a = 2);
  helpers.def("get_hash_code", nb::overload_cast<const std::string&>(&vlink::Helpers::get_hash_code), "str"_a);
  helpers.def("hash_combine", &vlink::Helpers::hash_combine, "a"_a, "b"_a);
  helpers.def("format_file_size", &vlink::Helpers::format_file_size, "bytes"_a);
  helpers.def("format_rate_size", &vlink::Helpers::format_rate_size, "bytes_per_sec"_a);
  helpers.def("format_milliseconds", &vlink::Helpers::format_milliseconds, "ms"_a, "show_millis"_a = true);
  helpers.def("format_date", &vlink::Helpers::format_date, "nanoseconds_since_epoch"_a);
  helpers.def("format_time_diff", &vlink::Helpers::format_time_diff, "milliseconds"_a);
  helpers.def("format_hex_number", nb::overload_cast<int64_t>(&vlink::Helpers::format_hex_number), "value"_a);
  helpers.def(
      "has_startwith",
      [](const std::string& s, const std::string& t) {
        return vlink::Helpers::has_startwith(std::string_view(s), std::string_view(t));
      },
      "str"_a, "target"_a);
  helpers.def(
      "has_endwith",
      [](const std::string& s, const std::string& t) {
        return vlink::Helpers::has_endwith(std::string_view(s), std::string_view(t));
      },
      "str"_a, "target"_a);
  helpers.def(
      "contains_substring",
      [](const std::string& s, const std::string& t) {
        return vlink::Helpers::contains_substring(std::string_view(s), std::string_view(t));
      },
      "str"_a, "target"_a);
  helpers.def("trim_string", &vlink::Helpers::trim_string, "str"_a);
  helpers.def(
      "replace_string",
      [](std::string str, const std::string& from_substring, const std::string& to_substring) {
        vlink::Helpers::replace_string(str, from_substring, to_substring);
        return str;
      },
      "str"_a, "from_substring"_a, "to_substring"_a);
  helpers.def("string_local_to_utf8", &vlink::Helpers::string_local_to_utf8, "local_str"_a);
  helpers.def("string_utf8_to_local", &vlink::Helpers::string_utf8_to_local, "utf8_str"_a);
  helpers.def(
      "string_to_wstring",
      [](const std::string& input) { return wide_string_to_python_str(vlink::Helpers::string_to_wstring(input)); },
      "input"_a);
  helpers.def(
      "wstring_to_string",
      [](nb::str input) { return vlink::Helpers::wstring_to_string(python_str_to_wide_string(input)); }, "input"_a);
  helpers.def(
      "path_to_string",
      [](const std::string& path) { return vlink::Helpers::path_to_string(std::filesystem::path(path)); }, "path"_a);
  helpers.def("format_hex_number_unsigned", nb::overload_cast<uint64_t>(&vlink::Helpers::format_hex_number), "value"_a);
  helpers.def(
      "get_split_string",
      [](const std::string& s, const std::string& d) {
        return vlink::Helpers::get_split_string(s, d.empty() ? ',' : d[0]);
      },
      "str"_a, "delimiter"_a = ",");
  helpers.def(
      "get_split_string_view",
      [](const std::string& s, const std::string& d) {
        auto views = vlink::Helpers::get_split_string_view(s, d.empty() ? ',' : d[0]);
        std::vector<std::string> parts;
        parts.reserve(views.size());
        for (auto view : views) {
          parts.emplace_back(view);
        }
        return parts;
      },
      "str"_a, "delimiter"_a = ",");
  helpers.def(
      "get_pair_string",
      [](const std::string& s, const std::string& d) {
        return vlink::Helpers::get_pair_string(s, d.empty() ? '=' : d[0]);
      },
      "str"_a, "delimiter"_a = "=");
  helpers.def(
      "get_pair_string_view",
      [](const std::string& s, const std::string& d) {
        auto pair = vlink::Helpers::get_pair_string_view(s, d.empty() ? '=' : d[0]);
        return std::make_pair(std::string(pair.first), std::string(pair.second));
      },
      "str"_a, "delimiter"_a = "=");
  helpers.def("convert_date_to_timestamp", &vlink::Helpers::convert_date_to_timestamp, "date_string"_a);

  nb::class_<vlink::Qos> qos_cls(m, "Qos", "DDS-compatible Quality of Service");

  nb::class_<vlink::Qos::Reliability> qos_rel(qos_cls, "Reliability");
  nb::enum_<vlink::Qos::Reliability::Kind>(qos_rel, "Kind")
      .value("BestEffort", vlink::Qos::Reliability::kBestEffort)
      .value("Reliable", vlink::Qos::Reliability::kReliable);
  qos_rel.def(nb::init<>())
      .def_rw("kind", &vlink::Qos::Reliability::kind)
      .def_rw("block_time", &vlink::Qos::Reliability::block_time)
      .def_rw("heartbeat_time", &vlink::Qos::Reliability::heartbeat_time);

  nb::class_<vlink::Qos::History> qos_hist(qos_cls, "History");
  nb::enum_<vlink::Qos::History::Kind>(qos_hist, "Kind")
      .value("KeepLast", vlink::Qos::History::kKeepLast)
      .value("KeepAll", vlink::Qos::History::kKeepAll);
  qos_hist.def(nb::init<>()).def_rw("kind", &vlink::Qos::History::kind).def_rw("depth", &vlink::Qos::History::depth);

  nb::class_<vlink::Qos::Durability> qos_dur(qos_cls, "Durability");
  nb::enum_<vlink::Qos::Durability::Kind>(qos_dur, "Kind")
      .value("Volatile", vlink::Qos::Durability::kVolatile)
      .value("TransientLocal", vlink::Qos::Durability::kTransientLocal)
      .value("Transient", vlink::Qos::Durability::kTransient)
      .value("Persistent", vlink::Qos::Durability::kPersistent);
  qos_dur.def(nb::init<>()).def_rw("kind", &vlink::Qos::Durability::kind);

  nb::class_<vlink::Qos::PublishMode> qos_pm(qos_cls, "PublishMode");
  nb::enum_<vlink::Qos::PublishMode::Kind>(qos_pm, "Kind")
      .value("Sync", vlink::Qos::PublishMode::kSync)
      .value("ASync", vlink::Qos::PublishMode::kASync);
  qos_pm.def(nb::init<>()).def_rw("kind", &vlink::Qos::PublishMode::kind);

  nb::class_<vlink::Qos::Liveliness> qos_lv(qos_cls, "Liveliness");
  nb::enum_<vlink::Qos::Liveliness::Kind>(qos_lv, "Kind")
      .value("Automatic", vlink::Qos::Liveliness::kAutomatic)
      .value("ManualParticipant", vlink::Qos::Liveliness::kManualParticipant)
      .value("ManualTopic", vlink::Qos::Liveliness::kManualTopic);
  qos_lv.def(nb::init<>())
      .def_rw("kind", &vlink::Qos::Liveliness::kind)
      .def_rw("duration", &vlink::Qos::Liveliness::duration);

  nb::class_<vlink::Qos::DestinationOrder> qos_do(qos_cls, "DestinationOrder");
  nb::enum_<vlink::Qos::DestinationOrder::Kind>(qos_do, "Kind")
      .value("ReceptionTimestamp", vlink::Qos::DestinationOrder::kReceptionTimestamp)
      .value("SourceTimestamp", vlink::Qos::DestinationOrder::kSourceTimestamp);
  qos_do.def(nb::init<>()).def_rw("kind", &vlink::Qos::DestinationOrder::kind);

  nb::class_<vlink::Qos::Ownership> qos_own(qos_cls, "Ownership");
  nb::enum_<vlink::Qos::Ownership::Kind>(qos_own, "Kind")
      .value("Shared", vlink::Qos::Ownership::kShared)
      .value("Exclusive", vlink::Qos::Ownership::kExclusive);
  qos_own.def(nb::init<>()).def_rw("kind", &vlink::Qos::Ownership::kind);

  nb::class_<vlink::Qos::Deadline>(qos_cls, "Deadline")
      .def(nb::init<>())
      .def_rw("period", &vlink::Qos::Deadline::period);
  nb::class_<vlink::Qos::Lifespan>(qos_cls, "Lifespan")
      .def(nb::init<>())
      .def_rw("duration", &vlink::Qos::Lifespan::duration);
  nb::class_<vlink::Qos::LatencyBudget>(qos_cls, "LatencyBudget")
      .def(nb::init<>())
      .def_rw("duration", &vlink::Qos::LatencyBudget::duration);

  nb::class_<vlink::Qos::ResourceLimits>(qos_cls, "ResourceLimits")
      .def(nb::init<>())
      .def_rw("max_samples", &vlink::Qos::ResourceLimits::max_samples)
      .def_rw("max_instances", &vlink::Qos::ResourceLimits::max_instances)
      .def_rw("max_samples_per_instance", &vlink::Qos::ResourceLimits::max_samples_per_instance);

  nb::class_<vlink::Qos::Additions> qos_add(qos_cls, "Additions");
  nb::enum_<vlink::Qos::Additions::Priority>(qos_add, "Priority")
      .value("RealTime", vlink::Qos::Additions::kPriorityRealTime)
      .value("High", vlink::Qos::Additions::kPriorityHigh)
      .value("Normal", vlink::Qos::Additions::kPriorityNormal)
      .value("Low", vlink::Qos::Additions::kPriorityLow)
      .value("Background", vlink::Qos::Additions::kPriorityBackground);
  qos_add.def(nb::init<>())
      .def_rw("priority", &vlink::Qos::Additions::priority)
      .def_rw("is_express", &vlink::Qos::Additions::is_express);

  qos_cls.def(nb::init<>())
      .def_prop_rw(
          "name", [](const vlink::Qos& q) { return std::string(q.name); },
          [](vlink::Qos& q, const std::string& s) {
            constexpr size_t kMax = sizeof(vlink::Qos::name) - 1;
            const size_t n = std::min(s.size(), kMax);
            std::memcpy(q.name, s.data(), n);
            q.name[n] = '\0';
          })
      .def_rw("valid", &vlink::Qos::valid)
      .def_rw("reliability", &vlink::Qos::reliability)
      .def_rw("history", &vlink::Qos::history)
      .def_rw("durability", &vlink::Qos::durability)
      .def_rw("publish_mode", &vlink::Qos::publish_mode)
      .def_rw("liveliness", &vlink::Qos::liveliness)
      .def_rw("destination_order", &vlink::Qos::destination_order)
      .def_rw("ownership", &vlink::Qos::ownership)
      .def_rw("deadline", &vlink::Qos::deadline)
      .def_rw("lifespan", &vlink::Qos::lifespan)
      .def_rw("latency_budget", &vlink::Qos::latency_budget)
      .def_rw("resource_limits", &vlink::Qos::resource_limits)
      .def_rw("additions", &vlink::Qos::additions)
      .def("__repr__", [](const vlink::Qos& q) {
        return std::string("Qos(name='") + q.name + "', valid=" + (q.valid ? "True" : "False") + ")";
      });

  auto qos_profile = m.def_submodule("QosProfile", "Pre-defined QoS profiles");
  qos_profile.attr("Event") = vlink::QosProfile::kEvent;
  qos_profile.attr("Method") = vlink::QosProfile::kMethod;
  qos_profile.attr("Field") = vlink::QosProfile::kField;
  qos_profile.attr("Sensor") = vlink::QosProfile::kSensor;
  qos_profile.attr("Parameter") = vlink::QosProfile::kParameter;
  qos_profile.attr("Service") = vlink::QosProfile::kService;
  qos_profile.attr("Clock") = vlink::QosProfile::kClock;
  qos_profile.attr("Static") = vlink::QosProfile::kStatic;
  qos_profile.attr("Light") = vlink::QosProfile::kLight;
  qos_profile.attr("Poor") = vlink::QosProfile::kPoor;
  qos_profile.attr("Better") = vlink::QosProfile::kBetter;
  qos_profile.attr("Best") = vlink::QosProfile::kBest;
  qos_profile.attr("Large") = vlink::QosProfile::kLarge;
  qos_profile.def("get_available_qos_map", &vlink::QosProfile::get_available_qos_map);

  nb::enum_<vlink::Status::Type>(m, "StatusType")
      .value("PublicationMatched", vlink::Status::kPublicationMatched)
      .value("SubscriptionMatched", vlink::Status::kSubscriptionMatched)
      .value("OfferedDeadlineMissed", vlink::Status::kOfferedDeadlineMissed)
      .value("RequestedDeadlineMissed", vlink::Status::kRequestedDeadlineMissed)
      .value("OfferedIncompatibleQos", vlink::Status::kOfferedIncompatibleQos)
      .value("RequestedIncompatibleQos", vlink::Status::kRequestedIncompatibleQos)
      .value("LivelinessLost", vlink::Status::kLivelinessLost)
      .value("LivelinessChanged", vlink::Status::kLivelinessChanged)
      .value("SampleRejected", vlink::Status::kSampleRejected)
      .value("SampleLost", vlink::Status::kSampleLost)
      .value("Unknown", vlink::Status::kUnknown);

  nb::class_<vlink::SslOptions>(m, "SslOptions")
      .def(nb::init<>())
      .def_rw("verify_peer", &vlink::SslOptions::verify_peer)
      .def_rw("ca_file", &vlink::SslOptions::ca_file)
      .def_rw("cert_file", &vlink::SslOptions::cert_file)
      .def_rw("key_file", &vlink::SslOptions::key_file)
      .def_rw("key_password", &vlink::SslOptions::key_password)
      .def_rw("server_name", &vlink::SslOptions::server_name)
      .def_rw("ciphers", &vlink::SslOptions::ciphers)
      .def("is_valid", &vlink::SslOptions::is_valid);

  auto status = m.def_submodule("Status", "Status helper functions");
  status.def("is_for_writer", &vlink::Status::is_for_writer, "type"_a);
  status.def("is_for_reader", &vlink::Status::is_for_reader, "type"_a);

  nb::class_<vlink::Security::Config::Advanced>(
      m, "SecurityConfigAdvanced", "Low-frequency security options for AAD, replay protection, and signing")
      .def(nb::init<>())
      .def_rw("aad_context", &vlink::Security::Config::Advanced::aad_context)
      .def_rw("replay_window", &vlink::Security::Config::Advanced::replay_window)
      .def_rw("signing_key_pem", &vlink::Security::Config::Advanced::signing_key_pem)
      .def_rw("verify_key_pem", &vlink::Security::Config::Advanced::verify_key_pem);

  nb::class_<vlink::Security::Config>(m, "SecurityConfig",
                                      "Aggregate of every parameter accepted by the Security constructor")
      .def(nb::init<>())
      .def_rw("key", &vlink::Security::Config::key)
      .def_rw("passphrase", &vlink::Security::Config::passphrase)
      .def_rw("pbkdf2_salt", &vlink::Security::Config::pbkdf2_salt)
      .def_rw("pbkdf2_iterations", &vlink::Security::Config::pbkdf2_iterations)
      .def_rw("public_key_pem", &vlink::Security::Config::public_key_pem)
      .def_rw("private_key_pem", &vlink::Security::Config::private_key_pem)
      .def_rw("advanced", &vlink::Security::Config::advanced)
      .def_prop_rw(
          "encrypt_callback",
          [](const vlink::Security::Config& self) -> nb::object {
            // The original Python callable has been wrapped into a C++ Function and is no longer
            // retrievable as a Python object.  Return @c True when a callback is installed, @c None
            // otherwise, so Python callers can at least query whether the slot is populated.
            return self.encrypt_callback ? nb::cast(true) : nb::none();
          },
          [](vlink::Security::Config& self, nb::callable cb) {
            self.encrypt_callback = make_security_callback(std::move(cb), "vlink::Security::Config.encrypt_callback");
          })
      .def_prop_rw(
          "decrypt_callback",
          [](const vlink::Security::Config& self) -> nb::object {
            return self.decrypt_callback ? nb::cast(true) : nb::none();
          },
          [](vlink::Security::Config& self, nb::callable cb) {
            self.decrypt_callback = make_security_callback(std::move(cb), "vlink::Security::Config.decrypt_callback");
          });

  nb::class_<vlink::Security>(m, "Security", "Authenticated message-level encryption (AEAD)")
      .def(nb::new_([](vlink::Security::Config cfg) { return new vlink::Security(std::move(cfg)); }),
           "cfg"_a = vlink::Security::Config{})
      .def(
          "encrypt",
          [](vlink::Security& self, nb::handle data) -> nb::object {
            PythonBufferView view(data);
            auto in_bytes = vlink::Bytes::shallow_copy(view.data(), view.size());
            vlink::Bytes out;
            if (self.encrypt(in_bytes, out)) return PythonCodec<vlink::Bytes>::to_python(out);
            return nb::none();
          },
          "data"_a)
      .def(
          "decrypt",
          [](vlink::Security& self, nb::handle data) -> nb::object {
            PythonBufferView view(data);
            auto in_bytes = vlink::Bytes::shallow_copy(view.data(), view.size());
            vlink::Bytes out;
            if (self.decrypt(in_bytes, out)) return PythonCodec<vlink::Bytes>::to_python(out);
            return nb::none();
          },
          "data"_a)
      .def("is_configured", &vlink::Security::is_configured,
           "Return True iff at least one cryptographic slot (symmetric key, RSA keypair, or "
           "encrypt+decrypt callback pair) is usable.")
      .def("can_encrypt", &vlink::Security::can_encrypt,
           "Return True iff encrypt() will produce a ciphertext for at least one configured mode "
           "(custom callbacks > RSA public key > symmetric key).")
      .def("can_decrypt", &vlink::Security::can_decrypt,
           "Return True iff decrypt() can recover a plaintext for at least one configured mode "
           "(custom callbacks > RSA private key > symmetric key).");

  nb::class_<vlink::UrlRemap>(m, "UrlRemap", "JSON-driven URL pattern remapping")
      .def(nb::init<>())
      .def("load", &vlink::UrlRemap::load, "file_path"_a)
      .def("unload", &vlink::UrlRemap::unload)
      .def("reload", &vlink::UrlRemap::reload, "file_path"_a)
      .def("convert", &vlink::UrlRemap::convert, "url"_a, nb::rv_policy::reference)
      .def("set_enable_log", &vlink::UrlRemap::set_enable_log, "enable"_a)
      .def("is_enable_log", &vlink::UrlRemap::is_enable_log)
      .def("is_valid", &vlink::UrlRemap::is_valid)
      .def("get_error_string", &vlink::UrlRemap::get_error_string);

  using BytesPub = vlink::Publisher<vlink::Bytes>;
  using BytesSub = vlink::Subscriber<vlink::Bytes>;
  using BytesSrv = vlink::Server<vlink::Bytes, vlink::Bytes>;
  using BytesCli = vlink::Client<vlink::Bytes, vlink::Bytes>;
  using BytesFireSrv = vlink::Server<vlink::Bytes, vlink::Traits::EmptyType>;
  using BytesFireCli = vlink::Client<vlink::Bytes, vlink::Traits::EmptyType>;
  using BytesSet = vlink::Setter<vlink::Bytes>;
  using BytesGet = vlink::Getter<vlink::Bytes>;
  using SecBytesPub = vlink::SecurityPublisher<vlink::Bytes>;
  using SecBytesSub = vlink::SecuritySubscriber<vlink::Bytes>;
  using SecBytesSrv = vlink::SecurityServer<vlink::Bytes, vlink::Bytes>;
  using SecBytesCli = vlink::SecurityClient<vlink::Bytes, vlink::Bytes>;
  using SecBytesFireSrv = vlink::SecurityServer<vlink::Bytes, vlink::Traits::EmptyType>;
  using SecBytesFireCli = vlink::SecurityClient<vlink::Bytes, vlink::Traits::EmptyType>;
  using SecBytesSet = vlink::SecuritySetter<vlink::Bytes>;
  using SecBytesGet = vlink::SecurityGetter<vlink::Bytes>;

  bind_publisher<BytesPub, vlink::Bytes>(m, "Publisher", "Event-model publisher");
  bind_subscriber<BytesSub, vlink::Bytes>(m, "Subscriber", "Event-model subscriber");
  bind_server<BytesSrv, vlink::Bytes, vlink::Bytes>(m, "Server", "Method-model server");
  bind_client<BytesCli, vlink::Bytes, vlink::Bytes>(m, "Client", "Method-model client");
  bind_fire_forget_server<BytesFireSrv, vlink::Bytes>(m, "FireForgetServer", "Fire-and-forget method server");
  bind_fire_forget_client<BytesFireCli, vlink::Bytes>(m, "FireForgetClient", "Fire-and-forget method client");
  bind_setter<BytesSet, vlink::Bytes>(m, "Setter", "Field-model setter");
  bind_getter<BytesGet, vlink::Bytes>(m, "Getter", "Field-model getter");
  bind_publisher<SecBytesPub, vlink::Bytes, PythonCodec<vlink::Bytes>, true>(
      m, "SecurityPublisher", "Event-model publisher with payload security");
  bind_subscriber<SecBytesSub, vlink::Bytes, PythonCodec<vlink::Bytes>, true>(
      m, "SecuritySubscriber", "Event-model subscriber with payload security");
  bind_server<SecBytesSrv, vlink::Bytes, vlink::Bytes, PythonCodec<vlink::Bytes>, PythonCodec<vlink::Bytes>, true>(
      m, "SecurityServer", "Method-model server with payload security");
  bind_client<SecBytesCli, vlink::Bytes, vlink::Bytes, PythonCodec<vlink::Bytes>, PythonCodec<vlink::Bytes>, true>(
      m, "SecurityClient", "Method-model client with payload security");
  bind_fire_forget_server<SecBytesFireSrv, vlink::Bytes, PythonCodec<vlink::Bytes>, true>(
      m, "SecurityFireForgetServer", "Fire-and-forget method server with payload security");
  bind_fire_forget_client<SecBytesFireCli, vlink::Bytes, PythonCodec<vlink::Bytes>, true>(
      m, "SecurityFireForgetClient", "Fire-and-forget method client with payload security");
  bind_setter<SecBytesSet, vlink::Bytes, PythonCodec<vlink::Bytes>, true>(m, "SecuritySetter",
                                                                          "Field-model setter with payload security");
  bind_getter<SecBytesGet, vlink::Bytes, PythonCodec<vlink::Bytes>, true>(m, "SecurityGetter",
                                                                          "Field-model getter with payload security");

  nb::class_<vlink::DiscoveryViewer> dv(m, "DiscoveryViewer", "Active endpoint discovery");
  nb::enum_<vlink::DiscoveryViewer::FilterType>(dv, "FilterType")
      .value("None_", vlink::DiscoveryViewer::kFilterNone)
      .value("Available", vlink::DiscoveryViewer::kFilterAvailable)
      .value("Native", vlink::DiscoveryViewer::kFilterNative);
  nb::class_<vlink::DiscoveryViewer::Process>(dv, "Process", "Process hosting an endpoint")
      .def_ro("type", &vlink::DiscoveryViewer::Process::type)
      .def_ro("host", &vlink::DiscoveryViewer::Process::host)
      .def_ro("pid", &vlink::DiscoveryViewer::Process::pid)
      .def_ro("name", &vlink::DiscoveryViewer::Process::name)
      .def_ro("ip", &vlink::DiscoveryViewer::Process::ip)
      .def_ro("profiler", &vlink::DiscoveryViewer::Process::profiler)
      .def("__repr__", [](const vlink::DiscoveryViewer::Process& p) {
        return "Process(name='" + p.name + "', pid=" + std::to_string(p.pid) + ", host='" + p.host + "')";
      });
  nb::class_<vlink::DiscoveryViewer::Info>(dv, "Info")
      .def_ro("sort_index", &vlink::DiscoveryViewer::Info::sort_index)
      .def_ro("type", &vlink::DiscoveryViewer::Info::type)
      .def_ro("url", &vlink::DiscoveryViewer::Info::url)
      .def_ro("ser_type", &vlink::DiscoveryViewer::Info::ser_type)
      .def_ro("schema_type", &vlink::DiscoveryViewer::Info::schema_type)
      .def_ro("process_list", &vlink::DiscoveryViewer::Info::process_list)
      .def("__repr__", [](const vlink::DiscoveryViewer::Info& i) {
        return "DiscoveryInfo(url='" + i.url + "', type=" + std::to_string(i.type) +
               ", processes=" + std::to_string(i.process_list.size()) + ")";
      });
  dv.def(nb::init<vlink::DiscoveryViewer::FilterType>(), "filter"_a = vlink::DiscoveryViewer::kFilterNone)
      .def(
          "register_callback",
          [](vlink::DiscoveryViewer& self, nb::callable callback) {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            self.register_callback([cb](const std::vector<vlink::DiscoveryViewer::Info>& info_list) {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                cb->fn(info_list);
              } catch (std::exception&) {
                report_current_exception("vlink::DiscoveryViewer.register_callback");
              }
            });
          },
          "callback"_a, "Register callback for discovery changes: callback(info_list)")
      .def("get_info_list", &vlink::DiscoveryViewer::get_info_list)
      .def("get_ser_type", &vlink::DiscoveryViewer::get_ser_type, "url"_a)
      .def("get_schema_type", &vlink::DiscoveryViewer::get_schema_type, "url"_a)
      .def_static(
          "convert_type", [](const std::string& type) { return vlink::DiscoveryViewer::convert_type(type); }, "type"_a)
      .def_static("get_listen_address", &vlink::DiscoveryViewer::get_listen_address)
      .def_static("global_get", &vlink::DiscoveryViewer::global_get, nb::rv_policy::reference)
      .def_static("convert_type_to_view", nb::overload_cast<uint32_t>(&vlink::DiscoveryViewer::convert_type_to_view),
                  "type"_a)
      .def_static("convert_type_to_view",
                  nb::overload_cast<uint32_t, const std::vector<vlink::DiscoveryViewer::Process>&>(
                      &vlink::DiscoveryViewer::convert_type_to_view),
                  "type"_a, "process_list"_a);

  nb::class_<vlink::BagWriter> bw(m, "BagWriter", "Message recorder");
  nb::enum_<vlink::BagWriter::CompressType>(bw, "CompressType")
      .value("NONE", vlink::BagWriter::kCompressNone)
      .value("AUTO", vlink::BagWriter::kCompressAuto)
      .value("ZSTD", vlink::BagWriter::kCompressZstd)
      .value("LZ4", vlink::BagWriter::kCompressLz4)
      .value("LZAV", vlink::BagWriter::kCompressLzav);
  nb::class_<vlink::BagWriter::Config>(bw, "Config")
      .def(nb::init<>())
      .def_rw("tag_name", &vlink::BagWriter::Config::tag_name)
      .def_rw("compress", &vlink::BagWriter::Config::compress)
      .def_rw("wal_mode", &vlink::BagWriter::Config::wal_mode)
      .def_rw("enable_limit", &vlink::BagWriter::Config::enable_limit)
      .def_rw("split_name_by_time", &vlink::BagWriter::Config::split_name_by_time)
      .def_rw("sync_mode", &vlink::BagWriter::Config::sync_mode)
      .def_rw("optimize_on_exit", &vlink::BagWriter::Config::optimize_on_exit)
      .def_rw("max_row_count", &vlink::BagWriter::Config::max_row_count)
      .def_rw("max_bytes_size", &vlink::BagWriter::Config::max_bytes_size)
      .def_rw("split_by_size", &vlink::BagWriter::Config::split_by_size)
      .def_rw("split_by_time", &vlink::BagWriter::Config::split_by_time)
      .def_rw("begin_time", &vlink::BagWriter::Config::begin_time)
      .def_rw("cache_size", &vlink::BagWriter::Config::cache_size)
      .def_rw("compress_start_size", &vlink::BagWriter::Config::compress_start_size)
      .def_rw("compress_level", &vlink::BagWriter::Config::compress_level)
      .def_rw("max_task_depth", &vlink::BagWriter::Config::max_task_depth)
      .def_rw("max_memory_size", &vlink::BagWriter::Config::max_memory_size)
      .def_rw("start_timestamp", &vlink::BagWriter::Config::start_timestamp)
      .def_rw("ignore_compress_urls", &vlink::BagWriter::Config::ignore_compress_urls);
  bw.def_static(
        "create",
        [](const std::string& path, const vlink::BagWriter::Config& cfg) {
          return vlink::BagWriter::create(path, cfg);
        },
        "path"_a, "config"_a = vlink::BagWriter::Config())
      .def_static("filter_get", &vlink::BagWriter::filter_get, "path"_a)
      .def_static("global_get", &vlink::BagWriter::global_get, nb::rv_policy::reference)
      .def(
          "push",
          [](vlink::BagWriter& self, const std::string& url, const std::string& ser_type, vlink::SchemaType schema_type,
             vlink::ActionType action_type, nb::handle data, std::optional<int64_t> timestamp_us, bool immediate) {
            auto bytes = PythonCodec<vlink::Bytes>::from_python_owned(data);
            int64_t ts = timestamp_us.value_or(0);
            int64_t* ts_ptr = timestamp_us.has_value() ? &ts : nullptr;
            nb::gil_scoped_release release;
            return self.push(url, ser_type, schema_type, action_type, bytes, ts_ptr, immediate);
          },
          "url"_a, "ser_type"_a, "schema_type"_a, "action_type"_a, "data"_a, "timestamp_us"_a = nb::none(),
          "immediate"_a = false)
      .def(
          "register_schema_callback",
          [](vlink::BagWriter& self, nb::callable callback) {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            self.register_schema_callback([cb](const std::string& ser_type, vlink::SchemaType schema_type) {
              if (!Py_IsInitialized()) return vlink::SchemaData{};
              nb::gil_scoped_acquire gil;
              try {
                nb::object result = cb->fn(ser_type, schema_type);
                if (result.is_none()) {
                  return vlink::SchemaData{};
                }
                return nb::cast<vlink::SchemaData>(result);
              } catch (std::exception&) {
                report_current_exception("vlink::BagWriter.register_schema_callback");
                return vlink::SchemaData{};
              }
            });
          },
          "callback"_a)
      .def(
          "push_schema",
          [](vlink::BagWriter& self, const vlink::SchemaData& schema_data, bool immediate) {
            auto schema_copy = schema_data;
            nb::gil_scoped_release release;
            return self.push_schema(schema_copy, immediate);
          },
          "schema_data"_a, "immediate"_a = false)
      .def(
          "register_split_callback",
          [](vlink::BagWriter& self, nb::callable callback, bool before) {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            self.register_split_callback(
                [cb](int idx, const std::string& file_name) {
                  if (!Py_IsInitialized()) return;
                  nb::gil_scoped_acquire gil;
                  try {
                    cb->fn(idx, file_name);
                  } catch (std::exception&) {
                    report_current_exception("vlink::BagWriter.register_split_callback");
                  }
                },
                before);
          },
          "callback"_a, "before"_a = false)
      .def("is_dumping", &vlink::BagWriter::is_dumping)
      .def("is_split_mode", &vlink::BagWriter::is_split_mode)
      .def("get_split_index", &vlink::BagWriter::get_split_index)
      .def("set_url_loss", &vlink::BagWriter::set_url_loss, "url"_a, "loss"_a)
      .def("async_run",
           [](vlink::BagWriter& self) {
             nb::gil_scoped_release release;
             return self.async_run();
           })
      .def(
          "quit",
          [](vlink::BagWriter& self, bool force) {
            nb::gil_scoped_release release;
            return self.quit(force);
          },
          "force"_a = false)
      .def(
          "wait_for_quit",
          [](vlink::BagWriter& self, int timeout_ms) {
            nb::gil_scoped_release release;
            return self.wait_for_quit(timeout_ms);
          },
          "timeout_ms"_a = -1)
      .def("is_running", &vlink::BagWriter::is_running)
      .def("__repr__", [](const vlink::BagWriter& self) {
        return std::string("BagWriter(running=") + (self.is_running() ? "True" : "False") + ")";
      });

  nb::class_<vlink::BagReader> br(m, "BagReader", "Message playback");
  nb::enum_<vlink::BagReader::Status>(br, "Status")
      .value("Stopped", vlink::BagReader::kStopped)
      .value("Paused", vlink::BagReader::kPaused)
      .value("Playing", vlink::BagReader::kPlaying);
  nb::class_<vlink::BagReader::Info::UrlMeta>(br, "UrlMeta")
      .def_ro("valid", &vlink::BagReader::Info::UrlMeta::valid)
      .def_ro("index", &vlink::BagReader::Info::UrlMeta::index)
      .def_ro("url", &vlink::BagReader::Info::UrlMeta::url)
      .def_ro("url_type", &vlink::BagReader::Info::UrlMeta::url_type)
      .def_ro("action_type", &vlink::BagReader::Info::UrlMeta::action_type)
      .def_ro("ser_type", &vlink::BagReader::Info::UrlMeta::ser_type)
      .def_ro("schema_type", &vlink::BagReader::Info::UrlMeta::schema_type)
      .def_ro("count", &vlink::BagReader::Info::UrlMeta::count)
      .def_ro("size", &vlink::BagReader::Info::UrlMeta::size)
      .def_ro("freq", &vlink::BagReader::Info::UrlMeta::freq)
      .def_ro("loss", &vlink::BagReader::Info::UrlMeta::loss)
      .def("__repr__", [](const vlink::BagReader::Info::UrlMeta& meta) {
        return "UrlMeta(url='" + meta.url + "', count=" + std::to_string(meta.count) + ")";
      });
  nb::class_<vlink::BagReader::Info>(br, "Info")
      .def_ro("file_name", &vlink::BagReader::Info::file_name)
      .def_ro("tag_name", &vlink::BagReader::Info::tag_name)
      .def_ro("version", &vlink::BagReader::Info::version)
      .def_ro("storage_type", &vlink::BagReader::Info::storage_type)
      .def_ro("compression_type", &vlink::BagReader::Info::compression_type)
      .def_ro("time_accuracy", &vlink::BagReader::Info::time_accuracy)
      .def_ro("process_name", &vlink::BagReader::Info::process_name)
      .def_ro("date_time", &vlink::BagReader::Info::date_time)
      .def_ro("has_completed", &vlink::BagReader::Info::has_completed)
      .def_ro("has_idx_elapsed", &vlink::BagReader::Info::has_idx_elapsed)
      .def_ro("has_idx_url", &vlink::BagReader::Info::has_idx_url)
      .def_ro("has_schema", &vlink::BagReader::Info::has_schema)
      .def_ro("timezone", &vlink::BagReader::Info::timezone)
      .def_ro("start_timestamp", &vlink::BagReader::Info::start_timestamp)
      .def_ro("blank_duration", &vlink::BagReader::Info::blank_duration)
      .def_ro("total_duration", &vlink::BagReader::Info::total_duration)
      .def_ro("file_size", &vlink::BagReader::Info::file_size)
      .def_ro("total_raw_size", &vlink::BagReader::Info::total_raw_size)
      .def_ro("message_count", &vlink::BagReader::Info::message_count)
      .def_ro("split_count", &vlink::BagReader::Info::split_count)
      .def_ro("split_by_size", &vlink::BagReader::Info::split_by_size)
      .def_ro("split_by_time", &vlink::BagReader::Info::split_by_time)
      .def_ro("url_metas", &vlink::BagReader::Info::url_metas)
      .def("__repr__", [](const vlink::BagReader::Info& info) {
        return "BagInfo(file='" + info.file_name + "', messages=" + std::to_string(info.message_count) +
               ", duration=" + std::to_string(info.total_duration) + "ms)";
      });
  nb::class_<vlink::BagReader::Config>(br, "Config")
      .def(nb::init<>())
      .def_rw("begin_time", &vlink::BagReader::Config::begin_time)
      .def_rw("end_time", &vlink::BagReader::Config::end_time)
      .def_rw("times", &vlink::BagReader::Config::times)
      .def_rw("rate", &vlink::BagReader::Config::rate)
      .def_rw("skip_blank", &vlink::BagReader::Config::skip_blank)
      .def_rw("force_delay", &vlink::BagReader::Config::force_delay)
      .def_rw("auto_pause", &vlink::BagReader::Config::auto_pause)
      .def_rw("auto_quit", &vlink::BagReader::Config::auto_quit)
      .def_rw("filter_urls", &vlink::BagReader::Config::filter_urls);
  br.attr("INFINITE") = vlink::BagReader::kInfinite;
  br.def_static(
        "create",
        [](const std::string& path, bool read_only, bool try_to_fix) {
          return vlink::BagReader::create(path, read_only, try_to_fix);
        },
        "path"_a, "read_only"_a = true, "try_to_fix"_a = false)
      .def(
          "register_output_callback",
          [](vlink::BagReader& self, nb::callable callback) {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            self.register_output_callback(
                [cb](int64_t ts, const std::string& url, vlink::ActionType action_type, const vlink::Bytes& data) {
                  if (!Py_IsInitialized()) return;
                  nb::gil_scoped_acquire gil;
                  try {
                    cb->fn(ts, url, action_type, PythonCodec<vlink::Bytes>::to_python(data));
                  } catch (std::exception&) {
                    report_current_exception("vlink::BagReader.register_output_callback");
                  }
                });
          },
          "callback"_a)
      .def(
          "register_status_callback",
          [](vlink::BagReader& self, nb::callable callback) {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            self.register_status_callback([cb](vlink::BagReader::Status status) {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                cb->fn(status);
              } catch (std::exception&) {
                report_current_exception("vlink::BagReader.register_status_callback");
              }
            });
          },
          "callback"_a)
      .def(
          "register_ready_callback",
          [](vlink::BagReader& self, nb::callable callback) {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            self.register_ready_callback([cb]() {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                cb->fn();
              } catch (std::exception&) {
                report_current_exception("vlink::BagReader.register_ready_callback");
              }
            });
          },
          "callback"_a)
      .def(
          "register_finish_callback",
          [](vlink::BagReader& self, nb::callable callback) {
            auto cb = std::make_shared<GilSafePyFunction>(std::move(callback));
            self.register_finish_callback([cb](bool interrupted) {
              if (!Py_IsInitialized()) return;
              nb::gil_scoped_acquire gil;
              try {
                cb->fn(interrupted);
              } catch (std::exception&) {
                report_current_exception("vlink::BagReader.register_finish_callback");
              }
            });
          },
          "callback"_a)
      .def("play", &vlink::BagReader::play, "config"_a = vlink::BagReader::Config())
      .def("stop", &vlink::BagReader::stop)
      .def("pause", &vlink::BagReader::pause)
      .def("resume", &vlink::BagReader::resume)
      .def("pause_to_next", &vlink::BagReader::pause_to_next)
      .def(
          "jump",
          [](vlink::BagReader& self, int64_t begin_time, double rate, int times, bool force_to_play) {
            nb::gil_scoped_release release;
            self.jump(begin_time, rate, times, force_to_play);
          },
          "begin_time"_a, "rate"_a = 1.0, "times"_a = 1, "force_to_play"_a = false)
      .def("check",
           [](vlink::BagReader& self) {
             auto future = self.check();
             nb::gil_scoped_release release;
             return future.get();
           })
      .def("reindex",
           [](vlink::BagReader& self) {
             auto future = self.reindex();
             nb::gil_scoped_release release;
             return future.get();
           })
      .def(
          "fix",
          [](vlink::BagReader& self, bool rebuild) {
            auto future = self.fix(rebuild);
            nb::gil_scoped_release release;
            return future.get();
          },
          "rebuild"_a = false)
      .def("tag", &vlink::BagReader::tag, "tag_name"_a)
      .def("get_timestamp", &vlink::BagReader::get_timestamp)
      .def("get_real_timestamp", &vlink::BagReader::get_real_timestamp)
      .def("get_status", &vlink::BagReader::get_status)
      .def("get_info", &vlink::BagReader::get_info, nb::rv_policy::reference)
      .def("detect_schema", &vlink::BagReader::detect_schema)
      .def("get_ser_type", &vlink::BagReader::get_ser_type, "url"_a)
      .def("get_schema_type", &vlink::BagReader::get_schema_type, "url"_a)
      .def("is_split_mode", &vlink::BagReader::is_split_mode)
      .def("get_split_index", &vlink::BagReader::get_split_index)
      .def("is_jumping", &vlink::BagReader::is_jumping)
      .def("async_run",
           [](vlink::BagReader& self) {
             nb::gil_scoped_release release;
             return self.async_run();
           })
      .def(
          "quit",
          [](vlink::BagReader& self, bool force) {
            nb::gil_scoped_release release;
            return self.quit(force);
          },
          "force"_a = false)
      .def(
          "wait_for_quit",
          [](vlink::BagReader& self, int timeout_ms) {
            nb::gil_scoped_release release;
            return self.wait_for_quit(timeout_ms);
          },
          "timeout_ms"_a = -1)
      .def("is_running", &vlink::BagReader::is_running)
      .def("__repr__", [](const vlink::BagReader& self) {
        const auto& info = self.get_info();
        return "BagReader(file='" + info.file_name + "', messages=" + std::to_string(info.message_count) + ")";
      });

  m.attr("VERSION") = VLINK_VERSION;
  m.attr("VERSION_MAJOR") = VLINK_VERSION_MAJOR;
  m.attr("VERSION_MINOR") = VLINK_VERSION_MINOR;
  m.attr("VERSION_PATCH") = VLINK_VERSION_PATCH;
}
