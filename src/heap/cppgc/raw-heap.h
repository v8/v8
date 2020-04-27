// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_RAW_HEAP_H_
#define V8_HEAP_CPPGC_RAW_HEAP_H_

#include <array>
#include <iterator>
#include <memory>

#include "include/cppgc/heap.h"
#include "src/base/logging.h"
#include "src/base/macros.h"

namespace cppgc {
namespace internal {

class Heap;
class BaseSpace;

// RawHeap is responsible for space management.
class V8_EXPORT_PRIVATE RawHeap final {
  static constexpr size_t kNumberOfSpaces = 9;

 public:
  using SpaceType = cppgc::Heap::SpaceType;
  using Spaces = std::array<std::unique_ptr<BaseSpace>, kNumberOfSpaces>;

  using iterator = Spaces::iterator;
  using const_iterator = Spaces::const_iterator;

  explicit RawHeap(Heap* heap);
  ~RawHeap();

  // Space iteration support.
  iterator begin() { return spaces_.begin(); }
  const_iterator begin() const { return spaces_.begin(); }
  iterator end() { return std::next(spaces_.begin(), used_spaces_); }
  const_iterator end() const {
    return std::next(spaces_.begin(), used_spaces_);
  }

  size_t size() const { return used_spaces_; }

  BaseSpace* Space(SpaceType type) {
    const size_t index = static_cast<size_t>(type);
    DCHECK_GT(spaces_.size(), index);
    BaseSpace* space = spaces_[index].get();
    DCHECK(space);
    return space;
  }
  const BaseSpace* Space(SpaceType space) const {
    return const_cast<RawHeap&>(*this).Space(space);
  }

  Heap* heap() { return main_heap_; }
  const Heap* heap() const { return main_heap_; }

 private:
  Heap* main_heap_;
  Spaces spaces_;
  size_t used_spaces_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_RAW_HEAP_H_
