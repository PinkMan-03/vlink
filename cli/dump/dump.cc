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

#include <vlink/base/elapsed_timer.h>
#include <vlink/base/helpers.h>
#include <vlink/base/utils.h>
#include <vlink/extension/bag_reader.h>
#include <vlink/extension/discovery_viewer.h>
#include <vlink/version.h>
#include <vlink/vlink.h>
#include <vlink/zerocopy/audio_frame.h>
#include <vlink/zerocopy/camera_frame.h>
#include <vlink/zerocopy/object_array.h>
#include <vlink/zerocopy/occupancy_grid.h>
#include <vlink/zerocopy/point_cloud.h>
#include <vlink/zerocopy/raw_data.h>
#include <vlink/zerocopy/tensor.h>

#if __has_include(<google/protobuf/compiler/importer.h>) && __has_include(<google/protobuf/text_format.h>)

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif

#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message.h>
#include <google/protobuf/reflection.h>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/text_format.h>

#if GOOGLE_PROTOBUF_VERSION >= 3004000
#define VLINK_HAS_PROTOBUF_COMPILER
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#endif

#if __has_include(<flatbuffers/idl.h>)
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/idl.h>

#include "private/flat_gen_text.h"
#define VLINK_HAS_FBS_PARSER
#endif

#if __has_include(<exprtk/exprtk.hpp>)
#include <exprtk/exprtk.hpp>
#define VLINK_HAS_EXPRTK
#endif

#include <argparse/argparse.hpp>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if __has_include(<unistd.h>)
#include <unistd.h>
#endif

#ifdef _WIN32
#include <Windows.h>
#undef min
#undef max
#undef GetMessage
#endif

enum class DumpType : uint8_t {
  kConsole = 0,
  kCsv,
  kJson,
  kBin,
  kJpg,
  kH264,
  kH265,
  kRaw,
  kPcd,
};

using VariantType = std::variant<int64_t, double, std::string, vlink::Bytes>;
using RawSub = vlink::Subscriber<vlink::Bytes>;

struct DumpRecord {
  int64_t timestamp{0};
  std::vector<VariantType> values;
  std::vector<double> expr_results;
};

using DumpCallback = vlink::Function<void(int64_t timestamp, const std::string& url, const std::string& ser,
                                          vlink::SchemaType schema_type, const vlink::Bytes& bytes)>;

[[maybe_unused]] static std::atomic_bool has_quit{false};
[[maybe_unused]] std::atomic_bool is_broken{false};
[[maybe_unused]] std::atomic_bool quit_flag{false};
[[maybe_unused]] std::atomic_bool data_has_changed{false};
[[maybe_unused]] std::atomic_bool callback_has_set{false};

[[maybe_unused]] bool quiet_flag{false};
[[maybe_unused]] bool detail_flag{false};
[[maybe_unused]] vlink::ConditionVariable quit_cv;
[[maybe_unused]] std::mutex print_mtx;
[[maybe_unused]] std::thread print_thread;
[[maybe_unused]] bool dump_for_bag{false};
[[maybe_unused]] DumpType dump_type{DumpType::kConsole};
[[maybe_unused]] int64_t begin_time{0};
[[maybe_unused]] int64_t end_time{0};
[[maybe_unused]] int max_count{0};
[[maybe_unused]] double max_hz{0};
[[maybe_unused]] std::shared_ptr<vlink::DiscoveryViewer> discovery_viewer;
[[maybe_unused]] std::shared_ptr<vlink::BagReader> bag_player;
[[maybe_unused]] vlink::BagReader::Config bag_config;
[[maybe_unused]] std::unordered_map<std::string, std::shared_ptr<RawSub>> sub_urls;
[[maybe_unused]] std::mutex sub_urls_mtx;
[[maybe_unused]] DumpCallback dump_callback;
[[maybe_unused]] vlink::ElapsedTimer main_elapsed_timer{vlink::ElapsedTimer::kMicro};
[[maybe_unused]] std::vector<DumpRecord> cache_buffer;
[[maybe_unused]] std::mutex cache_mtx;
[[maybe_unused]] std::vector<std::string> field_specs;
[[maybe_unused]] std::vector<std::vector<std::string>> field_paths;
[[maybe_unused]] std::atomic<int64_t> output_count{0};
[[maybe_unused]] std::atomic<int64_t> last_output_us{0};
[[maybe_unused]] std::vector<std::string> expr_strings;
[[maybe_unused]] std::vector<std::string> expr_var_names;

#ifdef VLINK_HAS_EXPRTK

struct ExprContext {
  exprtk::symbol_table<double> symbol_table;
  exprtk::expression<double> expr;
  std::vector<double> var_values;
  bool compiled{false};
};

[[maybe_unused]] std::vector<ExprContext> expr_contexts;
[[maybe_unused]] std::mutex expr_mtx;

static bool compile_expressions() {
  if (expr_strings.empty() || field_specs.empty()) {
    return true;
  }

  expr_var_names.resize(field_specs.size());

  for (size_t i = 0; i < field_specs.size(); ++i) {
    std::string var = field_specs[i];

    for (auto& ch : var) {
      if (ch == '.' || ch == '[' || ch == ']') {
        ch = '_';
      }
    }

    while (!var.empty() && var.back() == '_') {
      var.pop_back();
    }

    expr_var_names[i] = var;
  }

  expr_contexts.resize(expr_strings.size());

  for (size_t e = 0; e < expr_strings.size(); ++e) {
    auto& ctx = expr_contexts[e];
    ctx.var_values.resize(field_specs.size(), 0.0);

    ctx.symbol_table.add_constants();

    for (size_t i = 0; i < field_specs.size(); ++i) {
      ctx.symbol_table.add_variable(expr_var_names[i], ctx.var_values[i]);
    }

    ctx.expr.register_symbol_table(ctx.symbol_table);

    exprtk::parser<double> parser;

    if VUNLIKELY (!parser.compile(expr_strings[e], ctx.expr)) {
      std::cerr << "Failed to compile expression: " << expr_strings[e] << std::endl;
      return false;
    }

    ctx.compiled = true;
  }

  return true;
}

static double variant_to_double(const VariantType& v) {
  if (std::holds_alternative<int64_t>(v)) {
    return static_cast<double>(std::get<int64_t>(v));
  }

  if (std::holds_alternative<double>(v)) {
    return std::get<double>(v);
  }

  return 0.0;
}

static std::vector<double> evaluate_expressions(const std::vector<VariantType>& values) {
  std::lock_guard lock(expr_mtx);

  std::vector<double> results;
  results.reserve(expr_contexts.size());

  for (auto& ctx : expr_contexts) {
    if VUNLIKELY (!ctx.compiled) {
      results.emplace_back(0.0);
      continue;
    }

    for (size_t i = 0; i < values.size() && i < ctx.var_values.size(); ++i) {
      ctx.var_values[i] = variant_to_double(values[i]);
    }

    results.emplace_back(ctx.expr.value());
  }

  return results;
}

#endif

static std::string variant_to_string(const VariantType& v) {
  if (std::holds_alternative<int64_t>(v)) {
    return std::to_string(std::get<int64_t>(v));
  }

  if (std::holds_alternative<double>(v)) {
    std::ostringstream oss;
    oss << std::setprecision(12) << std::get<double>(v);
    return oss.str();
  }

  if (std::holds_alternative<std::string>(v)) {
    return std::get<std::string>(v);
  }

  if (std::holds_alternative<vlink::Bytes>(v)) {
    return "<bytes:" + std::to_string(std::get<vlink::Bytes>(v).size()) + ">";
  }

  return {};
}

static bool check_rate_limit() {
  if VUNLIKELY (max_count > 0 && output_count >= max_count) {
    return false;
  }

  if VUNLIKELY (max_hz > 0) {
    int64_t now = main_elapsed_timer.get();
    auto min_interval = static_cast<int64_t>(1000000.0 / max_hz);

    if VUNLIKELY (now - last_output_us < min_interval) {
      return false;
    }

    last_output_us = now;
  }

  ++output_count;
  return true;
}

#ifdef VLINK_HAS_PROTOBUF_COMPILER

static void start_print() {
  if (quiet_flag) {
    return;
  }

  main_elapsed_timer.start();

  if VUNLIKELY (print_thread.joinable()) {
    return;
  }

  print_thread = std::thread([]() {
    int64_t print_time = 0;
    int64_t real_begin_time = 0;
    int64_t real_end_time = 0;

    if (dump_for_bag) {
      real_begin_time = begin_time > 0 ? begin_time : bag_player->get_info().blank_duration;
      real_end_time = end_time > 0 ? end_time : bag_player->get_info().total_duration;
    }

    while (!quit_flag) {
      std::unique_lock lock(print_mtx);
      quit_cv.wait_for(lock, std::chrono::milliseconds(50), []() -> bool { return quit_flag; });

      if VUNLIKELY (quit_flag) {
        break;
      }

      if (detail_flag || dump_type == DumpType::kConsole) {
        continue;
      }

      if (dump_for_bag) {
        if VLIKELY (bag_player->get_status() == vlink::BagReader::kPlaying) {
          std::cout << "\033[2K\r";
          std::cout << "Progress: ";
          const auto duration = real_end_time - real_begin_time;
          std::cout << vlink::Helpers::double_to_string(
              duration > 0 ? static_cast<double>(100 * (bag_player->get_real_timestamp() - real_begin_time)) /
                                 static_cast<double>(duration)
                           : 0.0,
              2);
          std::cout << "% [" << output_count << " samples]";
          std::cout.flush();
        }
      } else {
        std::cout << "\033[2K\r";

        if (data_has_changed) {
          data_has_changed = false;
          std::cout << "\033[32m";
        } else {
          std::cout << "\033[31m";
        }

        print_time = main_elapsed_timer.get() / 1000;
        std::cout << vlink::Helpers::format_milliseconds(print_time + 50, false);
        std::cout << " (" << std::fixed << std::setprecision(1) << print_time / 1000.0F << "s)";
        std::cout << " [" << output_count << " samples]";
        std::cout << "\033[0m:";
        std::cout.flush();
      }
    }
  });
}

static void stop_print() {
  if (quiet_flag) {
    return;
  }

  std::unique_lock lock(print_mtx);

  if VLIKELY (!quit_flag) {
    quit_flag = true;
    lock.unlock();
    quit_cv.notify_all();

    if VLIKELY (print_thread.joinable()) {
      print_thread.join();
    }
  }
}

