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

#include "foxglove_parameters.h"

//
#include <flatbuffers/flatbuffers.h>

//
#include <algorithm>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

//
#include "WebvizParameterSnapshot.fbs.hpp"
#include "webviz_parameter_snapshot.pb.h"

namespace vlink {
namespace webviz {

using Json = nlohmann::json;

using ProtoSnapshot = ::vlink::webviz::foxglove::pb::ParameterSnapshot;
using FbsEntry = ::vlink::webviz::foxglove::fbs::ParameterEntry;

FoxgloveParameters::FoxgloveParameters(const Config& config) : config_(config) {
  if (parse_backend_encoding(config_.encoding, backend_encoding_)) {
    config_.encoding = to_string(backend_encoding_);
  }

  std::unique_lock lock(state_mtx_);

  for (const auto& entry : config_.values) {
    if (entry.has_value && !entry.name.empty()) {
      state_[entry.name] = entry;
    }
  }
}

FoxgloveParameters::~FoxgloveParameters() { stop(); }

bool FoxgloveParameters::start() {
  if VUNLIKELY (started_.exchange(true)) {
    return true;
  }

  if VUNLIKELY (!enabled()) {
    return true;
  }

  if VUNLIKELY (!parse_backend_encoding(config_.encoding, backend_encoding_)) {
    started_.store(false);
    MLOG_E("Invalid Foxglove parameters encoding: {}", config_.encoding);
    return false;
  }

  if (!config_.url.empty()) {
    setter_ = Setter<Bytes>::create_shared(config_.url, InitType::kWithoutInit);

    if VUNLIKELY (!setter_) {
      started_.store(false);
      MLOG_E("Failed to create Foxglove parameter setter for {}", config_.url);
      return false;
    }

    const auto ser = backend_ser(backend_encoding_);
    auto schema_type = SchemaType::kRaw;

    if (backend_encoding_ == FoxgloveParameters::kProtobuf) {
      schema_type = SchemaType::kProtobuf;
    } else if (backend_encoding_ == FoxgloveParameters::kFlatbuffers) {
      schema_type = SchemaType::kFlatbuffers;
    }

    setter_->set_ser_type(ser, schema_type);
    ProxyBridge::apply_transport(*setter_, config_.transport, false);

    if VUNLIKELY (!setter_->init()) {
      started_.store(false);
      MLOG_E("Failed to initialize Foxglove parameter setter on {} with {}", config_.url, config_.encoding);
      setter_.reset();
      return false;
    }
  } else if VUNLIKELY (backend_encoding_ != FoxgloveParameters::kJson) {
    MLOG_W("Foxglove parameters encoding {} is ignored without parameters.url; forcing json", config_.encoding);
    backend_encoding_ = FoxgloveParameters::kJson;
    config_.encoding = to_string(backend_encoding_);
  }

  size_t value_count = 0;

  {
    std::shared_lock lock(state_mtx_);
    value_count = state_.size();
  }

  if VUNLIKELY (!config_.url.empty() && value_count == 0U) {
    MLOG_W("Foxglove parameters.url is configured without parameters.values; Parameters panel will start empty");
  }

  return true;
}

void FoxgloveParameters::stop() {
  if VUNLIKELY (!started_.exchange(false)) {
    return;
  }

  if (setter_) {
    setter_->deinit();
    setter_.reset();
  }
}

std::vector<std::string> FoxgloveParameters::get_names() const {
  std::vector<std::string> names;
  std::shared_lock lock(state_mtx_);
  names.reserve(state_.size());

  for (const auto& state_entry : state_) {
    names.emplace_back(state_entry.first);
  }

  return names;
}

bool FoxgloveParameters::parse_config_values(const nlohmann::json& parameters_root, std::vector<ParameterEntry>& out,
                                             std::string& error) {
  out.clear();

  if VUNLIKELY (!parameters_root.contains("values")) {
    return true;
  }

  const auto& values = parameters_root["values"];

  if VUNLIKELY (!values.is_array()) {
    error = "parameters.values must be an array";
    return false;
  }

  nlohmann::json request;
  request["parameters"] = values;

  if VUNLIKELY (!parse_parameter_entries(request, out, error)) {
    return false;
  }

  for (const auto& entry : out) {
    if VUNLIKELY (!entry.has_value) {
      error = "parameters.values entries must include value: " + entry.name;
      out.clear();
      return false;
    }
  }

  return true;
}

Json FoxgloveParameters::build_parameter_values(const std::vector<std::string>& names, std::string_view id) const {
  Json msg;
  msg["op"] = "parameterValues";
  msg["parameters"] = Json::array();

  {
    std::shared_lock lock(state_mtx_);

    if (names.empty()) {
      for (const auto& state_entry : state_) {
        msg["parameters"].emplace_back(make_parameter_json(state_entry.second));
      }
    } else {
      for (const auto& name : names) {
        auto state_iter = state_.find(name);

        if (state_iter != state_.end()) {
          msg["parameters"].emplace_back(make_parameter_json(state_iter->second));
        }
      }
    }
  }

  if (!id.empty()) {
    msg["id"] = std::string(id);
  }

  return msg;
}

bool FoxgloveParameters::apply_set_parameters(const Json& request, Json& response, std::vector<ParameterEntry>& delta,
                                              std::string& error) {
  if VUNLIKELY (!request.contains("parameters") || !request["parameters"].is_array()) {
    error = "setParameters requires a parameters array";
    return false;
  }

  std::vector<ParameterEntry> requested;

  if VUNLIKELY (!parse_parameter_entries(request, requested, error)) {
    return false;
  }

  ParameterMap old_state;
  ParameterMap new_state;
  std::vector<std::string> requested_names;
  requested_names.reserve(requested.size());

  {
    std::unique_lock lock(state_mtx_);
    old_state = state_;
    new_state = state_;

    for (const auto& entry : requested) {
      requested_names.emplace_back(entry.name);

      if (entry.has_value) {
        new_state[entry.name] = entry;
      } else {
        new_state.erase(entry.name);
      }
    }

    state_ = new_state;
  }

  if (setter_) {
    Bytes payload;

    if VUNLIKELY (!encode_snapshot(new_state, payload)) {
      error = "failed to encode parameter snapshot";
      std::unique_lock lock(state_mtx_);
      state_ = std::move(old_state);
      return false;
    }

    setter_->set(payload);
  } else if VUNLIKELY (!config_.url.empty()) {
    error = "parameter setter is not initialized";
    std::unique_lock lock(state_mtx_);
    state_ = std::move(old_state);
    return false;
  }

  delta = diff_states(old_state, new_state);

  std::string response_id;

  if (request.contains("id") && request["id"].is_string()) {
    response_id = request["id"].get<std::string>();
  }

  response = build_parameter_values(requested_names, response_id);
  return true;
}

bool FoxgloveParameters::parse_backend_encoding(std::string_view encoding, BackendEncoding& out) {
  if (encoding == "json") {
    out = FoxgloveParameters::kJson;
    return true;
  }

  if (encoding == "protobuf") {
    out = FoxgloveParameters::kProtobuf;
    return true;
  }

  if (encoding == "flatbuffer" || encoding == "flatbuffers") {
    out = FoxgloveParameters::kFlatbuffers;
    return true;
  }

  return false;
}

const char* FoxgloveParameters::to_string(BackendEncoding encoding) {
  switch (encoding) {
    case FoxgloveParameters::kJson:
      return "json";
    case FoxgloveParameters::kProtobuf:
      return "protobuf";
    case FoxgloveParameters::kFlatbuffers:
      return "flatbuffer";
    default:
      return "unknown";
  }
}

std::string FoxgloveParameters::backend_ser(BackendEncoding encoding) {
  switch (encoding) {
    case FoxgloveParameters::kJson:
      return "json";
    case FoxgloveParameters::kProtobuf:
      return "vlink.webviz.foxglove.pb.ParameterSnapshot";
    case FoxgloveParameters::kFlatbuffers:
      return "vlink.webviz.foxglove.fbs.ParameterSnapshot";
    default:
      return {};
  }
}

bool FoxgloveParameters::is_supported_parameter_type(std::string_view type) {
  return type.empty() || type == "byte_array" || type == "float64" || type == "float64_array";
}

bool FoxgloveParameters::is_supported_parameter_value(const Json& value) {
  if VUNLIKELY (value.is_null()) {
    return false;
  }

  if (value.is_boolean() || value.is_number() || value.is_string()) {
    return true;
  }

  if (value.is_array()) {
    for (const auto& item : value) {
      if VUNLIKELY (!is_supported_parameter_value(item)) {
        return false;
      }
    }

    return true;
  }

  if (value.is_object()) {
    for (const auto& kv : value.items()) {
      const auto& item = kv.value();
      if VUNLIKELY (!is_supported_parameter_value(item)) {
        return false;
      }
    }

    return true;
  }

  return false;
}

bool FoxgloveParameters::validate_parameter_value(const Json& value, std::string_view type, std::string& error) {
  if VUNLIKELY (!is_supported_parameter_type(type)) {
    error = "unsupported parameter type";
    return false;
  }

  if VUNLIKELY (value.is_null()) {
    error = "parameter value must not be null";
    return false;
  }

  if VUNLIKELY (type == "byte_array") {
    if (value.is_string()) {
      return true;
    }

    error = "byte_array parameter requires a base64 string value";
    return false;
  }

  if VUNLIKELY (type == "float64") {
    if (value.is_number()) {
      return true;
    }

    error = "float64 parameter requires a numeric value";
    return false;
  }

  if VUNLIKELY (type == "float64_array") {
    if VUNLIKELY (!value.is_array()) {
      error = "float64_array parameter requires an array value";
      return false;
    }

    for (const auto& item : value) {
      if VUNLIKELY (!item.is_number()) {
        error = "float64_array parameter requires numeric array items";
        return false;
      }
    }

    return true;
  }

  if (is_supported_parameter_value(value)) {
    return true;
  }

  error = "parameter value contains unsupported JSON types";
  return false;
}

bool FoxgloveParameters::parse_parameter_entry(const Json& item, ParameterEntry& out, std::string& error) {
  if VUNLIKELY (!item.is_object()) {
    error = "parameter entry must be an object";
    return false;
  }

  if VUNLIKELY (!item.contains("name") || !item["name"].is_string()) {
    error = "parameter entry requires string name";
    return false;
  }

  out = ParameterEntry{};
  out.name = item["name"].get<std::string>();

  if VUNLIKELY (out.name.empty()) {
    error = "parameter name must not be empty";
    return false;
  }

  if (item.contains("type")) {
    if VUNLIKELY (!item["type"].is_string()) {
      error = "parameter type must be a string";
      return false;
    }

    out.type = item["type"].get<std::string>();
  }

  out.has_value = item.contains("value");

  if VUNLIKELY (!out.has_value) {
    if VUNLIKELY (!out.type.empty()) {
      error = "parameter removals must not include type";
      return false;
    }

    return true;
  }

  out.value = item["value"];

  if VUNLIKELY (!validate_parameter_value(out.value, out.type, error)) {
    if (!error.empty()) {
      error = "parameter '" + out.name + "': " + error;
    }

    return false;
  }

  return true;
}

bool FoxgloveParameters::parse_parameter_entries(const Json& root, std::vector<ParameterEntry>& out,
                                                 std::string& error) {
  std::unordered_set<std::string> names;
  out.clear();

  for (const auto& item : root.at("parameters")) {
    ParameterEntry entry;

    if VUNLIKELY (!parse_parameter_entry(item, entry, error)) {
      return false;
    }

    if VUNLIKELY (!names.insert(entry.name).second) {
      error = "duplicate parameter name: " + entry.name;
      return false;
    }

    out.emplace_back(std::move(entry));
  }

  return true;
}

Json FoxgloveParameters::make_parameter_json(const ParameterEntry& entry) {
  Json item;
  item["name"] = entry.name;

  if (entry.has_value) {
    item["value"] = entry.value;

    if (!entry.type.empty()) {
      item["type"] = entry.type;
    }
  }

  return item;
}

std::string FoxgloveParameters::encode_json_fragment(const Json& value) { return value.dump(); }

bool FoxgloveParameters::encode_json_payload(const ParameterMap& state, Bytes& payload) {
  Json root;
  root["parameters"] = Json::array();

  for (const auto& state_entry : state) {
    root["parameters"].emplace_back(make_parameter_json(state_entry.second));
  }

  auto json_text = root.dump();
  payload = Bytes::deep_copy(reinterpret_cast<const uint8_t*>(json_text.data()), json_text.size());
  return true;
}

std::vector<FoxgloveParameters::ParameterEntry> FoxgloveParameters::diff_states(const ParameterMap& old_state,
                                                                                const ParameterMap& new_state) {
  std::vector<ParameterEntry> delta;

  for (const auto& [name, entry] : new_state) {
    auto old_state_iter = old_state.find(name);

    if VUNLIKELY (old_state_iter == old_state.end()) {
      delta.emplace_back(entry);
      continue;
    }

    if VUNLIKELY (old_state_iter->second.type != entry.type || old_state_iter->second.value != entry.value) {
      delta.emplace_back(entry);
    }
  }

  for (const auto& old_entry : old_state) {
    if (new_state.find(old_entry.first) != new_state.end()) {
      continue;
    }

    ParameterEntry removed;
    removed.name = old_entry.first;
    removed.has_value = false;
    delta.emplace_back(std::move(removed));
  }

  return delta;
}

bool FoxgloveParameters::encode_snapshot(const ParameterMap& state, Bytes& payload) const {
  if (backend_encoding_ == FoxgloveParameters::kJson) {
    return encode_json_payload(state, payload);
  }

  if VUNLIKELY (backend_encoding_ == FoxgloveParameters::kProtobuf) {
    ProtoSnapshot snapshot;

    for (const auto& state_entry : state) {
      const auto& entry = state_entry.second;
      auto* parameter = snapshot.add_parameters();
      parameter->set_name(entry.name);
      parameter->set_has_value(entry.has_value);
      if (entry.type == "byte_array") {
        parameter->set_type(::vlink::webviz::foxglove::pb::ParameterType::PARAMETER_TYPE_BYTE_ARRAY);
      } else if (entry.type == "float64") {
        parameter->set_type(::vlink::webviz::foxglove::pb::ParameterType::PARAMETER_TYPE_FLOAT64);
      } else if (entry.type == "float64_array") {
        parameter->set_type(::vlink::webviz::foxglove::pb::ParameterType::PARAMETER_TYPE_FLOAT64_ARRAY);
      } else {
        parameter->set_type(::vlink::webviz::foxglove::pb::ParameterType::PARAMETER_TYPE_UNSPECIFIED);
      }
      parameter->set_json_value(encode_json_fragment(entry.value));
    }

    auto byte_size = snapshot.ByteSizeLong();

    if VUNLIKELY (byte_size > std::numeric_limits<int>::max()) {
      return false;
    }

    // NOLINTNEXTLINE(readability-redundant-casting)
    payload = Bytes::create(static_cast<size_t>(byte_size));
    return snapshot.SerializeToArray(payload.data(), static_cast<int>(byte_size));
  }

  flatbuffers::FlatBufferBuilder builder(1024);
  std::vector<flatbuffers::Offset<FbsEntry>> entries;
  entries.reserve(state.size());

  for (const auto& state_entry : state) {
    const auto& entry = state_entry.second;
    auto name_offset = builder.CreateString(entry.name);
    auto json_offset = builder.CreateString(encode_json_fragment(entry.value));
    ::vlink::webviz::foxglove::fbs::ParameterType fbs_type = ::vlink::webviz::foxglove::fbs::ParameterType::Unspecified;

    if (entry.type == "byte_array") {
      fbs_type = ::vlink::webviz::foxglove::fbs::ParameterType::ByteArray;
    } else if (entry.type == "float64") {
      fbs_type = ::vlink::webviz::foxglove::fbs::ParameterType::Float64;
    } else if (entry.type == "float64_array") {
      fbs_type = ::vlink::webviz::foxglove::fbs::ParameterType::Float64Array;
    }

    entries.emplace_back(vlink::webviz::foxglove::fbs::CreateParameterEntry(builder, name_offset, entry.has_value,
                                                                            fbs_type, json_offset));
  }

  auto parameters_offset = builder.CreateVector(entries);
  auto snapshot_offset = vlink::webviz::foxglove::fbs::CreateParameterSnapshot(builder, parameters_offset);
  builder.Finish(snapshot_offset);
  payload = Bytes::deep_copy(builder.GetBufferPointer(), builder.GetSize());
  return !payload.empty();
}

}  // namespace webviz
}  // namespace vlink
