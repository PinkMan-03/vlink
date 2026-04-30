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

#include "./extension/bag_reader.h"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

#include "./base/helpers.h"
#include "./base/logger.h"
#include "./extension/bag_reader_plugin_interface.h"
#include "./extension/database_reader.h"
#include "./extension/mcap_reader.h"
#include "./impl/url.h"

namespace vlink {

// UrlMeta
bool BagReader::Info::UrlMeta::operator<(const BagReader::Info::UrlMeta& target) const noexcept {
  int lindex = Url::get_sort_index(url);
  int rindex = Url::get_sort_index(target.url);

  if (lindex < rindex) {
    return true;
  } else if (lindex > rindex) {
    return false;
  } else if (url < target.url) {
    return true;
  } else if (url > target.url) {
    return false;
  }

  return index < target.index;
}

// BagReaderImpl
struct BagReaderImpl final {
  BagReader::OutputCallback output_callback;

  std::shared_ptr<BagReaderPluginInterface> plugin_interface;
};

// BagReader
std::shared_ptr<BagReader> BagReader::create(const std::string& path, bool read_only, bool try_to_fix) {
  std::string suffix_check = path;

  std::transform(suffix_check.begin(), suffix_check.end(), suffix_check.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (Helpers::has_endwith(suffix_check, ".vdb") || Helpers::has_endwith(suffix_check, ".vdbx")) {
    return std::make_shared<DatabaseReader>(path, read_only, try_to_fix);
  } else if (Helpers::has_endwith(suffix_check, ".vcap") || Helpers::has_endwith(suffix_check, ".vcapx")) {
    return std::make_shared<McapReader>(path, read_only, try_to_fix);
  } else {
    CLOG_F("BagReader: Unknown bag suffix, path=%s", path.c_str());
    return nullptr;
  }
}

BagReader::BagReader(const std::string& path, bool read_only, bool try_to_fix)
    : impl_(std::make_unique<BagReaderImpl>()) {
  (void)path;
  (void)read_only;
  (void)try_to_fix;

  Bytes::init_memory_pool();
}

BagReader::~BagReader() = default;

void BagReader::bind_plugin_interface(const std::shared_ptr<BagReaderPluginInterface>& plugin_interface) {
  impl_->plugin_interface = plugin_interface;

  if VLIKELY (impl_->plugin_interface) {
    impl_->plugin_interface->register_output_callback(
        [this](int64_t timestamp, const std::string& url, ActionType action_type, const Bytes& data) {
          if (impl_->output_callback) {
            impl_->output_callback(timestamp, url, action_type, data);
          }
        });
  }
}

void BagReader::register_status_callback(StatusCallback&& status_callback) { (void)status_callback; }

void BagReader::register_ready_callback(ReadyCallback&& ready_callback) { (void)ready_callback; }

void BagReader::register_finish_callback(FinishCallback&& finish_callback) { (void)finish_callback; }

void BagReader::register_output_callback(OutputCallback&& output_callback) {
  impl_->output_callback = std::move(output_callback);
}

void BagReader::process_output(int64_t timestamp, const std::string& url, ActionType action_type, const Bytes& data) {
  if (impl_->plugin_interface) {
    impl_->plugin_interface->push(timestamp, url, action_type, data);
  } else {
    if (impl_->output_callback) {
      impl_->output_callback(timestamp, url, action_type, data);
    }
  }
}

void BagReader::process_url_metas(std::vector<Info::UrlMeta>& url_metas) {
  if (impl_->plugin_interface) {
    url_metas.erase(std::remove_if(url_metas.begin(), url_metas.end(),
                                   [this](Info::UrlMeta& meta) {
                                     return !impl_->plugin_interface->convert_url_meta(meta.url, meta.ser_type,
                                                                                       meta.schema_type);
                                   }),
                    url_metas.end());
  }
}

void BagReader::rebuild_url_meta_maps(const std::vector<Info::UrlMeta>& url_metas,
                                      std::unordered_map<std::string, std::string>& ser_map,
                                      std::unordered_map<std::string, SchemaType>& schema_type_map) {
  ser_map.clear();
  schema_type_map.clear();
  ser_map.reserve(url_metas.size());
  schema_type_map.reserve(url_metas.size());

  std::unordered_set<std::string> ser_conflict_urls;
  std::unordered_set<std::string> schema_conflict_urls;

  ser_conflict_urls.reserve(url_metas.size());
  schema_conflict_urls.reserve(url_metas.size());

  for (const auto& meta : url_metas) {
    auto& merged_ser_type = ser_map[meta.url];
    auto& merged_schema_type = schema_type_map[meta.url];

    if (ser_conflict_urls.count(meta.url) == 0U && !meta.ser_type.empty()) {
      if (merged_ser_type.empty() || merged_ser_type == "Bytes") {
        merged_ser_type = meta.ser_type;
      } else if (meta.ser_type != "Bytes" && meta.ser_type != merged_ser_type) {
        CLOG_E("BagReader: URL remap collision on %s, keeping ser_type unknown. ser [%s] vs [%s].", meta.url.c_str(),
               merged_ser_type.c_str(), meta.ser_type.c_str());
        merged_ser_type.clear();
        ser_conflict_urls.emplace(meta.url);
      }
    }

    if (schema_conflict_urls.count(meta.url) == 0U && meta.schema_type != SchemaType::kUnknown) {
      if (merged_schema_type == SchemaType::kUnknown) {
        merged_schema_type = meta.schema_type;
      } else if (merged_schema_type != meta.schema_type) {
        const auto current_label = SchemaData::convert_type(merged_schema_type);
        const auto new_label = SchemaData::convert_type(meta.schema_type);
        CLOG_E("BagReader: URL remap collision on %s, keeping schema_type unknown. schema [%.*s] vs [%.*s].",
               meta.url.c_str(), static_cast<int>(current_label.size()), current_label.data(),
               static_cast<int>(new_label.size()), new_label.data());
        merged_schema_type = SchemaType::kUnknown;
        schema_conflict_urls.emplace(meta.url);
      }
    }
  }
}

ActionType BagReader::convert_action(std::string_view str) {
  if (str == "C/Req") {
    return ActionType::kClientRequest;
  } else if (str == "C/Resp") {
    return ActionType::kClientResponse;
  } else if (str == "S/Req") {
    return ActionType::kServerRequest;
  } else if (str == "S/Resp") {
    return ActionType::kServerResponse;
  } else if (str == "Pub") {
    return ActionType::kPublish;
  } else if (str == "Sub") {
    return ActionType::kSubscribe;
  } else if (str == "Set") {
    return ActionType::kSet;
  } else if (str == "Get") {
    return ActionType::kGet;
  } else {
    return ActionType::kUnknownAction;
  }
}

}  // namespace vlink
