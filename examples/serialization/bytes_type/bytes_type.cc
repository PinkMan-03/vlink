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

/// Bytes type serialization example
///
/// Demonstrates using Publisher<Bytes> / Subscriber<Bytes> over dds:// transport.
/// Bytes is the most fundamental type in VLink (kBytesType = 1). It is a pass-through
/// type that requires no serialization -- the raw binary data is delivered as-is.
///
/// This example covers:
///   1. Creating Bytes from various sources (create, from_string, initializer_list, vector)
///   2. Publishing and subscribing to raw binary data
///   3. Bytes utility functions (to_string, size, hex conversion)
int main() {
  // Create a message loop for event dispatching
  vlink::MessageLoop loop;

  // ======== Compile-time type verification ========
  // Bytes is always detected as kBytesType -- the first in the detection chain
  static_assert(vlink::Serializer::get_type_of<vlink::Bytes>() == vlink::Serializer::kBytesType,
                "Bytes must be detected as kBytesType");
  static_assert(vlink::Serializer::is_supported(vlink::Serializer::kBytesType), "kBytesType must be supported");

  int received_count = 0;

  // ======== Subscriber setup ========
  // Subscribe to raw byte messages on dds:// transport
  vlink::Subscriber<vlink::Bytes> sub("dds://example/bytes_type");
  sub.attach(&loop);
  sub.listen([&received_count](const vlink::Bytes& msg) {
    received_count++;
    std::cout << "[Sub] Received #" << received_count << " size=" << msg.size() << " content: " << msg.to_string()
              << std::endl;

    // Display hex representation for binary data

    // NOLINTNEXTLINE(readability-container-size-empty)
    if (msg.size() > 0) {
      std::cout << "      Hex: " << vlink::Bytes::convert_to_hex_str(msg.data(), msg.size()) << std::endl;
    }
  });

  // ======== Publisher setup ========
  vlink::Publisher<vlink::Bytes> pub("dds://example/bytes_type");
  pub.attach(&loop);

  // Use a timer to publish different Bytes payloads
  vlink::Timer timer(&loop, 50, 1);
  timer.start([&pub]() {
    // Method 1: Create from a string
    auto bytes_from_str = vlink::Bytes::from_string("Hello VLink Bytes!");
    pub.publish(bytes_from_str);

    // Method 2: Create from an initializer list (raw binary data)
    vlink::Bytes bytes_from_list{0x48, 0x65, 0x6C, 0x6C, 0x6F};  // "Hello" in ASCII
    pub.publish(bytes_from_list);

    // Method 3: Create a fixed-size buffer and fill it manually
    auto bytes_manual = vlink::Bytes::create(8);
    std::memset(bytes_manual.data(), 0xAB, bytes_manual.size());
    pub.publish(bytes_manual);

    // Method 4: Create from a std::vector
    std::vector<uint8_t> vec = {0x01, 0x02, 0x03, 0x04, 0xFF};
    vlink::Bytes bytes_from_vec(vec);
    pub.publish(bytes_from_vec);

    // Method 5: Publish an empty Bytes (valid -- zero-length message)
    pub.publish(vlink::Bytes{});
  });

  // Let the loop process all timer and subscriber callbacks
  loop.async_run();
  loop.wait_for_idle(1000);
  loop.quit();
  loop.wait_for_quit();

  std::cout << "\n[Summary] Total messages received: " << received_count << std::endl;

  // ======== Bytes utility demonstrations ========
  auto sample = vlink::Bytes::from_string("VLink");

  // Size and content accessors
  std::cout << "\n[Utilities]" << std::endl;
  std::cout << "  to_string:      " << sample.to_string() << std::endl;
  std::cout << "  size:           " << sample.size() << std::endl;
  std::cout << "  is_owner:       " << std::boolalpha << sample.is_owner() << std::endl;
  std::cout << "  empty:          " << sample.empty() << std::endl;

  // Hex conversion
  std::cout << "  hex:            " << vlink::Bytes::convert_to_hex_str(sample.data(), sample.size()) << std::endl;

  // Base-64 encode / decode round-trip
  auto b64 = vlink::Bytes::encode_to_base64(sample);
  auto decoded = vlink::Bytes::decode_from_base64(b64);
  std::cout << "  base64:         " << b64 << std::endl;
  std::cout << "  decoded:        " << decoded.to_string() << std::endl;

  // CRC-32 checksum
  std::cout << "  crc32:          0x" << std::hex << vlink::Bytes::get_crc_32(sample) << std::dec << std::endl;

  // Equality comparison
  auto copy = vlink::Bytes::deep_copy(sample.data(), sample.size());
  std::cout << "  deep_copy == original: " << std::boolalpha << (copy == sample) << std::endl;

  return 0;
}