static void import_protos(google::protobuf::compiler::Importer* importer, const std::filesystem::path& root_dir,
                          const std::filesystem::path& sub_dir, bool& has_import, int depth = 0) {
  if VUNLIKELY (depth >= 100) {
    return;
  }

  std::vector<std::filesystem::directory_entry> file_list;

  try {
    for (const auto& entry : std::filesystem::directory_iterator(sub_dir)) {
      file_list.emplace_back(entry);
    }
  } catch (std::filesystem::filesystem_error&) {
    return;
  }

  if VUNLIKELY (file_list.empty() || file_list.size() > 1000) {
    return;
  }

  for (const auto& file : file_list) {
    try {
      if (file.is_regular_file() && file.path().extension() == ".proto") {
#ifdef _WIN32
        auto relative_path = vlink::Helpers::path_to_string(std::filesystem::relative(file.path(), root_dir));
        std::replace(relative_path.begin(), relative_path.end(), '\\', '/');
#else
        auto relative_path = std::filesystem::relative(file.path(), root_dir).string();
#endif

        if VLIKELY (importer->Import(relative_path)) {
          has_import = true;
        }
      } else if (file.is_directory()) {
        import_protos(importer, root_dir, file.path(), has_import, depth + 1);
      }
    } catch (std::filesystem::filesystem_error&) {
      continue;
    }
  }
}

static bool extract_proto_value(const google::protobuf::Message& message, const std::vector<std::string>& path_parts,
                                size_t depth, VariantType& result) {
  if VUNLIKELY (depth >= path_parts.size()) {
    return false;
  }

  const auto* descriptor = message.GetDescriptor();
  const auto* reflection = message.GetReflection();

  std::string field_name = path_parts[depth];
  int array_index = -1;

  auto bracket_pos = field_name.find('[');

  if VUNLIKELY (bracket_pos != std::string::npos) {
    auto close_pos = field_name.find(']', bracket_pos);

    if VLIKELY (close_pos != std::string::npos && close_pos > bracket_pos) {
      std::from_chars(field_name.data() + bracket_pos + 1, field_name.data() + close_pos, array_index);
      field_name = field_name.substr(0, bracket_pos);
    }
  }

  const auto* field = descriptor->FindFieldByName(field_name);

  if VUNLIKELY (!field) {
    return false;
  }

  bool is_leaf = (depth == path_parts.size() - 1);

  auto extract_scalar = [&field, &result](auto get_fn) -> bool {
    if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      return false;
    }

    result = get_fn(field->cpp_type());
    return true;
  };

  if (field->is_repeated()) {
    if VUNLIKELY (array_index < 0 || array_index >= reflection->FieldSize(message, field)) {
      return false;
    }

    if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      const auto& sub_msg = reflection->GetRepeatedMessage(message, field, array_index);

      if (is_leaf) {
        std::string text;
        bool ret = google::protobuf::TextFormat::PrintToString(sub_msg, &text);

        if VLIKELY (ret) {
          result = std::move(text);
        } else {
          result = "";
        }

        return true;
      }

      return extract_proto_value(sub_msg, path_parts, depth + 1, result);
    }

    if (!is_leaf) {
      return false;
    }

    return extract_scalar([&](int cpp_type) -> VariantType {
      using google::protobuf::FieldDescriptor;

      switch (cpp_type) {
        case FieldDescriptor::CPPTYPE_INT32:
          return static_cast<int64_t>(reflection->GetRepeatedInt32(message, field, array_index));
        case FieldDescriptor::CPPTYPE_INT64:
          return reflection->GetRepeatedInt64(message, field, array_index);
        case FieldDescriptor::CPPTYPE_UINT32:
          return static_cast<int64_t>(reflection->GetRepeatedUInt32(message, field, array_index));
        case FieldDescriptor::CPPTYPE_UINT64:
          return static_cast<int64_t>(reflection->GetRepeatedUInt64(message, field, array_index));
        case FieldDescriptor::CPPTYPE_DOUBLE:
          return reflection->GetRepeatedDouble(message, field, array_index);
        case FieldDescriptor::CPPTYPE_FLOAT:
          return static_cast<double>(reflection->GetRepeatedFloat(message, field, array_index));
        case FieldDescriptor::CPPTYPE_BOOL:
          return static_cast<int64_t>(reflection->GetRepeatedBool(message, field, array_index));
        case FieldDescriptor::CPPTYPE_ENUM:
          return static_cast<int64_t>(reflection->GetRepeatedEnumValue(message, field, array_index));
        case FieldDescriptor::CPPTYPE_STRING:
          if (field->type() == FieldDescriptor::TYPE_BYTES) {
            return vlink::Bytes::from_string(reflection->GetRepeatedString(message, field, array_index));
          }

          return reflection->GetRepeatedString(message, field, array_index);
        default:
          return int64_t{0};
      }
    });
  }

  if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
    const auto& sub_msg = reflection->GetMessage(message, field);

    if (is_leaf) {
      std::string text;
      bool ret = google::protobuf::TextFormat::PrintToString(sub_msg, &text);

      if VLIKELY (ret) {
        result = std::move(text);
      } else {
        result = "";
      }

      return true;
    }

    return extract_proto_value(sub_msg, path_parts, depth + 1, result);
  }

  if (!is_leaf) {
    return false;
  }

  return extract_scalar([&](int cpp_type) -> VariantType {
    using google::protobuf::FieldDescriptor;

    switch (cpp_type) {
      case FieldDescriptor::CPPTYPE_INT32:
        return static_cast<int64_t>(reflection->GetInt32(message, field));
      case FieldDescriptor::CPPTYPE_INT64:
        return reflection->GetInt64(message, field);
      case FieldDescriptor::CPPTYPE_UINT32:
        return static_cast<int64_t>(reflection->GetUInt32(message, field));
      case FieldDescriptor::CPPTYPE_UINT64:
        return static_cast<int64_t>(reflection->GetUInt64(message, field));
      case FieldDescriptor::CPPTYPE_DOUBLE:
        return reflection->GetDouble(message, field);
      case FieldDescriptor::CPPTYPE_FLOAT:
        return static_cast<double>(reflection->GetFloat(message, field));
      case FieldDescriptor::CPPTYPE_BOOL:
        return static_cast<int64_t>(reflection->GetBool(message, field));
      case FieldDescriptor::CPPTYPE_ENUM:
        return static_cast<int64_t>(reflection->GetEnumValue(message, field));
      case FieldDescriptor::CPPTYPE_STRING:
        if (field->type() == FieldDescriptor::TYPE_BYTES) {
          return vlink::Bytes::from_string(reflection->GetString(message, field));
        }

        return reflection->GetString(message, field);
      default:
        return int64_t{0};
    }
  });
}

#ifdef VLINK_HAS_FBS_PARSER

static void import_fbs(std::shared_ptr<flatbuffers::Parser>& parser, const std::string& target_ser,
                       const std::filesystem::path& root_dir, const std::filesystem::path& sub_dir, bool& has_import,
                       int depth = 0) {
  if VUNLIKELY (parser || depth >= 100) {
    return;
  }

  auto target_parser = std::make_shared<flatbuffers::Parser>();

  std::vector<std::filesystem::directory_entry> file_list;

  try {
    for (const auto& entry : std::filesystem::directory_iterator(sub_dir)) {
      file_list.emplace_back(entry);
    }
  } catch (std::filesystem::filesystem_error&) {
    return;
  }

  if VUNLIKELY (file_list.empty() || file_list.size() > 1000) {
    return;
  }

  std::string root_dir_str = root_dir.string();
  std::string sub_dir_str = sub_dir.string();
  const char* include_root_dirs[] = {root_dir_str.c_str(), nullptr};
  const char* include_dirs[] = {root_dir_str.c_str(), sub_dir_str.c_str(), nullptr};

  std::string schema_file;

  for (const auto& file : file_list) {
    try {
      if (file.is_regular_file() && file.path().extension() == ".fbs") {
        if VUNLIKELY (!flatbuffers::LoadFile(file.path().string().c_str(), false, &schema_file)) {
          continue;
        }

        bool ret = (root_dir == sub_dir) ? target_parser->Parse(schema_file.c_str(), include_root_dirs)
                                         : target_parser->Parse(schema_file.c_str(), include_dirs);

        if VUNLIKELY (!ret) {
          continue;
        }

        if VLIKELY (target_parser->LookupStruct(target_ser)) {
          target_parser->SetRootType(target_ser.c_str());
          parser = std::move(target_parser);
          has_import = true;
          return;
        }
      } else if (file.is_directory()) {
        import_fbs(parser, target_ser, root_dir, file.path(), has_import, depth + 1);
      }
    } catch (std::filesystem::filesystem_error&) {
      continue;
    }
  }
}

#endif

static std::string format_zerocopy_header(const vlink::zerocopy::Header& header) {
  std::string s;
  s += "header {\n";
  s += "  frame_id: " + std::string(header.frame_id_view()) + "\n";
  s += "  seq: " + std::to_string(header.seq) + "\n";
  s += "  time_meas: " + vlink::Helpers::format_date(header.time_meas) + "\n";
  s += "  time_pub: " + vlink::Helpers::format_date(header.time_pub) + "\n";
  s += "}\n";
  return s;
}

static std::string format_raw_data(const vlink::zerocopy::RawData& raw_data) {
  std::string s = format_zerocopy_header(raw_data.header);
  s += "size: " + std::to_string(raw_data.size()) + "\n";
  s += "data: {...}\n";
  return s;
}

static std::string format_camera_frame(const vlink::zerocopy::CameraFrame& frame) {
  std::string s = format_zerocopy_header(frame.header);
  s += "channel: " + std::to_string(frame.channel()) + "\n";
  s += "height: " + std::to_string(frame.height()) + "\n";
  s += "width: " + std::to_string(frame.width()) + "\n";
  s += "freq: " + std::to_string(frame.freq()) + "\n";
  s += "format: " + std::string(vlink::NameDetector::get_enum(frame.format())) + "\n";
  s += "stream: " + std::string(vlink::NameDetector::get_enum(frame.stream())) + "\n";
  s += "size: " + std::to_string(frame.size()) + "\n";
  s += "data: {...}\n";
  return s;
}

