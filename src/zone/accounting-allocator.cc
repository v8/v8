// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zone/accounting-allocator.h"

#include <cstdlib>

#if V8_LIBC_BIONIC
#include <malloc.h>  // NOLINT
#endif

#include "src/allocation.h"
#include "src/asan.h"
#include "src/msan.h"

namespace v8 {
namespace internal {

AccountingAllocator::AccountingAllocator() : unused_segments_mutex_() {
  static const size_t kDefaultBucketMaxSize = 5;

  std::fill(unused_segments_heads_, unused_segments_heads_ + kNumberBuckets,
            nullptr);
  std::fill(unused_segments_sizes_, unused_segments_sizes_ + kNumberBuckets, 0);
  std::fill(unused_segments_max_sizes_,
            unused_segments_max_sizes_ + kNumberBuckets, kDefaultBucketMaxSize);
}

AccountingAllocator::~AccountingAllocator() { ClearPool(); }

void AccountingAllocator::MemoryPressureNotification(
    MemoryPressureLevel level) {
  memory_pressure_level_.store(level);

  if (level != MemoryPressureLevel::kNone) {
    ClearPool();
  }
}

void AccountingAllocator::ConfigureSegmentPool(const size_t max_pool_size) {
  // The sum of the bytes of one segment of each size.
  static const size_t full_size = (size_t(1) << (kMaxSegmentSizePower + 1)) -
                                  (size_t(1) << kMinSegmentSizePower);
  size_t fits_fully = max_pool_size / full_size;

  base::MutexGuard lock_guard(&unused_segments_mutex_);

  // We assume few zones (less than 'fits_fully' many) to be active at the same
  // time. When zones grow regularly, they will keep requesting segments of
  // increasing size each time. Therefore we try to get as many segments with an
  // equal number of segments of each size as possible.
  // The remaining space is used to make more room for an 'incomplete set' of
  // segments beginning with the smaller ones.
  // This code will work best if the max_pool_size is a multiple of the
  // full_size. If max_pool_size is no sum of segment sizes the actual pool
  // size might be smaller then max_pool_size. Note that no actual memory gets
  // wasted though.
  // TODO(heimbuef): Determine better strategy generating a segment sizes
  // distribution that is closer to real/benchmark usecases and uses the given
  // max_pool_size more efficiently.
  size_t total_size = fits_fully * full_size;

  for (size_t power = 0; power < kNumberBuckets; ++power) {
    if (total_size + (size_t(1) << (power + kMinSegmentSizePower)) <=
        max_pool_size) {
      unused_segments_max_sizes_[power] = fits_fully + 1;
      total_size += size_t(1) << power;
    } else {
      unused_segments_max_sizes_[power] = fits_fully;
    }
  }
}

Segment* AccountingAllocator::GetSegment(size_t bytes) {
  Segment* result = GetSegmentFromPool(bytes);
  if (result == nullptr) {
    result = AllocateSegment(bytes);
    if (result != nullptr) {
      result->Initialize(bytes);
    }
  }

  return result;
}

Segment* AccountingAllocator::AllocateSegment(size_t bytes) {
  void* memory = AllocWithRetry(bytes);
  if (memory != nullptr) {
    size_t current =
        current_memory_usage_.fetch_add(bytes, std::memory_order_relaxed);
    size_t max = max_memory_usage_.load(std::memory_order_relaxed);
    while (current > max && !max_memory_usage_.compare_exchange_weak(
                                max, current, std::memory_order_relaxed)) {
      // {max} was updated by {compare_exchange_weak}; retry.
    }
  }
  return reinterpret_cast<Segment*>(memory);
}

void AccountingAllocator::ReturnSegment(Segment* segment) {
  segment->ZapContents();

  if (memory_pressure_level_.load() != MemoryPressureLevel::kNone) {
    FreeSegment(segment);
  } else if (!AddSegmentToPool(segment)) {
    FreeSegment(segment);
  }
}

void AccountingAllocator::FreeSegment(Segment* memory) {
  current_memory_usage_.fetch_sub(memory->size(), std::memory_order_relaxed);
  memory->ZapHeader();
  free(memory);
}

Segment* AccountingAllocator::GetSegmentFromPool(size_t requested_size) {
  if (requested_size > (1 << kMaxSegmentSizePower)) {
    return nullptr;
  }

  size_t power = kMinSegmentSizePower;
  while (requested_size > (static_cast<size_t>(1) << power)) power++;

  DCHECK_GE(power, kMinSegmentSizePower + 0);
  power -= kMinSegmentSizePower;

  Segment* segment;

  {
    base::MutexGuard lock_guard(&unused_segments_mutex_);

    segment = unused_segments_heads_[power];
    if (segment == nullptr) return nullptr;

    unused_segments_heads_[power] = segment->next();
    segment->set_next(nullptr);

    unused_segments_sizes_[power]--;
  }

  current_pool_size_.fetch_sub(segment->size(), std::memory_order_relaxed);
  ASAN_UNPOISON_MEMORY_REGION(reinterpret_cast<void*>(segment->start()),
                              segment->size());
  MSAN_ALLOCATED_UNINITIALIZED_MEMORY(segment->start(), segment->size());
  DCHECK_GE(segment->size(), requested_size);
  return segment;
}

bool AccountingAllocator::AddSegmentToPool(Segment* segment) {
  size_t size = segment->size();

  if (size >= (1 << (kMaxSegmentSizePower + 1))) return false;

  if (size < (1 << kMinSegmentSizePower)) return false;

  size_t power = kMaxSegmentSizePower;

  while (size < (static_cast<size_t>(1) << power)) power--;

  DCHECK_GE(power, kMinSegmentSizePower + 0);
  power -= kMinSegmentSizePower;

  {
    base::MutexGuard lock_guard(&unused_segments_mutex_);

    if (unused_segments_sizes_[power] >= unused_segments_max_sizes_[power]) {
      return false;
    }

    segment->set_next(unused_segments_heads_[power]);
    unused_segments_heads_[power] = segment;
    unused_segments_sizes_[power]++;
    // Poisoning needs to happen while still holding the mutex to guarantee that
    // it happens before the segment is taken from the pool again.
    ASAN_POISON_MEMORY_REGION(reinterpret_cast<void*>(segment->start()),
                              segment->size());
  }

  current_pool_size_.fetch_add(size, std::memory_order_relaxed);

  return true;
}

void AccountingAllocator::ClearPool() {
  base::MutexGuard lock_guard(&unused_segments_mutex_);

  for (size_t power = 0; power <= kMaxSegmentSizePower - kMinSegmentSizePower;
       power++) {
    Segment* current = unused_segments_heads_[power];
    while (current) {
      Segment* next = current->next();
      FreeSegment(current);
      current = next;
    }
    unused_segments_heads_[power] = nullptr;
  }
}

}  // namespace internal
}  // namespace v8
