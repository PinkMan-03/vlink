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

/**
 * @file types.h
 * @brief Core type definitions shared across all VLink node implementations.
 *
 * @details
 * Defines the fundamental enumerations and plain-data structs that are used
 * throughout the VLink implementation layer.  These types are part of the
 * internal ABI between the @c Node<> template, the transport @c Conf
 * implementations, and the @c NodeImpl backend classes.
 *
 * @par Node Type Bitmask (@c ImplType)
 * @c ImplType values can be bitwise-OR'd together when querying combined roles:
 *
 * | Value             | Hex  | Description                       |
 * | ----------------- | ---- | --------------------------------- |
 * | kUnknownImplType  | 0x00 | Type not yet determined.          |
 * | kPublisher        | 0x01 | Event publisher node.             |
 * | kSubscriber       | 0x02 | Event subscriber node.            |
 * | kSetter           | 0x04 | Field setter node.                |
 * | kGetter           | 0x08 | Field getter node.                |
 * | kServer           | 0x10 | Method server node.               |
 * | kClient           | 0x20 | Method client node.               |
 *
 * @par Transport Types (@c TransportType)
 *
 * | Value    | URL Prefix   | Transport                            |
 * | -------- | ------------ | ------------------------------------ |
 * | kUnknown | (none)       | Unknown or unsupported.              |
 * | kIntra   | intra://     | In-process queue (no serialisation). |
 * | kShm     | shm://       | Iceoryx shared memory.               |
 * | kShm2    | shm2://      | Iceoryx2 shared memory.              |
 * | kZenoh   | zenoh://     | Zenoh publish/subscribe.             |
 * | kDds     | dds://       | Fast-DDS RTPS.                       |
 * | kDdsc    | ddsc://      | CycloneDDS.                          |
 * | kDdsr    | ddsr://      | RTI DDS.                             |
 * | kDdst    | ddst://      | TravoDDS.                            |
 * | kSomeip  | someip://    | SOME/IP via vsomeip.                 |
 * | kMqtt    | mqtt://      | MQTT publish/subscribe.            |
 * | kFdbus   | fdbus://     | FDBus IPC.                           |
 * | kQnx     | qnx://       | QNX IPC (QNX only).                  |
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "../base/bytes.h"
#include "../base/macros.h"

namespace vlink {

/**
 * @enum ImplType
 * @brief Bitmask identifying the role of a VLink node implementation.
 *
 * @details
 * Values can be combined with bitwise-OR to represent compound roles, e.g.
 * @c (kPublisher | kSubscriber) for a topic that has both publisher and
 * subscriber endpoints.  The @c VLINK_ALLOW_IMPL_TYPE macro uses these
 * combined flags to restrict which node types a transport @c Conf may serve.
 */
enum ImplType : uint8_t {
  kUnknownImplType = 0,  ///< Type not yet determined.
  kServer = 16,          ///< Method server (RPC responder).
  kClient = 32,          ///< Method client (RPC caller).
  kPublisher = 1,        ///< Event publisher (1-to-many broadcast).
  kSubscriber = 2,       ///< Event subscriber (receive broadcast).
  kSetter = 4,           ///< Field setter (update latest value).
  kGetter = 8,           ///< Field getter (retrieve latest value).
};

/**
 * @enum TransportType
 * @brief Enumeration of all supported transport backends.
 *
 * @details
 * Determined at URL construction time from the transport prefix string.
 * Concrete transport @c Conf classes use this enum to identify themselves.
 */
enum class TransportType : uint8_t {
  kUnknown = 0,  ///< Unknown or unsupported transport.
  kIntra = 1,    ///< In-process queue (intra://).
  kShm = 2,      ///< Iceoryx shared memory (shm://).
  kShm2 = 3,     ///< Iceoryx2 shared memory (shm2://).
  kZenoh = 4,    ///< Zenoh publish/subscribe (zenoh://).
  kDds = 5,      ///< Fast-DDS RTPS (dds://).
  kDdsc = 6,     ///< CycloneDDS (ddsc://).
  kDdsr = 7,     ///< RTI DDS (ddsr://).
  kDdst = 8,     ///< TravoDDS (ddst://).
  kSomeip = 9,   ///< SOME/IP via vsomeip (someip://).
  kMqtt = 10,    ///< MQTT (mqtt://).
  kFdbus = 11,   ///< FDBus IPC (fdbus://).
  kQnx = 12,     ///< QNX IPC (qnx://; QNX only).
};

