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

#include <vlink/extension/bag_reader_plugin_interface.h>
#include <vlink/extension/bag_reader_processor.h>

#include <optional>
#include <string>

#include "./ffmpeg_decoder.h"

class PluginDecoder : public vlink::BagReaderPluginInterface {
 public:
  PluginDecoder() {
    FFmpegDecoder::Config ffmpeg_config;
    ffmpeg_config.in_type = FFmpegDecoder::InType::kH264;
    ffmpeg_config.out_type = FFmpegDecoder::OutType::kNV12;
    ffmpeg_config.width = 1920;
    ffmpeg_config.height = 1080;

    decoder_.emplace(ffmpeg_config);

    decoder_->register_handler(
        [this](int channel, int seq, int width, int height, const vlink::Bytes& img_data) { camera_data_ = img_data; });

    vlink::BagReaderProcessor::Config processor_config;
    processor_config.min_cache_time = 1000;
    processor_config.max_cache_size = 1024UL * 1024UL * 1024 * 4;

    processor_.emplace(processor_config);

    processor_->register_output_callback(
        [this](int64_t timestamp, const std::string& url, vlink::ActionType action_type, const vlink::Bytes& data) {
          on_output(timestamp, url, action_type, data);
        });
  }

  ~PluginDecoder() override { processor_.reset(); }

  VersionInfo get_version_info() const override {
    VersionInfo info;

    info.name = "PluginDecoder";
    info.version = "1.0.0";
    info.timestamp = "";
    info.tag = "";
    info.commit_id = "";

    return info;
  }

  bool convert_url_meta(std::string& url, std::string& ser_type, vlink::SchemaType& schema_type) override {
    (void)url;
    (void)ser_type;
    (void)schema_type;

    if (url == "shm://hal/compressed/cam_flb?depth=5") {
      url = "shm://hal/raw/cam_flb?depth=5";
      return true;
    }

    return false;
  }

  void push(int64_t timestamp, const std::string& url, vlink::ActionType action_type,
            const vlink::Bytes& data) override {
    if (!processor_.has_value()) {
      return;
    }

    if (url == "shm://hal/compressed/cam_flb?depth=5") {
      decoder_->post_data(0, 0, data);
      decoder_->wait_for_idle();
      processor_->push(timestamp, url, action_type, camera_data_);
    } else {
      processor_->push(timestamp, url, action_type, data);
    }
  }

  void on_output(int64_t timestamp, const std::string& url, vlink::ActionType action_type, const vlink::Bytes& data) {
    if (output_callback_) {
      if (url == "shm://hal/compressed/cam_flb?depth=5") {
        std::string convert_url = "shm://hal/raw/cam_flb?depth=5";
        output_callback_(timestamp, convert_url, action_type, data);
      } else {
        output_callback_(timestamp, url, action_type, data);
      }
    }
  }

 private:
  std::optional<FFmpegDecoder> decoder_;
  std::optional<vlink::BagReaderProcessor> processor_;

  vlink::Bytes camera_data_;
};

VLINK_PLUGIN_DECLARE(PluginDecoder, 1, 0);
