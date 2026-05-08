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

#ifndef EXAMPLES_ZEROCOPY_ZEROCOPY_POINT_CLOUD_CLOUD_BUILDER_H_
#define EXAMPLES_ZEROCOPY_ZEROCOPY_POINT_CLOUD_CLOUD_BUILDER_H_

#include <vlink/zerocopy/point_cloud.h>

#include <cmath>
#include <cstdint>
#include <iostream>

namespace cloud_builder {

// Build a test point cloud with XYZ float data in a grid pattern.
// Useful for unit tests and demos.
inline vlink::zerocopy::PointCloud build_grid_cloud(uint32_t seq, int grid_size, float spacing) {
  vlink::zerocopy::PointCloud pc;
  pc.create_v3f(grid_size * grid_size);
  pc.header.seq = seq;

  for (int row = 0; row < grid_size; ++row) {
    for (int col = 0; col < grid_size; ++col) {
      float x = static_cast<float>(col) * spacing;
      float y = static_cast<float>(row) * spacing;
      float z = 0.0f;
      pc.push_value_v3f(x, y, z);
    }
  }

  return pc;
}

// Build a test point cloud with XYZ + intensity in a spherical pattern.
inline vlink::zerocopy::PointCloud build_sphere_cloud(uint32_t seq, int num_points, float radius) {
  vlink::zerocopy::PointCloud pc;
  pc.create_v3f<float>(num_points, {"intensity"});
  pc.header.seq = seq;

  for (int i = 0; i < num_points; ++i) {
    float theta = static_cast<float>(i) * 2.0f * 3.14159265f / static_cast<float>(num_points);
    float phi = static_cast<float>(i) * 3.14159265f / static_cast<float>(num_points);
    float x = radius * std::sin(phi) * std::cos(theta);
    float y = radius * std::sin(phi) * std::sin(theta);
    float z = radius * std::cos(phi);
    float intensity = static_cast<float>(i) / static_cast<float>(num_points);
    pc.push_value_v3f(x, y, z, intensity);
  }

  return pc;
}

// Print a summary of the point cloud schema and contents.
inline void print_cloud_info(const vlink::zerocopy::PointCloud& pc) {
  std::cout << "  [PointCloud] seq=" << pc.header.seq << " points=" << pc.size() << " pack_size=" << pc.pack_size()
            << " bytes/point"
            << " is_owner=" << std::boolalpha << pc.is_owner() << std::endl;
  std::cout << "    schema: " << pc.get_protocol_name_str() << std::endl;
  std::cout << "    types:  " << pc.get_protocol_type_str() << std::endl;
}

}  // namespace cloud_builder

#endif  // EXAMPLES_ZEROCOPY_ZEROCOPY_POINT_CLOUD_CLOUD_BUILDER_H_
