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

struct MemoryFreeNode final {
  MemoryFreeNode* next{nullptr};
};

struct MemoryChunk final {
  void* ptr{nullptr};
  size_t bytes{0};
};

// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
struct alignas(64) MemoryTierState final {
  size_t max_size{0};
  size_t block_size{0};
  size_t blocks_per_chunk{0};
  size_t next_chunk_blocks{0};

  MemoryFreeNode* free_list_head{nullptr};
  std::vector<MemoryChunk> chunks;

  alignas(64) SpinLock mtx;
  alignas(64) std::atomic<uint64_t> hit_count{0};
  alignas(64) std::atomic<uint64_t> deallocate_count{0};

  alignas(64) std::atomic<uint64_t> chunk_count{0};
  std::atomic<uint64_t> upstream_alloc_count{0};
  std::atomic<uint64_t> upstream_alloc_bytes{0};
};

struct alignas(64) MemoryAllocCounters final {
  std::atomic<uint64_t> count{0};
  std::atomic<uint64_t> bytes{0};
};

static constexpr size_t round_up(size_t value, size_t alignment) noexcept {
  return (value + alignment - 1U) & ~(alignment - 1U);
}

static constexpr bool default_tier_table_well_formed() noexcept {
  for (const auto& row : kDefaultTierTable) {
    for (size_t t = 0; t < kMaxTierCount; ++t) {
      const auto& tier = row[t];

      if (tier.max_size == 0U) {
        return false;
      }

      if (tier.max_size < sizeof(MemoryFreeNode)) {
        return false;
      }

      if (tier.blocks_per_chunk == 0U) {
        return false;
      }

      if (t > 0U && tier.max_size <= row[t - 1U].max_size) {
        return false;
      }
    }
  }

  return true;
}

static_assert(default_tier_table_well_formed(),
              "MemoryPool: kDefaultTierTable contains a malformed row "
              "(zero field, undersized tier, or non-monotonic max_size)");

static bool grow_tier_chunk(MemoryTierState& state) noexcept {
  size_t blocks = state.next_chunk_blocks;

  if VUNLIKELY (blocks > state.blocks_per_chunk) {
    blocks = state.blocks_per_chunk;
  }

  const size_t block_size = state.block_size;
  const size_t chunk_bytes = block_size * blocks;

  if VUNLIKELY (chunk_bytes / block_size != blocks) {
    return false;
  }

  state.mtx.unlock();

  void* ptr = ::operator new(chunk_bytes, std::align_val_t{MemoryPool::kBlockAlignment}, std::nothrow);

  if VUNLIKELY (ptr == nullptr) {
    state.mtx.lock();
    return false;
  }

  state.upstream_alloc_count.fetch_add(1, std::memory_order_relaxed);
  state.upstream_alloc_bytes.fetch_add(chunk_bytes, std::memory_order_relaxed);

  auto* base = static_cast<std::byte*>(ptr);
  MemoryFreeNode* local_head = nullptr;

  for (size_t i = blocks; i > 0; --i) {
    auto* node = reinterpret_cast<MemoryFreeNode*>(base + (i - 1U) * block_size);

    node->next = local_head;
    local_head = node;
  }

  state.mtx.lock();

  const auto advance_next_chunk_blocks = [&]() noexcept {
    const size_t doubled = blocks * 2U;
    const size_t target = (doubled < blocks || doubled > state.blocks_per_chunk) ? state.blocks_per_chunk : doubled;

    if (target > state.next_chunk_blocks) {
      state.next_chunk_blocks = target;
    }
  };

  if VUNLIKELY (state.free_list_head != nullptr) {
    state.mtx.unlock();
    ::operator delete(ptr, chunk_bytes, std::align_val_t{MemoryPool::kBlockAlignment});
    state.mtx.lock();

    advance_next_chunk_blocks();

    return true;
  }

  try {
    state.chunks.push_back(MemoryChunk{ptr, chunk_bytes});
  } catch (std::exception&) {
    state.mtx.unlock();
    ::operator delete(ptr, chunk_bytes, std::align_val_t{MemoryPool::kBlockAlignment});
    state.mtx.lock();

    return false;
  }

  state.chunk_count.fetch_add(1, std::memory_order_relaxed);

  advance_next_chunk_blocks();

  state.free_list_head = local_head;

  return true;
}

