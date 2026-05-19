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

// IntraData zero-serialisation example
// Demonstrates VLINK_INTRA_DATA_DECLARE for zero-copy in-process messaging.

#include <vlink/base/logger.h>
#include <vlink/impl/intra_data.h>
#include <vlink/vlink.h>

#include <cstring>
#include <iostream>
#include <string>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

// MyStruct provides operator>>/<<, so it is detected as kCustomType.
// IntraData wrappers may target any supported Serializer::Type.
struct MyStruct {
  int32_t id;
  float temperature;
  char label[32];

  void operator>>(vlink::Bytes& out) const {  // NOLINT(google-runtime-operator)
    out = vlink::Bytes::create(sizeof(MyStruct));
    std::memcpy(out.data(), this, sizeof(MyStruct));
  }

  void operator<<(const vlink::Bytes& in) {
    if (in.size() >= sizeof(MyStruct)) {
      std::memcpy(this, in.data(), sizeof(MyStruct));
    }
  }
};

// Declare IntraData wrapper types:
//   MyIntraType -- concrete IntraDataType subclass holding MyStruct value
//   MyIntra     -- shared_ptr<MyIntraType> wrapper with create() factory
VLINK_INTRA_DATA_DECLARE(MyStruct, MyIntra)

int main() {
  // ======== Section 1: Creating IntraData Instances ========
  {
    std::cout << "\n[1] Creating IntraData via MyIntra::create()" << std::endl;

    // Factory method returns a shared_ptr-based handle
    auto data = MyIntra::create();

    // Access the embedded value directly -- no serialisation
    data->value.id = 42;
    data->value.temperature = 36.6F;
    std::strncpy(data->value.label, "sensor_A", sizeof(data->value.label) - 1);

    std::cout << "  id:          " << data->value.id << std::endl;
    std::cout << "  temperature: " << data->value.temperature << std::endl;
    std::cout << "  label:       " << data->value.label << std::endl;

    // Check the compile-time type tag
    std::cout << "  kValueType:  " << static_cast<int>(MyIntraType::kValueType) << std::endl;
    std::cout << "  type string: " << MyIntraType::get_serialized_type() << std::endl;
  }

  // ======== Section 2: Serialisation / Deserialisation ========
  // IntraData supports on-demand serialisation for cross-transport fallback.
  {
    std::cout << "\n[2] Manual Serialisation / Deserialisation" << std::endl;

    auto original = MyIntra::create();
    original->value.id = 100;
    original->value.temperature = 25.0F;
    std::strncpy(original->value.label, "motor_B", sizeof(original->value.label) - 1);

    // Serialise to Bytes
    vlink::Bytes wire;
    bool ok = (*original) >> wire;
    std::cout << "  Serialize ok:     " << std::boolalpha << ok << std::endl;
    std::cout << "  Serialized size:  " << wire.size() << " bytes" << std::endl;
    std::cout << "  get_serialized_size: " << original->get_serialized_size() << " bytes" << std::endl;

    // Deserialise from Bytes
    auto restored = MyIntra::create();
    ok = (*restored) << wire;
    std::cout << "  Deserialize ok:   " << std::boolalpha << ok << std::endl;
    std::cout << "  Restored id:      " << restored->value.id << std::endl;
    std::cout << "  Restored temp:    " << restored->value.temperature << std::endl;
    std::cout << "  Restored label:   " << restored->value.label << std::endl;
  }

  // ======== Section 3: Zero-Copy Publish / Subscribe on intra:// ========
  // When both publisher and subscriber are in the same process, IntraData
  // passes the shared_ptr directly -- no serialisation at all.
  {
    std::cout << "\n[3] Zero-Copy Pub/Sub on intra://" << std::endl;

    int received_count = 0;
    const std::string topic_url = "intra://example/intra_data/struct#direct";

    vlink::Subscriber<MyIntra> sub(topic_url);
    sub.listen([&](const MyIntra& typed) {
      received_count++;
      if (typed) {
        std::cout << "  [Sub] #" << received_count << " id=" << typed->value.id << " temp=" << typed->value.temperature
                  << " label=" << typed->value.label << std::endl;
      }
    });

    vlink::Publisher<MyIntra> pub(topic_url);

    pub.wait_for_subscribers();

    // Publish several IntraData messages
    for (int i = 1; i <= 5; ++i) {
      auto data = MyIntra::create();
      data->value.id = i;
      data->value.temperature = 20.0F + static_cast<float>(i);
      std::snprintf(data->value.label, sizeof(data->value.label), "reading_%d", i);
      pub.publish(data);
    }

    std::cout << "  Total received: " << received_count << std::endl;
  }

  // ======== Section 4: Shared Ownership ========
  // IntraData is reference-counted. Publisher and subscriber can both hold
  // a shared_ptr to the same underlying data without copying.
  {
    std::cout << "\n[4] Shared Ownership via shared_ptr" << std::endl;

    auto data = MyIntra::create();
    data->value.id = 999;

    // Copy the handle (increments reference count, not data)
    MyIntra alias = data;  // NOLINT(performance-unnecessary-copy-initialization)
    std::cout << "  data use_count:  " << data.use_count() << std::endl;
    std::cout << "  alias use_count: " << alias.use_count() << std::endl;
    std::cout << "  Same object:     " << std::boolalpha << (data.get() == alias.get()) << std::endl;
  }

  // ======== Section 5: IntraData vs Regular Types ========
  {
    std::cout << "\n[5] IntraData vs Regular Types" << std::endl;
    std::cout << "  +--------------------------+-----------------------------+" << std::endl;
    std::cout << "  | Regular Type             | IntraData Type              |" << std::endl;
    std::cout << "  +--------------------------+-----------------------------+" << std::endl;
    std::cout << "  | serialize on publish     | zero-copy (shared_ptr)      |" << std::endl;
    std::cout << "  | deserialize on receive   | direct pointer access       |" << std::endl;
    std::cout << "  | Works on all transports  | Only intra:// transport     |" << std::endl;
    std::cout << "  | Value semantics          | Reference semantics         |" << std::endl;
    std::cout << "  +--------------------------+-----------------------------+" << std::endl;
    std::cout << std::endl;
    std::cout << "  Use IntraData when:" << std::endl;
    std::cout << "    - Publisher and subscriber are in the same process" << std::endl;
    std::cout << "    - Data is large (images, point clouds) and copies are expensive" << std::endl;
    std::cout << "    - You need zero-copy performance on intra://" << std::endl;
  }

  VLOG_I("IntraData example complete.");
  return 0;
}