static std::string format_point_cloud(const vlink::zerocopy::PointCloud& pc) {
  std::string s = format_zerocopy_header(pc.header);
  s += "protocol {\n";
  s += "  size_list: " + pc.get_protocol_size_str() + "\n";
  s += "  name_list: " + pc.get_protocol_name_str() + "\n";
  s += "  type_list: " + pc.get_protocol_type_str() + "\n";
  s += "}\n";
  s += "size: " + std::to_string(pc.size()) + "\n";
  s += "pack_size: " + std::to_string(pc.pack_size()) + "\n";
  return s;
}

static std::string format_occupancy_grid(const vlink::zerocopy::OccupancyGrid& og) {
  std::string s = format_zerocopy_header(og.header);
  s += "map_id: " + std::string(og.map_id()) + "\n";
  s += "channel: " + std::to_string(og.channel()) + "\n";
  s += "freq: " + std::to_string(og.freq()) + "\n";
  s += "width: " + std::to_string(og.width()) + "\n";
  s += "height: " + std::to_string(og.height()) + "\n";
  s += "resolution: " + std::to_string(og.resolution()) + "\n";
  s += "origin_x: " + std::to_string(og.origin_x()) + "\n";
  s += "origin_y: " + std::to_string(og.origin_y()) + "\n";
  s += "origin_z: " + std::to_string(og.origin_z()) + "\n";
  s += "origin_yaw: " + std::to_string(og.origin_yaw()) + "\n";
  s += "cell_type: " + std::string(vlink::NameDetector::get_enum(og.cell_type())) + "\n";
  s += "default_value: " + std::to_string(og.default_value()) + "\n";
  s += "value_min: " + std::to_string(og.value_min()) + "\n";
  s += "value_max: " + std::to_string(og.value_max()) + "\n";
  s += "occupied_threshold: " + std::to_string(og.occupied_threshold()) + "\n";
  s += "free_threshold: " + std::to_string(og.free_threshold()) + "\n";
  s += "valid_cell_count: " + std::to_string(og.valid_cell_count()) + "\n";
  s += "update_time_ns: " + std::to_string(og.update_time_ns()) + "\n";
  s += "size: " + std::to_string(og.size()) + "\n";
  s += "data: {...}\n";
  return s;
}

static std::string format_tensor(const vlink::zerocopy::Tensor& tensor) {
  std::string s = format_zerocopy_header(tensor.header);
  s += "name: " + std::string(tensor.name()) + "\n";
  s += "model_id: " + std::string(tensor.model_id()) + "\n";
  s += "layout: " + std::string(tensor.layout()) + "\n";
  s += "dtype: " + std::string(vlink::NameDetector::get_enum(tensor.dtype())) + "\n";
  s += "device: " + std::string(vlink::NameDetector::get_enum(tensor.device())) + "\n";
  s += "rank: " + std::to_string(static_cast<uint32_t>(tensor.rank())) + "\n";
  s += "num_elements: " + std::to_string(tensor.num_elements()) + "\n";
  s += "element_size: " + std::to_string(static_cast<uint32_t>(tensor.element_size())) + "\n";
  s += "batch_size: " + std::to_string(tensor.batch_size()) + "\n";

  std::string shape_str = "[";

  for (uint8_t i = 0; i < tensor.rank(); ++i) {
    if (i > 0) {
      shape_str += ", ";
    }

    shape_str += std::to_string(tensor.shape_at(i));
  }

  shape_str += "]";
  s += "shape: " + shape_str + "\n";
  s += "quant_scale: " + std::to_string(tensor.quant_scale()) + "\n";
  s += "quant_zero_point: " + std::to_string(tensor.quant_zero_point()) + "\n";
  s += "channel: " + std::to_string(tensor.channel()) + "\n";
  s += "freq: " + std::to_string(tensor.freq()) + "\n";
  s += "update_time_ns: " + std::to_string(tensor.update_time_ns()) + "\n";
  s += "size: " + std::to_string(tensor.size()) + "\n";
  s += "data: {...}\n";
  return s;
}

static std::string format_object_array(const vlink::zerocopy::ObjectArray& arr) {
  std::string s = format_zerocopy_header(arr.header);
  s += "source_id: " + std::string(arr.source_id()) + "\n";
  s += "channel: " + std::to_string(arr.channel()) + "\n";
  s += "freq: " + std::to_string(arr.freq()) + "\n";
  s += "count: " + std::to_string(arr.count()) + "\n";
  s += "pack_size: " + std::to_string(arr.pack_size()) + "\n";
  s += "update_time_ns: " + std::to_string(arr.update_time_ns()) + "\n";
  s += "data: {...}\n";
  return s;
}

static std::string format_audio_frame(const vlink::zerocopy::AudioFrame& frame) {
  std::string s = format_zerocopy_header(frame.header);
  s += "channel: " + std::to_string(frame.channel()) + "\n";
  s += "freq: " + std::to_string(frame.freq()) + "\n";
  s += "sample_rate: " + std::to_string(frame.sample_rate()) + "\n";
  s += "num_samples: " + std::to_string(frame.num_samples()) + "\n";
  s += "num_channels: " + std::to_string(static_cast<uint32_t>(frame.num_channels())) + "\n";
  s += "bit_depth: " + std::to_string(static_cast<uint32_t>(frame.bit_depth())) + "\n";
  s += "bitrate: " + std::to_string(frame.bitrate()) + "\n";
  s += "format: " + std::string(vlink::NameDetector::get_enum(frame.format())) + "\n";
  s += "layout: " + std::string(vlink::NameDetector::get_enum(frame.layout())) + "\n";
  s += "codec: " + std::string(frame.codec()) + "\n";
  s += "language: " + std::string(frame.language()) + "\n";
  s += "duration_ns: " + std::to_string(frame.duration_ns()) + "\n";
  s += "update_time_ns: " + std::to_string(frame.update_time_ns()) + "\n";
  s += "size: " + std::to_string(frame.size()) + "\n";
  s += "data: {...}\n";
  return s;
}

static bool extract_zerocopy_header_value(const vlink::zerocopy::Header& header, const std::string& field,
                                          VariantType& result) {
  if (field == "header.seq") {
    result = static_cast<int64_t>(header.seq);
    return true;
  }

  if (field == "header.time_meas") {
    result = static_cast<int64_t>(header.time_meas);
    return true;
  }

  if (field == "header.time_pub") {
    result = static_cast<int64_t>(header.time_pub);
    return true;
  }

  if (field == "header.frame_id") {
    result = std::string(header.frame_id_view());
    return true;
  }

  return false;
}

