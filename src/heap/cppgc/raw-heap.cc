// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/raw-heap.h"

#include "src/heap/cppgc/heap-space.h"

namespace cppgc {
namespace internal {

RawHeap::RawHeap(Heap* heap) : main_heap_(heap), used_spaces_(0) {
  size_t i = 0;
  for (; i < static_cast<size_t>(SpaceType::kLarge); ++i) {
    spaces_[i] = std::make_unique<NormalPageSpace>(this, i);
  }
  spaces_[i] = std::make_unique<LargePageSpace>(
      this, static_cast<size_t>(SpaceType::kLarge));
  used_spaces_ = i + 1;
}

RawHeap::~RawHeap() = default;

}  // namespace internal
}  // namespace cppgc
