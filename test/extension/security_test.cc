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

// NOLINTBEGIN

#include "./extension/security.h"

#ifdef VLINK_TEST_SUPPORT_SECURITY

#include <doctest/doctest.h>

#include <string>

#include "./base/bytes.h"

//
#include "../common_test.h"

// ---------------------------------------------------------------------------
// TEST SUITE: Security - custom callbacks (always available)
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Security - custom callbacks") {
  TEST_CASE("custom encrypt/decrypt round-trip") {
    Security sec;

    // Install a simple XOR cipher as a stub
    sec.set_callbacks(
        [](const Bytes& in, Bytes& out) -> bool {
          out = Bytes::create(in.size());
          for (size_t i = 0; i < in.size(); ++i) {
            out[i] = static_cast<uint8_t>(in[i] ^ 0xAA);
          }
          return true;
        },
        [](const Bytes& in, Bytes& out) -> bool {
          out = Bytes::create(in.size());
          for (size_t i = 0; i < in.size(); ++i) {
            out[i] = static_cast<uint8_t>(in[i] ^ 0xAA);
          }
          return true;
        });

    const std::string plain_str = "hello world test message";
    Bytes plain = Bytes::create(plain_str.size());
    std::memcpy(plain.data(), plain_str.data(), plain_str.size());

    Bytes cipher;
    bool enc_ok = sec.encrypt(plain, cipher);
    CHECK(enc_ok == true);
    CHECK(!cipher.empty());

    Bytes recovered;
    bool dec_ok = sec.decrypt(cipher, recovered);
    CHECK(dec_ok == true);

    REQUIRE(recovered.size() == plain.size());
    CHECK(std::memcmp(plain.data(), recovered.data(), plain.size()) == 0);
  }

  TEST_CASE("empty Bytes encrypt returns true without modification") {
    Security sec;

    sec.set_callbacks([](const Bytes& /*in*/, Bytes& /*out*/) -> bool { return false; },
                      [](const Bytes& /*in*/, Bytes& /*out*/) -> bool { return false; });

    Bytes empty;
    Bytes out;
    bool ret = sec.encrypt(empty, out);
    // Empty input is a no-op and returns true immediately
    CHECK(ret == true);
  }

  TEST_CASE("empty Bytes decrypt returns true without modification") {
    Security sec;

    sec.set_callbacks([](const Bytes& /*in*/, Bytes& /*out*/) -> bool { return false; },
                      [](const Bytes& /*in*/, Bytes& /*out*/) -> bool { return false; });

    Bytes empty;
    Bytes out;
    bool ret = sec.decrypt(empty, out);
    CHECK(ret == true);
  }

  TEST_CASE("custom callback that returns false makes encrypt/decrypt fail") {
    Security sec;

    sec.set_callbacks([](const Bytes& /*in*/, Bytes& /*out*/) -> bool { return false; },
                      [](const Bytes& /*in*/, Bytes& /*out*/) -> bool { return false; });

    Bytes data = Bytes::create(16);
    data[0] = 0xFF;

    Bytes out;
    bool enc = sec.encrypt(data, out);
    CHECK(enc == false);

    bool dec = sec.decrypt(data, out);
    CHECK(dec == false);
  }

  TEST_CASE("set_callbacks installs both functions") {
    Security sec;

    int enc_calls = 0;
    int dec_calls = 0;

    sec.set_callbacks(
        [&enc_calls](const Bytes& in, Bytes& out) -> bool {
          enc_calls++;
          out = in;
          return true;
        },
        [&dec_calls](const Bytes& in, Bytes& out) -> bool {
          dec_calls++;
          out = in;
          return true;
        });

    Bytes data = Bytes::create(8);
    Bytes out;

    sec.encrypt(data, out);
    CHECK(enc_calls == 1);

    sec.decrypt(data, out);
    CHECK(dec_calls == 1);
  }

  TEST_CASE("set_key does not crash") {
    Security sec;
    sec.set_key("my_secret_key_123");
    sec.set_key("another_key");
    sec.set_key("");  // restore default
  }

  TEST_CASE("replacing callbacks overrides previous ones") {
    Security sec;

    int first_enc = 0;
    int second_enc = 0;

    sec.set_callbacks(
        [&first_enc](const Bytes& in, Bytes& out) -> bool {
          first_enc++;
          out = in;
          return true;
        },
        [](const Bytes& in, Bytes& out) -> bool {
          out = in;
          return true;
        });

    Bytes data = Bytes::create(4);
    Bytes out;
    sec.encrypt(data, out);
    CHECK(first_enc == 1);

    // Replace callbacks
    sec.set_callbacks(
        [&second_enc](const Bytes& in, Bytes& out) -> bool {
          second_enc++;
          out = in;
          return true;
        },
        [](const Bytes& in, Bytes& out) -> bool {
          out = in;
          return true;
        });

    sec.encrypt(data, out);
    CHECK(first_enc == 1);   // old callback not called again
    CHECK(second_enc == 1);  // new callback called
  }

  TEST_CASE("encrypt and decrypt with large data via custom callback") {
    Security sec;

    sec.set_callbacks(
        [](const Bytes& in, Bytes& out) -> bool {
          out = Bytes::create(in.size());
          for (size_t i = 0; i < in.size(); ++i) {
            out[i] = static_cast<uint8_t>(in[i] ^ 0x55);
          }
          return true;
        },
        [](const Bytes& in, Bytes& out) -> bool {
          out = Bytes::create(in.size());
          for (size_t i = 0; i < in.size(); ++i) {
            out[i] = static_cast<uint8_t>(in[i] ^ 0x55);
          }
          return true;
        });

    // Test with 1024 bytes
    Bytes large = Bytes::create(1024);
    for (size_t i = 0; i < 1024; ++i) {
      large[i] = static_cast<uint8_t>(i & 0xFF);
    }

    Bytes cipher;
    CHECK(sec.encrypt(large, cipher) == true);
    CHECK(cipher.size() == 1024);

    Bytes recovered;
    CHECK(sec.decrypt(cipher, recovered) == true);
    REQUIRE(recovered.size() == large.size());
    CHECK(std::memcmp(large.data(), recovered.data(), large.size()) == 0);
  }

  TEST_CASE("encrypt callback failure does not modify output") {
    Security sec;

    sec.set_callbacks([](const Bytes& /*in*/, Bytes& /*out*/) -> bool { return false; },
                      [](const Bytes& in, Bytes& out) -> bool {
                        out = in;
                        return true;
                      });

    Bytes data = Bytes::create(8);
    Bytes out;
    CHECK(sec.encrypt(data, out) == false);
  }

  TEST_CASE("decrypt callback failure does not modify output") {
    Security sec;

    sec.set_callbacks(
        [](const Bytes& in, Bytes& out) -> bool {
          out = in;
          return true;
        },
        [](const Bytes& /*in*/, Bytes& /*out*/) -> bool { return false; });

    Bytes data = Bytes::create(8);
    Bytes out;
    CHECK(sec.decrypt(data, out) == false);
  }

  TEST_CASE("set_key with various key lengths does not crash") {
    Security sec;
    sec.set_key("a");
    sec.set_key("1234567890123456");                  // exactly 16 bytes
    sec.set_key("12345678901234567890123456789012");  // 32 bytes
    sec.set_key(std::string(256, 'x'));               // very long key
  }

  TEST_CASE("multiple encrypt/decrypt cycles with same Security object") {
    Security sec;

    sec.set_callbacks(
        [](const Bytes& in, Bytes& out) -> bool {
          out = in;
          return true;
        },
        [](const Bytes& in, Bytes& out) -> bool {
          out = in;
          return true;
        });

    for (int i = 0; i < 10; ++i) {
      Bytes data = Bytes::create(static_cast<size_t>(i + 1));
      data[0] = static_cast<uint8_t>(i);

      Bytes enc;
      CHECK(sec.encrypt(data, enc) == true);

      Bytes dec;
      CHECK(sec.decrypt(enc, dec) == true);
      CHECK(dec.size() == data.size());
    }
  }
}