static bool extract_zerocopy_value(const std::string& ser, const vlink::Bytes& bytes, const std::string& field,
                                   VariantType& result) {
  if (ser.find("RawData") != std::string::npos) {
    vlink::zerocopy::RawData raw_data;

    if VUNLIKELY (!vlink::Serializer::convert(bytes, raw_data)) {
      return false;
    }

    if (extract_zerocopy_header_value(raw_data.header, field, result)) {
      return true;
    }

    if (field == "size") {
      result = static_cast<int64_t>(raw_data.size());
      return true;
    }

    if (field == "data") {
      result = std::string("<binary:" + std::to_string(raw_data.size()) + ">");
      return true;
    }

    return false;
  }

  if (ser.find("CameraFrame") != std::string::npos) {
    vlink::zerocopy::CameraFrame frame;

    if VUNLIKELY (!vlink::Serializer::convert(bytes, frame)) {
      return false;
    }

    if (extract_zerocopy_header_value(frame.header, field, result)) {
      return true;
    }

    if (field == "width") {
      result = static_cast<int64_t>(frame.width());
      return true;
    }

    if (field == "height") {
      result = static_cast<int64_t>(frame.height());
      return true;
    }

    if (field == "channel") {
      result = static_cast<int64_t>(frame.channel());
      return true;
    }

    if (field == "freq") {
      result = static_cast<int64_t>(frame.freq());
      return true;
    }

    if (field == "format") {
      result = static_cast<int64_t>(frame.format());
      return true;
    }

    if (field == "stream") {
      result = static_cast<int64_t>(frame.stream());
      return true;
    }

    if (field == "size") {
      result = static_cast<int64_t>(frame.size());
      return true;
    }

    if (field == "data") {
      result = std::string("<binary:" + std::to_string(frame.size()) + ">");
      return true;
    }

    return false;
  }

  if (ser.find("PointCloud") != std::string::npos) {
    vlink::zerocopy::PointCloud pc;

    if VUNLIKELY (!vlink::Serializer::convert(bytes, pc)) {
      return false;
    }

    if (extract_zerocopy_header_value(pc.header, field, result)) {
      return true;
    }

    if (field == "size") {
      result = static_cast<int64_t>(pc.size());
      return true;
    }

    if (field == "pack_size") {
      result = static_cast<int64_t>(pc.pack_size());
      return true;
    }

    if (vlink::Helpers::has_startwith(field, "data")) {
      auto pos_left = field.find('[');
      auto pos_right = field.find(']');
      int array_pos = -1;

      if (pos_left != std::string::npos && pos_right != std::string::npos && pos_right > pos_left) {
        std::from_chars(field.data() + pos_left + 1, field.data() + pos_right, array_pos);
      }

      if (array_pos < 0 || static_cast<size_t>(array_pos) >= pc.size()) {
        return false;
      }

      std::string value_name = (pos_right + 2 < field.size()) ? field.substr(pos_right + 2) : "";

      if (value_name.empty()) {
        return false;
      }

      vlink::zerocopy::PointCloud::KeyList key_list;
      auto key_map = pc.get_key_map(&key_list);

      for (const auto& key : key_list) {
        if (key.name != value_name) {
          continue;
        }

        if (key.type != vlink::zerocopy::PointCloud::kUnknownType) {
          switch (key.type) {
            case vlink::zerocopy::PointCloud::kFloatType:
              result = static_cast<double>(pc.get_value<float>(array_pos, key_map, key.name));
              return true;
            case vlink::zerocopy::PointCloud::kDoubleType:
              result = pc.get_value<double>(array_pos, key_map, key.name);
              return true;
            case vlink::zerocopy::PointCloud::kInt8Type:
              result = static_cast<int64_t>(pc.get_value<int8_t>(array_pos, key_map, key.name));
              return true;
            case vlink::zerocopy::PointCloud::kUint8Type:
            case vlink::zerocopy::PointCloud::kBoolType:
              result = static_cast<int64_t>(pc.get_value<uint8_t>(array_pos, key_map, key.name));
              return true;
            case vlink::zerocopy::PointCloud::kInt16Type:
              result = static_cast<int64_t>(pc.get_value<int16_t>(array_pos, key_map, key.name));
              return true;
            case vlink::zerocopy::PointCloud::kUint16Type:
              result = static_cast<int64_t>(pc.get_value<uint16_t>(array_pos, key_map, key.name));
              return true;
            case vlink::zerocopy::PointCloud::kInt32Type:
              result = static_cast<int64_t>(pc.get_value<int32_t>(array_pos, key_map, key.name));
              return true;
            case vlink::zerocopy::PointCloud::kUint32Type:
              result = static_cast<int64_t>(pc.get_value<uint32_t>(array_pos, key_map, key.name));
              return true;
            case vlink::zerocopy::PointCloud::kInt64Type:
              result = pc.get_value<int64_t>(array_pos, key_map, key.name);
              return true;
            case vlink::zerocopy::PointCloud::kUint64Type:
              result = static_cast<int64_t>(pc.get_value<uint64_t>(array_pos, key_map, key.name));
              return true;
            default:
              return false;
          }
        } else {
          if (key.size == 1) {
            result = static_cast<int64_t>(pc.get_value<uint8_t>(array_pos, key_map, key.name));
          } else if (key.size == 2) {
            result = static_cast<int64_t>(pc.get_value<int16_t>(array_pos, key_map, key.name));
          } else if (key.size == 4) {
            result = static_cast<double>(pc.get_value<float>(array_pos, key_map, key.name));
          } else if (key.size == 8) {
            result = pc.get_value<double>(array_pos, key_map, key.name);
          } else {
            return false;
          }

          return true;
        }
      }
    }

    return false;
  }

  if (ser.find("OccupancyGrid") != std::string::npos) {
    vlink::zerocopy::OccupancyGrid og;

    if VUNLIKELY (!vlink::Serializer::convert(bytes, og)) {
      return false;
    }

    if (extract_zerocopy_header_value(og.header, field, result)) {
      return true;
    }

    if (field == "map_id") {
      result = std::string(og.map_id());
      return true;
    }

    if (field == "channel") {
      result = static_cast<int64_t>(og.channel());
      return true;
    }

    if (field == "freq") {
      result = static_cast<int64_t>(og.freq());
      return true;
    }

    if (field == "width") {
      result = static_cast<int64_t>(og.width());
      return true;
    }

    if (field == "height") {
      result = static_cast<int64_t>(og.height());
      return true;
    }

    if (field == "resolution") {
      result = static_cast<double>(og.resolution());
      return true;
    }

    if (field == "origin_x") {
      result = static_cast<double>(og.origin_x());
      return true;
    }

    if (field == "origin_y") {
      result = static_cast<double>(og.origin_y());
      return true;
    }

    if (field == "origin_z") {
      result = static_cast<double>(og.origin_z());
      return true;
    }

    if (field == "origin_yaw") {
      result = static_cast<double>(og.origin_yaw());
      return true;
    }

    if (field == "cell_type") {
      result = static_cast<int64_t>(og.cell_type());
      return true;
    }

    if (field == "default_value") {
      result = static_cast<int64_t>(og.default_value());
      return true;
    }

    if (field == "value_min") {
      result = static_cast<double>(og.value_min());
      return true;
    }

    if (field == "value_max") {
      result = static_cast<double>(og.value_max());
      return true;
    }

    if (field == "occupied_threshold") {
      result = static_cast<double>(og.occupied_threshold());
      return true;
    }

    if (field == "free_threshold") {
      result = static_cast<double>(og.free_threshold());
      return true;
    }

    if (field == "valid_cell_count") {
      result = static_cast<int64_t>(og.valid_cell_count());
      return true;
    }

    if (field == "update_time_ns") {
      result = static_cast<int64_t>(og.update_time_ns());
      return true;
    }

    if (field == "size") {
      result = static_cast<int64_t>(og.size());
      return true;
    }

    if (field == "data") {
      result = std::string("<binary:" + std::to_string(og.size()) + ">");
      return true;
    }

    return false;
  }

  if (ser.find("Tensor") != std::string::npos) {
    vlink::zerocopy::Tensor tensor;

    if VUNLIKELY (!vlink::Serializer::convert(bytes, tensor)) {
      return false;
    }

    if (extract_zerocopy_header_value(tensor.header, field, result)) {
      return true;
    }

    if (field == "name") {
      result = std::string(tensor.name());
      return true;
    }

    if (field == "model_id") {
      result = std::string(tensor.model_id());
      return true;
    }

    if (field == "layout") {
      result = std::string(tensor.layout());
      return true;
    }

    if (field == "dtype") {
      result = static_cast<int64_t>(tensor.dtype());
      return true;
    }

    if (field == "device") {
      result = static_cast<int64_t>(tensor.device());
      return true;
    }

    if (field == "rank") {
      result = static_cast<int64_t>(tensor.rank());
      return true;
    }

    if (field == "num_elements") {
      result = static_cast<int64_t>(tensor.num_elements());
      return true;
    }

    if (field == "element_size") {
      result = static_cast<int64_t>(tensor.element_size());
      return true;
    }

    if (field == "batch_size") {
      result = static_cast<int64_t>(tensor.batch_size());
      return true;
    }

    if (field == "quant_scale") {
      result = static_cast<double>(tensor.quant_scale());
      return true;
    }

    if (field == "quant_zero_point") {
      result = static_cast<int64_t>(tensor.quant_zero_point());
      return true;
    }

    if (field == "channel") {
      result = static_cast<int64_t>(tensor.channel());
      return true;
    }

    if (field == "freq") {
      result = static_cast<int64_t>(tensor.freq());
      return true;
    }

    if (field == "update_time_ns") {
      result = static_cast<int64_t>(tensor.update_time_ns());
      return true;
    }

    if (field == "size") {
      result = static_cast<int64_t>(tensor.size());
      return true;
    }

    if (field == "data") {
      result = std::string("<binary:" + std::to_string(tensor.size()) + ">");
      return true;
    }

    if (field == "shape") {
      std::string shape_str = "[";

      for (uint8_t i = 0; i < tensor.rank(); ++i) {
        if (i > 0) {
          shape_str += ", ";
        }

        shape_str += std::to_string(tensor.shape_at(i));
      }

      shape_str += "]";
      result = shape_str;
      return true;
    }

    if (vlink::Helpers::has_startwith(field, "shape[")) {
      auto pos_left = field.find('[');
      auto pos_right = field.find(']');
      int dim_pos = -1;

      if (pos_left != std::string::npos && pos_right != std::string::npos && pos_right > pos_left) {
        std::from_chars(field.data() + pos_left + 1, field.data() + pos_right, dim_pos);
      }

      if (dim_pos < 0 || dim_pos >= tensor.rank()) {
        return false;
      }

      result = static_cast<int64_t>(tensor.shape_at(static_cast<uint8_t>(dim_pos)));
      return true;
    }

    return false;
  }

  if (ser.find("ObjectArray") != std::string::npos) {
    vlink::zerocopy::ObjectArray arr;

    if VUNLIKELY (!vlink::Serializer::convert(bytes, arr)) {
      return false;
    }

    if (extract_zerocopy_header_value(arr.header, field, result)) {
      return true;
    }

    if (field == "source_id") {
      result = std::string(arr.source_id());
      return true;
    }

    if (field == "channel") {
      result = static_cast<int64_t>(arr.channel());
      return true;
    }

    if (field == "freq") {
      result = static_cast<int64_t>(arr.freq());
      return true;
    }

    if (field == "count") {
      result = static_cast<int64_t>(arr.count());
      return true;
    }

    if (field == "pack_size") {
      result = static_cast<int64_t>(arr.pack_size());
      return true;
    }

    if (field == "update_time_ns") {
      result = static_cast<int64_t>(arr.update_time_ns());
      return true;
    }

    if (field == "data") {
      result = std::string("<objects:" + std::to_string(arr.count()) + ">");
      return true;
    }

    return false;
  }

  if (ser.find("AudioFrame") != std::string::npos) {
    vlink::zerocopy::AudioFrame frame;

    if VUNLIKELY (!vlink::Serializer::convert(bytes, frame)) {
      return false;
    }

    if (extract_zerocopy_header_value(frame.header, field, result)) {
      return true;
    }

    if (field == "channel") {
      result = static_cast<int64_t>(frame.channel());
      return true;
    }

    if (field == "freq") {
      result = static_cast<int64_t>(frame.freq());
      return true;
    }

    if (field == "sample_rate") {
      result = static_cast<int64_t>(frame.sample_rate());
      return true;
    }

    if (field == "num_samples") {
      result = static_cast<int64_t>(frame.num_samples());
      return true;
    }

    if (field == "num_channels") {
      result = static_cast<int64_t>(frame.num_channels());
      return true;
    }

    if (field == "bit_depth") {
      result = static_cast<int64_t>(frame.bit_depth());
      return true;
    }

    if (field == "bitrate") {
      result = static_cast<int64_t>(frame.bitrate());
      return true;
    }

    if (field == "format") {
      result = static_cast<int64_t>(frame.format());
      return true;
    }

    if (field == "layout") {
      result = static_cast<int64_t>(frame.layout());
      return true;
    }

    if (field == "codec") {
      result = std::string(frame.codec());
      return true;
    }

    if (field == "language") {
      result = std::string(frame.language());
      return true;
    }

    if (field == "duration_ns") {
      result = static_cast<int64_t>(frame.duration_ns());
      return true;
    }

    if (field == "update_time_ns") {
      result = static_cast<int64_t>(frame.update_time_ns());
      return true;
    }

    if (field == "size") {
      result = static_cast<int64_t>(frame.size());
      return true;
    }

    if (field == "data") {
      result = std::string("<binary:" + std::to_string(frame.size()) + ">");
      return true;
    }

    return false;
  }

  return false;
}

