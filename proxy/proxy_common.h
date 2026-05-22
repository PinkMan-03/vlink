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

#pragma once

#include <vlink/base/bytes.h>
#include <vlink/base/logger.h>
#include <vlink/external/proxy_api.h>
//
#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
//
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#define VLINK_PROXY_ENABLE_FILTER 1

#define VLINK_PROXY_ENABLE_HANDSHAKE 1

namespace vlink {

namespace proxy {

[[maybe_unused]] static constexpr std::string_view kSocketBufStr = "8388608";
[[maybe_unused]] static constexpr std::string_view kSocketMtuStr = "65500";

[[maybe_unused]] static constexpr std::string_view kDataUrlCtx = "://proxy/proxy_data/v3?qos=better&domain=";
[[maybe_unused]] static constexpr std::string_view kViewerDataUrlCtx = "://proxy/viewer_data/v3?qos=better&domain=";

[[maybe_unused]] static constexpr std::string_view kDataReliableUrlCtx =
    "://proxy/proxy_data/reliable/v3?qos=large&domain=";
[[maybe_unused]] static constexpr std::string_view kViewerDataReliableUrlCtx =
    "://proxy/viewer_data/reliable/v3?qos=large&domain=";

[[maybe_unused]] static constexpr std::string_view kTimeUrlCtx = "://proxy/time/v3?qos=clock&domain=";
[[maybe_unused]] static constexpr std::string_view kInfoListUrlCtx = "://proxy/info_list/v3?qos=poor&domain=";
[[maybe_unused]] static constexpr std::string_view kControlUrlCtx = "://proxy/control/v3?qos=best&domain=";
[[maybe_unused]] static constexpr std::string_view kHandshakeUrlCtx = "://proxy/handshake/v3?qos=best&domain=";

[[maybe_unused]] static constexpr std::string_view kDataShmUrlCtx = "://proxy/proxy_data/v3?domain=";
[[maybe_unused]] static constexpr std::string_view kViewerDataShmUrlCtx = "://proxy/viewer_data/v3?domain=";

[[maybe_unused]] static constexpr uint32_t kHandshakeWaitMs = 800;
[[maybe_unused]] static constexpr uint32_t kHandshakeInvokeMs = 800;

enum HandshakeResult : uint8_t {
  kHandshakeOk = 0,
  kHandshakeVersionMismatch = 1,
  kHandshakeInternalError = 2,
};

[[nodiscard]] inline std::string make_url(std::string_view scheme, std::string_view ctx, std::string_view domain) {
  std::string out;

  out.reserve(scheme.size() + ctx.size() + domain.size());

  out.append(scheme).append(ctx).append(domain);

  return out;
}

template <typename T>
inline bool encode_to_bytes(const T& src, Bytes& des) noexcept {
  thread_local std::ostringstream oss;

  oss.str(std::string());
  oss.clear();

  try {
    {
      cereal::PortableBinaryOutputArchive archive(oss);
      archive(src);
    }

    const std::string& str = oss.str();
    des = Bytes::deep_copy(reinterpret_cast<const uint8_t*>(str.data()), str.size());

    return true;
  } catch (const std::exception& e) {
    VLOG_T("ProxyCommon: Cereal serialize failed: ", e.what(), ".");
    return false;
  }
}

template <typename T>
inline bool decode_from_bytes(const Bytes& src, T& des) noexcept {
  if VUNLIKELY (src.empty()) {
    des = T{};
    return true;
  }

  thread_local std::istringstream iss;

  iss.str(std::string(reinterpret_cast<const char*>(src.data()), src.size()));
  iss.clear();

  try {
    cereal::PortableBinaryInputArchive archive(iss);
    archive(des);

    return true;
  } catch (const std::exception& e) {
    VLOG_T("ProxyCommon: Cereal deserialize failed: ", e.what(), ".");
    return false;
  }
}

}  // namespace proxy

template <class Archive>
inline void serialize(Archive& ar, ProxyAPI::Process& msg) {
  ar(msg.type, msg.host, msg.pid, msg.name, msg.ip);
}

template <class Archive>
inline void save(Archive& ar, const ProxyAPI::UrlMeta& msg) {
  ar(msg.url, msg.ser, static_cast<uint8_t>(msg.schema), static_cast<uint8_t>(msg.type));
}

template <class Archive>
inline void load(Archive& ar, ProxyAPI::UrlMeta& msg) {
  uint8_t schema_value{0};
  uint8_t type_value{0};

  ar(msg.url, msg.ser, schema_value, type_value);

  msg.schema = static_cast<SchemaType>(schema_value);
  msg.type = static_cast<ImplType>(type_value);
}

template <class Archive>
inline void save(Archive& ar, const ProxyAPI::Info& msg) {
  ar(msg.type, msg.url, msg.ser, static_cast<uint8_t>(msg.schema), static_cast<uint8_t>(msg.status), msg.freq, msg.rate,
     msg.loss, msg.latency, msg.process_list);
}

template <class Archive>
inline void load(Archive& ar, ProxyAPI::Info& msg) {
  uint8_t schema_value{0};
  uint8_t status_value{0};

  ar(msg.type, msg.url, msg.ser, schema_value, status_value, msg.freq, msg.rate, msg.loss, msg.latency,
     msg.process_list);

  msg.schema = static_cast<SchemaType>(schema_value);
  msg.status = static_cast<ProxyAPI::Status>(status_value);
}

template <class Archive>
inline void save(Archive& ar, const ProxyAPI::Control& msg) {
  ar(static_cast<uint8_t>(msg.mode), msg.url_meta_list, msg.filter_by_process, msg.filter_str, msg.filter_type);
}

template <class Archive>
inline void load(Archive& ar, ProxyAPI::Control& msg) {
  uint8_t mode_value{0};

  ar(mode_value, msg.url_meta_list, msg.filter_by_process, msg.filter_str, msg.filter_type);

  msg.mode = static_cast<ProxyAPI::Mode>(mode_value);
}

namespace proxy {

struct ControlPacket final {
  uint32_t control_id{0};
  std::string token;
  ProxyAPI::Control body;

