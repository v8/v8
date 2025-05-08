// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_NUMBER_STRING_CACHE_H_
#define V8_OBJECTS_NUMBER_STRING_CACHE_H_

#include "src/common/globals.h"
#include "src/handles/maybe-handles.h"
#include "src/objects/fixed-array.h"
#include "src/objects/objects.h"
#include "src/objects/smi.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

// Used for mapping non-zero Smi to Strings.
V8_OBJECT class SmiStringCache : public FixedArray {
 public:
  using Super = FixedArray;

  // Empty entries are initialized with this sentinel (both key and value).
  static constexpr Tagged<Smi> kEmptySentinel = Smi::zero();

  static constexpr int kEntryKeyIndex = 0;
  static constexpr int kEntryValueIndex = 1;
  static constexpr int kEntrySize = 2;

  static constexpr int kInitialSize = 128;

  // Maximal allowed length, in number of elements.
  static constexpr int kMaxCapacity = FixedArray::kMaxCapacity / kEntrySize;

  inline uint32_t capacity() const;

  // Clears all entries in the table.
  inline void Clear();

  // Iterates the table and computes the number of occupied entries.
  uint32_t GetUsedEntriesCount();

  // Prints contents of the cache with a comment.
  void Print(const char* comment);

  // Returns entry index corresponding to given number.
  inline InternalIndex GetEntryFor(Tagged<Smi> number) const;
  static inline InternalIndex GetEntryFor(Isolate* isolate, Tagged<Smi> number);

  // Attempt to find the number in a cache. In case of success, returns
  // the string representation of the number. Otherwise returns undefined.
  static inline Handle<Object> Get(Isolate* isolate, InternalIndex entry,
                                   Tagged<Smi> number);

  // Puts <number, string> entry to the cache, potentially overwriting
  // existing entry.
  static inline void Set(Isolate* isolate, InternalIndex entry,
                         Tagged<Smi> number, DirectHandle<String> string);

  template <class IsolateT>
  static inline DirectHandle<SmiStringCache> New(IsolateT* isolate,
                                                 int capacity);

 protected:
  using Super::capacity;
  using Super::get;
  using Super::length;
  using Super::OffsetOfElementAt;
  using Super::set;
} V8_OBJECT_END;

// Used for mapping HeapNumbers to Strings.
// TODO(ishell): store doubles as raw values.
V8_OBJECT class DoubleStringCache : public SmiStringCache {
 public:
  using Super = SmiStringCache;

  static constexpr int kInitialSize = 128;

  // Returns entry index corresponding to given number.
  inline InternalIndex GetEntryFor(Tagged<HeapNumber> number) const;
  static inline InternalIndex GetEntryFor(Isolate* isolate,
                                          Tagged<HeapNumber> number);

  // Attempt to find the number in a cache. In case of success, returns
  // the string representation of the number. Otherwise returns undefined.
  static inline Handle<Object> Get(Isolate* isolate, InternalIndex entry,
                                   Tagged<HeapNumber> number);

  // Puts <number, string> entry to the cache, potentially overwriting
  // existing entry.
  static inline void Set(Isolate* isolate, InternalIndex entry,
                         DirectHandle<HeapNumber> number,
                         DirectHandle<String> string);

  template <class IsolateT>
  static inline DirectHandle<DoubleStringCache> New(IsolateT* isolate,
                                                    int capacity);

 private:
  using Super::Get;
  using Super::GetEntryFor;
  using Super::New;
  using Super::Set;
} V8_OBJECT_END;

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_NUMBER_STRING_CACHE_H_
