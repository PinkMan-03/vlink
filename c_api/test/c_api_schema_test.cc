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

#include <vlink/external/c_api.h>
#include <vlink/vlink.h>

#include <cstdio>

extern "C" int test_schema_type_mapping(void) {
  struct Case final {
    const char* url;
    const char* ser;
    vlink_schema_t schema;
    vlink::SchemaType expected;
  };

  const Case cases[] = {
      {"dds://c_interface/schema_type/protobuf", "demo.proto.PointCloud", VLINK_SCHEMA_PROTOBUF,
       vlink::SchemaType::kProtobuf},
      {"dds://c_interface/schema_type/flatbuffers", "demo.fbs.PointCloud", VLINK_SCHEMA_FLATBUFFERS,
       vlink::SchemaType::kFlatbuffers},
      {"dds://c_interface/schema_type/raw", "text", VLINK_SCHEMA_RAW, vlink::SchemaType::kRaw},
      {"dds://c_interface/schema_type/zerocopy", "vlink::zerocopy::RawData", VLINK_SCHEMA_ZEROCOPY,
       vlink::SchemaType::kZeroCopy},
  };

  int ret = 0;
  std::printf("test_schema_type_mapping...\n");
  std::fflush(stdout);

  for (const auto& test_case : cases) {
    vlink_publisher_handle_t pub_handle{};
    const vlink_schema_info_t schema_info = {test_case.ser, test_case.schema};

    ret += vlink_create_publisher(test_case.url, &schema_info, &pub_handle);

    auto* publisher = static_cast<vlink::Publisher<vlink::Bytes>*>(pub_handle.native_handle);

    if (publisher == nullptr || publisher->get_schema_type() != test_case.expected ||
        publisher->get_ser_type() != test_case.ser) {
      ++ret;
    }

    if (pub_handle.native_handle != nullptr) {
      ret += vlink_destroy_publisher(&pub_handle);
    }
  }

  return ret;
}