static void* tier_allocate(MemoryTierState& state) noexcept {
  SpinLockGuard lock(state.mtx);

  while (state.free_list_head == nullptr) {
    if VUNLIKELY (!grow_tier_chunk(state)) {
      return nullptr;
    }
  }

  MemoryFreeNode* node = state.free_list_head;
  state.free_list_head = node->next;

  return node;
}

static void tier_deallocate(MemoryTierState& state, void* p) noexcept {
  SpinLockGuard lock(state.mtx);

  auto* node = static_cast<MemoryFreeNode*>(p);
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

    if VUNLIKELY (tiers[i].max_size < sizeof(MemoryFreeNode)) {
      CLOG_E(
          "MemoryPool: tier %zu max_size (%zu) is below the minimum block size %zu; "
          "falling back to default pyramid.",
          i, tiers[i].max_size, sizeof(MemoryFreeNode));
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

// MemoryPool::Impl
struct MemoryPool::Impl final {  // NOLINT(clang-analyzer-optin.performance.Padding)
  alignas(64) size_t tier_max_sizes[kMaxTierCount]{};
  MemoryTierState* tier_states[kMaxTierCount]{};
  size_t tier_count{0};

  std::vector<std::unique_ptr<MemoryTierState>> owned_states;

  MemoryAllocCounters oversized_alloc;
  alignas(64) std::atomic<uint64_t> oversized_dealloc_count{0};
};

// MemoryPool
MemoryPool::MemoryPool(const std::vector<Tier>& tiers) : impl_(std::make_unique<Impl>()) {
  const bool use_caller = !tiers.empty() && validate_tiers_log(tiers);

  std::vector<Tier> fallback;

  if VUNLIKELY (!use_caller) {
    const auto& row = kDefaultTierTable[kDefaultMemoryLevel - kMinMemoryLevel];
    fallback.assign(row, row + kMaxTierCount);
  }

  const std::vector<Tier>& source = use_caller ? tiers : fallback;

  impl_->tier_count = source.size();
  impl_->owned_states.reserve(source.size());

  for (size_t i = 0; i < source.size(); ++i) {
    const auto& cfg = source[i];

    auto state = std::make_unique<MemoryTierState>();
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
    for (const MemoryChunk& chunk : state->chunks) {
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

    impl_->oversized_alloc.count.fetch_add(1, std::memory_order_relaxed);
    impl_->oversized_alloc.bytes.fetch_add(bytes, std::memory_order_relaxed);

    return p;
  }

  MemoryTierState& state = *impl_->tier_states[idx];
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

  MemoryTierState& state = *impl_->tier_states[idx];
  tier_deallocate(state, p);
  state.deallocate_count.fetch_add(1, std::memory_order_relaxed);
}

size_t MemoryPool::get_tier_count() const noexcept { return impl_->tier_count; }

std::vector<MemoryPool::TierStats> MemoryPool::get_stats() const noexcept {
  const size_t count = impl_->tier_count;

  std::vector<TierStats> result;
  result.reserve(count);

  for (size_t i = 0; i < count; ++i) {
    const MemoryTierState& state = *impl_->tier_states[i];
    const uint64_t hits = state.hit_count.load(std::memory_order_relaxed);
    const uint64_t deallocs = state.deallocate_count.load(std::memory_order_relaxed);

    TierStats item;
    item.max_size = state.max_size;
    item.blocks_per_chunk = state.blocks_per_chunk;
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

  result.alloc_count = impl_->oversized_alloc.count.load(std::memory_order_relaxed);
  result.alloc_bytes = impl_->oversized_alloc.bytes.load(std::memory_order_relaxed);
  result.dealloc_count = impl_->oversized_dealloc_count.load(std::memory_order_relaxed);

  return result;
}

void MemoryPool::reset_stats() noexcept {
  const size_t count = impl_->tier_count;

  for (size_t i = 0; i < count; ++i) {
    MemoryTierState& state = *impl_->tier_states[i];
    state.hit_count.store(0, std::memory_order_relaxed);
    state.deallocate_count.store(0, std::memory_order_relaxed);
  }

  impl_->oversized_alloc.count.store(0, std::memory_order_relaxed);
  impl_->oversized_alloc.bytes.store(0, std::memory_order_relaxed);
  impl_->oversized_dealloc_count.store(0, std::memory_order_relaxed);
}

void MemoryPool::clear() noexcept {
  constexpr size_t kStackSlots = 64U;

  for (auto& state : impl_->owned_states) {
    size_t stack_free_counts[kStackSlots] = {};
    MemoryChunk stack_to_delete[kStackSlots];

    std::vector<size_t> heap_free_counts;

    MemoryChunk* to_delete = nullptr;
    size_t to_delete_count = 0U;

    {
      SpinLockGuard lock(state->mtx);

      const size_t chunk_count = state->chunks.size();

      if VUNLIKELY (chunk_count == 0U) {
        continue;
      }

      size_t* free_counts = stack_free_counts;
      const bool spill_to_heap = (chunk_count > kStackSlots);

      if VUNLIKELY (spill_to_heap) {
        try {
          heap_free_counts.assign(chunk_count, 0U);
        } catch (std::exception&) {
          continue;
        }

        free_counts = heap_free_counts.data();
      }

      const size_t block_size = state->block_size;

      const auto in_chunk = [&](std::uintptr_t addr, size_t i) noexcept {
        const auto chunk_start = reinterpret_cast<std::uintptr_t>(state->chunks[i].ptr);
        const auto chunk_end = chunk_start + state->chunks[i].bytes;

        return addr >= chunk_start && addr < chunk_end;
      };

      for (MemoryFreeNode* node = state->free_list_head; node != nullptr; node = node->next) {
        const auto node_addr = reinterpret_cast<std::uintptr_t>(node);

        for (size_t i = 0; i < chunk_count; ++i) {
          if (in_chunk(node_addr, i)) {
            ++free_counts[i];
            break;
          }
        }
      }

      MemoryFreeNode* new_head = nullptr;
      MemoryFreeNode* current = state->free_list_head;

      while (current != nullptr) {
        MemoryFreeNode* next = current->next;
        const auto current_addr = reinterpret_cast<std::uintptr_t>(current);

        bool keep = false;

        for (size_t i = 0; i < chunk_count; ++i) {
          if (in_chunk(current_addr, i)) {
            const size_t total_blocks = state->chunks[i].bytes / block_size;
            keep = (free_counts[i] != total_blocks);

            break;
          }
        }

        if (keep) {
          current->next = new_head;
          new_head = current;
        }

        current = next;
      }

      state->free_list_head = new_head;

      size_t released = 0U;
      size_t write = 0U;

      for (size_t read = 0U; read < chunk_count; ++read) {
        const size_t total_blocks = state->chunks[read].bytes / block_size;

        if (free_counts[read] == total_blocks) {
          if VLIKELY (!spill_to_heap) {
            stack_to_delete[released] = state->chunks[read];
          } else {
            ::operator delete(state->chunks[read].ptr, state->chunks[read].bytes, std::align_val_t{kBlockAlignment});
          }

          ++released;
        } else {
          if (read != write) {
            state->chunks[write] = state->chunks[read];
          }

          ++write;
        }
      }

      if VLIKELY (released > 0U) {
        state->chunks.resize(write);
        state->chunk_count.fetch_sub(released, std::memory_order_relaxed);
      }

      if VLIKELY (!spill_to_heap) {
        to_delete = stack_to_delete;
        to_delete_count = released;
      }
    }

    for (size_t i = 0; i < to_delete_count; ++i) {
      ::operator delete(to_delete[i].ptr, to_delete[i].bytes, std::align_val_t{kBlockAlignment});
    }
  }
}

void MemoryPool::trim() noexcept { clear(); }

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