/**
 * @enum InitType
 * @brief Controls whether a node is initialised immediately at construction.
 *
 * @details
 * Pass @c kWithoutInit to defer initialisation until @c init() is called
 * explicitly, allowing properties (e.g. DDS QoS, IP binding) to be set
 * before the underlying transport is started.
 */
enum class InitType : uint8_t {
  kWithoutInit = 0,  ///< Defer initialisation; call init() manually.
  kWithInit = 1,     ///< Initialise immediately in the constructor.
};

/**
 * @enum SecurityType
 * @brief Controls whether a node uses encrypted/authenticated transport.
 *
 * @details
 * Selects the security variant at compile time via the template parameter
 * of @c Publisher<T, SecurityType>, @c Subscriber<T, SecurityType>, etc.
 * @c kWithSecurity enables authenticated AES-128-GCM encryption (with optional
 * RSA-OAEP hybrid key wrap and RSA-PSS signature) on the serialised message
 * payload.  Supported on all transports except @c intra:// and @c dds:// with
 * CDR serialisation; on those, the @c Security::Config passed to the
 * @c SecurityXxx constructor is ignored with a warning and the node continues
 * in plaintext mode.
 */
enum class SecurityType : uint8_t {
  kWithoutSecurity = 0,  ///< Plain (unauthenticated) transport.
  kWithSecurity = 1,     ///< Encrypted and authenticated transport.
};

/**
 * @enum ActionType
 * @brief Identifies the type of message action for recording purposes.
 *
 * @details
 * Used by @c NodeImpl::try_record() to tag each captured message with its
 * originating operation so that the bag writer can reconstruct message flow
 * during playback.
 */
enum class ActionType : uint8_t {
  kUnknownAction = 0,   ///< Action type not classified.
  kClientRequest = 1,   ///< RPC request sent by a Client node.
  kClientResponse = 2,  ///< RPC response received by a Client node.
  kServerRequest = 3,   ///< RPC request received by a Server node.
  kServerResponse = 4,  ///< RPC response sent by a Server node.
  kPublish = 5,         ///< Message published by a Publisher node.
  kSubscribe = 6,       ///< Message received by a Subscriber node.
  kSet = 7,             ///< Field value written by a Setter node.
  kGet = 8              ///< Field value read by a Getter node.
};

/**
 * @enum SchemaType
 * @brief Coarse runtime schema family used by discovery, bag metadata, and proxy routing.
 *
 * @details
 * @c ser_type stores the concrete wire/type identifier (for example a protobuf
 * full name or a FlatBuffers table name), while @c SchemaType captures only the
 * high-level runtime decoding category so tools can choose the correct decode
 * stack without maintaining a second parallel enum.
 */
enum class SchemaType : uint8_t {
  kUnknown = 0,      ///< Decode category is not known.
  kRaw = 1,          ///< Treat the payload as opaque/raw bytes.
  kZeroCopy = 2,     ///< Decode using VLink zero-copy message structs.
  kProtobuf = 3,     ///< Decode using the Protocol Buffers stack.
  kFlatbuffers = 4,  ///< Decode using the FlatBuffers stack.
};

/**
 * @struct Timeout
 * @brief Compile-time timeout constants used by blocking wait methods.
 *
 * @details
 * Provides canonical timeout values used throughout the @c Publisher,
 * @c Subscriber, @c Client, etc. public APIs for @c wait_for_* calls.
 */
