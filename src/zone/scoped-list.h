// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ZONE_SCOPED_LIST_H_
#define V8_ZONE_SCOPED_LIST_H_

#include <type_traits>
#include <vector>

#include "src/base/logging.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

template <typename T>
class Vector;

template <typename T>
class ZoneList;

// ScopedList is a scope-lifetime list with a std::vector backing that can be
// re-used between ScopedLists. Note that a ScopedList in an outer scope cannot
// add any entries if there is a ScopedList with the same backing in an inner
// scope.
//
// TODO(ishell): move header to src/utils/ once zone dependency is resolved.
template <typename T, typename TBacking = T>
class ScopedList final {
  // The backing can either be the same type as the list type, or, for pointers,
  // we additionally allow a void* backing store.
  STATIC_ASSERT((std::is_same<TBacking, T>::value) ||
                (std::is_same<TBacking, void*>::value &&
                 std::is_pointer<T>::value));

 public:
  explicit ScopedList(std::vector<TBacking>* buffer)
      : buffer_(*buffer), start_(buffer->size()), end_(buffer->size()) {}

  ~ScopedList() { Rewind(); }

  void Rewind() {
    DCHECK_EQ(buffer_.size(), end_);
    buffer_.resize(start_);
    end_ = start_;
  }

  void MergeInto(ScopedList* parent) {
    DCHECK_EQ(parent->end_, start_);
    parent->end_ = end_;
    start_ = end_;
    DCHECK_EQ(0, length());
  }

  int length() const { return static_cast<int>(end_ - start_); }

  const T& at(int i) const {
    size_t index = start_ + i;
    DCHECK_LE(start_, index);
    DCHECK_LT(index, buffer_.size());
    return *reinterpret_cast<T*>(&buffer_[index]);
  }

  T& at(int i) {
    size_t index = start_ + i;
    DCHECK_LE(start_, index);
    DCHECK_LT(index, buffer_.size());
    return *reinterpret_cast<T*>(&buffer_[index]);
  }

  void CopyTo(ZoneList<T>* target, Zone* zone) const {
    DCHECK_LE(end_, buffer_.size());
    // Make sure we don't reference absent elements below.
    if (length() == 0) return;
    target->Initialize(length(), zone);
    T* data = reinterpret_cast<T*>(&buffer_[start_]);
    target->AddAll(Vector<T>(data, length()), zone);
  }

  Vector<T> CopyTo(Zone* zone) {
    DCHECK_LE(end_, buffer_.size());
    T* data = zone->NewArray<T>(length());
    if (length() != 0) {
      MemCopy(data, &buffer_[start_], length() * sizeof(T));
    }
    return Vector<T>(data, length());
  }

  void Add(const T& value) {
    DCHECK_EQ(buffer_.size(), end_);
    buffer_.push_back(value);
    ++end_;
  }

  void AddAll(const ZoneList<T>& list) {
    DCHECK_EQ(buffer_.size(), end_);
    buffer_.reserve(buffer_.size() + list.length());
    for (int i = 0; i < list.length(); i++) {
      buffer_.push_back(list.at(i));
    }
    end_ += list.length();
  }

  using iterator = T*;
  inline iterator begin() const {
    return reinterpret_cast<T*>(buffer_.data() + start_);
  }
  inline iterator end() const {
    return reinterpret_cast<T*>(buffer_.data() + end_);
  }

 private:
  std::vector<TBacking>& buffer_;
  size_t start_;
  size_t end_;
};

template <typename T>
using ScopedPtrList = ScopedList<T*, void*>;

}  // namespace internal
}  // namespace v8

#endif  // V8_ZONE_SCOPED_LIST_H_
