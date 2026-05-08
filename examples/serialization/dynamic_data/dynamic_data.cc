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
// DynamicData type-erased container
#include <vlink/extension/dynamic_data.h>

#include <iostream>
#include <string>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// DynamicData serialization example
///
/// DynamicData is a type-erased container (kDynamicType = 2) that stores any serializable
/// value together with a type-name tag in a single Bytes buffer. This allows:
///   1. Multiple different types to share the SAME topic URL
///   2. Runtime type inspection via get_type()
///   3. Deferred deserialization -- transport the raw data first, deserialize later
///
/// Internal layout: [type name in the first kOffset = 20 bytes] [serialized payload]
/// The literal length passed to load() (including NUL) must be < 20 (static_assert).
///
/// Key API:
///   - load("TypeName", value)  -> serialize value with a type tag
///   - as<T>()                  -> deserialize back to type T
///   - convert(T& out)          -> deserialize into an existing object
///   - get_type()               -> retrieve the embedded type name
///   - operator>> / operator<<  -> wire-format serialization for transport

// A simple POD type to demonstrate DynamicData wrapping
struct Temperature {
  double celsius;
  uint32_t sensor_id;
};

struct Pressure {
  double hpa;
  uint32_t sensor_id;
};

int main() {
  vlink::MessageLoop loop;

  // ======== Compile-time type verification ========
  static_assert(vlink::Serializer::get_type_of<vlink::DynamicData>() == vlink::Serializer::kDynamicType,
                "DynamicData must be detected as kDynamicType");

  // ======== Basic DynamicData usage ========
  std::cout << "[Basic Usage]" << std::endl;

  // Load a string value into DynamicData with a type name tag
  vlink::DynamicData dd_str;
  dd_str.load("StringMsg", std::string("Hello from DynamicData!"));
  std::cout << "  Type: " << dd_str.get_type() << std::endl;
  std::cout << "  Empty: " << std::boolalpha << dd_str.is_empty() << std::endl;

  // Retrieve the value using as<T>()
  auto recovered_str = dd_str.as<std::string>();
  std::cout << "  Value: \"" << recovered_str << "\"" << std::endl;

  // Load a POD type
  vlink::DynamicData dd_temp;
  Temperature temp{36.6, 42};
  dd_temp.load("Temperature", temp);
  std::cout << "\n  Type: " << dd_temp.get_type() << std::endl;

  // Retrieve using convert(T&)
  Temperature restored_temp{};
  dd_temp.convert(restored_temp);
  std::cout << "  celsius: " << restored_temp.celsius << " sensor_id: " << restored_temp.sensor_id << std::endl;

  // ======== Multi-type on a single topic ========
  // DynamicData allows DIFFERENT types on the SAME topic URL
  std::cout << "\n[Multi-type Topic]" << std::endl;

  int msg_count = 0;
  vlink::Subscriber<vlink::DynamicData> sub("dds://example/dynamic/multi");
  sub.attach(&loop);
  sub.listen([&msg_count](const vlink::DynamicData& dd) {
    msg_count++;
    std::string type_name(dd.get_type());

    std::cout << "  [Sub] #" << msg_count << " type=\"" << type_name << "\"";

    // Dispatch based on the runtime type tag
    if (type_name == "Temperature") {
      auto t = dd.as<Temperature>();
      std::cout << " celsius=" << t.celsius << " sensor=" << t.sensor_id;
    } else if (type_name == "Pressure") {
      auto p = dd.as<Pressure>();
      std::cout << " hpa=" << p.hpa << " sensor=" << p.sensor_id;
    } else if (type_name == "Status") {
      auto s = dd.as<std::string>();
      std::cout << " status=\"" << s << "\"";
    }
    std::cout << std::endl;
  });

  vlink::Publisher<vlink::DynamicData> pub("dds://example/dynamic/multi");
  pub.attach(&loop);

  vlink::Timer timer(&loop, 50, 1);
  timer.start([&pub]() {
    // Publish Temperature on the shared topic
    vlink::DynamicData dd1;
    dd1.load("Temperature", Temperature{22.5, 1});
    pub.publish(dd1);

    // Publish Pressure on the SAME topic
    vlink::DynamicData dd2;
    dd2.load("Pressure", Pressure{1013.25, 2});
    pub.publish(dd2);

    // Publish a string status on the SAME topic
    vlink::DynamicData dd3;
    dd3.load("Status", std::string("all_sensors_ok"));
    pub.publish(dd3);

    // Publish another Temperature
    vlink::DynamicData dd4;
    dd4.load("Temperature", Temperature{-5.0, 3});
    pub.publish(dd4);
  });

  loop.async_run();
  loop.wait_for_idle(1000);
  loop.quit();
  loop.wait_for_quit();

  // ======== Wire format: operator>> and operator<< ========
  std::cout << "\n[Wire Format]" << std::endl;

  vlink::DynamicData dd_wire;
  dd_wire.load("TestType", Temperature{100.0, 99});

  // Serialize to wire format (Bytes)
  vlink::Bytes wire_bytes;
  dd_wire >> wire_bytes;
  std::cout << "  Serialized wire size: " << wire_bytes.size() << " bytes" << std::endl;

  // Deserialize from wire format
  vlink::DynamicData dd_from_wire;
  dd_from_wire << wire_bytes;
  std::cout << "  Recovered type: " << dd_from_wire.get_type() << std::endl;

  Temperature from_wire{};
  dd_from_wire.convert(from_wire);
  std::cout << "  celsius: " << from_wire.celsius << " sensor_id: " << from_wire.sensor_id << std::endl;

  // ======== Equality comparison ========
  std::cout << "\n[Equality]" << std::endl;
  vlink::DynamicData a;
  vlink::DynamicData b;
  a.load("Test", 42);
  b.load("Test", 42);
  std::cout << "  Same content: " << std::boolalpha << (a == b) << std::endl;

  b.load("Test", 99);
  std::cout << "  Different content: " << (a != b) << std::endl;

  std::cout << "\n[Summary] Messages received: " << msg_count << std::endl;

  return 0;
}
