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

// RawData zero-copy container example
// Demonstrates create, fill_data, header, serialize/deserialize, shallow/deep copy.

#include <vlink/base/logger.h>
#include <vlink/vlink.h>
#include <vlink/zerocopy/raw_data.h>

#include <cstring>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

int main() {
  // ======== Section 1: Create and Fill RawData ========
  {
    std::cout << "\n[1] Create and Fill RawData" << std::endl;

    vlink::zerocopy::RawData rd;

    // Allocate an owned buffer of 1024 bytes
    bool ok = rd.create(1024);
    std::cout << "  create(1024) = " << std::boolalpha << ok << std::endl;
    std::cout << "  size:      " << rd.size() << " bytes" << std::endl;
    std::cout << "  is_valid:  " << std::boolalpha << rd.is_valid() << std::endl;
    std::cout << "  is_owner:  " << std::boolalpha << rd.is_owner() << std::endl;

    // The buffer content is uninitialized after create().
    // Use fill_data or deep_copy to populate it.
  }

  // ======== Section 2: fill_data from Raw Pointer ========
  {
    std::cout << "\n[2] fill_data from Raw Pointer" << std::endl;

    // Simulate incoming sensor data
    constexpr size_t kDataSize = 512;
    auto* raw_buf = new uint8_t[kDataSize];
    for (size_t i = 0; i < kDataSize; ++i) {
      raw_buf[i] = static_cast<uint8_t>(i % 256);
    }

    vlink::zerocopy::RawData rd;
    // fill_data is an alias for deep_copy(uint8_t*, size_t)
    bool ok = rd.fill_data(raw_buf, kDataSize);
    std::cout << "  fill_data() = " << std::boolalpha << ok << std::endl;
    std::cout << "  size:    " << rd.size() << std::endl;
    std::cout << "  is_owner:" << std::boolalpha << rd.is_owner() << std::endl;
    std::cout << "  data[0]: " << static_cast<int>(rd.data()[0]) << std::endl;
    std::cout << "  data[255]:" << static_cast<int>(rd.data()[255]) << std::endl;

    delete[] raw_buf;
  }

  // ======== Section 3: Header Fields ========
  {
    std::cout << "\n[3] Header Fields" << std::endl;

    vlink::zerocopy::RawData rd;
    rd.create(256);

    rd.header.seq = 42;
    std::strncpy(rd.header.frame_id, "raw_0", sizeof(rd.header.frame_id) - 1);
    rd.header.frame_id[sizeof(rd.header.frame_id) - 1] = '\0';
    rd.header.time_meas = 1711612800000000000ULL;
    rd.header.time_pub = 1711612800001000000ULL;

    std::cout << "  seq:       " << rd.header.seq << std::endl;
    std::cout << "  frame_id:  " << rd.header.frame_id << std::endl;
    std::cout << "  time_meas: " << rd.header.time_meas << " ns" << std::endl;
    std::cout << "  time_pub:  " << rd.header.time_pub << " ns" << std::endl;

    // reserved_buf: user-defined 16-bit field that survives serialisation
    rd.reserved_buf() = 0xABCD;
    std::cout << "  reserved_buf: 0x" << std::hex << rd.reserved_buf() << std::dec << std::endl;
  }

  // ======== Section 4: Serialize / Deserialize ========
  {
    std::cout << "\n[4] Serialize / Deserialize" << std::endl;

    vlink::zerocopy::RawData original;
    original.create(2048);
    original.header.seq = 100;
    original.header.time_meas = 9999999;
    original.reserved_buf() = 42;

    // Fill with pattern data
    for (size_t i = 0; i < original.size(); ++i) {
      const_cast<uint8_t*>(original.data())[i] = static_cast<uint8_t>(i & 0xFF);
    }

    // Serialize to Bytes
    vlink::Bytes wire;
    original >> wire;
    std::cout << "  Serialized size: " << wire.size() << " bytes" << std::endl;
    std::cout << "  get_serialized_size: " << original.get_serialized_size() << std::endl;

    // Validate wire buffer
    bool valid = vlink::zerocopy::RawData::check_valid(wire);
    std::cout << "  check_valid:     " << std::boolalpha << valid << std::endl;

    // Deserialize (zero-copy: data borrows wire memory)
    vlink::zerocopy::RawData restored;
    restored << wire;
    std::cout << "  Restored size:     " << restored.size() << std::endl;
    std::cout << "  Restored seq:      " << restored.header.seq << std::endl;
    std::cout << "  Restored reserved: " << restored.reserved_buf() << std::endl;
    std::cout << "  is_owner:          " << std::boolalpha << restored.is_owner() << std::endl;
    std::cout << "  data[0]:           " << static_cast<int>(restored.data()[0]) << std::endl;
    std::cout << "  data[255]:         " << static_cast<int>(restored.data()[255]) << std::endl;
  }

  // ======== Section 5: Shallow Copy vs Deep Copy ========
  {
    std::cout << "\n[5] Shallow Copy vs Deep Copy" << std::endl;

    vlink::zerocopy::RawData source;
    source.create(128);
    source.header.seq = 77;

    // Shallow copy: borrows data pointer, no allocation
    vlink::zerocopy::RawData shallow;
    shallow.shallow_copy(source);
    std::cout << "  [shallow_copy]" << std::endl;
    std::cout << "    is_owner:      " << std::boolalpha << shallow.is_owner() << std::endl;
    std::cout << "    data == source: " << (shallow.data() == source.data()) << std::endl;

    // Deep copy: allocates new buffer
    vlink::zerocopy::RawData deep;
    deep.deep_copy(source);
    std::cout << "  [deep_copy]" << std::endl;
    std::cout << "    is_owner:      " << std::boolalpha << deep.is_owner() << std::endl;
    std::cout << "    data == source: " << (deep.data() == source.data()) << std::endl;

    // Move copy: transfers ownership
    vlink::zerocopy::RawData moved;
    moved.move_copy(source);
    std::cout << "  [move_copy]" << std::endl;
    std::cout << "    moved is_valid: " << std::boolalpha << moved.is_valid() << std::endl;
    std::cout << "    source is_valid:" << std::boolalpha << source.is_valid() << std::endl;
  }

  // ======== Section 6: Clear ========
  {
    std::cout << "\n[6] Clear" << std::endl;

    vlink::zerocopy::RawData rd;
    rd.create(256);
    rd.header.seq = 50;

    std::cout << "  Before clear: size=" << rd.size() << " seq=" << rd.header.seq << std::endl;
    rd.clear();
    std::cout << "  After clear:  size=" << rd.size() << " is_valid=" << std::boolalpha << rd.is_valid() << std::endl;
  }

  // ======== Section 7: Pub/Sub with RawData ========
  {
    std::cout << "\n[7] Pub/Sub with RawData" << std::endl;

    vlink::MessageLoop loop;
    loop.set_name("rawdata_loop");
    loop.async_run();

    int received = 0;
    vlink::Subscriber<vlink::zerocopy::RawData> sub("shm://zerocopy/rawdata");
    sub.attach(&loop);
    sub.listen([&received](const vlink::zerocopy::RawData& rd) {
      received++;
      std::cout << "  [Sub] seq=" << rd.header.seq << " size=" << rd.size() << " bytes" << std::endl;
    });

    vlink::Publisher<vlink::zerocopy::RawData> pub("shm://zerocopy/rawdata");
    pub.wait_for_subscribers();

    for (uint32_t i = 1; i <= 3; ++i) {
      vlink::zerocopy::RawData rd;
      rd.create(64 * i);
      rd.header.seq = i;
      rd.reserved_buf() = static_cast<uint16_t>(i * 10);
      pub.publish(rd);
    }

    loop.wait_for_idle(1000);
    std::cout << "  Total received: " << received << std::endl;

    loop.quit();
    loop.wait_for_quit();
  }

  VLOG_I("RawData example complete.");
  return 0;
}
