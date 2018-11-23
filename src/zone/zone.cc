// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zone/zone.h"

#include <cstring>

#include "src/asan.h"
#include "src/utils.h"
#include "src/v8.h"

namespace v8 {
namespace internal {

namespace {

#ifdef V8_USE_ADDRESS_SANITIZER

constexpr size_t kASanRedzoneBytes = 24;  // Must be a multiple of 8.

#else  // !V8_USE_ADDRESS_SANITIZER

constexpr size_t kASanRedzoneBytes = 0;

#endif  // V8_USE_ADDRESS_SANITIZER

}  // namespace

Zone::Zone(AccountingAllocator* allocator, const char* name)
    : allocation_size_(0),
      segment_bytes_allocated_(0),
      position_(0),
      limit_(0),
      allocator_(allocator),
      segment_head_(nullptr),
      name_(name),
      sealed_(false) {
  allocator_->ZoneCreation(this);
}

Zone::~Zone() {
  allocator_->ZoneDestruction(this);
  DeleteAll();

  DCHECK_EQ(segment_bytes_allocated_, 0);
}

void* Zone::AsanNew(size_t size) {
  CHECK(!sealed_);

  // Round up the requested size to fit the alignment.
  size = RoundUp(size, kAlignmentInBytes);

  // Check if the requested size is available without expanding.
  Address result = position_;

  const size_t size_with_redzone = size + kASanRedzoneBytes;
  DCHECK_LE(position_, limit_);
  if (size_with_redzone > limit_ - position_) {
    result = NewExpand(size_with_redzone);
  } else {
    position_ += size_with_redzone;
  }

  Address redzone_position = result + size;
  DCHECK_EQ(redzone_position + kASanRedzoneBytes, position_);
  ASAN_POISON_MEMORY_REGION(reinterpret_cast<void*>(redzone_position),
                            kASanRedzoneBytes);

  // Check that the result has the proper alignment and return it.
  DCHECK(IsAligned(result, kAlignmentInBytes));
  return reinterpret_cast<void*>(result);
}

void Zone::Reset() {
  if (!segment_head_) return;
  allocator_->ZoneDestruction(this);
  Segment* keep = segment_head_;
  segment_head_ = segment_head_->next();
  keep->set_next(nullptr);
  DeleteAll();
  allocator_->ZoneCreation(this);
  keep->ZapContents();
  segment_head_ = keep;
}

void Zone::DeleteAll() {
  // Traverse the chained list of segments and return them all to the allocator.
  for (Segment* current = segment_head_; current;) {
    Segment* next = current->next();
    size_t size = current->size();

    // Un-poison the segment content so we can re-use or zap it later.
    ASAN_UNPOISON_MEMORY_REGION(reinterpret_cast<void*>(current->start()),
                                current->capacity());

    segment_bytes_allocated_ -= size;
    allocator_->ReturnSegment(current);
    current = next;
  }

  position_ = limit_ = 0;
  allocation_size_ = 0;
  segment_head_ = nullptr;
}

Address Zone::NewExpand(size_t size) {
  // Make sure the requested size is already properly aligned and that
  // there isn't enough room in the Zone to satisfy the request.
  DCHECK_EQ(size, RoundDown(size, kAlignmentInBytes));
  DCHECK_LT(limit_ - position_, size);

  // Commit the allocation_size_ of segment_head_ if any.
  allocation_size_ = allocation_size();
  static const size_t kSegmentOverhead = sizeof(Segment) + kAlignmentInBytes;
  const size_t min_size = kSegmentOverhead + size;
  // Guard against integer overflow.
  if (V8_UNLIKELY(!IsInRange(min_size, size, static_cast<size_t>(INT_MAX)))) {
    V8::FatalProcessOutOfMemory(nullptr, "Zone");
    return kNullAddress;
  }
  const size_t requested_size = Max(min_size, kDefaultSegmentSize);
  Segment* segment = allocator_->GetSegment(requested_size);
  if (V8_UNLIKELY(segment == nullptr)) {
    V8::FatalProcessOutOfMemory(nullptr, "Zone");
    return kNullAddress;
  }

  DCHECK_GE(segment->size(), requested_size);
  segment_bytes_allocated_ += segment->size();
  segment->set_next(segment_head_);
  segment_head_ = segment;

  // Recompute 'top' and 'limit' based on the new segment.
  Address result = RoundUp(segment->start(), kAlignmentInBytes);
  position_ = result + size;
  // Check for address overflow.
  // (Should not happen since the segment is guaranteed to accommodate
  // size bytes + header and alignment padding)
  DCHECK_LE(result, position_);
  limit_ = segment->end();
  DCHECK_LE(position_, limit_);
  return result;
}

}  // namespace internal
}  // namespace v8