struct Timeout final {
  [[maybe_unused]] static constexpr std::chrono::milliseconds kDefaultInterval{
      5'000};                                                                 ///< Default wait timeout: 5 seconds.
  [[maybe_unused]] static constexpr std::chrono::milliseconds kInfinite{-1};  ///< Wait indefinitely (negative timeout).
};

/**
 * @struct SampleLostInfo
 * @brief Cumulative sample delivery statistics for a subscriber or getter.
 *
 * @details
 * Populated by @c SubscriberImpl::get_lost() and @c GetterImpl::get_lost().
 * The @c total field counts every message expected (both delivered and lost);
 * @c lost counts those that were never delivered due to queue overflow or
 * network loss.
 * Supports streaming via @c operator<<.
 */
struct SampleLostInfo final {
  uint64_t total{0};  ///< Total number of samples expected (delivered + lost).
  uint64_t lost{0};   ///< Number of samples that were dropped or missed.

  /**
   * @brief Streams a human-readable summary to @p ostream.
   *
   * @details
   * Format: @c "SampleLostInfo:[total]N[lost]M".
   *
   * @param ostream  Output stream to write to.
   * @param info     Instance to print.
   * @return         Reference to @p ostream.
   */
  VLINK_EXPORT friend std::ostream& operator<<(std::ostream& ostream, const SampleLostInfo& info) noexcept;
};

/**
 * @struct SchemaData
 * @brief Carries one serialized schema blob for runtime registration or embedding.
 *
 * @details
 * Used by schema-aware tools such as bag readers, MCAP writers, and schema
 * plugins to move imported schema metadata around without assuming a specific
 * schema language. The @c encoding field stores the original schema payload
 * encoding (for example @c "protobuf", @c "flatbuffers", or @c "vlink_msg"),
 * while @c schema_type captures the coarse runtime family used by discovery,
 * bag routing, and proxy consumers.
 */
struct VLINK_EXPORT SchemaData final {
  std::string name;      ///< Schema subject name, typically a fully-qualified message or table type.
  std::string encoding;  ///< Schema encoding identifier (e.g. @c "protobuf" or @c "flatbuffers").
  SchemaType schema_type{SchemaType::kUnknown};  ///< Coarse runtime schema family derived from @c encoding.
  Bytes data;                                    ///< Raw serialized schema bytes (e.g. FileDescriptorSet or BFBS).

  /**
   * @brief Returns whether a schema type enum value is within the supported range.
   *
   * @param schema_type  Schema type value to validate.
   * @return @c true when @p schema_type is a supported enum member.
   */
  [[nodiscard]] static bool is_valid_type(SchemaType schema_type) noexcept;

  /**
   * @brief Returns whether a schema type carries concrete runtime schema metadata.
   *
   * @details
   * Unlike @c is_valid_type(), this excludes @c kUnknown and @c kRaw.  It is
   * used by schema caching / bag embedding code to decide whether a schema can
   * be indexed or persisted as a real schema entry.
   *
   * @param schema_type  Schema type value to classify.
   * @return @c true for protobuf / flatbuffers / zerocopy families.
   */
  [[nodiscard]] static bool is_real_type(SchemaType schema_type) noexcept;

  /**
   * @brief Converts a schema type to its canonical persisted encoding label.
   *
   * @param schema_type  Schema type to convert.
   * @return Canonical encoding string, or an empty view for unknown values.
   */
  [[nodiscard]] static std::string_view convert_type(SchemaType schema_type) noexcept;

  /**
   * @brief Parses a schema type from an encoding label.
   *
   * @param encoding  Encoding string such as @c "protobuf", @c "fbs", @c "blob", or @c "zerocopy".
   * @return Matching schema type, or @c SchemaType::kUnknown.
   */
  [[nodiscard]] static SchemaType convert_encoding(std::string_view encoding) noexcept;

  /**
   * @brief Infers a coarse schema family directly from a concrete @c ser_type string.
   *
   * @details
   * This lightweight constexpr helper is intentionally conservative:
   * - zero-copy types are recognised by the @c "vlink::zerocopy::" prefix
   * - textual/raw payload types map to @c SchemaType::kRaw
   * - protobuf / flatbuffers are not guessed from names here
   *
   * @param ser_type  Concrete serialisation type string.
   * @return Matching schema family, or @c SchemaType::kUnknown.
   */
  [[nodiscard]] static constexpr SchemaType infer_ser_type(std::string_view ser_type) noexcept;

