// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_SMALL_VECTOR_H_
#define V8_BASE_SMALL_VECTOR_H_

#include <type_traits>

#include "src/base/bits.h"
#include "src/base/macros.h"

namespace v8 {
namespace base {

// Minimal SmallVector implementation. Uses inline storage first, switches to
// malloc when it overflows.
template <typename T, size_t kInlineSize>
class SmallVector {
  // Currently only support trivially copyable and trivially destructible data
  // types, as it uses memcpy to copy elements and never calls destructors.
  ASSERT_TRIVIALLY_COPYABLE(T);
  STATIC_ASSERT(std::is_trivially_destructible<T>::value);

 public:
  SmallVector() = default;
  ~SmallVector() {
    if (is_big()) free(begin_);
  }

  T* data() const { return begin_; }
  T* begin() const { return begin_; }
  T* end() const { return end_; }
  size_t size() const { return end_ - begin_; }
  bool empty() const { return end_ == begin_; }

  T& back() {
    DCHECK_NE(0, size());
    return end_[-1];
  }

  template <typename... Args>
  void emplace_back(Args&&... args) {
    if (V8_UNLIKELY(end_ == end_of_storage_)) Grow();
    new (end_) T(std::forward<Args>(args)...);
    ++end_;
  }

  void pop(size_t count) {
    DCHECK_GE(size(), count);
    end_ -= count;
  }

  void clear() { end_ = begin_; }

 private:
  T* begin_ = inline_storage_begin();
  T* end_ = begin_;
  T* end_of_storage_ = begin_ + kInlineSize;
  typename std::aligned_storage<sizeof(T) * kInlineSize, alignof(T)>::type
      inline_storage_;

  void Grow() {
    size_t in_use = end_ - begin_;
    size_t new_capacity = base::bits::RoundUpToPowerOfTwo(2 * in_use);
    T* new_storage = reinterpret_cast<T*>(malloc(sizeof(T) * new_capacity));
    memcpy(new_storage, begin_, sizeof(T) * in_use);
    if (is_big()) free(begin_);
    begin_ = new_storage;
    end_ = new_storage + in_use;
    end_of_storage_ = new_storage + new_capacity;
  }

  bool is_big() const { return begin_ != inline_storage_begin(); }

  T* inline_storage_begin() { return reinterpret_cast<T*>(&inline_storage_); }
  const T* inline_storage_begin() const {
    return reinterpret_cast<const T*>(&inline_storage_);
  }

  DISALLOW_COPY_AND_ASSIGN(SmallVector);
};

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_SMALL_VECTOR_H_
