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

#include "./zerocopy/point_cloud.h"

#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace vlink {

namespace zerocopy {

// PointCloud::Vector3f
PointCloud::Vector3f::Vector3f() noexcept {
#if defined(__arm__) || defined(__x86__) || defined(__i386__)
#ifndef __ANDROID__
#warning "[PointCloud::Vector3f] No support for 32-bit architecture."
#endif
#else
  static_assert(sizeof(Vector3f) == 12, "Sizeof must be 12 bytes.");
#endif
}

PointCloud::Vector3f::Vector3f(float _x, float _y, float _z) noexcept : x(_x), y(_y), z(_z) {}

std::ostream& operator<<(std::ostream& ostream, const PointCloud::Vector3f& v3f) noexcept {
  ostream << "(" << v3f.x << ", " << v3f.y << ", " << v3f.z << ")";

  return ostream;
}

PointCloud::Vector3d::Vector3d(double _x, double _y, double _z) noexcept : x(_x), y(_y), z(_z) {}

// PointCloud::Vector3d
PointCloud::Vector3d::Vector3d() noexcept {
#if defined(__arm__) || defined(__x86__) || defined(__i386__)
#ifndef __ANDROID__
#warning "[PointCloud::Vector3d] No support for 32-bit architecture."
#endif
#else
  static_assert(sizeof(Vector3d) == 24, "Sizeof must be 24 bytes.");
#endif
}

std::ostream& operator<<(std::ostream& ostream, const PointCloud::Vector3d& v3d) noexcept {
  ostream << "(" << v3d.x << ", " << v3d.y << ", " << v3d.z << ")";

  return ostream;
}

// PointCloud
PointCloud::PointCloud() noexcept {
#if defined(__arm__) || defined(__x86__) || defined(__i386__)
#ifndef __ANDROID__
#warning "[PointCloud] No support for 32-bit architecture."
#endif
#else
  static_assert(sizeof(PointCloud) == 256, "Sizeof must be 256 bytes.");
#endif
}

PointCloud::~PointCloud() noexcept {
  if (is_owner_ && data_ && capacity_ != 0) {
    Bytes::bytes_free(data_, capacity_);
  }
}

PointCloud::PointCloud(const PointCloud& target) noexcept { deep_copy(target); }

PointCloud::PointCloud(PointCloud&& target) noexcept { move_copy(target); }

PointCloud& PointCloud::operator=(const PointCloud& target) noexcept {
  if VUNLIKELY (this == &target) {
    return *this;
  }

  deep_copy(target);

  return *this;
}

PointCloud& PointCloud::operator=(PointCloud&& target) noexcept {
  if VUNLIKELY (this == &target) {
    return *this;
  }

  move_copy(target);

  return *this;
}

bool PointCloud::operator<<(const Bytes& bytes) noexcept {
  static constexpr size_t kMagicNumberBeginSize = sizeof(kMagicNumberBegin);
  // static constexpr size_t kMagicNumberEndSize = sizeof(kMagicNumberEnd);

  if VUNLIKELY (bytes.empty()) {
    return false;
  }

  if VUNLIKELY (!check_valid(bytes)) {
    return false;
  }

  if (is_owner_ && data_ && capacity_ != 0) {
    Bytes::bytes_free(data_, capacity_);
  }

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif

  auto* target_ptr = reinterpret_cast<uint8_t*>(this);

  std::memcpy(target_ptr, bytes.data() + kMagicNumberBeginSize, sizeof(PointCloud));

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

  is_owner_ = false;
  index_ = 0;

  if VUNLIKELY (bytes.size() != get_serialized_size()) {
    clear(true);

    return false;
  }

  data_ = const_cast<uint8_t*>(bytes.data() + kMagicNumberBeginSize + sizeof(PointCloud));

  capacity_ = 0;

  return true;
}

bool PointCloud::operator>>(Bytes& bytes) const noexcept {
  static constexpr size_t kMagicNumberBeginSize = sizeof(kMagicNumberBegin);
  static constexpr size_t kMagicNumberEndSize = sizeof(kMagicNumberEnd);

  if (bytes.empty() || bytes.size() != get_serialized_size()) {
    bytes = Bytes::create(get_serialized_size());
  }

  std::memcpy(bytes.data(), &kMagicNumberBegin, kMagicNumberBeginSize);

  // NOLINTNEXTLINE(bugprone-undefined-memory-manipulation)
  std::memcpy(bytes.data() + kMagicNumberBeginSize, this, sizeof(PointCloud));

  if VLIKELY (data_ != nullptr && size_ != 0 && pack_size_ != 0) {
    std::memcpy(bytes.data() + kMagicNumberBeginSize + sizeof(PointCloud), data_, size_ * pack_size_);
  }

  std::memcpy(bytes.data() + kMagicNumberBeginSize + sizeof(PointCloud) + (size_ * pack_size_), &kMagicNumberEnd,
              kMagicNumberEndSize);

  return true;
}

bool PointCloud::check_valid(const Bytes& bytes) noexcept {
  static constexpr size_t kMagicNumberBeginSize = sizeof(kMagicNumberBegin);
  static constexpr size_t kMagicNumberEndSize = sizeof(kMagicNumberEnd);

  if VUNLIKELY (bytes.size() < kMagicNumberBeginSize + sizeof(PointCloud) + kMagicNumberEndSize) {
    return false;
  }

  uint32_t check_magic = 0;

  std::memcpy(&check_magic, bytes.begin(), kMagicNumberBeginSize);

  if VUNLIKELY (check_magic != kMagicNumberBegin) {
    return false;
  }

  std::memcpy(&check_magic, bytes.end() - kMagicNumberEndSize, kMagicNumberEndSize);

  if VUNLIKELY (check_magic != kMagicNumberEnd) {
    return false;
  }

  return true;
}

bool PointCloud::is_valid() const noexcept { return data_ != nullptr && size_ != 0 && pack_size_ != 0; }

bool PointCloud::shallow_copy(const PointCloud& target) noexcept {
  if VUNLIKELY (this == &target) {
    return false;
  }

  if (is_owner_ && data_ && capacity_ != 0) {
    Bytes::bytes_free(data_, capacity_);
  }

  header = target.header;
  capacity_ = 0;
  size_ = target.size_;
  reserved_buf_ = target.reserved_buf_;
  pack_size_ = target.pack_size_;
  is_owner_ = false;
  index_ = target.index_;
  data_ = target.data_;

  protocol_.size_num = target.protocol_.size_num;
  protocol_.type_num = target.protocol_.type_num;
  std::memcpy(protocol_.names, target.protocol_.names, sizeof(protocol_.names));

  return true;
}

bool PointCloud::deep_copy(const PointCloud& target) noexcept {
  if VLIKELY (data_ && is_owner_ && target.data_ && capacity_ != 0 && capacity_ == target.size_ * target.pack_size_) {
    if VUNLIKELY (this == &target) {
      return false;
    }

    header = target.header;
    size_ = target.size_;
    reserved_buf_ = target.reserved_buf_;
    pack_size_ = target.pack_size_;
    index_ = target.index_;

    protocol_.size_num = target.protocol_.size_num;
    protocol_.type_num = target.protocol_.type_num;
    std::memcpy(protocol_.names, target.protocol_.names, sizeof(protocol_.names));

    std::memcpy(data_, target.data_, capacity_);

    return true;
  }

  if VUNLIKELY (!shallow_copy(target)) {
    return false;
  }

  if VLIKELY (data_ != nullptr && size_ != 0 && pack_size_ != 0) {
    capacity_ = size_ * pack_size_;

    data_ = Bytes::bytes_malloc(capacity_);

    std::memcpy(data_, target.data_, capacity_);

    is_owner_ = true;
  }

  return true;
}

bool PointCloud::move_copy(PointCloud& target) noexcept {
  if VUNLIKELY (!shallow_copy(target)) {
    return false;
  }

  is_owner_ = target.is_owner_;

  target.capacity_ = 0;
  target.size_ = 0;
  target.reserved_buf_ = 0;
  target.pack_size_ = 0;
  target.is_owner_ = false;
  target.index_ = 0;
  target.data_ = nullptr;

  target.protocol_.size_num = 0;
  target.protocol_.type_num = 0;
  std::memset(&target.protocol_.names, 0, sizeof(protocol_.names));

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif

  std::memset(&target.header, 0, sizeof(header));

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

  return true;
}

size_t PointCloud::get_serialized_size() const noexcept {
  static constexpr size_t kMagicNumberBeginSize = sizeof(kMagicNumberBegin);
  static constexpr size_t kMagicNumberEndSize = sizeof(kMagicNumberEnd);

  return kMagicNumberBeginSize + sizeof(PointCloud) + (size_ * pack_size_) + kMagicNumberEndSize;
}

PointCloud::KeyMap PointCloud::get_key_map(KeyList* key_list) const noexcept {
  KeyMap map;

  auto target_key_list = protocol_.get_key_list();

  uint16_t index = 0;

  for (const auto& key : target_key_list) {
    map.try_emplace(key.name, index);

    index += key.size;
  }

  if (key_list) {
    *key_list = std::move(target_key_list);
  }

  return map;
}

size_t PointCloud::size() const noexcept { return size_; }

size_t PointCloud::pack_size() const noexcept { return pack_size_; }

bool PointCloud::is_owner() const noexcept { return is_owner_; }

uint64_t PointCloud::get_protocol_size_num() const noexcept { return protocol_.size_num; }

uint64_t PointCloud::get_protocol_type_num() const noexcept { return protocol_.type_num; }

std::string PointCloud::get_protocol_size_str() const noexcept { return protocol_.get_size_for_print(); }

std::string PointCloud::get_protocol_name_str() const noexcept { return protocol_.names; }

std::string PointCloud::get_protocol_type_str() const noexcept { return protocol_.get_type_for_print(); }

const uint8_t* PointCloud::get_internal_data() const noexcept { return data_; }

size_t PointCloud::get_reserved_size() const noexcept {
  if (pack_size_ == 0) {
    return 0;
  }

  return capacity_ / pack_size_;
}

bool PointCloud::get_value_v3f(float& x, float& y, float& z, size_t loop_index) const noexcept {
  if VUNLIKELY (loop_index * pack_size_ + sizeof(float) * 3 > size_ * pack_size_) {
    return false;
  }

  std::memcpy(&x, data_ + (loop_index * pack_size_), sizeof(float));
  std::memcpy(&y, data_ + (loop_index * pack_size_) + sizeof(float), sizeof(float));
  std::memcpy(&z, data_ + (loop_index * pack_size_) + sizeof(float) + sizeof(float), sizeof(float));

  return true;
}

bool PointCloud::get_value_v3f(Vector3f& v3f, size_t loop_index) const noexcept {
  if VUNLIKELY ((loop_index * pack_size_) + (sizeof(float) * 3) > size_ * pack_size_) {
    return false;
  }

  std::memcpy(&v3f, data_ + (loop_index * pack_size_), sizeof(float) * 3);

  return true;
}

PointCloud::Vector3f PointCloud::get_value_v3f(size_t loop_index) const noexcept {
  PointCloud::Vector3f v3f;

  get_value_v3f(v3f, loop_index);

  return v3f;
}

bool PointCloud::get_value_v3d(double& x, double& y, double& z, size_t loop_index) const noexcept {
  if VUNLIKELY ((loop_index * pack_size_) + (sizeof(double) * 3) > size_ * pack_size_) {
    return false;
  }

  std::memcpy(&x, data_ + (loop_index * pack_size_), sizeof(double));
  std::memcpy(&y, data_ + (loop_index * pack_size_) + sizeof(double), sizeof(double));
  std::memcpy(&z, data_ + (loop_index * pack_size_) + sizeof(double) + sizeof(double), sizeof(double));

  return true;
}

bool PointCloud::get_value_v3d(Vector3d& v3d, size_t loop_index) const noexcept {
  if VUNLIKELY ((loop_index * pack_size_) + (sizeof(double) * 3) > size_ * pack_size_) {
    return false;
  }

  std::memcpy(&v3d, data_ + (loop_index * pack_size_), sizeof(double) * 3);

  return true;
}

PointCloud::Vector3d PointCloud::get_value_v3d(size_t loop_index) const noexcept {
  PointCloud::Vector3d v3d;

  get_value_v3d(v3d, loop_index);

  return v3d;
}

double PointCloud::get_value_for_double_float(size_t loop_index, uint16_t offset, uint8_t type) const noexcept {
  switch (type) {
    case kBoolType:
      return get_value<uint8_t>(loop_index, offset);
    case kInt8Type:
      return get_value<int8_t>(loop_index, offset);
    case kUint8Type:
      return get_value<uint8_t>(loop_index, offset);
    case kInt16Type:
      return get_value<int16_t>(loop_index, offset);
    case kUint16Type:
      return get_value<uint16_t>(loop_index, offset);
    case kInt32Type:
      return get_value<int32_t>(loop_index, offset);
    case kUint32Type:
      return get_value<uint32_t>(loop_index, offset);
    case kInt64Type:
      return get_value<int64_t>(loop_index, offset);
    case kUint64Type:
      return get_value<uint64_t>(loop_index, offset);
    case kFloatType:
      return get_value<float>(loop_index, offset);
    case kDoubleType:
      return get_value<double>(loop_index, offset);
    default:
      return 0;
  }
}

double PointCloud::get_value_for_double_float(size_t loop_index, KeyMap& key_map, std::string_view key,
                                              uint8_t type) const noexcept {
  switch (type) {
    case kBoolType:
      return get_value<uint8_t>(loop_index, key_map, key);
    case kInt8Type:
      return get_value<int8_t>(loop_index, key_map, key);
    case kUint8Type:
      return get_value<uint8_t>(loop_index, key_map, key);
    case kInt16Type:
      return get_value<int16_t>(loop_index, key_map, key);
    case kUint16Type:
      return get_value<uint16_t>(loop_index, key_map, key);
    case kInt32Type:
      return get_value<int32_t>(loop_index, key_map, key);
    case kUint32Type:
      return get_value<uint32_t>(loop_index, key_map, key);
    case kInt64Type:
      return get_value<int64_t>(loop_index, key_map, key);
    case kUint64Type:
      return get_value<uint64_t>(loop_index, key_map, key);
    case kFloatType:
      return get_value<float>(loop_index, key_map, key);
    case kDoubleType:
      return get_value<double>(loop_index, key_map, key);
    default:
      return 0;
  }
}

std::string PointCloud::get_value_for_print(size_t loop_index, uint16_t offset, uint8_t type) const noexcept {
  switch (type) {
    case kBoolType:
      return get_value<bool>(loop_index, offset) ? "true" : "false";
    case kInt8Type:
      return std::to_string(get_value<int8_t>(loop_index, offset));
    case kUint8Type:
      return std::to_string(get_value<uint8_t>(loop_index, offset));
    case kInt16Type:
      return std::to_string(get_value<int16_t>(loop_index, offset));
    case kUint16Type:
      return std::to_string(get_value<uint16_t>(loop_index, offset));
    case kInt32Type:
      return std::to_string(get_value<int32_t>(loop_index, offset));
    case kUint32Type:
      return std::to_string(get_value<uint32_t>(loop_index, offset));
    case kInt64Type:
      return std::to_string(get_value<int64_t>(loop_index, offset));
    case kUint64Type:
      return std::to_string(get_value<uint64_t>(loop_index, offset));
    case kFloatType:
      return std::to_string(get_value<float>(loop_index, offset));
    case kDoubleType:
      return std::to_string(get_value<double>(loop_index, offset));
    default:
      return std::string();
  }
}

std::string PointCloud::get_value_for_print(size_t loop_index, KeyMap& key_map, std::string_view key,
                                            uint8_t type) const noexcept {
  switch (type) {
    case kBoolType:
      return get_value<bool>(loop_index, key_map, key) ? "true" : "false";
    case kInt8Type:
      return std::to_string(get_value<int8_t>(loop_index, key_map, key));
    case kUint8Type:
      return std::to_string(get_value<uint8_t>(loop_index, key_map, key));
    case kInt16Type:
      return std::to_string(get_value<int16_t>(loop_index, key_map, key));
    case kUint16Type:
      return std::to_string(get_value<uint16_t>(loop_index, key_map, key));
    case kInt32Type:
      return std::to_string(get_value<int32_t>(loop_index, key_map, key));
    case kUint32Type:
      return std::to_string(get_value<uint32_t>(loop_index, key_map, key));
    case kInt64Type:
      return std::to_string(get_value<int64_t>(loop_index, key_map, key));
    case kUint64Type:
      return std::to_string(get_value<uint64_t>(loop_index, key_map, key));
    case kFloatType:
      return std::to_string(get_value<float>(loop_index, key_map, key));
    case kDoubleType:
      return std::to_string(get_value<double>(loop_index, key_map, key));
    default:
      return std::string();
  }
}

bool PointCloud::create(size_t size, uint64_t size_num, uint64_t type_num, std::string_view key_str) noexcept {
  if VUNLIKELY (!Protocol::check_valid(size_num, key_str)) {
    return false;
  }

  if (is_owner_ && data_ && capacity_ != 0) {
    Bytes::bytes_free(data_, capacity_);
  }

  protocol_.size_num = size_num;
  std::memset(protocol_.names, 0, sizeof(protocol_.names));
  std::memcpy(protocol_.names, key_str.data(), key_str.size());
  protocol_.type_num = type_num;

  size_ = 0;
  pack_size_ = protocol_.get_pack_size();
  capacity_ = size * pack_size_;

  if VLIKELY (capacity_ != 0) {
    data_ = Bytes::bytes_malloc(capacity_);
    is_owner_ = true;
  }

  index_ = 0;

  return true;
}

void PointCloud::clear(bool force) noexcept {
  if (force) {
    if (is_owner_ && data_ && capacity_ != 0) {
      Bytes::bytes_free(data_, capacity_);
    }

    pack_size_ = 0;
    capacity_ = 0;
    data_ = nullptr;
    reserved_buf_ = 0;
    is_owner_ = false;

    protocol_.size_num = 0;
    std::memset(protocol_.names, 0, sizeof(protocol_.names));
    protocol_.type_num = 0;

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif

    std::memset(&header, 0, sizeof(header));

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
  }

  size_ = 0;
  index_ = 0;
}

// PointCloud::Protocol
bool PointCloud::Protocol::check_valid(uint64_t _size_num, std::string_view _names) noexcept {
  if VUNLIKELY (_size_num == 0 || _names.empty() || _names.size() > sizeof(names)) {
    return false;
  }

  uint16_t num_count = 0;
  uint16_t key_count = 0;

  do {
    num_count++;
    _size_num >>= 4;
  } while (_size_num != 0);

  for (auto c : _names) {
    if (c == ',') {
      ++key_count;
    }
  }

  if (!_names.empty()) {
    ++key_count;
  }

  if VUNLIKELY (key_count != num_count || key_count < 3 || key_count > 16) {
    return false;
  }

  return true;
}

std::string PointCloud::Protocol::get_names(const std::vector<std::string>& keys) noexcept {
  std::string key_str;

  for (const auto& key : keys) {
    if (!key_str.empty()) {
      key_str += ',';
    }

    key_str += key;
  }

  if VUNLIKELY (key_str.size() > sizeof(names)) {
    return std::string();
  }

  return key_str;
}

PointCloud::KeyList PointCloud::Protocol::get_key_list() const noexcept {
  KeyList key_list;
  key_list.reserve(16);

  uint64_t temp_size_num = size_num;
  uint64_t temp_type_num = type_num;

  std::istringstream iss(names);
  std::string token;

  bool leading_zero = true;

  for (int i = 15; i >= 0; --i) {
    uint8_t size_nibble = (temp_size_num >> (i * 4)) & 0xF;
    uint8_t type_nibble = (temp_type_num >> (i * 4)) & 0xF;

    if VUNLIKELY (leading_zero && size_nibble == 0) {
      continue;
    }

    leading_zero = false;

    if VUNLIKELY (!std::getline(iss, token, ',')) {
      break;
    }

    Key key;
    key.size = size_nibble;
    key.type = type_nibble;
    key.name = token;

    key_list.emplace_back(std::move(key));
  }

  return key_list;
}

std::string PointCloud::Protocol::get_size_for_print() const noexcept {
  bool leading_zero = true;

  uint64_t temp_size_num = size_num;

  std::string print_str;

  for (int i = 15; i >= 0; --i) {
    uint8_t size_nibble = (temp_size_num >> (i * 4)) & 0xF;

    if VUNLIKELY (leading_zero && size_nibble == 0) {
      continue;
    }

    leading_zero = false;

    if (!print_str.empty()) {
      print_str += ",";
    }

    print_str += std::to_string(size_nibble);
  }

  return print_str;
}

std::string PointCloud::Protocol::get_type_for_print() const noexcept {
  bool leading_zero = true;

  uint64_t temp_type_num = type_num;

  std::string print_str;

  for (int i = 15; i >= 0; --i) {
    uint8_t type_nibble = (temp_type_num >> (i * 4)) & 0xF;

    if VUNLIKELY (leading_zero && type_nibble == 0) {
      continue;
    }

    leading_zero = false;

    if (!print_str.empty()) {
      print_str += ",";
    }

    switch (type_nibble) {
      case kBoolType:
        print_str += "bool";
        break;
      case kInt8Type:
        print_str += "int8";
        break;
      case kUint8Type:
        print_str += "uint8";
        break;
      case kInt16Type:
        print_str += "int16";
        break;
      case kUint16Type:
        print_str += "uint16";
        break;
      case kInt32Type:
        print_str += "int32";
        break;
      case kUint32Type:
        print_str += "uint32";
        break;
      case kInt64Type:
        print_str += "int64";
        break;
      case kUint64Type:
        print_str += "uint64";
        break;
      case kFloatType:
        print_str += "float";
        break;
      case kDoubleType:
        print_str += "double";
        break;
      default:
        break;
    }
  }

  return print_str;
}

}  // namespace zerocopy

}  // namespace vlink
