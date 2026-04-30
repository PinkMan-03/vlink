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

// PointCloud zero-copy container example
// Demonstrates create, push_value_v3f, get_value_v3f, schema inspection,
// serialize/deserialize, and pub/sub on dds://.

#include <vlink/base/logger.h>
#include <vlink/vlink.h>
#include <vlink/zerocopy/point_cloud.h>

#include <cstring>
#include <iostream>
#include <thread>

#include "cloud_builder.h"

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ======== Section 1: Create a Point Cloud with float XYZ ========
  {
    std::cout << "\n[1] Create Point Cloud with float XYZ" << std::endl;

    vlink::zerocopy::PointCloud pc;

    // create<float,float,float>(max_points, {"x","y","z"})
    bool ok = pc.create<float, float, float>(1000, {"x", "y", "z"});
    std::cout << "  create<float,float,float>(1000) = " << std::boolalpha << ok << std::endl;
    std::cout << "  pack_size:  " << pc.pack_size() << " bytes/point" << std::endl;
    std::cout << "  size:       " << pc.size() << " points" << std::endl;
    std::cout << "  is_owner:   " << std::boolalpha << pc.is_owner() << std::endl;

    // Push points
    pc.push_value(1.0F, 2.0F, 3.0F);
    pc.push_value(4.0F, 5.0F, 6.0F);
    pc.push_value(7.0F, 8.0F, 9.0F);
    std::cout << "  After push: " << pc.size() << " points" << std::endl;

    // Read back using get_value_v3f
    float x;
    float y;
    float z;
    pc.get_value_v3f(x, y, z, 0);
    std::cout << "  Point[0]:   (" << x << ", " << y << ", " << z << ")" << std::endl;

    // Read back using Vector3f
    auto v = pc.get_value_v3f(1);
    std::cout << "  Point[1]:   (" << v.x << ", " << v.y << ", " << v.z << ")" << std::endl;
  }

  // ======== Section 2: Float XYZ with Extra Fields ========
  {
    std::cout << "\n[2] Float XYZ + Intensity (create_v3f)" << std::endl;

    vlink::zerocopy::PointCloud pc;

    // create_v3f<float>(n, extra_keys) automatically prepends x,y,z
    pc.create_v3f<float>(500, {"intensity"});
    std::cout << "  pack_size: " << pc.pack_size() << " bytes/point (3*4 + 1*4 = 16)" << std::endl;

    // Push points with v3f helper
    pc.push_value_v3f(1.0F, 2.0F, 3.0F, 0.8F);
    pc.push_value_v3f(4.0F, 5.0F, 6.0F, 0.5F);
    pc.push_value_v3f(10.0F, 20.0F, 30.0F, 1.0F);
    std::cout << "  size:      " << pc.size() << " points" << std::endl;

    // Read XYZ via get_value_v3f
    float x;
    float y;
    float z;
    pc.get_value_v3f(x, y, z, 2);
    std::cout << "  Point[2]:  (" << x << ", " << y << ", " << z << ")" << std::endl;

    // Read extra field using KeyMap
    auto key_map = pc.get_key_map();
    auto intensity = pc.get_value<float>(2, key_map, "intensity");
    std::cout << "  Intensity: " << intensity << std::endl;
  }

  // ======== Section 3: Schema Inspection ========
  {
    std::cout << "\n[3] Schema Inspection" << std::endl;

    vlink::zerocopy::PointCloud pc;
    pc.create<float, float, float, uint8_t>(100, {"x", "y", "z", "ring"});

    std::cout << "  Protocol names:    " << pc.get_protocol_name_str() << std::endl;
    std::cout << "  Protocol sizes:    " << pc.get_protocol_size_str() << std::endl;
    std::cout << "  Protocol types:    " << pc.get_protocol_type_str() << std::endl;
    std::cout << "  Protocol size_num: 0x" << std::hex << pc.get_protocol_size_num() << std::dec << std::endl;
    std::cout << "  Protocol type_num: 0x" << std::hex << pc.get_protocol_type_num() << std::dec << std::endl;

    // Get detailed key list
    vlink::zerocopy::PointCloud::KeyList key_list;
    auto key_map = pc.get_key_map(&key_list);
    std::cout << "  Fields:" << std::endl;
    for (const auto& key : key_list) {
      std::cout << "    name=\"" << key.name << "\" type=" << static_cast<int>(key.type)
                << " size=" << static_cast<int>(key.size) << " offset=" << key_map[key.name] << std::endl;
    }
  }

  // ======== Section 4: Serialize / Deserialize ========
  {
    std::cout << "\n[4] Serialize / Deserialize" << std::endl;

    vlink::zerocopy::PointCloud original;
    original.create_v3f(1000);
    original.header.seq = 99;
    std::strncpy(original.header.frame_id, "lidar_0", sizeof(original.header.frame_id) - 1);
    original.header.frame_id[sizeof(original.header.frame_id) - 1] = '\0';

    for (int i = 0; i < 100; ++i) {
      auto fi = static_cast<float>(i);
      original.push_value_v3f(fi, fi * 2, fi * 3);
    }

    // Serialize
    vlink::Bytes wire;
    original >> wire;
    std::cout << "  Points:          " << original.size() << std::endl;
    std::cout << "  Serialized size: " << wire.size() << " bytes" << std::endl;
    std::cout << "  get_serialized_size: " << original.get_serialized_size() << std::endl;

    // Validate
    bool valid = vlink::zerocopy::PointCloud::check_valid(wire);
    std::cout << "  check_valid:     " << std::boolalpha << valid << std::endl;

    // Deserialize (zero-copy: data borrows wire memory)
    vlink::zerocopy::PointCloud restored;
    restored << wire;
    std::cout << "  Restored size:   " << restored.size() << " points" << std::endl;
    std::cout << "  Restored seq:    " << restored.header.seq << std::endl;
    std::cout << "  is_owner:        " << std::boolalpha << restored.is_owner() << std::endl;

    auto v = restored.get_value_v3f(50);
    std::cout << "  Point[50]:       (" << v.x << ", " << v.y << ", " << v.z << ")" << std::endl;
  }

  // ======== Section 5: set_value and resize ========
  {
    std::cout << "\n[5] set_value and resize" << std::endl;

    vlink::zerocopy::PointCloud pc;
    pc.create<float, float, float>(10, {"x", "y", "z"});

    // resize sets the logical size so set_value can overwrite records
    pc.resize(5);
    std::cout << "  After resize(5): size=" << pc.size() << std::endl;

    // Overwrite specific points
    pc.set_value_v3f(0, 100.0F, 200.0F, 300.0F);
    pc.set_value_v3f(4, 400.0F, 500.0F, 600.0F);

    auto v0 = pc.get_value_v3f(0);
    auto v4 = pc.get_value_v3f(4);
    std::cout << "  Point[0]: (" << v0.x << ", " << v0.y << ", " << v0.z << ")" << std::endl;
    std::cout << "  Point[4]: (" << v4.x << ", " << v4.y << ", " << v4.z << ")" << std::endl;
  }

  // ======== Section 6: Pub/Sub with PointCloud ========
  {
    std::cout << "\n[6] Pub/Sub with PointCloud" << std::endl;

    vlink::MessageLoop loop;
    loop.set_name("pc_loop");
    loop.async_run();

    int received = 0;
    vlink::Subscriber<vlink::zerocopy::PointCloud> sub("dds://zerocopy/pointcloud");
    sub.attach(&loop);
    sub.listen([&received](const vlink::zerocopy::PointCloud& pc) {
      received++;
      std::cout << "  [Sub] seq=" << pc.header.seq << " points=" << pc.size() << " pack=" << pc.pack_size()
                << std::endl;
    });

    vlink::Publisher<vlink::zerocopy::PointCloud> pub("dds://zerocopy/pointcloud");
    pub.wait_for_subscribers();

    for (uint32_t i = 1; i <= 3; ++i) {
      vlink::zerocopy::PointCloud pc;
      pc.create_v3f(100);
      pc.header.seq = i;
      for (int j = 0; j < 10; ++j) {
        auto fj = static_cast<float>(j);
        pc.push_value_v3f(fj, fj, fj);
      }
      pub.publish(pc);
    }

    loop.wait_for_idle(1000);
    std::cout << "  Total received: " << received << std::endl;

    loop.quit();
    loop.wait_for_quit();
  }

  // ======== Section 7: Double-Precision Variant ========
  {
    std::cout << "\n[7] Double-Precision (create_v3d / get_value_v3d)" << std::endl;

    vlink::zerocopy::PointCloud pc;
    pc.create_v3d(100);

    pc.push_value_v3d(1.111111, 2.222222, 3.333333);
    pc.push_value_v3d(4.444444, 5.555555, 6.666666);

    std::cout << "  pack_size: " << pc.pack_size() << " bytes/point (3*8=24)" << std::endl;

    auto v = pc.get_value_v3d(0);
    std::cout << "  Point[0]:  (" << v.x << ", " << v.y << ", " << v.z << ")" << std::endl;
  }

  VLOG_I("PointCloud example complete.");
  return 0;
}
