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
#include <sstream>
#include <string>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

/// Stream type serialization example
///
/// Demonstrates types that are serialized via std::stringstream operator<< and operator>>.
/// These are detected as kStreamType (= 12).
///
/// kStreamType detection requires:
///   - The type supports both std::stringstream << t and std::stringstream >> t
///   - The type is NOT detected by any higher-priority serializer
///
/// In the detection chain, kStreamType (position 12) ranks LOWER than kCustomType (position 9).
/// This means:
///   - If a type has BOTH Bytes operator>>/operator<< AND stream operators, it is kCustomType
///   - kStreamType is only for types that have stringstream operators but NOT Bytes operators
///
/// Typical kStreamType candidates are types that already provide iostream overloads,
/// such as user-defined lightweight value types.

// Stream types defined in stream_types.h -- see that file for struct definitions
#include "stream_types.h"

int main() {
  vlink::MessageLoop loop;

  // ======== Color pub/sub (kStreamType) ========
  int color_count = 0;
  vlink::Subscriber<Color> color_sub("dds://example/stream/color");
  color_sub.attach(&loop);
  color_sub.listen([&color_count](const Color& c) {
    color_count++;
    std::cout << "[Color] #" << color_count << " r=" << c.r << " g=" << c.g << " b=" << c.b << " a=" << c.a
              << std::endl;
  });

  vlink::Publisher<Color> color_pub("dds://example/stream/color");
  color_pub.attach(&loop);

  // ======== Size2D pub/sub (kStreamType) ========
  int size_count = 0;
  vlink::Subscriber<Size2D> size_sub("dds://example/stream/size");
  size_sub.attach(&loop);
  size_sub.listen([&size_count](const Size2D& s) {
    size_count++;
    std::cout << "[Size2D] #" << size_count << " width=" << s.width << " height=" << s.height << std::endl;
  });

  vlink::Publisher<Size2D> size_pub("dds://example/stream/size");
  size_pub.attach(&loop);

  vlink::Timer timer(&loop, 50, 1);
  timer.start([&]() {
    // Publish Color values
    color_pub.publish(Color{255, 0, 0, 255});    // Red
    color_pub.publish(Color{0, 128, 255, 200});  // Semi-transparent blue
    color_pub.publish(Color{0, 0, 0, 0});        // Fully transparent black

    // Publish Size2D values
    size_pub.publish(Size2D{1920.0, 1080.0});
    size_pub.publish(Size2D{0.5, 0.5});
  });

  loop.async_run();
  loop.wait_for_idle(1000);
  loop.quit();
  loop.wait_for_quit();

  // ======== Show the text-based wire format ========
  std::cout << "\n[Wire Format Demonstration]" << std::endl;
  Color c{100, 200, 50, 128};
  vlink::Bytes buf;
  vlink::Serializer::serialize(c, buf);
  std::cout << "  Color{100,200,50,128} serialized to: \"" << buf.to_string() << "\"" << std::endl;
  std::cout << "  Serialized size: " << buf.size() << " bytes (text representation)" << std::endl;

  Color restored;
  vlink::Serializer::deserialize(buf, restored);
  std::cout << "  Restored: r=" << restored.r << " g=" << restored.g << " b=" << restored.b << " a=" << restored.a
            << std::endl;

  // Show that kStreamType uses text encoding (not binary)
  Size2D sz{1920.0, 1080.0};
  vlink::Bytes sz_buf;
  vlink::Serializer::serialize(sz, sz_buf);
  std::cout << "\n  Size2D{1920,1080} serialized to: \"" << sz_buf.to_string() << "\"" << std::endl;

  // ======== Priority comparison ========
  std::cout << "\n[Detection Priority]" << std::endl;
  std::cout << "  Color      -> kStreamType  (position 12)" << std::endl;
  std::cout << "  Hybrid     -> kCustomType  (position  9, Bytes ops take priority)" << std::endl;
  std::cout << "  std::string -> kStringType (position 10, checked before stream)" << std::endl;
  std::cout << "  int        -> kStandardType(position 13, trivial+standard_layout)" << std::endl;

  std::cout << "\n[Summary] Colors=" << color_count << " Sizes=" << size_count << std::endl;

  return 0;
}
