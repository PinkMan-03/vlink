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

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// POD (Plain Old Data) type serialization example
///
/// Demonstrates publishing and subscribing to trivial standard-layout structs
/// (kStandardType = 13). POD types are byte-copied directly into/from a Bytes buffer
/// of exactly sizeof(T) bytes -- no encoding overhead, no schema, just raw memory copy.
///
/// Requirements for kStandardType:
///   - std::is_trivial_v<T> == true    (trivial default ctor, trivial copy/move/dtor)
///   - std::is_standard_layout_v<T> == true (no virtual functions, single access specifier)
///   - T is not a pointer type

// POD types defined in pod_types.h -- see that file for struct definitions and static_asserts
#include "pod_types.h"

// NOTE: The following types are NOT POD and would fail static_assert:
// struct NotPod {
//   std::string name;         // std::string is not trivial
//   virtual void foo() {}     // virtual function breaks standard-layout
// };

int main() {
  vlink::MessageLoop loop;

  // ======== Compile-time type verification ========
  // POD types are detected as kStandardType
  static_assert(vlink::Serializer::get_type_of<Point2D>() == vlink::Serializer::kStandardType,
                "Point2D must be kStandardType");
  static_assert(vlink::Serializer::get_type_of<SensorReading>() == vlink::Serializer::kStandardType,
                "SensorReading must be kStandardType");
  static_assert(vlink::Serializer::get_type_of<CanFrame>() == vlink::Serializer::kStandardType,
                "CanFrame must be kStandardType");

  static_assert(vlink::Serializer::get_type_of<int>() == vlink::Serializer::kStandardType,
                "int must be kStandardType (trivial + standard-layout)");
  static_assert(vlink::Serializer::get_type_of<double>() == vlink::Serializer::kStandardType,
                "double must be kStandardType (trivial + standard-layout)");

  // Serialized size of a POD type is always sizeof(T)
  Point2D sample{1.0F, 2.0F};
  std::cout << "[Info] sizeof(Point2D) = " << sizeof(Point2D) << std::endl;
  std::cout << "[Info] get_serialized_size(Point2D) = " << vlink::Serializer::get_serialized_size(sample) << std::endl;
  std::cout << "[Info] sizeof(SensorReading) = " << sizeof(SensorReading) << std::endl;
  std::cout << "[Info] sizeof(CanFrame) = " << sizeof(CanFrame) << std::endl;

  // ======== Example 1: Simple Point2D pub/sub ========
  int point_count = 0;
  vlink::Subscriber<Point2D> point_sub("dds://example/pod/point");
  point_sub.attach(&loop);
  point_sub.listen([&point_count](const Point2D& pt) {
    point_count++;
    std::cout << "[Point2D] #" << point_count << " x=" << pt.x << " y=" << pt.y << std::endl;
  });

  vlink::Publisher<Point2D> point_pub("dds://example/pod/point");
  point_pub.attach(&loop);

  // ======== Example 2: SensorReading pub/sub ========
  int sensor_count = 0;
  vlink::Subscriber<SensorReading> sensor_sub("dds://example/pod/sensor");
  sensor_sub.attach(&loop);
  sensor_sub.listen([&sensor_count](const SensorReading& reading) {
    sensor_count++;
    std::cout << "[Sensor] #" << sensor_count << " id=" << reading.sensor_id << " temp=" << reading.temperature
              << " humidity=" << reading.humidity << " status=" << static_cast<int>(reading.status) << std::endl;
  });

  vlink::Publisher<SensorReading> sensor_pub("dds://example/pod/sensor");
  sensor_pub.attach(&loop);

  // ======== Example 3: CanFrame pub/sub ========
  int can_count = 0;
  vlink::Subscriber<CanFrame> can_sub("dds://example/pod/can");
  can_sub.attach(&loop);
  can_sub.listen([&can_count](const CanFrame& frame) {
    can_count++;
    std::cout << "[CAN] #" << can_count << " id=0x" << std::hex << frame.id << std::dec
              << " dlc=" << static_cast<int>(frame.dlc) << " data[0]=0x" << std::hex << static_cast<int>(frame.data[0])
              << std::dec << std::endl;
  });

  vlink::Publisher<CanFrame> can_pub("dds://example/pod/can");
  can_pub.attach(&loop);

  // Publish test data using a one-shot timer
  vlink::Timer timer(&loop, 50, 1);
  timer.start([&]() {
    // Publish Point2D values
    point_pub.publish(Point2D{1.5F, 2.5F});
    point_pub.publish(Point2D{-3.14F, 0.0F});
    point_pub.publish(Point2D{100.0F, 200.0F});

    // Publish a SensorReading
    SensorReading reading{};
    reading.sensor_id = 42;
    reading.temperature = 23.5;
    reading.humidity = 65.2;
    reading.timestamp_us = 1711612800000000LL;
    reading.status = 1;
    sensor_pub.publish(reading);

    // Publish a CAN frame
    CanFrame frame{};
    frame.id = 0x7E0;
    frame.dlc = 4;
    frame.data[0] = 0x02;
    frame.data[1] = 0x10;
    frame.data[2] = 0x01;
    frame.data[3] = 0x00;
    can_pub.publish(frame);
  });

  loop.async_run();
  loop.wait_for_idle(1000);
  loop.quit();
  loop.wait_for_quit();

  // ======== Manual serialization / deserialization verification ========
  std::cout << "\n[Manual Serialize/Deserialize Test]" << std::endl;
  Point2D original{42.0F, -7.5F};
  vlink::Bytes buf;
  vlink::Serializer::serialize(original, buf);
  std::cout << "  Serialized size: " << buf.size() << " bytes" << std::endl;

  Point2D restored{};
  vlink::Serializer::deserialize(buf, restored);
  std::cout << "  Restored: x=" << restored.x << " y=" << restored.y << std::endl;
  std::cout << "  Match: " << std::boolalpha << (original.x == restored.x && original.y == restored.y) << std::endl;

  std::cout << "\n[Summary] Points=" << point_count << " Sensors=" << sensor_count << " CAN=" << can_count << std::endl;

  return 0;
}
