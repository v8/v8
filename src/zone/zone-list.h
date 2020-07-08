// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ZONE_ZONE_LIST_H_
#define V8_ZONE_ZONE_LIST_H_

#include <initializer_list>

#include "src/base/logging.h"
#include "src/zone/zone-fwd.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

template <typename T>
class Vector;

// ZoneLists are growable lists with constant-time access to the
// elements. The list itself and all its elements are allocated in the
// Zone. ZoneLists cannot be deleted individually; you can delete all
// objects in the Zone by calling Zone::DeleteAll().
template <typename T>
class ZoneList final {
 public:
  // Construct a new ZoneList with the given capacity; the length is
  // always zero. The capacity must be non-negative.
  ZoneList(int capacity, Zone* zone) { Initialize(capacity, zone); }
  // Construct a new ZoneList from a std::initializer_list
  ZoneList(std::initializer_list<T> list, Zone* zone) {
    Initialize(static_cast<int>(list.size()), zone);
    for (auto& i : list) Add(i, zone);
  }
  // Construct a new ZoneList by copying the elements of the given ZoneList.
  ZoneList(const ZoneList<T>& other, Zone* zone) {
    Initialize(other.length(), zone);
    AddAll(other, zone);
  }

  V8_INLINE ~ZoneList() { DeleteData(data_); }

  // Please the MSVC compiler.  We should never have to execute this.
  V8_INLINE void operator delete(void* p, ZoneAllocationPolicy allocator) {
    UNREACHABLE();
  }

  void* operator new(size_t size, Zone* zone) { return zone->New(size); }

  // Returns a reference to the element at index i. This reference is not safe
  // to use after operations that can change the list's backing store
  // (e.g. Add).
  inline T& operator[](int i) const {
    DCHECK_LE(0, i);
    DCHECK_GT(static_cast<unsigned>(length_), static_cast<unsigned>(i));
    return data_[i];
  }
  inline T& at(int i) const { return operator[](i); }
  inline T& last() const { return at(length_ - 1); }
  inline T& first() const { return at(0); }

  using iterator = T*;
  inline iterator begin() const { return &data_[0]; }
  inline iterator end() const { return &data_[length_]; }

  V8_INLINE bool is_empty() const { return length_ == 0; }
  V8_INLINE int length() const { return length_; }
  V8_INLINE int capacity() const { return capacity_; }

  Vector<T> ToVector() const { return Vector<T>(data_, length_); }
  Vector<T> ToVector(int start, int length) const {
    return Vector<T>(data_ + start, std::min(length_ - start, length));
  }

  Vector<const T> ToConstVector() const {
    return Vector<const T>(data_, length_);
  }

  V8_INLINE void Initialize(int capacity, Zone* zone) {
    DCHECK_GE(capacity, 0);
    data_ = (capacity > 0) ? NewData(capacity, ZoneAllocationPolicy(zone))
                           : nullptr;
    capacity_ = capacity;
    length_ = 0;
  }

  // Adds a copy of the given 'element' to the end of the list,
  // expanding the list if necessary.
  void Add(const T& element, Zone* zone);
  // Add all the elements from the argument list to this list.
  void AddAll(const ZoneList<T>& other, Zone* zone);
  // Add all the elements from the vector to this list.
  void AddAll(const Vector<T>& other, Zone* zone);
  // Inserts the element at the specific index.
  void InsertAt(int index, const T& element, Zone* zone);

  // Added 'count' elements with the value 'value' and returns a
  // vector that allows access to the elements. The vector is valid
  // until the next change is made to this list.
  Vector<T> AddBlock(T value, int count, Zone* zone);

  // Overwrites the element at the specific index.
  void Set(int index, const T& element);

  // Removes the i'th element without deleting it even if T is a
  // pointer type; moves all elements above i "down". Returns the
  // removed element.  This function's complexity is linear in the
  // size of the list.
  T Remove(int i);

  // Removes the last element without deleting it even if T is a
  // pointer type. Returns the removed element.
  V8_INLINE T RemoveLast() { return Remove(length_ - 1); }

  // Clears the list by freeing the storage memory. If you want to keep the
  // memory, use Rewind(0) instead. Be aware, that even if T is a
  // pointer type, clearing the list doesn't delete the entries.
  V8_INLINE void Clear();

  // Drops all but the first 'pos' elements from the list.
  V8_INLINE void Rewind(int pos);

  inline bool Contains(const T& elm) const {
    for (int i = 0; i < length_; i++) {
      if (data_[i] == elm) return true;
    }
    return false;
  }

  // Iterate through all list entries, starting at index 0.
  template <class Visitor>
  void Iterate(Visitor* visitor);

  // Sort all list entries (using QuickSort)
  template <typename CompareFunction>
  void Sort(CompareFunction cmp);
  template <typename CompareFunction>
  void StableSort(CompareFunction cmp, size_t start, size_t length);

  void operator delete(void* pointer) { UNREACHABLE(); }
  void operator delete(void* pointer, Zone* zone) { UNREACHABLE(); }

 private:
  T* data_;
  int capacity_;
  int length_;

  V8_INLINE T* NewData(int n, ZoneAllocationPolicy allocator) {
    return static_cast<T*>(allocator.New(n * sizeof(T)));
  }
  V8_INLINE void DeleteData(T* data) { ZoneAllocationPolicy::Delete(data); }

  // Increase the capacity of a full list, and add an element.
  // List must be full already.
  void ResizeAdd(const T& element, ZoneAllocationPolicy allocator);

  // Inlined implementation of ResizeAdd, shared by inlined and
  // non-inlined versions of ResizeAdd.
  void ResizeAddInternal(const T& element, ZoneAllocationPolicy allocator);

  // Resize the list.
  void Resize(int new_capacity, ZoneAllocationPolicy allocator);

  DISALLOW_COPY_AND_ASSIGN(ZoneList);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_ZONE_ZONE_LIST_H_