static std::string format_zerocopy_message(const std::string& ser, const vlink::Bytes& bytes) {
  if (ser.find("RawData") != std::string::npos) {
    vlink::zerocopy::RawData raw_data;

    if (vlink::Serializer::convert(bytes, raw_data)) {
      return format_raw_data(raw_data);
    }
  } else if (ser.find("CameraFrame") != std::string::npos) {
    vlink::zerocopy::CameraFrame frame;

    if (vlink::Serializer::convert(bytes, frame)) {
      return format_camera_frame(frame);
    }
  } else if (ser.find("PointCloud") != std::string::npos) {
    vlink::zerocopy::PointCloud pc;

    if (vlink::Serializer::convert(bytes, pc)) {
      return format_point_cloud(pc);
    }
  } else if (ser.find("OccupancyGrid") != std::string::npos) {
    vlink::zerocopy::OccupancyGrid og;

    if (vlink::Serializer::convert(bytes, og)) {
      return format_occupancy_grid(og);
    }
  } else if (ser.find("Tensor") != std::string::npos) {
    vlink::zerocopy::Tensor tensor;

    if (vlink::Serializer::convert(bytes, tensor)) {
      return format_tensor(tensor);
    }
  } else if (ser.find("ObjectArray") != std::string::npos) {
    vlink::zerocopy::ObjectArray arr;

    if (vlink::Serializer::convert(bytes, arr)) {
      return format_object_array(arr);
    }
  } else if (ser.find("AudioFrame") != std::string::npos) {
    vlink::zerocopy::AudioFrame frame;

    if (vlink::Serializer::convert(bytes, frame)) {
      return format_audio_frame(frame);
    }
  }

  return "<unsupported zerocopy type>";
}

static vlink::Bytes extract_zerocopy_binary(const std::string& ser, const vlink::Bytes& bytes,
                                            const std::string& field) {
  if (field == "data") {
    if (ser.find("RawData") != std::string::npos) {
      vlink::zerocopy::RawData raw_data;

      if (vlink::Serializer::convert(bytes, raw_data)) {
        return vlink::Bytes::shallow_copy(const_cast<uint8_t*>(raw_data.data()), raw_data.size());
      }
    } else if (ser.find("CameraFrame") != std::string::npos) {
      vlink::zerocopy::CameraFrame frame;

      if (vlink::Serializer::convert(bytes, frame)) {
        return vlink::Bytes::shallow_copy(const_cast<uint8_t*>(frame.data()), frame.size());
      }
    } else if (ser.find("PointCloud") != std::string::npos) {
      vlink::zerocopy::PointCloud pc;

      if (vlink::Serializer::convert(bytes, pc)) {
        // NOLINTNEXTLINE(readability-redundant-casting)
        auto payload_size = static_cast<size_t>(pc.size()) * static_cast<size_t>(pc.pack_size());
        return vlink::Bytes::shallow_copy(const_cast<uint8_t*>(pc.get_internal_data()), payload_size);
      }
    } else if (ser.find("OccupancyGrid") != std::string::npos) {
      vlink::zerocopy::OccupancyGrid og;

      if (vlink::Serializer::convert(bytes, og)) {
        return vlink::Bytes::shallow_copy(const_cast<uint8_t*>(og.data()), og.size());
      }
    } else if (ser.find("Tensor") != std::string::npos) {
      vlink::zerocopy::Tensor tensor;

      if (vlink::Serializer::convert(bytes, tensor)) {
        return vlink::Bytes::shallow_copy(const_cast<uint8_t*>(tensor.data()), tensor.size());
      }
    } else if (ser.find("ObjectArray") != std::string::npos) {
      vlink::zerocopy::ObjectArray arr;

      if (vlink::Serializer::convert(bytes, arr)) {
        // NOLINTNEXTLINE(readability-redundant-casting)
        auto payload_size = static_cast<size_t>(arr.count()) * static_cast<size_t>(arr.pack_size());
        return vlink::Bytes::shallow_copy(const_cast<uint8_t*>(arr.data()), payload_size);
      }
    } else if (ser.find("AudioFrame") != std::string::npos) {
      vlink::zerocopy::AudioFrame frame;

      if (vlink::Serializer::convert(bytes, frame)) {
        return vlink::Bytes::shallow_copy(const_cast<uint8_t*>(frame.data()), frame.size());
      }
    }
  }

  return {};
}

static bool write_pcd_file(const std::string& file_path, const vlink::zerocopy::PointCloud& pc) {
  if VUNLIKELY (!pc.is_valid()) {
    return false;
  }

  vlink::zerocopy::PointCloud::KeyList key_list;
  (void)pc.get_key_map(&key_list);

  if VUNLIKELY (key_list.empty()) {
    return false;
  }

  std::ofstream file(file_path, std::ios::binary);

  if VUNLIKELY (!file.is_open()) {
    return false;
  }

  std::string fields_str;
  std::string size_str;
  std::string type_str;
  std::string count_str;

  for (size_t i = 0; i < key_list.size(); ++i) {
    const auto& key = key_list[i];

    if (i > 0) {
      fields_str += " ";
      size_str += " ";
      type_str += " ";
      count_str += " ";
    }

    fields_str += key.name;
    size_str += std::to_string(key.size);
    count_str += "1";

    switch (key.type) {
      case vlink::zerocopy::PointCloud::kFloatType:
      case vlink::zerocopy::PointCloud::kDoubleType:
        type_str += "F";
        break;
      case vlink::zerocopy::PointCloud::kInt8Type:
      case vlink::zerocopy::PointCloud::kInt16Type:
      case vlink::zerocopy::PointCloud::kInt32Type:
      case vlink::zerocopy::PointCloud::kInt64Type:
        type_str += "I";
        break;
      case vlink::zerocopy::PointCloud::kUint8Type:
      case vlink::zerocopy::PointCloud::kUint16Type:
      case vlink::zerocopy::PointCloud::kUint32Type:
      case vlink::zerocopy::PointCloud::kUint64Type:
      case vlink::zerocopy::PointCloud::kBoolType:
        type_str += "U";
        break;
      default:
        if (key.size == 4 || key.size == 8) {
          type_str += "F";
        } else {
          type_str += "U";
        }

        break;
    }
  }

  file << "# .PCD v0.7 - Point Cloud Data file format\n";
  file << "VERSION 0.7\n";
  file << "FIELDS " << fields_str << "\n";
  file << "SIZE " << size_str << "\n";
  file << "TYPE " << type_str << "\n";
  file << "COUNT " << count_str << "\n";
  file << "WIDTH " << pc.size() << "\n";
  file << "HEIGHT 1\n";
  file << "VIEWPOINT 0 0 0 1 0 0 0\n";
  file << "POINTS " << pc.size() << "\n";
  file << "DATA binary\n";

  const auto* data = pc.get_internal_data();

  if (data) {
    file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(pc.size() * pc.pack_size()));
  }

  file.close();
  return true;
}

static int start_bag_play(const std::string& bag_file) {
  bag_config.begin_time = begin_time;
  bag_config.end_time = end_time;
  bag_config.times = 1;
  bag_config.rate = 1.0;
  bag_config.skip_blank = false;
  bag_config.force_delay = 0;
  bag_config.auto_pause = false;
  bag_config.auto_quit = true;

  try {
    bag_player = vlink::BagReader::create(bag_file, true);
  } catch (vlink::Exception::RuntimeError& e) {
    std::cerr << e.what() << std::endl;
    has_quit = true;
    return -1;
  }

  bag_player->register_output_callback(
      [](int64_t timestamp, const std::string& url, vlink::ActionType action_type, const vlink::Bytes& data) {
        (void)action_type;

        if VUNLIKELY (!callback_has_set || !dump_callback) {
          return;
        }

        dump_callback(timestamp, url, bag_player->get_ser_type(url), bag_player->get_schema_type(url), data);
      });

  return 0;
}

static int stop_bag_play() {
  if VUNLIKELY (!bag_player) {
    return -1;
  }

  has_quit = true;
  bag_player.reset();
  return 0;
}

static int start_viewer(bool native_mode) {
  try {
    auto filter_type = native_mode ? vlink::DiscoveryViewer::kFilterNative : vlink::DiscoveryViewer::kFilterNone;
    discovery_viewer = std::make_shared<vlink::DiscoveryViewer>(filter_type);
  } catch (vlink::Exception::RuntimeError& e) {
    std::cerr << e.what() << std::endl;
    has_quit = true;
    return -1;
  }

  discovery_viewer->async_run();

  vlink::Utils::register_terminate_signal([](int) {
    if VUNLIKELY (has_quit) {
      return;
    }

    has_quit = true;
    discovery_viewer->quit(true);
  });

  std::cout << "Information Collecting, Please Wait...";
  std::cout.flush();
  discovery_viewer->wait_for_quit(1000);
  std::cout << "\033[2K\r";
  std::cout.flush();

  main_elapsed_timer.restart();

  auto sync_subs = [native_mode](const std::vector<vlink::DiscoveryViewer::Info>& info_list) {
    std::unordered_set<std::string> current_urls;
    current_urls.reserve(info_list.size());

    {
      std::lock_guard lock(sub_urls_mtx);

      for (const auto& info : info_list) {
        if ((info.type & vlink::kPublisher) == 0 && (info.type & vlink::kSetter) == 0) {
          continue;
        }

        current_urls.emplace(info.url);

        auto sub_iter = sub_urls.find(info.url);

        if (sub_iter != sub_urls.end()) {
          const auto current_schema_type = sub_iter->second->get_schema_type();
          const auto expected_schema_type =
              info.schema_type == vlink::SchemaType::kUnknown ? current_schema_type : info.schema_type;

          if VUNLIKELY (sub_iter->second->get_ser_type() != info.ser_type ||
                        current_schema_type != expected_schema_type) {
            sub_iter->second->set_ser_type(info.ser_type, info.schema_type);
          }

          continue;
        }

        std::shared_ptr<RawSub> raw_sub;

        try {
          raw_sub = std::make_shared<RawSub>(info.url, vlink::InitType::kWithoutInit);
        } catch (vlink::Exception::RuntimeError&) {
          continue;
        }

        if (native_mode) {
          raw_sub->set_property("dds.ip", "127.0.0.1");
        }

        raw_sub->set_ser_type(info.ser_type, info.schema_type);
        raw_sub->init();
        std::weak_ptr<RawSub> weak_sub = raw_sub;
        raw_sub->listen([weak_sub, url = info.url](const vlink::Bytes& bytes) {
          if VUNLIKELY (has_quit || !callback_has_set || !dump_callback) {
            return;
          }

          auto sub = weak_sub.lock();

          if VUNLIKELY (!sub) {
            return;
          }

          dump_callback(main_elapsed_timer.get(), url, sub->get_ser_type(), sub->get_schema_type(), bytes);
        });
        sub_urls.emplace(info.url, std::move(raw_sub));
      }

      for (auto iter = sub_urls.begin(); iter != sub_urls.end();) {
        if VUNLIKELY (current_urls.count(iter->first) == 0) {
          iter = sub_urls.erase(iter);
        } else {
          ++iter;
        }
      }
    }
  };

  sync_subs(discovery_viewer->get_info_list());
  discovery_viewer->register_callback(
      [sync_subs = std::move(sync_subs)](const std::vector<vlink::DiscoveryViewer::Info>& info_list) {
        sync_subs(info_list);
      });

  return 0;
}

