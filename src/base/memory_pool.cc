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

#include "./base/memory_pool.h"

#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "./base/logger.h"
#include "./base/spin_lock.h"
#include "./base/utils.h"

namespace vlink {

static constexpr int kMinMemoryLevel = 1;
static constexpr int kMaxMemoryLevel = 6;
static constexpr int kDefaultMemoryLevel = 3;
static constexpr size_t kMaxTierCount = 8U;
static constexpr size_t kMaxLevelCount = 6U;
static constexpr size_t kInitialBlocksPerChunk = 1U;
static constexpr size_t kInitialChunksReserve = 16U;

static constexpr MemoryPool::Tier kDefaultTierTable[kMaxLevelCount][kMaxTierCount] = {
    // L1
    {
        {128U, 32U},
        {512U, 16U},
        {2U * 1024U, 8U},
        {8U * 1024U, 4U},
        {64U * 1024U, 2U},
        {512U * 1024U, 1U},
        {4U * 1024U * 1024U, 1U},
        {12U * 1024U * 1024U, 1U},
    },
    // L2
    {
        {128U, 64U},
        {512U, 32U},
        {2U * 1024U, 16U},
        {8U * 1024U, 8U},
        {64U * 1024U, 4U},
        {512U * 1024U, 2U},
        {4U * 1024U * 1024U, 1U},
        {12U * 1024U * 1024U, 1U},
    },
    // L3
    {
        {128U, 128U},
        {512U, 64U},
        {2U * 1024U, 32U},
        {8U * 1024U, 16U},
        {64U * 1024U, 8U},
        {512U * 1024U, 4U},
        {4U * 1024U * 1024U, 2U},
        {12U * 1024U * 1024U, 1U},
    },
    // L4
    {
        {128U, 256U},
        {512U, 128U},
        {2U * 1024U, 64U},
        {8U * 1024U, 32U},
        {64U * 1024U, 16U},
        {512U * 1024U, 8U},
        {4U * 1024U * 1024U, 4U},
        {12U * 1024U * 1024U, 1U},
    },
    // L5
    {
        {128U, 512U},
        {512U, 256U},
        {2U * 1024U, 128U},
        {8U * 1024U, 64U},
        {64U * 1024U, 32U},
        {512U * 1024U, 16U},
        {4U * 1024U * 1024U, 8U},
        {12U * 1024U * 1024U, 2U},
    },
    // L6
    {
        {128U, 512U},
        {512U, 256U},
        {2U * 1024U, 256U},
        {8U * 1024U, 128U},
        {64U * 1024U, 64U},
        {512U * 1024U, 32U},
        {4U * 1024U * 1024U, 16U},
        {12U * 1024U * 1024U, 4U},
    },
};

struct FreeNode final {
  FreeNode* next{nullptr};
};

struct Chunk final {
  void* ptr{nullptr};
  size_t bytes{0};
};

// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
struct alignas(64) TierState final {
  size_t max_size{0};
  size_t block_size{0};
  size_t blocks_per_chunk{0};
  size_t next_chunk_blocks{0};

  FreeNode* free_list_head{nullptr};
  std::vector<Chunk> chunks;

  alignas(64) SpinLock mtx;
  alignas(64) std::atomic<uint64_t> hit_count{0};
  alignas(64) std::atomic<uint64_t> deallocate_count{0};