  /**
   * @brief Resolves the best available schema family from explicit, encoding and ser hints.
   *
   * @details
   * Resolution order is:
   * 1. explicit @p schema_type when already known
   * 2. inferred family from @p encoding
   * 3. inferred family from @p ser_type
   *
   * @param schema_type  Explicit schema family hint.
   * @param ser_type     Concrete serialisation type string.
   * @param encoding     Persisted schema encoding label.
   * @return Best-effort resolved schema family, or @c SchemaType::kUnknown.
   */
  [[nodiscard]] static SchemaType resolve_type(SchemaType schema_type, std::string_view ser_type = {},
                                               std::string_view encoding = {}) noexcept;
};

/**
 * @struct Version
 * @brief Semantic version number with comparison and string conversion utilities.
 *
 * @details
 * Used by @c NodeImpl::check_version() to compare the compile-time VLink version
 * against the runtime library version.  All fields default to @c -1 (invalid).
 */
struct VLINK_EXPORT Version final {
  int major{-1};  ///< Major version number; -1 if not set.
  int minor{-1};  ///< Minor version number; -1 if not set.
  int patch{-1};  ///< Patch version number; -1 if not set.

  /**
   * @brief Returns @c true when both versions are identical.
   *
   * @param target  Version to compare against.
   * @return        @c true if major, minor, and patch are all equal.
   */
  [[nodiscard]] bool operator==(const Version& target) const noexcept;

  /**
   * @brief Returns @c true when versions differ.
   *
   * @param target  Version to compare against.
   * @return        Logical negation of @c operator==.
   */
  [[nodiscard]] bool operator!=(const Version& target) const noexcept;

  /**
   * @brief Returns @c true when this version is older than @p target.
   *
   * @details
   * Compares major first, then minor, then patch (numeric order).
   *
   * @param target  Version to compare against.
   * @return        @c true if this version is strictly less than @p target.
   */
  [[nodiscard]] bool operator<(const Version& target) const noexcept;

  /**
   * @brief Returns @c true when this version is newer than @p target.
   *
   * @param target  Version to compare against.
   * @return        @c true if this version is strictly greater than @p target.
   */
  [[nodiscard]] bool operator>(const Version& target) const noexcept;

  /**
   * @brief Parses a version string in @c "major.minor.patch" format.
   *
   * @details
   * Uses @c std::from_chars for each component; components missing from the
   * string remain @c -1.
   *
   * @param version_str  String such as @c "2.1.0".
   * @return             Parsed @c Version; any missing component stays @c -1.
   */
  [[nodiscard]] static Version from_string(const std::string& version_str) noexcept;

  /**
   * @brief Converts this version to a @c "major.minor.patch" string.
   *
   * @return Formatted version string, e.g. @c "2.1.0".
   */
  [[nodiscard]] std::string to_string() const noexcept;

  /**
   * @brief Returns @c true when all three components are non-negative.
   *
   * @return @c true if the version was successfully parsed or explicitly set.
   */
  [[nodiscard]] bool is_valid() const noexcept;
};

////////////////////////////////////////////////////////////////
/// Details
////////////////////////////////////////////////////////////////

constexpr SchemaType SchemaData::infer_ser_type(std::string_view ser_type) noexcept {
  constexpr auto kHasPrefixFunction = [](std::string_view value, std::string_view prefix) noexcept {
    if (prefix.size() > value.size()) {
      return false;
    }

    for (size_t i = 0; i < prefix.size(); ++i) {
      if (value[i] != prefix[i]) {
        return false;
      }
    }

    return true;
  };

  if (kHasPrefixFunction(ser_type, "vlink::zerocopy::")) {
    return SchemaType::kZeroCopy;
  }

  if (ser_type == "raw" || ser_type == "string" || ser_type == "std::string" || ser_type == "text" ||
      ser_type == "json" || ser_type == "application/json" || ser_type == "text/json") {
    return SchemaType::kRaw;
  }

  return SchemaType::kUnknown;
}

}  // namespace vlink