static int stop_viewer() {
  if VUNLIKELY (!discovery_viewer) {
    return -1;
  }

  has_quit = true;
  discovery_viewer.reset();
  return 0;
}

static void print_console_header(int64_t timestamp, int64_t seq) {
  std::cout << "\033[36m--- [" << seq << "] ";
  std::cout << std::fixed << std::setprecision(6) << timestamp / 1000000.0;
  std::cout.unsetf(std::ios::fixed);
  std::cout << "s ---\033[0m" << std::endl;
}

static void print_console_fields(int64_t timestamp, int64_t seq, const std::vector<VariantType>& values,
                                 const std::vector<double>& expr_results = {}) {
  std::cout << "\033[36m[" << seq << "] ";
  std::cout << std::fixed << std::setprecision(6) << timestamp / 1000000.0;
  std::cout.unsetf(std::ios::fixed);
  std::cout << "s\033[0m ";

  for (size_t i = 0; i < field_specs.size() && i < values.size(); ++i) {
    if (i > 0) {
      std::cout << ", ";
    }

    std::cout << "\033[33m" << field_specs[i] << "\033[0m=";
    std::cout << variant_to_string(values[i]);
  }

  for (size_t i = 0; i < expr_results.size() && i < expr_strings.size(); ++i) {
    std::cout << ", \033[35mexpr(" << expr_strings[i] << ")\033[0m=";
    std::ostringstream oss;
    oss << std::setprecision(12) << expr_results[i];
    std::cout << oss.str();
  }

  std::cout << std::endl;
}