  alignas(64) std::atomic<uint64_t> chunk_count{0};
  std::atomic<uint64_t> upstream_alloc_count{0};
  std::atomic<uint64_t> upstream_alloc_bytes{0};
};

static constexpr size_t round_up(size_t value, size_t alignment) noexcept {
  return (value + alignment - 1U) & ~(alignment - 1U);
}

static bool grow_tier_chunk(TierState& state, SpinLock& mtx) noexcept {
  size_t blocks = state.next_chunk_blocks;

  if VUNLIKELY (blocks > state.blocks_per_chunk) {
    blocks = state.blocks_per_chunk;
  }

  const size_t block_size = state.block_size;
  const size_t chunk_bytes = block_size * blocks;

  if VUNLIKELY (chunk_bytes / block_size != blocks) {
    return false;
  }

  mtx.unlock();

  void* ptr = ::operator new(chunk_bytes, std::align_val_t{MemoryPool::kBlockAlignment}, std::nothrow);

  if VUNLIKELY (ptr == nullptr) {
    mtx.lock();
    return false;
  }

  auto* base = static_cast<std::byte*>(ptr);
  FreeNode* local_head = nullptr;

  for (size_t i = blocks; i > 0; --i) {
    auto* node = reinterpret_cast<FreeNode*>(base + (i - 1U) * block_size);
    node->next = local_head;
    local_head = node;
  }

  mtx.lock();

  if VUNLIKELY (state.free_list_head != nullptr) {
    mtx.unlock();
    ::operator delete(ptr, chunk_bytes, std::align_val_t{MemoryPool::kBlockAlignment});
    mtx.lock();
    return true;
  }

  try {
    state.chunks.push_back(Chunk{ptr, chunk_bytes});
  } catch (std::exception&) {
    mtx.unlock();
    ::operator delete(ptr, chunk_bytes, std::align_val_t{MemoryPool::kBlockAlignment});
    mtx.lock();
    return false;
  }

  state.chunk_count.fetch_add(1, std::memory_order_relaxed);
  state.upstream_alloc_count.fetch_add(1, std::memory_order_relaxed);
  state.upstream_alloc_bytes.fetch_add(chunk_bytes, std::memory_order_relaxed);

  const size_t doubled = blocks * 2U;
  state.next_chunk_blocks = (doubled < blocks || doubled > state.blocks_per_chunk) ? state.blocks_per_chunk : doubled;

  state.free_list_head = local_head;
  return true;
}

static void* tier_allocate(TierState& state) noexcept {
  SpinLockGuard lock(state.mtx);

  while (state.free_list_head == nullptr) {
    if VUNLIKELY (!grow_tier_chunk(state, state.mtx)) {
      return nullptr;
    }
  }

  FreeNode* node = state.free_list_head;
  state.free_list_head = node->next;

  return node;
}

static void tier_deallocate(TierState& state, void* p) noexcept {
  SpinLockGuard lock(state.mtx);

  auto* node = static_cast<FreeNode*>(p);
  node->next = state.free_list_head;
  state.free_list_head = node;
}

static bool validate_tiers_log(const std::vector<MemoryPool::Tier>& tiers) noexcept {
  static constexpr size_t kMaxTierSize = SIZE_MAX - MemoryPool::kBlockAlignment + 1U;

  if VUNLIKELY (tiers.size() > kMaxTierCount) {
    CLOG_E("MemoryPool: tier count %zu exceeds max %zu; falling back to default pyramid.", tiers.size(), kMaxTierCount);
    return false;
  }

  for (size_t i = 0; i < tiers.size(); ++i) {
    if VUNLIKELY (tiers[i].max_size == 0) {
      CLOG_E("MemoryPool: tier %zu has max_size == 0; falling back to default pyramid.", i);
      return false;
    }

    if VUNLIKELY (tiers[i].max_size < sizeof(FreeNode)) {
      CLOG_E(
          "MemoryPool: tier %zu max_size (%zu) is below the minimum block size %zu; "
          "falling back to default pyramid.",
          i, tiers[i].max_size, sizeof(FreeNode));
      return false;
    }

    if VUNLIKELY (tiers[i].max_size > kMaxTierSize) {
      CLOG_E("MemoryPool: tier %zu max_size overflows after alignment rounding; falling back.", i);
      return false;
    }

    if VUNLIKELY (tiers[i].blocks_per_chunk == 0) {
      CLOG_E("MemoryPool: tier %zu has blocks_per_chunk == 0; falling back to default pyramid.", i);
      return false;
    }

    if VUNLIKELY (i > 0 && tiers[i].max_size <= tiers[i - 1].max_size) {
      CLOG_E("MemoryPool: tier %zu max_size is not strictly increasing; falling back to default pyramid.", i);
      return false;
    }
  }

  return true;
}

// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
struct MemoryPool::Impl final {
  alignas(64) size_t tier_max_sizes[kMaxTierCount]{};
  TierState* tier_states[kMaxTierCount]{};
  size_t tier_count{0};

