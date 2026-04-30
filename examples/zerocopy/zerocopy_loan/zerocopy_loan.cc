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

// Zero-copy loan API example
// Demonstrates loan(), return_loan(), is_support_loan(), set_manual_unloan().

#include <vlink/base/logger.h>
#include <vlink/vlink.h>

#include <cstring>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;  // NOLINT(build/namespaces, google-build-using-namespace)

// Simple POD payload
struct SensorData {
  uint32_t id;
  double value;
  uint64_t timestamp;
};

int main() {
  // ======== Section 1: Check Loan Support ========
  // is_support_loan() returns true when the transport provides a managed
  // memory pool (e.g., SHM via Iceoryx). dds:// does not support loans.
  {
    std::cout << "\n[1] Check Loan Support" << std::endl;

    vlink::Publisher<vlink::Bytes> pub_dds("dds://zerocopy_loan/check");
    bool loan_supported = pub_dds.is_support_loan();
    std::cout << "  dds:// is_support_loan() = " << std::boolalpha << loan_supported << std::endl;
    std::cout << "  (dds:// typically returns false -- loans are SHM-specific)" << std::endl;

    // Note: For SHM transport, the check would be:
    //   Publisher<Bytes> pub_shm("shm://zerocopy_loan/check");
    //   pub_shm.is_support_loan() => true
  }

  // ======== Section 2: Loan and Return Loan ========
  // loan(size) borrows a buffer from the transport's memory pool.
  // return_loan(bytes) returns an unused loan back to the pool.
  {
    std::cout << "\n[2] Loan / Return Loan" << std::endl;

    vlink::Publisher<vlink::Bytes> pub("dds://zerocopy_loan/loan_demo");

    // Attempt to loan a buffer
    vlink::Bytes loaned = pub.loan(sizeof(SensorData));

    if (!loaned.empty()) {
      std::cout << "  Loan succeeded: " << loaned.size() << " bytes" << std::endl;

      // Fill the loaned buffer
      auto* sensor = reinterpret_cast<SensorData*>(loaned.data());
      sensor->id = 1;
      sensor->value = 42.0;
      sensor->timestamp = 1234567890;

      // Publish uses the loaned buffer directly (no extra copy)
      pub.publish(loaned);
      std::cout << "  Published via loaned buffer" << std::endl;

      // Alternatively, return the loan without publishing:
      vlink::Bytes unused_loan = pub.loan(128);
      if (!unused_loan.empty()) {
        bool returned = pub.return_loan(unused_loan);
        std::cout << "  return_loan() = " << std::boolalpha << returned << std::endl;
      }
    } else {
      // dds:// does not support loans; this is expected
      std::cout << "  Loan not supported on this transport (expected for dds://)" << std::endl;
      std::cout << "  Demonstrating the API pattern with fallback..." << std::endl;

      // Fallback pattern: allocate a regular Bytes and publish
      vlink::Bytes data = vlink::Bytes::create(sizeof(SensorData));
      auto* sensor = reinterpret_cast<SensorData*>(data.data());
      sensor->id = 1;
      sensor->value = 42.0;
      sensor->timestamp = 1234567890;
      std::cout << "  Fallback: created Bytes of " << data.size() << " bytes" << std::endl;
    }
  }

  // ======== Section 3: Manual Unloan Mode ========
  // By default, the transport automatically returns subscriber loan buffers.
  // set_manual_unloan(true) lets the user control when buffers are returned.
  {
    std::cout << "\n[3] Manual Unloan Mode" << std::endl;

    vlink::MessageLoop loop;
    loop.set_name("loan_loop");
    loop.async_run();

    vlink::Subscriber<vlink::Bytes> sub("dds://zerocopy_loan/manual");
    sub.attach(&loop);

    // Check default state
    std::cout << "  Default is_manual_unloan() = " << std::boolalpha << sub.is_manual_unloan() << std::endl;

    // Enable manual unloan
    sub.set_manual_unloan(true);
    std::cout << "  After set_manual_unloan(true) = " << std::boolalpha << sub.is_manual_unloan() << std::endl;

    int received = 0;
    sub.listen([&received, &sub](const vlink::Bytes& msg) {
      received++;
      std::cout << "  [Sub] Received " << msg.size() << " bytes" << std::endl;

      // In manual mode, user is responsible for returning the loan
      // after consuming the data. On SHM transport:
      //   sub.return_loan(msg);
      // On dds://, return_loan is a no-op but safe to call.
      sub.return_loan(msg);
    });

    // Publish some test data
    vlink::Publisher<vlink::Bytes> pub("dds://zerocopy_loan/manual");
    pub.wait_for_subscribers();

    pub.publish(vlink::Bytes::from_string("manual_unloan_test"));

    loop.wait_for_idle(1000);

    // Disable manual unloan
    sub.set_manual_unloan(false);
    std::cout << "  After set_manual_unloan(false) = " << std::boolalpha << sub.is_manual_unloan() << std::endl;
    std::cout << "  Messages received: " << received << std::endl;

    loop.quit();
    loop.wait_for_quit();
  }

  // ======== Section 4: Recommended Loan Pattern ========
  // Demonstrates the complete loan-with-fallback pattern using real API calls.
  {
    std::cout << "\n[4] Recommended Loan Pattern" << std::endl;

    constexpr size_t kPayloadSize = sizeof(SensorData);

    // Use dds:// here; on SHM transport is_support_loan() would return true
    vlink::Publisher<vlink::Bytes> pub("dds://zerocopy_loan/pattern");

    if (pub.is_support_loan()) {
      // Transport supports zero-copy loans (e.g., SHM)
      VLOG_I("[Pattern] Transport supports loan -- using zero-copy path");
      vlink::Bytes buf = pub.loan(kPayloadSize);
      if (!buf.empty()) {
        auto* sensor = reinterpret_cast<SensorData*>(buf.data());
        sensor->id = 99;
        sensor->value = 3.14;
        sensor->timestamp = 9999999;
        pub.publish(buf);  // loan is returned automatically after publish
        VLOG_I("[Pattern] Published via loaned buffer, size=", buf.size());
      }
    } else {
      // Fallback: allocate and publish normally
      VLOG_I("[Pattern] Transport does not support loan -- using fallback allocation");
      vlink::Bytes buf = vlink::Bytes::create(kPayloadSize);
      auto* sensor = reinterpret_cast<SensorData*>(buf.data());
      sensor->id = 99;
      sensor->value = 3.14;
      sensor->timestamp = 9999999;
      VLOG_I("[Pattern] Fallback: allocated ", buf.size(), " bytes, ready to publish");
    }
  }

  // ======== Section 5: API Summary ========
  {
    std::cout << "[5] API Summary" << std::endl;
    std::cout << "  +---------------------------+--------------------------------------------+" << std::endl;
    std::cout << "  | Method                    | Description                                |" << std::endl;
    std::cout << "  +---------------------------+--------------------------------------------+" << std::endl;
    std::cout << "  | is_support_loan()         | True if transport provides a loan pool     |" << std::endl;
    std::cout << "  | loan(size)                | Borrow buffer from transport pool          |" << std::endl;
    std::cout << "  | return_loan(bytes)        | Return unused loan to pool                 |" << std::endl;
    std::cout << "  | set_manual_unloan(true)   | Subscriber controls buffer lifetime        |" << std::endl;
    std::cout << "  | is_manual_unloan()        | Check if manual unloan is active           |" << std::endl;
    std::cout << "  +---------------------------+--------------------------------------------+" << std::endl;
    std::cout << std::endl;
    std::cout << "  Transports with loan support: shm://, shm2://" << std::endl;
    std::cout << "  Transports without loan:      intra://, dds://, zenoh://, mqtt://" << std::endl;
  }

  VLOG_I("Zerocopy loan example complete.");
  return 0;
}