// NOLINTNEXTLINE(google-readability-function-size)
static int start_dump(const std::string& target_url, const std::string& out_dir, const std::string& base_name,
                      const std::string& proto_dir, [[maybe_unused]] const std::string& fbs_dir,
                      const std::string& dump_type_suffix) {
  std::shared_ptr<google::protobuf::compiler::DiskSourceTree> source_tree;
  std::shared_ptr<google::protobuf::compiler::Importer> importer;
  std::shared_ptr<google::protobuf::DynamicMessageFactory> factory;
  const google::protobuf::DescriptorPool* des_pool = nullptr;
  std::unique_ptr<google::protobuf::Message> root_msg;
  std::string cached_ser;

  try {
#ifdef _WIN32
    auto filesys_out_dir = std::filesystem::path(vlink::Helpers::string_to_wstring(out_dir));
    auto filesys_proto_dir = std::filesystem::path(vlink::Helpers::string_to_wstring(proto_dir));
#else
    auto filesys_out_dir = std::filesystem::path(out_dir);
    auto filesys_proto_dir = std::filesystem::path(proto_dir);
#endif

    if (!std::filesystem::exists(filesys_out_dir)) {
      std::filesystem::create_directories(filesys_out_dir);
    }

    if (std::filesystem::exists(filesys_proto_dir) && std::filesystem::is_directory(filesys_proto_dir)) {
      factory = std::make_shared<google::protobuf::DynamicMessageFactory>();
      source_tree = std::make_shared<google::protobuf::compiler::DiskSourceTree>();
      importer = std::make_shared<google::protobuf::compiler::Importer>(source_tree.get(), nullptr);

#ifdef _WIN32
      source_tree->MapPath("", vlink::Helpers::path_to_string(filesys_proto_dir));
#else
      source_tree->MapPath("", filesys_proto_dir.string());
#endif

      bool has_import = false;
      import_protos(importer.get(), filesys_proto_dir, filesys_proto_dir, has_import);

      if (has_import) {
        des_pool = importer->pool();
      }
    }
  } catch (std::filesystem::filesystem_error& e) {
    std::cerr << e.what() << std::endl;
    has_quit = true;
    return -1;
  }

#ifdef VLINK_HAS_FBS_PARSER
  std::shared_ptr<flatbuffers::Parser> fbs_parser;
#endif
  bool warned_flatbuffers_fields = false;

  std::string out_file_name;

  try {
    std::filesystem::path out_file = std::filesystem::path(out_dir) / std::filesystem::path(base_name);
#ifdef _WIN32
    out_file_name = vlink::Helpers::path_to_string(out_file);
#else
    out_file_name = out_file.string();
#endif
  } catch (std::filesystem::filesystem_error& e) {
    std::cerr << e.what() << std::endl;
    has_quit = true;
    return -1;
  }

  int64_t dump_seq = 0;

  auto reset_cached_decoder = [&root_msg
#ifdef VLINK_HAS_FBS_PARSER
                               ,
                               &fbs_parser
#endif
  ]() {
    root_msg.reset();
#ifdef VLINK_HAS_FBS_PARSER
    fbs_parser.reset();
#endif
  };

  auto sync_cached_ser = [&cached_ser, &reset_cached_decoder](const std::string& ser) {
    if (cached_ser == ser) {
      return;
    }

    cached_ser = ser;
    reset_cached_decoder();
  };

  auto ensure_proto_message = [&des_pool, &factory, &root_msg,
                               &sync_cached_ser](const std::string& ser) -> google::protobuf::Message* {
    if (des_pool == nullptr || factory == nullptr) {
      return nullptr;
    }

    sync_cached_ser(ser);

    if (!root_msg) {
      const auto* descriptor = des_pool->FindMessageTypeByName(ser);

      if (descriptor == nullptr) {
        return nullptr;
      }

      const auto* prototype = factory->GetPrototype(descriptor);

      if (prototype == nullptr) {
        return nullptr;
      }

      root_msg.reset(prototype->New());
    }

    return root_msg.get();
  };

#ifdef VLINK_HAS_FBS_PARSER
  auto ensure_fbs_parser = [&fbs_dir, &fbs_parser, &sync_cached_ser](const std::string& ser) -> flatbuffers::Parser* {
    if (fbs_dir.empty()) {
      return nullptr;
    }

    sync_cached_ser(ser);

    if (!fbs_parser) {
      bool has_import = false;

      try {
        auto fbs_path = std::filesystem::path(fbs_dir);
        import_fbs(fbs_parser, ser, fbs_path, fbs_path, has_import);
      } catch (std::filesystem::filesystem_error&) {
      }
    }

    return fbs_parser.get();
  };
#endif

  auto warn_flatbuffers_fields = [&warned_flatbuffers_fields](std::string_view mode) {
    if (warned_flatbuffers_fields) {
      return;
    }

    warned_flatbuffers_fields = true;
    std::cerr << "Warning: FlatBuffers field extraction is not supported in dump " << mode
              << "; use explicit protobuf fields or raw/console output." << std::endl;
  };

  dump_callback = [target_url, &dump_seq, &out_file_name, &root_msg, &ensure_proto_message,
#ifdef VLINK_HAS_FBS_PARSER
                   &ensure_fbs_parser,
#endif
                   &warn_flatbuffers_fields, &dump_type_suffix](int64_t timestamp, const std::string& url,
                                                                const std::string& ser, vlink::SchemaType schema_type,
                                                                const vlink::Bytes& bytes) {
    if VLIKELY (target_url != url) {
      return;
    }

    if VUNLIKELY (!check_rate_limit()) {
      if VUNLIKELY (max_count > 0 && output_count >= max_count) {
        if (dump_for_bag) {
          bag_player->stop();
        } else {
          discovery_viewer->quit(true);
        }
      }

      return;
    }

    data_has_changed = true;

    const auto resolved_schema_type = vlink::SchemaData::resolve_type(schema_type, ser);
    const bool is_zerocopy = resolved_schema_type == vlink::SchemaType::kZeroCopy;

    auto print_raw_summary = [&bytes, &ser, resolved_schema_type]() {
      const auto schema_label = vlink::SchemaData::convert_type(resolved_schema_type);
      std::cout << "<raw " << bytes.size() << " bytes, ser=" << ser
                << ", schema=" << (schema_label.empty() ? std::string_view{"unknown"} : schema_label) << ">"
                << std::endl;
    };

    if (dump_type == DumpType::kConsole) {
      if (field_specs.empty()) {
        print_console_header(timestamp, output_count);

        if (is_zerocopy) {
          std::cout << format_zerocopy_message(ser, bytes) << std::endl;
        } else if (resolved_schema_type == vlink::SchemaType::kProtobuf) {
          auto* proto_message = ensure_proto_message(ser);

          if (proto_message != nullptr && proto_message->ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) {
            std::string text;
            bool ret = google::protobuf::TextFormat::PrintToString(*proto_message, &text);

            if VLIKELY (ret) {
              std::cout << text << std::endl;
            }
          } else {
            print_raw_summary();
          }
        } else if (resolved_schema_type == vlink::SchemaType::kFlatbuffers) {
#ifdef VLINK_HAS_FBS_PARSER
          auto* parser = ensure_fbs_parser(ser);

          if (parser != nullptr) {
            std::string text;
            const auto* error_chars = flatbuffers::custom::GenText(*parser, bytes.data(), &text);

            if VLIKELY (error_chars == nullptr) {
              std::cout << text << std::endl;
            } else {
              print_raw_summary();
            }
          } else
#endif
          {
            print_raw_summary();
          }
        } else {
          print_raw_summary();
        }
      } else {
        bool proto_parsed = false;

        if (resolved_schema_type == vlink::SchemaType::kProtobuf) {
          auto* proto_message = ensure_proto_message(ser);
          proto_parsed =
              proto_message != nullptr && proto_message->ParseFromArray(bytes.data(), static_cast<int>(bytes.size()));
        } else if (resolved_schema_type == vlink::SchemaType::kFlatbuffers) {
          warn_flatbuffers_fields("console mode");
        }

        std::vector<VariantType> values;
        values.reserve(field_specs.size());

        for (size_t i = 0; i < field_specs.size(); ++i) {
          VariantType val;
          bool found = false;

          if (is_zerocopy) {
            found = extract_zerocopy_value(ser, bytes, field_specs[i], val);
          } else if (proto_parsed) {
            found = extract_proto_value(*root_msg, field_paths[i], 0, val);
          }

          values.emplace_back(found ? std::move(val) : VariantType{std::string{"N/A"}});
        }

        std::vector<double> expr_results;
#ifdef VLINK_HAS_EXPRTK

        if (!expr_contexts.empty()) {
          expr_results = evaluate_expressions(values);
        }
#endif
        print_console_fields(timestamp, output_count, values, expr_results);
      }

      return;
    }

    if (dump_type == DumpType::kBin) {
      ++dump_seq;
      std::ofstream file(out_file_name + "." + std::to_string(dump_seq) + ".bin", std::ios::binary);

      if VLIKELY (file.is_open()) {
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      }

      return;
    }

    if (dump_type == DumpType::kPcd) {
      if (!is_zerocopy || ser.find("PointCloud") == std::string::npos) {
        return;
      }

      vlink::zerocopy::PointCloud pc;

      if VUNLIKELY (!vlink::Serializer::convert(bytes, pc)) {
        return;
      }

      ++dump_seq;
      std::string pcd_path = out_file_name + "." + std::to_string(dump_seq) + ".pcd";

      if (write_pcd_file(pcd_path, pc)) {
        if (!quiet_flag && detail_flag) {
          std::cout << "PCD: " << pcd_path << " (" << pc.size() << " points)" << std::endl;
        }
      }

      return;
    }

    if (dump_type == DumpType::kJpg || dump_type == DumpType::kH264 || dump_type == DumpType::kH265 ||
        dump_type == DumpType::kRaw) {
      vlink::Bytes out_bytes;
      std::string field_to_extract = field_specs.empty() ? "data" : field_specs[0];

      if (is_zerocopy) {
        out_bytes = extract_zerocopy_binary(ser, bytes, field_to_extract);
      } else if (resolved_schema_type == vlink::SchemaType::kProtobuf) {
        auto* proto_message = ensure_proto_message(ser);

        if (proto_message != nullptr && proto_message->ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) {
          VariantType val;
          auto path = vlink::Helpers::get_split_string(field_to_extract, '.');

          if (extract_proto_value(*proto_message, path, 0, val) && std::holds_alternative<vlink::Bytes>(val)) {
            out_bytes = std::get<vlink::Bytes>(std::move(val));
          }
        }
      } else if (resolved_schema_type == vlink::SchemaType::kFlatbuffers) {
        warn_flatbuffers_fields("binary mode");
      }

      if VLIKELY (!out_bytes.empty()) {
        ++dump_seq;
        std::ofstream file(out_file_name + "." + std::to_string(dump_seq) + "." + dump_type_suffix, std::ios::binary);

        if VLIKELY (file.is_open()) {
          file.write(reinterpret_cast<const char*>(out_bytes.data()), static_cast<std::streamsize>(out_bytes.size()));
        }
      }

      return;
    }

    if (dump_type == DumpType::kCsv || dump_type == DumpType::kJson) {
      bool proto_parsed = false;

      if (resolved_schema_type == vlink::SchemaType::kProtobuf) {
        auto* proto_message = ensure_proto_message(ser);
        proto_parsed =
            proto_message != nullptr && proto_message->ParseFromArray(bytes.data(), static_cast<int>(bytes.size()));
      } else if (resolved_schema_type == vlink::SchemaType::kFlatbuffers) {
        warn_flatbuffers_fields("csv/json mode");
      }

      DumpRecord record;
      record.timestamp = timestamp;
      record.values.reserve(field_specs.size());

      for (size_t i = 0; i < field_specs.size(); ++i) {
        VariantType val;
        bool found = false;

        if (is_zerocopy) {
          found = extract_zerocopy_value(ser, bytes, field_specs[i], val);
        } else if (proto_parsed) {
          found = extract_proto_value(*root_msg, field_paths[i], 0, val);
        }

        record.values.emplace_back(found ? std::move(val) : VariantType{std::string{"N/A"}});
      }

#ifdef VLINK_HAS_EXPRTK

      if (!expr_contexts.empty()) {
        record.expr_results = evaluate_expressions(record.values);
      }
#endif

      if (!quiet_flag && detail_flag) {
        std::cout << "timestamp: " << std::fixed << std::setprecision(6) << timestamp / 1000000.0;
        std::cout.unsetf(std::ios::fixed);

        for (size_t i = 0; i < field_specs.size() && i < record.values.size(); ++i) {
          std::cout << ", " << field_specs[i] << "=" << variant_to_string(record.values[i]);
        }

        for (size_t i = 0; i < record.expr_results.size() && i < expr_strings.size(); ++i) {
          std::cout << ", expr(" << expr_strings[i] << ")=" << std::setprecision(12) << record.expr_results[i];
        }

        std::cout << std::endl;
      }

      {
        std::lock_guard lock(cache_mtx);
        cache_buffer.emplace_back(std::move(record));
      }

      return;
    }
  };

  callback_has_set = true;

  if (dump_for_bag) {
    start_print();
    bag_player->play(bag_config);
    bag_player->run();
    has_quit = true;
    stop_print();

    if (!quiet_flag) {
      std::cout << "\033[2K\r" << (is_broken ? "Break." : "Done.") << std::endl;
    }
  } else {
    start_print();

    auto quit_function = [](int) {
      if (has_quit) {
        return;
      }

      has_quit = true;
      discovery_viewer->quit(true);
      is_broken = true;
    };

    vlink::Utils::start_detect_keyboard([&quit_function](const std::string& key) {
      if (key == "q" || key == "esc") {
        quit_function(0);
      }
    });

    vlink::Utils::register_terminate_signal(quit_function);

    discovery_viewer->wait_for_quit();
    has_quit = true;
    stop_print();
    vlink::Utils::stop_detect_keyboard();

    if (!quiet_flag) {
      std::cout << "\033[2K\r"
                << "Done." << std::endl;
    }
  }

  if (dump_type == DumpType::kCsv) {
    std::ofstream file(out_file_name + "." + dump_type_suffix);

    if (file.is_open()) {
      file << "timestamp";

      for (const auto& spec : field_specs) {
        file << "," << spec;
      }

      for (const auto& ex : expr_strings) {
        file << ",expr(" << ex << ")";
      }

      file << "\n";

      std::lock_guard lock(cache_mtx);

      for (const auto& rec : cache_buffer) {
        file << std::fixed << std::setprecision(6) << rec.timestamp / 1000000.0;
        file.unsetf(std::ios::fixed);

        for (const auto& val : rec.values) {
          file << ",";

          if (std::holds_alternative<int64_t>(val)) {
            file << std::get<int64_t>(val);
          } else if (std::holds_alternative<double>(val)) {
            file << std::setprecision(12) << std::get<double>(val);
          } else if (std::holds_alternative<std::string>(val)) {
            const auto& s = std::get<std::string>(val);

            if (s.find(',') != std::string::npos || s.find('"') != std::string::npos) {
              file << "\"";

              for (char c : s) {
                if (c == '"') {
                  file << "\"\"";
                } else {
                  file << c;
                }
              }

              file << "\"";
            } else {
              file << s;
            }
          } else if (std::holds_alternative<vlink::Bytes>(val)) {
            file << "<bytes:" << std::get<vlink::Bytes>(val).size() << ">";
          }
        }

        for (const auto& er : rec.expr_results) {
          file << "," << std::setprecision(12) << er;
        }

        file << "\n";
      }

      file.close();
    }

    if (!quiet_flag) {
      std::cout << "Saved " << cache_buffer.size() << " records to " << out_file_name << "." << dump_type_suffix
                << std::endl;
    }
  } else if (dump_type == DumpType::kJson) {
    std::ofstream file(out_file_name + "." + dump_type_suffix);

    if (file.is_open()) {
      nlohmann::ordered_json root_json;

      std::lock_guard lock(cache_mtx);

      for (const auto& rec : cache_buffer) {
        nlohmann::ordered_json json;
        json["timestamp"] = rec.timestamp / 1000000.0;

        for (size_t i = 0; i < field_specs.size() && i < rec.values.size(); ++i) {
          const auto& val = rec.values[i];

          if (std::holds_alternative<int64_t>(val)) {
            json[field_specs[i]] = std::get<int64_t>(val);
          } else if (std::holds_alternative<double>(val)) {
            json[field_specs[i]] = std::get<double>(val);
          } else if (std::holds_alternative<std::string>(val)) {
            json[field_specs[i]] = std::get<std::string>(val);
          } else if (std::holds_alternative<vlink::Bytes>(val)) {
            json[field_specs[i]] = "<bytes:" + std::to_string(std::get<vlink::Bytes>(val).size()) + ">";
          }
        }

        for (size_t i = 0; i < rec.expr_results.size() && i < expr_strings.size(); ++i) {
          json["expr(" + expr_strings[i] + ")"] = rec.expr_results[i];
        }

        root_json.emplace_back(json);
      }

      file << root_json.dump(2);
      file.close();
    }

    if (!quiet_flag) {
      std::cout << "Saved " << cache_buffer.size() << " records to " << out_file_name << "." << dump_type_suffix
                << std::endl;
    }
  }

  if (!quiet_flag && (dump_type == DumpType::kBin || dump_type == DumpType::kJpg || dump_type == DumpType::kH264 ||
                      dump_type == DumpType::kH265 || dump_type == DumpType::kRaw || dump_type == DumpType::kPcd)) {
    std::cout << "Saved " << dump_seq << " files to " << out_dir << std::endl;
  }

  has_quit = true;

  {
    std::lock_guard lock(sub_urls_mtx);
    sub_urls.clear();
  }

  {
    std::lock_guard lock(cache_mtx);
    cache_buffer.clear();
  }

  return 0;
}

#endif

