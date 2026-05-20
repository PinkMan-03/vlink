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

#include "./extension/bag_writer.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "./base/helpers.h"
#include "./base/logger.h"
#include "./base/utils.h"
#include "./extension/database_writer.h"
#include "./extension/mcap_writer.h"
#include "./extension/schema_plugin_manager.h"

namespace vlink {

// GlobalWriter
struct GlobalWriter final {
  GlobalWriter() {
    const std::string& bag_path = Utils::get_env("VLINK_BAG_PATH");

    if (!bag_path.empty()) {
      VLOG_I("BagWriter: Global recorder is enabled.");
      CLOG_I("BagWriter: Record path: %s.", bag_path.c_str());

      instance = BagWriter::create(bag_path);

      if VLIKELY (instance) {
        instance->async_run();
      } else {
        CLOG_E("BagWriter: Global recorder is disabled because VLINK_BAG_PATH has an unsupported suffix.");
      }
    }
  }

  ~GlobalWriter() {
    if (instance) {
      VLOG_I("BagWriter: Please wait for record (up to 30s).");

      instance.reset();
    }
  }

  static GlobalWriter& get() {
    static GlobalWriter global_writer;
    return global_writer;
  }

  std::mutex mtx;
  std::unordered_map<std::string, std::weak_ptr<BagWriter>> writer_map;

  std::shared_ptr<BagWriter> instance;