  template <class Archive>
  void serialize(Archive& ar) {
    ar(control_id, token, body);
  }

  bool operator>>(Bytes& des) const noexcept { return encode_to_bytes(*this, des); }
  bool operator<<(const Bytes& src) noexcept { return decode_from_bytes(src, *this); }
};

struct InfoListPacket final {
  uint32_t control_id{0};
  std::string hostname;
  std::vector<ProxyAPI::Info> info_list;

  template <class Archive>
  void serialize(Archive& ar) {
    ar(control_id, hostname, info_list);
  }

  bool operator>>(Bytes& des) const noexcept { return encode_to_bytes(*this, des); }
  bool operator<<(const Bytes& src) noexcept { return decode_from_bytes(src, *this); }
};

struct TimePacket final {
  uint32_t control_id{0};
  ProxyAPI::Mode mode{ProxyAPI::kOffline};
  uint64_t sys_time{0};
  uint64_t boot_time{0};
  bool reliable_mode{false};
  bool tcp_mode{false};
  bool direct_mode{false};
  std::string version;
  std::string hostname;
  std::string machine_id;
  double cpu_usage{0.0};
  double memory_usage{0.0};
  std::string token;

  template <class Archive>
  void save(Archive& ar) const {
    ar(control_id, static_cast<uint8_t>(mode), sys_time, boot_time, reliable_mode, tcp_mode, direct_mode, version,
       hostname, machine_id, cpu_usage, memory_usage, token);
  }

  template <class Archive>
  void load(Archive& ar) {
    uint8_t mode_value{0};

    ar(control_id, mode_value, sys_time, boot_time, reliable_mode, tcp_mode, direct_mode, version, hostname, machine_id,
       cpu_usage, memory_usage, token);

    mode = static_cast<ProxyAPI::Mode>(mode_value);
  }

  bool operator>>(Bytes& des) const noexcept { return encode_to_bytes(*this, des); }
  bool operator<<(const Bytes& src) noexcept { return decode_from_bytes(src, *this); }
};

struct HandshakeReqPacket final {
  uint32_t control_id{0};
  std::string version;
  std::string hostname;
  std::string role;

  template <class Archive>
  void serialize(Archive& ar) {
    ar(control_id, version, hostname, role);
  }

  bool operator>>(Bytes& des) const noexcept { return encode_to_bytes(*this, des); }
  bool operator<<(const Bytes& src) noexcept { return decode_from_bytes(src, *this); }
};

struct HandshakeRespPacket final {
  HandshakeResult result{kHandshakeOk};
  std::string token;
  std::string version;
  std::string hostname;
  std::string machine_id;

  template <class Archive>
  void save(Archive& ar) const {
    ar(static_cast<uint8_t>(result), token, version, hostname, machine_id);
  }

  template <class Archive>
  void load(Archive& ar) {
    uint8_t result_value{0};

    ar(result_value, token, version, hostname, machine_id);

    result = static_cast<HandshakeResult>(result_value);
  }

  bool operator>>(Bytes& des) const noexcept { return encode_to_bytes(*this, des); }
  bool operator<<(const Bytes& src) noexcept { return decode_from_bytes(src, *this); }
};

}  // namespace proxy

}  // namespace vlink
