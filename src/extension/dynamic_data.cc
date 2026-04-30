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

#include "./extension/dynamic_data.h"

#include <string_view>

namespace vlink {

// DynamicData
DynamicData::DynamicData() = default;

const Bytes& DynamicData::get_data() const { return data_; }

const std::string_view& DynamicData::get_type() const { return type_; }

bool DynamicData::is_empty() const { return data_.empty(); }

bool DynamicData::operator==(const DynamicData& target) const { return data_ == target.data_ && type_ == target.type_; }

bool DynamicData::operator!=(const DynamicData& target) const { return !(*this == target); }

bool DynamicData::operator<<(const Bytes& bytes) noexcept {
  if VUNLIKELY (bytes.size() < get_offset()) {
    return false;
  }

  data_ = Bytes::deep_copy(bytes.data() + get_offset(), bytes.size() - get_offset(), get_offset());

  std::memcpy(data_.real_data(), bytes.data(), get_offset());

  const char* type_s = reinterpret_cast<const char*>(data_.real_data());

  const auto* type_end = static_cast<const char*>(std::memchr(type_s, '\0', get_offset()));

  if VUNLIKELY (type_end == nullptr) {
    return false;
  }

  type_ = std::string_view(type_s, static_cast<size_t>(type_end - type_s));

  return true;
}

bool DynamicData::operator>>(Bytes& bytes) const noexcept {
  if VUNLIKELY (data_.empty() || data_.offset() == 0 || !data_.is_owner()) {
    return false;
  }

  bytes = Bytes::shallow_copy(data_.real_data(), data_.real_size());

  return true;
}

}  // namespace vlink