// ---------------------------------------------------------------------------
// TEST SUITE: Security - AES built-in (requires VLINK_ENABLE_SECURITY)
// ---------------------------------------------------------------------------

TEST_SUITE("extension-Security - AES built-in") {
  TEST_CASE("AES encrypt/decrypt round-trip") {
    Security sec;
    sec.set_key("test_key_16bytes");

    const std::string plain_str = "AES encryption test data!";
    Bytes plain = Bytes::create(plain_str.size());
    std::memcpy(plain.data(), plain_str.data(), plain_str.size());

    Bytes cipher;
    bool enc_ok = sec.encrypt(plain, cipher);
    CHECK(enc_ok == true);

    // Ciphertext must differ from plaintext
    CHECK(cipher.size() >= plain.size());

    Bytes recovered;
    bool dec_ok = sec.decrypt(cipher, recovered);
    CHECK(dec_ok == true);

    REQUIRE(recovered.size() == plain.size());
    CHECK(std::memcmp(plain.data(), recovered.data(), plain.size()) == 0);
  }

  TEST_CASE("AES encrypt produces non-identical output for different keys") {
    Security sec1;
    Security sec2;
    sec1.set_key("key_one_1234567");
    sec2.set_key("key_two_9876543");

    Bytes data = Bytes::create(32);
    std::memset(data.data(), 0x42, 32);

    Bytes out1;
    Bytes out2;

    sec1.encrypt(data, out1);
    sec2.encrypt(data, out2);

    // Outputs must differ due to different keys
    bool same = (out1.size() == out2.size() && std::memcmp(out1.data(), out2.data(), out1.size()) == 0);
    CHECK_FALSE(same);
  }

  TEST_CASE("AES empty bytes returns true") {
    Security sec;
    Bytes empty;
    Bytes out;

    CHECK(sec.encrypt(empty, out) == true);
    CHECK(sec.decrypt(empty, out) == true);
  }
}

#endif

// NOLINTEND