  std::vector<Tier> tiers;
  std::vector<std::unique_ptr<TierState>> owned_states;

  alignas(64) std::atomic<uint64_t> oversized_alloc_count{0};
  alignas(64) std::atomic<uint64_t> oversized_alloc_bytes{0};
  alignas(64) std::atomic<uint64_t> oversized_dealloc_count{0};
};

MemoryPool::MemoryPool(const std::vector<Tier>& tiers) : impl_(std::make_unique<Impl>()) {
  const bool use_caller = !tiers.empty() && validate_tiers_log(tiers);

  std::vector<Tier> fallback;

  if VUNLIKELY (!use_caller) {
    const auto& row = kDefaultTierTable[kDefaultMemoryLevel - kMinMemoryLevel];
    fallback.assign(row, row + kMaxTierCount);
  }

  const std::vector<Tier>& source = use_caller ? tiers : fallback;

  impl_->tiers = source;
  impl_->tier_count = source.size();
  impl_->owned_states.reserve(source.size());

  for (size_t i = 0; i < source.size(); ++i) {
    const auto& cfg = source[i];

    auto state = std::make_unique<TierState>();
    state->max_size = cfg.max_size;
    state->blocks_per_chunk = cfg.blocks_per_chunk;
    state->next_chunk_blocks = kInitialBlocksPerChunk;
    state->chunks.reserve(kInitialChunksReserve);
    state->block_size = round_up(cfg.max_size, kBlockAlignment);

    impl_->tier_max_sizes[i] = cfg.max_size;
    impl_->tier_states[i] = state.get();
    impl_->owned_states.emplace_back(std::move(state));
  }
}

MemoryPool::~MemoryPool() {
  for (auto& state : impl_->owned_states) {
    for (const Chunk& chunk : state->chunks) {
      ::operator delete(chunk.ptr, chunk.bytes, std::align_val_t{kBlockAlignment});
    }

    state->chunks.clear();
    state->free_list_head = nullptr;
  }
}

void* MemoryPool::allocate(size_t bytes, size_t alignment) noexcept {
  const size_t idx = find_tier(bytes);

  if VUNLIKELY (idx == kMaxTierCount || alignment > kBlockAlignment) {
    void* p = ::operator new(bytes, std::align_val_t{alignment}, std::nothrow);

    if VUNLIKELY (p == nullptr) {
      return nullptr;
    }

    impl_->oversized_alloc_count.fetch_add(1, std::memory_order_relaxed);
    impl_->oversized_alloc_bytes.fetch_add(bytes, std::memory_order_relaxed);

    return p;
  }

  TierState& state = *impl_->tier_states[idx];
  void* block = tier_allocate(state);

  if VUNLIKELY (block == nullptr) {
    return nullptr;
  }

  state.hit_count.fetch_add(1, std::memory_order_relaxed);

  return block;
}

void MemoryPool::deallocate(void* p, size_t bytes, size_t alignment) noexcept {
  if VUNLIKELY (p == nullptr) {
    return;
  }

  const size_t idx = find_tier(bytes);

  if VUNLIKELY (idx == kMaxTierCount || alignment > kBlockAlignment) {
    ::operator delete(p, bytes, std::align_val_t{alignment});
    impl_->oversized_dealloc_count.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  TierState& state = *impl_->tier_states[idx];
  state.deallocate_count.fetch_add(1, std::memory_order_relaxed);
  tier_deallocate(state, p);
}

size_t MemoryPool::get_tier_count() const noexcept { return impl_->tier_count; }

std::vector<MemoryPool::TierStats> MemoryPool::get_stats() const noexcept {
  const size_t count = impl_->tier_count;
  std::vector<TierStats> result;
  result.reserve(count);

  for (size_t i = 0; i < count; ++i) {
    const TierState& state = *impl_->tier_states[i];
    const uint64_t hits = state.hit_count.load(std::memory_order_relaxed);
    const uint64_t deallocs = state.deallocate_count.load(std::memory_order_relaxed);

    TierStats item;
    item.max_size = impl_->tiers[i].max_size;
    item.blocks_per_chunk = impl_->tiers[i].blocks_per_chunk;
    item.block_size = state.block_size;
    item.hit_count = hits;
    item.deallocate_count = deallocs;
    item.in_use_blocks = (hits >= deallocs) ? (hits - deallocs) : 0U;
    item.upstream_alloc_count = state.upstream_alloc_count.load(std::memory_order_relaxed);
    item.upstream_alloc_bytes = state.upstream_alloc_bytes.load(std::memory_order_relaxed);
    item.chunk_count = state.chunk_count.load(std::memory_order_relaxed);

    result.emplace_back(item);
  }

  return result;
}

MemoryPool::OversizedStats MemoryPool::get_oversized_stats() const noexcept {
  OversizedStats result;

  result.alloc_count = impl_->oversized_alloc_count.load(std::memory_order_relaxed);
  result.alloc_bytes = impl_->oversized_alloc_bytes.load(std::memory_order_relaxed);
  result.dealloc_count = impl_->oversized_dealloc_count.load(std::memory_order_relaxed);

  return result;
}

void MemoryPool::reset_stats() noexcept {
  const size_t count = impl_->tier_count;

  for (size_t i = 0; i < count; ++i) {
    TierState& state = *impl_->tier_states[i];
    state.hit_count.store(0, std::memory_order_relaxed);
    state.deallocate_count.store(0, std::memory_order_relaxed);
  }

  impl_->oversized_alloc_count.store(0, std::memory_order_relaxed);
  impl_->oversized_alloc_bytes.store(0, std::memory_order_relaxed);
  impl_->oversized_dealloc_count.store(0, std::memory_order_relaxed);
}

size_t MemoryPool::find_tier(size_t bytes) const noexcept {
  const size_t count = impl_->tier_count;
  const size_t* const sizes = impl_->tier_max_sizes;

  for (size_t i = 0; i < count; ++i) {
    if (bytes <= sizes[i]) {
      return i;
    }
  }

  return kMaxTierCount;
}

MemoryPool& MemoryPool::global_instance(bool use_env_level) {
  static MemoryPool instance(use_env_level ? default_tiers() : std::vector<Tier>{});

  return instance;
}

std::vector<MemoryPool::Tier> MemoryPool::default_tiers() {
  static int level = []() noexcept {
    const std::string env_value = Utils::get_env("VLINK_MEMORY_LEVEL", "3");
    int parsed = kDefaultMemoryLevel;
    const char* first = env_value.data();
    const char* last = first + env_value.size();
    auto [ptr, ec] = std::from_chars(first, last, parsed);

    if VUNLIKELY (ec != std::errc() || ptr != last) {
      CLOG_W("MemoryPool: VLINK_MEMORY_LEVEL=\"%s\" is not a valid integer, fallback to %d.", env_value.c_str(),
             kDefaultMemoryLevel);
      return kDefaultMemoryLevel;
    }
    if VUNLIKELY (parsed < kMinMemoryLevel || parsed > kMaxMemoryLevel) {
      CLOG_W("MemoryPool: VLINK_MEMORY_LEVEL=%d out of range [%d, %d], clamped.", parsed, kMinMemoryLevel,
             kMaxMemoryLevel);
      return parsed < kMinMemoryLevel ? kMinMemoryLevel : kMaxMemoryLevel;
    }
    return parsed;
  }();

  const auto& row = kDefaultTierTable[level - kMinMemoryLevel];
  return std::vector<Tier>(row, row + kMaxTierCount);
}

}  // namespace vlink