int main(int argc, char* argv[]) {
  vlink::Utils::set_console_utf8_output();

#ifdef VLINK_HAS_PROTOBUF_COMPILER
  vlink::Logger::set_file_level(vlink::Logger::kOff);
  vlink::Logger::init("vlink-dump");

  vlink::Utils::unset_env("VLINK_BAG_PATH");

  argparse::ArgumentParser program("vlink-dump", VLINK_VERSION, argparse::default_arguments::all);

  program.add_description(
      "Versatile data extraction and export tool for VLink topics.\n"
      "Note: You may need to add multicast/broadcast [" +
      vlink::DiscoveryViewer::get_listen_address() + "]");

  program.add_argument("url").help("Target topic URL").required();

  program.add_argument("-t", "--type")
      .help("Output type: console/text, csv, json, bin, jpg/jpeg, h264, h265, raw, pcd")
      .default_value(std::string("csv"));

  program.add_argument("-c", "--condition")
      .help("Field(s) to extract, comma-separated (e.g. 'header.seq,pose.x,pose.y')")
      .default_value(std::string());

  program.add_argument("-o", "--out_dir").help("Output directory").default_value(std::string("./"));

  program.add_argument("-m", "--base_name").help("Output file base name").default_value(std::string("output"));

  program.add_argument("-f", "--bag_file")
      .help("Bag file path (.vdb / .vdbx / .vcap / .vcapx)")
      .default_value(std::string());

  program.add_argument("-b", "--begin_time")
      .help("Playback start time in seconds (bag mode)")
      .scan<'g', double>()
      .default_value(0.0);

  program.add_argument("-e", "--end_time")
      .help("Playback end time in seconds (bag mode)")
      .scan<'g', double>()
      .default_value(0.0);

  program.add_argument("-n", "--count")
      .help("Maximum number of samples (0 = unlimited)")
      .scan<'d', int>()
      .default_value(0);

  program.add_argument("--hz").help("Maximum output rate in Hz (0 = unlimited)").scan<'g', double>().default_value(0.0);

  program.add_argument("--native").help("Use native/loopback mode").default_value(false).implicit_value(true);

  program.add_argument("-d", "--proto_dir").help("Protobuf .proto directory").default_value(std::string());

  program.add_argument("--fbs_dir").help("FlatBuffers .fbs directory (for FBS types)").default_value(std::string());

  program.add_argument("-q", "--quiet").help("Suppress progress output").default_value(false).implicit_value(true);

  program.add_argument("-l", "--detail")
      .help("Print each value in real-time")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("-x", "--expression")
      .help(
          "Math expression(s) on extracted fields, semicolon-separated.\n"
          "Variables are field names with dots replaced by underscores.\n"
          "E.g. -c 'pose.x,pose.y' -x 'sqrt(pose_x*pose_x + pose_y*pose_y)'\n"
          "Multiple: -x 'pose_x+pose_y;pose_x-pose_y'\n"
          "Functions: -x 'min(pose_x, pose_y);max(pose_x, pose_y)'")
      .default_value(std::string());

  std::string example_str = "Examples:\n";
  example_str += "  vlink-dump dds://test -c 'header.seq' -t csv -f /tmp/bag.vdb\n";
  example_str += "  vlink-dump dds://test -c 'pose.x,pose.y,pose.z' -t csv --hz 10\n";
  example_str += "  vlink-dump dds://test -t console -n 5\n";
  example_str += "  vlink-dump dds://camera -c 'data' -t jpg -f /tmp/bag.vcap\n";
  example_str += "  vlink-dump dds://test -t bin -o /tmp/raw_output\n";
  example_str += "  vlink-dump dds://test -c 'header.seq' -t json --hz 1\n";
  example_str +=
      "  vlink-dump dds://test -c 'pose.x,pose.y' -x 'sqrt(pose_x*pose_x+pose_y*pose_y);pose_x-pose_y' -t csv\n";
  example_str += "  vlink-dump dds://lidar -t pcd -f /tmp/bag.vdb";
  program.add_epilog(example_str);

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    std::cerr << program << std::endl;
    return 1;
  }

  const auto& target_url = program.get<std::string>("url");
  const auto& type = program.get<std::string>("-t");
  auto condition = program.get<std::string>("-c");
  const auto& out_dir = program.get<std::string>("-o");
  const auto& base_name = program.get<std::string>("-m");

  auto bag_file = program.get<std::string>("-f");

#ifdef _WIN32
  try {
    bag_file = vlink::Helpers::path_to_string(std::filesystem::path(bag_file));
  } catch (std::filesystem::filesystem_error&) {
  }
#endif

  dump_for_bag = !bag_file.empty();

  if (dump_for_bag) {
    if VUNLIKELY (program.is_used("--native")) {
      std::cerr << "native [--native] is only valid without bag_file [-f]" << std::endl;
      return -1;
    }
  } else {
    if VUNLIKELY (program.is_used("-b")) {
      std::cerr << "begin_time [-b] is only valid with bag_file [-f]" << std::endl;
      return -1;
    }

    if VUNLIKELY (program.is_used("-e")) {
      std::cerr << "end_time [-e] is only valid with bag_file [-f]" << std::endl;
      return -1;
    }
  }

  begin_time = static_cast<int64_t>(program.get<double>("-b") * 1000);
  end_time = static_cast<int64_t>(program.get<double>("-e") * 1000);
  max_count = program.get<int>("-n");
  max_hz = program.get<double>("--hz");

  auto native_mode = program.is_used("--native");

  auto proto_dir = program.get<std::string>("-d");
  auto fbs_dir = program.get<std::string>("--fbs_dir");

  if (proto_dir.empty()) {
    proto_dir = vlink::Utils::get_env("VLINK_PROTO_DIR");
  }

  if (fbs_dir.empty()) {
    fbs_dir = vlink::Utils::get_env("VLINK_FBS_DIR");
  }

#ifdef _WIN32

  if (program.is_used("-d")) {
    try {
      proto_dir = vlink::Helpers::path_to_string(std::filesystem::path(proto_dir));
    } catch (std::filesystem::filesystem_error&) {
    }
  }

  std::replace(proto_dir.begin(), proto_dir.end(), '\\', '/');

  if (program.is_used("--fbs_dir")) {
    try {
      fbs_dir = vlink::Helpers::path_to_string(std::filesystem::path(fbs_dir));
    } catch (std::filesystem::filesystem_error&) {
    }
  }

  std::replace(fbs_dir.begin(), fbs_dir.end(), '\\', '/');
#endif

  if (!proto_dir.empty() && proto_dir.back() == '/') {
    proto_dir.pop_back();
  }

  if (!fbs_dir.empty() && fbs_dir.back() == '/') {
    fbs_dir.pop_back();
  }

  std::string dump_type_suffix;

  if (type == "console" || type == "text") {
    dump_type = DumpType::kConsole;
  } else if (type == "csv") {
    dump_type = DumpType::kCsv;
  } else if (type == "json") {
    dump_type = DumpType::kJson;
  } else if (type == "bin") {
    dump_type = DumpType::kBin;
  } else if (type == "jpg" || type == "jpeg") {
    dump_type = DumpType::kJpg;
    dump_type_suffix = "jpg";
  } else if (type == "h264") {
    dump_type = DumpType::kH264;
    dump_type_suffix = "h264";
  } else if (type == "h265") {
    dump_type = DumpType::kH265;
    dump_type_suffix = "h265";
  } else if (type == "raw") {
    dump_type = DumpType::kRaw;
    dump_type_suffix = "raw";
  } else if (type == "pcd") {
    dump_type = DumpType::kPcd;
    dump_type_suffix = "pcd";
  } else {
    std::cerr << "Unknown type: " << type << std::endl;
    std::cerr << "Supported: console/text, csv, json, bin, jpg/jpeg, h264, h265, raw, pcd" << std::endl;
    return -1;
  }

  if (dump_type_suffix.empty()) {
    dump_type_suffix = type;
  }

  if (!condition.empty()) {
    field_specs = vlink::Helpers::get_split_string(condition, ',');

    for (auto& spec : field_specs) {
      while (!spec.empty() && spec.front() == ' ') {
        spec.erase(spec.begin());
      }

      while (!spec.empty() && spec.back() == ' ') {
        spec.pop_back();
      }

      field_paths.emplace_back(vlink::Helpers::get_split_string(spec, '.'));
    }
  }

  auto expr_raw = program.get<std::string>("-x");

  if (!expr_raw.empty()) {
    expr_strings = vlink::Helpers::get_split_string(expr_raw, ';');

    for (auto& ex : expr_strings) {
      while (!ex.empty() && ex.front() == ' ') {
        ex.erase(ex.begin());
      }

      while (!ex.empty() && ex.back() == ' ') {
        ex.pop_back();
      }
    }
  }

  if VUNLIKELY (target_url.empty()) {
    std::cerr << "[url] cannot be empty." << std::endl;
    return -1;
  }

  if ((dump_type == DumpType::kCsv || dump_type == DumpType::kJson) && field_specs.empty()) {
    std::cerr << "CSV/JSON mode requires -c to specify fields." << std::endl;
    std::cerr << "Example: -c 'header.seq,pose.x,pose.y'" << std::endl;
    return -1;
  }

  if (!expr_strings.empty() && field_specs.empty()) {
    std::cerr << "Expression (-x) requires -c to specify fields as variables." << std::endl;
    return -1;
  }

#ifdef VLINK_HAS_EXPRTK

  if (!compile_expressions()) {
    return -1;
  }
#else

  if (!expr_strings.empty()) {
    std::cerr << "Expression support requires exprtk library." << std::endl;
    return -1;
  }
#endif

  if (proto_dir.empty() && fbs_dir.empty()) {
    std::cerr << "Warning: No proto_dir or fbs_dir set, only zerocopy types will work." << std::endl;
    std::cerr << "Set via [-d] / [--fbs_dir] or env VLINK_PROTO_DIR / VLINK_FBS_DIR" << std::endl;
  }

  if VUNLIKELY (std::abs(begin_time) > 0 && std::abs(end_time) > 0 && begin_time >= end_time) {
    std::cerr << "Invalid begin_time and end_time [-b] [-e]" << std::endl;
    return -1;
  }

  quiet_flag = program.is_used("-q");
  detail_flag = program.is_used("-l");

  int ret = 0;

  if (dump_for_bag) {
    ret = start_bag_play(bag_file);

    if (ret != 0) {
      return ret;
    }
  } else {
    ret = start_viewer(native_mode);

    if (ret != 0) {
      return ret;
    }
  }

  ret = start_dump(target_url, out_dir, base_name, proto_dir, fbs_dir, dump_type_suffix);

  if (dump_for_bag) {
    stop_bag_play();
  } else {
    stop_viewer();
  }

  return ret;
#else
  (void)argc;
  (void)argv;

  std::cerr << "The lower version of protobuf is not supported. Please change to a higher version of protobuf."
            << std::endl;
  return -1;
#endif
}
