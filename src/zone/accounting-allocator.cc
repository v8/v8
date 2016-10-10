// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zone/accounting-allocator.h"

#include <cstdlib>

#if V8_LIBC_BIONIC
#include <malloc.h>  // NOLINT
#endif

namespace v8 {
namespace internal {

AccountingAllocator::AccountingAllocator() : unused_segments_mutex_() {
  memory_pressure_level_.SetValue(MemoryPressureLevel::kNone);
  std::fill(unused_segments_heads_,
            unused_segments_heads_ +
                (1 + kMaxSegmentSizePower - kMinSegmentSizePower),
            nullptr);
  std::fill(
      unused_segments_sizes,
      unused_segments_sizes + (1 + kMaxSegmentSizePower - kMinSegmentSizePower),
      0);
}

AccountingAllocator::~AccountingAllocator() { ClearPool(); }

void AccountingAllocator::MemoryPressureNotification(
    MemoryPressureLevel level) {
  memory_pressure_level_.SetValue(level);

  if (level != MemoryPressureLevel::kNone) {
    ClearPool();
  }
}

Segment* AccountingAllocator::GetSegment(size_t bytes) {
  Segment* result = GetSegmentFromPool(bytes);
  if (result == nullptr) {
    result = AllocateSegment(bytes);
    result->Initialize(bytes);
  }

  return result;
}

Segment* AccountingAllocator::AllocateSegment(size_t bytes) {
  void* memory = malloc(bytes);
  if (memory) {
    base::AtomicWord current =
        base::NoBarrier_AtomicIncrement(&current_memory_usage_, bytes);
    base::AtomicWord max = base::NoBarrier_Load(&max_memory_usage_);
    while (current > max) {
      max = base::NoBarrier_CompareAndSwap(&max_memory_usage_, max, current);
    }
  }
  return reinterpret_cast<Segment*>(memory);
}

void AccountingAllocator::ReturnSegment(Segment* segment) {
  segment->ZapContents();

  if (memory_pressure_level_.Value() != MemoryPressureLevel::kNone) {
    FreeSegment(segment);
  } else if (!AddSegmentToPool(segment)) {
    FreeSegment(segment);
  }
}

void AccountingAllocator::FreeSegment(Segment* memory) {
  base::NoBarrier_AtomicIncrement(
      &current_memory_usage_, -static_cast<base::AtomicWord>(memory->size()));
  memory->ZapHeader();
  free(memory);
}

size_t AccountingAllocator::GetCurrentMemoryUsage() const {
  return base::NoBarrier_Load(&current_memory_usage_);
}

size_t AccountingAllocator::GetMaxMemoryUsage() const {
  return base::NoBarrier_Load(&max_memory_usage_);
}

Segment* AccountingAllocator::GetSegmentFromPool(size_t requested_size) {
  if (requested_size > (1 << kMaxSegmentSizePower)) {
    return nullptr;
  }

  uint8_t power = kMinSegmentSizePower;
  while (requested_size > (static_cast<size_t>(1) << power)) power++;

  DCHECK_GE(power, kMinSegmentSizePower + 0);
  power -= kMinSegmentSizePower;

  Segment* segment;
  {
    base::LockGuard<base::Mutex> lock_guard(&unused_segments_mutex_);

    segment = unused_segments_heads_[power];

    if (segment != nullptr) {
      unused_segments_heads_[power] = segment->next();
      segment->set_next(nullptr);

      unused_segments_sizes[power]--;
      unused_segments_size_ -= segment->size();
    }
  }

  if (segment) {
    DCHECK_GE(segment->size(), requested_size);
  }
  return segment;
}

bool AccountingAllocator::AddSegmentToPool(Segment* segment) {
  size_t size = segment->size();

  if (size >= (1 << (kMaxSegmentSizePower + 1))) return false;

  if (size < (1 << kMinSegmentSizePower)) return false;

  uint8_t power = kMaxSegmentSizePower;

  while (size < (static_cast<size_t>(1) << power)) power--;

  DCHECK_GE(power, kMinSegmentSizePower + 0);
  power -= kMinSegmentSizePower;

  {
    base::LockGuard<base::Mutex> lock_guard(&unused_segments_mutex_);

    if (unused_segments_sizes[power] >= kMaxSegmentsPerBucket) {
      return false;
    }

    segment->set_next(unused_segments_heads_[power]);
    unused_segments_heads_[power] = segment;
    unused_segments_size_ += size;
    unused_segments_sizes[power]++;
  }

  return true;
}

void AccountingAllocator::ClearPool() {
  base::LockGuard<base::Mutex> lock_guard(&unused_segments_mutex_);

  for (uint8_t power = 0; power <= kMaxSegmentSizePower - kMinSegmentSizePower;
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
