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

#include <iostream>
#include <string>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// String type serialization example
///
/// Demonstrates using Publisher<std::string> / Subscriber<std::string> over dds://.
/// std::string is detected as kStringType (= 10). The serialization simply converts
/// the string content into a Bytes buffer. This example covers:
///   1. Normal ASCII strings
///   2. UTF-8 encoded strings (Chinese, Japanese, emoji, etc.)
///   3. Empty strings (valid edge case)
///   4. Long strings (exceeding SBO threshold)
///   5. Strings with special characters
int main() {
  vlink::MessageLoop loop;

  // ======== Compile-time type verification ========
  static_assert(vlink::Serializer::get_type_of<std::string>() == vlink::Serializer::kStringType,
                "std::string must be detected as kStringType");

  int received_count = 0;

  // ======== Subscriber ========
  vlink::Subscriber<std::string> sub("dds://example/string_type");
  sub.attach(&loop);
  sub.listen([&received_count](const std::string& msg) {
    received_count++;
    std::cout << "[Sub] #" << received_count << " length=" << msg.size() << " content=\"" << msg << "\"" << std::endl;
  });

  // ======== Publisher ========
  vlink::Publisher<std::string> pub("dds://example/string_type");
  pub.attach(&loop);

  vlink::Timer timer(&loop, 50, 1);
  timer.start([&pub]() {
    // Case 1: Simple ASCII string
    pub.publish(std::string("Hello, VLink!"));

    // Case 2: UTF-8 encoded string (multi-byte characters)
    pub.publish(std::string("VLink supports UTF-8 encoding"));

    // Case 3: Empty string (valid zero-length message)
    pub.publish(std::string(""));

    // Case 4: Long string exceeding the Bytes SBO threshold (96 bytes)
    std::string long_str(200, 'A');
    long_str += " -- this string is longer than the 96-byte SBO threshold";
    pub.publish(long_str);

    // Case 5: String with special characters (newlines, tabs, null bytes)
    pub.publish(std::string("line1\nline2\ttab\0embedded_null", 29));

    // Case 6: JSON-like string payload
    pub.publish(std::string(R"({"key":"value","count":42})"));

    // Case 7: Numeric string

    // NOLINTNEXTLINE(modernize-use-std-numbers)
    pub.publish(std::to_string(3.14159265358979));
  });

  loop.async_run();
  loop.wait_for_idle(1000);
  loop.quit();
  loop.wait_for_quit();

  // ======== Manual serialization round-trip ========
  std::cout << "\n[Manual Serialize/Deserialize Test]" << std::endl;
  std::string original = "round-trip test payload";
  vlink::Bytes buf;
  vlink::Serializer::serialize(original, buf);
  std::cout << "  Serialized size: " << buf.size() << " bytes" << std::endl;
  std::cout << "  Bytes content:   " << buf.to_string() << std::endl;

  std::string restored;
  vlink::Serializer::deserialize(buf, restored);
  std::cout << "  Restored:        \"" << restored << "\"" << std::endl;
  std::cout << "  Match:           " << std::boolalpha << (original == restored) << std::endl;

  std::cout << "\n[Summary] Total messages received: " << received_count << std::endl;

  return 0;
}
