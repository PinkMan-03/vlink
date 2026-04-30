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

// VLink core communication API
#include <vlink/vlink.h>

#include <cstring>
#include <iostream>
#include <string>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// Custom type serialization example
///
/// Demonstrates using user-defined types with operator>>(Bytes&) and operator<<(const Bytes&)
/// for serialization. These are detected as kCustomType (= 3) in the detection chain.
///
/// Custom types require:
///   - void operator>>(Bytes& out) const   -> serialize (write this object into out)
///   - void operator<<(const Bytes& in)    -> deserialize (read from in into this object)
///
/// This example shows:
///   1. Fixed-length custom type (simple memcpy-like encoding)
///   2. Variable-length custom type (string + binary data with length prefix)
///   3. The serialization flow: publish() -> operator>> -> transport -> operator<< -> callback

// Custom types defined in custom_types.h -- see that file for struct definitions
#include "custom_types.h"

int main() {
  vlink::MessageLoop loop;

  // ======== Vec3 pub/sub ========
  int vec3_count = 0;
  vlink::Subscriber<Vec3> vec3_sub("dds://example/custom/vec3");
  vec3_sub.attach(&loop);
  vec3_sub.listen([&vec3_count](const Vec3& v) {
    vec3_count++;
    std::cout << "[Vec3] #" << vec3_count << " x=" << v.x << " y=" << v.y << " z=" << v.z << std::endl;
  });

  vlink::Publisher<Vec3> vec3_pub("dds://example/custom/vec3");
  vec3_pub.attach(&loop);

  // ======== NamedValue pub/sub ========
  int nv_count = 0;
  vlink::Subscriber<NamedValue> nv_sub("dds://example/custom/named");
  nv_sub.attach(&loop);
  nv_sub.listen([&nv_count](const NamedValue& nv) {
    nv_count++;
    std::cout << "[NamedValue] #" << nv_count << " name=\"" << nv.name << "\""
              << " code=" << nv.code << " value=" << nv.value << std::endl;
  });

  vlink::Publisher<NamedValue> nv_pub("dds://example/custom/named");
  nv_pub.attach(&loop);

  vlink::Timer timer(&loop, 50, 1);
  timer.start([&]() {
    // Publish Vec3 values
    Vec3 v1{1.0F, 2.0F, 3.0F};
    vec3_pub.publish(v1);

    Vec3 v2{-0.5F, 100.0F, -999.0F};
    vec3_pub.publish(v2);

    // Publish NamedValue with variable-length name
    NamedValue nv1;
    nv1.name = "temperature";
    nv1.code = 100;
    nv1.value = 36.6;
    nv_pub.publish(nv1);

    // Publish with empty name (edge case)
    NamedValue nv2;
    nv2.name = "";
    nv2.code = -1;
    nv2.value = 0.0;
    nv_pub.publish(nv2);

    // Publish with long name
    NamedValue nv3;
    nv3.name = "a_very_long_sensor_name_that_exceeds_the_typical_short_string_optimization_threshold";
    nv3.code = 42;
    nv3.value = 3.14159;  // NOLINT(modernize-use-std-numbers)
    nv_pub.publish(nv3);
  });

  loop.async_run();
  loop.wait_for_idle(1000);
  loop.quit();
  loop.wait_for_quit();

  // ======== Serialization flow explanation ========
  std::cout << "\n[Serialization Flow]" << std::endl;
  std::cout << "  publish(msg)" << std::endl;
  std::cout << "    -> Serializer detects kCustomType" << std::endl;
  std::cout << "    -> calls msg.operator>>(Bytes& out)" << std::endl;
  std::cout << "    -> transport delivers Bytes to subscriber" << std::endl;
  std::cout << "    -> Serializer calls msg.operator<<(const Bytes& in)" << std::endl;
  std::cout << "    -> callback receives deserialized msg" << std::endl;

  // Verify round-trip manually
  std::cout << "\n[Manual Round-trip Test]" << std::endl;
  NamedValue original;
  original.name = "test_field";
  original.code = 777;
  original.value = 2.71828;  // NOLINT(modernize-use-std-numbers)

  vlink::Bytes buf;
  vlink::Serializer::serialize(original, buf);
  std::cout << "  Serialized size: " << buf.size() << " bytes" << std::endl;

  NamedValue restored;
  vlink::Serializer::deserialize(buf, restored);
  std::cout << "  name:  \"" << restored.name << "\" (match: " << std::boolalpha << (original.name == restored.name)
            << ")" << std::endl;
  std::cout << "  code:  " << restored.code << " (match: " << (original.code == restored.code) << ")" << std::endl;
  std::cout << "  value: " << restored.value << " (match: " << (original.value == restored.value) << ")" << std::endl;

  std::cout << "\n[Summary] Vec3=" << vec3_count << " NamedValue=" << nv_count << std::endl;

  return 0;
}
