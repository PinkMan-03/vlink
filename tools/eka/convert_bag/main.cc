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

#include <eka-rt/extension/bag_reader.h>
#include <eka-rt/msg/camera_frame.h>
#include <eka-rt/msg/point_cloud.h>
#include <vlink/extension/bag_writer.h>
#include <vlink/vlink.h>
#include <vlink/zerocopy/camera_frame.h>
#include <vlink/zerocopy/point_cloud.h>

int main(int argc, char* argv[]) {
  if (argc != 3) {
    VLOG_E("Args error");
    return -1;
  }

  std::string eka_bag = argv[1];

  std::string vlink_bag = argv[2];

  auto eka_reader = eka::rt::BagReader::create(eka_bag);

  auto vlink_writer = vlink::BagWriter::create(vlink_bag);

  eka_reader->register_output_callback([&vlink_writer, &eka_reader](int64_t timestamp, const std::string& url,
                                                                    eka::rt::ActionType action_type,
                                                                    const eka::rt::Bytes& data) {
    auto ser = eka_reader->get_ser_type(url);

    if (ser == "gpal::zero_copy_types::CameraFrame" || ser == "eka::rt::msg::CameraFrame") {
      eka::rt::msg::CameraFrame eka_frame;
      vlink::zerocopy::CameraFrame vlink_frame;

      vlink::Bytes vlink_data;

      eka_frame << data;

      vlink_frame.header.seq = eka_frame.header.seq;
      vlink_frame.header.time_meas = eka_frame.header.time_meas;
      vlink_frame.header.time_pub = eka_frame.header.time_pub;

      vlink_frame.set_channel(eka_frame.channel);
      vlink_frame.set_format(vlink::zerocopy::CameraFrame::kFormatJpeg);
      vlink_frame.set_freq(eka_frame.freq);
      vlink_frame.set_width(eka_frame.width);
      vlink_frame.set_height(eka_frame.height);

      vlink_frame.shallow_copy(eka_frame.data, eka_frame.size);

      vlink_frame >> vlink_data;

      vlink_writer->push(url, "vlink::zerocopy::CameraFrame", vlink::SchemaType::kZeroCopy,
                         (vlink::ActionType)action_type, vlink_data, &timestamp, true);
    } else if (ser == "gpal::zero_copy_types::PointCloud" || ser == "eka::rt::msg::PointCloud") {
      eka::rt::msg::PointCloud eka_frame;
      vlink::zerocopy::PointCloud vlink_frame;

      vlink::Bytes vlink_data;

      eka_frame << data;

      vlink_frame.header.seq = eka_frame.header.seq;
      vlink_frame.header.time_meas = eka_frame.header.time_meas;
      vlink_frame.header.time_pub = eka_frame.header.time_pub;

      eka::rt::msg::PointCloud::KeyList key_list;
      auto keymap = eka_frame.get_key_map(&key_list);

      bool ret = vlink_frame.create(eka_frame.size(), eka_frame.get_protocol_size_num(),
                                    eka_frame.get_protocol_type_num(), eka_frame.get_protocol_name_str());

      ret = vlink_frame.fill_packed_data(eka_frame.get_internal_data(), eka_frame.size());

      vlink_frame >> vlink_data;

      vlink_writer->push(url, "vlink::zerocopy::PointCloud", vlink::SchemaType::kZeroCopy,
                         (vlink::ActionType)action_type, vlink_data, &timestamp, true);
    } else {
      auto inferred_schema = vlink::SchemaData::infer_ser_type(ser);
      vlink_writer->push(url, ser, inferred_schema, (vlink::ActionType)action_type,
                         vlink::Bytes::shallow_copy(data.data(), data.size()), &timestamp, true);
    }

    MLOG_I("Percent: {:.2f}%", static_cast<double>(timestamp / 1000.0) / eka_reader->get_info().total_duration * 100.0);
  });

  eka_reader->register_finish_callback([&eka_reader](bool is_interrupted) { eka_reader->quit(); });

  eka::rt::BagReader::Config config;
  config.force_delay = 0;

  eka_reader->play(config);

  eka_reader->run();

  return 0;
}