  VLINK_DISALLOW_COPY_AND_ASSIGN(GlobalWriter)
};

// BagWriter::Impl
struct BagWriter::Impl final {
  std::unordered_map<int, std::string> index_to_url_map;
  std::unordered_map<int, std::string> index_to_ser_map;
  std::unordered_map<std::string, int> url_to_index_map;
  std::unordered_map<std::string, int> ser_to_index_map;
  int current_url_index{0};
  int current_ser_index{0};
  mutable std::shared_mutex shared_mtx;
};

// BagWriter
std::shared_ptr<BagWriter> BagWriter::create(const std::string& path, const Config& config) {
  std::string suffix_check = path;

  std::transform(suffix_check.begin(), suffix_check.end(), suffix_check.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (Helpers::has_endwith(suffix_check, ".vdb") || Helpers::has_endwith(suffix_check, ".vdbx")) {
    return std::make_shared<DatabaseWriter>(path, config);
  } else if (Helpers::has_endwith(suffix_check, ".vcap") || Helpers::has_endwith(suffix_check, ".vcapx")) {
    return std::make_shared<McapWriter>(path, config);
  } else {
    CLOG_E("BagWriter: Unknown bag suffix, path=%s", path.c_str());
    return nullptr;
  }
}

std::shared_ptr<BagWriter> BagWriter::filter_get(const std::string& path) {
  static auto& instance = GlobalWriter::get();
  std::string suffix_check = path;

  std::transform(suffix_check.begin(), suffix_check.end(), suffix_check.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  const bool is_database = Helpers::has_endwith(suffix_check, ".vdb") || Helpers::has_endwith(suffix_check, ".vdbx");
  const bool is_mcap = Helpers::has_endwith(suffix_check, ".vcap") || Helpers::has_endwith(suffix_check, ".vcapx");

  if VUNLIKELY (!is_database && !is_mcap) {
    CLOG_E("BagWriter: Unknown bag suffix, path=%s", path.c_str());
    return nullptr;
  }

  std::lock_guard lock(instance.mtx);

  auto iter = instance.writer_map.find(path);

  if (iter != instance.writer_map.end()) {
    if (auto target = iter->second.lock()) {
      return target;
    }

    instance.writer_map.erase(iter);
  }

  {
    std::shared_ptr<BagWriter> target;

    if (is_mcap) {
      auto* ptr = new McapWriter(path);

      target = std::shared_ptr<McapWriter>(ptr, [path](McapWriter* ptr) {
        {
          std::lock_guard lock(instance.mtx);
          auto iter = instance.writer_map.find(path);

          if (iter != instance.writer_map.end() && iter->second.expired()) {
            instance.writer_map.erase(iter);
          }
        }

        delete ptr;
      });
    } else {
      auto* ptr = new DatabaseWriter(path);

      target = std::shared_ptr<DatabaseWriter>(ptr, [path](DatabaseWriter* ptr) {
        {
          std::lock_guard lock(instance.mtx);
          auto iter = instance.writer_map.find(path);

          if (iter != instance.writer_map.end() && iter->second.expired()) {
            instance.writer_map.erase(iter);
          }
        }

        delete ptr;
      });
    }

    target->async_run();

    instance.writer_map.emplace(path, target);

    return target;
  }
}

BagWriter* BagWriter::global_get() { return GlobalWriter::get().instance.get(); }

BagWriter::BagWriter(const std::string& path, const Config& config) : impl_(std::make_unique<Impl>()) {
  (void)path;
  (void)config;

  if (!config.sync_mode) {
    impl_->index_to_url_map.reserve(128);
    impl_->index_to_ser_map.reserve(128);
    impl_->url_to_index_map.reserve(128);
    impl_->ser_to_index_map.reserve(128);
  }

  Bytes::init_memory_pool();
}

void BagWriter::get_url_meta(const std::string& url, const std::string& ser, int& url_index, int& ser_index) const {
  {
    std::shared_lock read_lock(impl_->shared_mtx);

    auto url_iter = impl_->url_to_index_map.find(url);
    auto ser_iter = impl_->ser_to_index_map.find(ser);

    if VLIKELY (url_iter != impl_->url_to_index_map.end() && ser_iter != impl_->ser_to_index_map.end()) {
      url_index = url_iter->second;
      ser_index = ser_iter->second;
      return;
    }
  }

  std::unique_lock write_lock(impl_->shared_mtx);

  auto& url_id = impl_->url_to_index_map.try_emplace(url, -1).first->second;

  if (url_id < 0) {
    url_id = ++impl_->current_url_index;
    impl_->index_to_url_map[url_id] = url;
  }

  auto& ser_id = impl_->ser_to_index_map.try_emplace(ser, -1).first->second;

  if (ser_id < 0) {
    ser_id = ++impl_->current_ser_index;
    impl_->index_to_ser_map[ser_id] = ser;
  }

  url_index = url_id;
  ser_index = ser_id;
}

void BagWriter::get_url_meta(int url_index, int ser_index, std::string& url, std::string& ser) const {
  std::shared_lock read_lock(impl_->shared_mtx);

  auto url_iter = impl_->index_to_url_map.find(url_index);

  if (url_iter != impl_->index_to_url_map.end()) {
    url = url_iter->second;
  }

  auto ser_iter = impl_->index_to_ser_map.find(ser_index);

  if (ser_iter != impl_->index_to_ser_map.end()) {
    ser = ser_iter->second;
  }
}

BagWriter::~BagWriter() = default;

const std::string& BagWriter::get_default_tag_name() {
  static std::string tag_name_env_str = Utils::get_env("VLINK_BAG_TAG", "Empty");
  return tag_name_env_str;
}

const std::string& BagWriter::get_default_app_name() {
  static std::string app_name = Utils::get_app_name();
  return app_name;
}

SchemaPluginInterface* BagWriter::get_schema_interface() { return SchemaPluginManager::get().get_interface().get(); }

int32_t BagWriter::get_default_timezone_diff() { return Utils::get_timezone_diff(); }

std::string_view BagWriter::convert_action(ActionType type) {
  switch (type) {
    case ActionType::kClientRequest:
      return "C/Req";
    case ActionType::kClientResponse:
      return "C/Resp";
    case ActionType::kServerRequest:
      return "S/Req";
    case ActionType::kServerResponse:
      return "S/Resp";
    case ActionType::kPublish:
      return "Pub";
    case ActionType::kSubscribe:
      return "Sub";
    case ActionType::kSet:
      return "Set";
    case ActionType::kGet:
      return "Get";
    default:
      return "Unknown";
  }
}

std::string BagWriter::get_format_date(SystemClock* current, bool file_format) {
  SystemClock time_point;

  if (current) {
    time_point = *current;
  } else {
    time_point = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
  }

  auto milliseconds = time_point.time_since_epoch().count() % 1000U;

  std::time_t now_time_t = std::chrono::system_clock::to_time_t(time_point);

  std::tm now_tm{};

#ifdef _WIN32
  localtime_s(&now_tm, &now_time_t);
#else
  localtime_r(&now_time_t, &now_tm);
#endif

  char buffer[32];
  char full_buffer[64];

  if (file_format) {
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M-%S", &now_tm);
    std::snprintf(full_buffer, sizeof(full_buffer), "%s-%03lld", buffer,
                  static_cast<long long>(milliseconds));  // NOLINT(runtime/int, google-runtime-int)
  } else {
    std::strftime(buffer, sizeof(buffer), "%Y/%m/%d %H:%M:%S", &now_tm);
    std::snprintf(full_buffer, sizeof(full_buffer), "%s:%03lld", buffer,
                  static_cast<long long>(milliseconds));  // NOLINT(runtime/int, google-runtime-int)
  }

  return full_buffer;
}

}  // namespace vlink
