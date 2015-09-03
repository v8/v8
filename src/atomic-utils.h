// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ATOMIC_UTILS_H_
#define V8_ATOMIC_UTILS_H_

#include <limits.h>

#include "src/base/atomicops.h"
#include "src/base/macros.h"

namespace v8 {
namespace internal {

class AtomicValue {
 public:
  AtomicValue() : value_(0) {}
  explicit AtomicValue(base::AtomicWord initial) : value_(initial) {}

  V8_INLINE void Increment(base::AtomicWord increment) {
    base::NoBarrier_AtomicIncrement(&value_, increment);
  }

  V8_INLINE base::AtomicWord Value() { return base::NoBarrier_Load(&value_); }

  V8_INLINE void SetValue(base::AtomicWord new_value) {
    base::NoBarrier_Store(&value_, new_value);
  }

 private:
  base::AtomicWord value_;
};


// See utils.h for EnumSet. Storage is always base::AtomicWord.
// Requirements on E:
// - No explicit values.
// - E::kLastValue defined to be the last actually used value.
//
// Example:
// enum E { kA, kB, kC, kLastValue = kC };
template <class E>
class AtomicEnumSet {
 public:
  explicit AtomicEnumSet(base::AtomicWord bits = 0) : bits_(bits) {}

  bool IsEmpty() const { return ToIntegral() == 0; }

  bool Contains(E element) const { return (ToIntegral() & Mask(element)) != 0; }

  bool ContainsAnyOf(const AtomicEnumSet& set) const {
    return (ToIntegral() & set.ToIntegral()) != 0;
  }

  void RemoveAll() { base::NoBarrier_Store(&bits_, 0); }

  bool operator==(const AtomicEnumSet& set) const {
    return ToIntegral() == set.ToIntegral();
  }

  bool operator!=(const AtomicEnumSet& set) const {
    return ToIntegral() != set.ToIntegral();
  }

  AtomicEnumSet<E> operator|(const AtomicEnumSet& set) const {
    return AtomicEnumSet<E>(ToIntegral() | set.ToIntegral());
  }

// The following operations modify the underlying storage.

#define ATOMIC_SET_WRITE(OP, NEW_VAL)                                     \
  do {                                                                    \
    base::AtomicWord old;                                                 \
    do {                                                                  \
      old = base::Acquire_Load(&bits_);                                   \
    } while (base::Release_CompareAndSwap(&bits_, old, old OP NEW_VAL) != \
             old);                                                        \
  } while (false)

  void Add(E element) { ATOMIC_SET_WRITE(|, Mask(element)); }

  void Add(const AtomicEnumSet& set) { ATOMIC_SET_WRITE(|, set.ToIntegral()); }

  void Remove(E element) { ATOMIC_SET_WRITE(&, Mask(element)); }

  void Remove(const AtomicEnumSet& set) {
    ATOMIC_SET_WRITE(&, ~set.ToIntegral());
  }

  void Intersect(const AtomicEnumSet& set) {
    ATOMIC_SET_WRITE(&, set.ToIntegral());
  }

#undef ATOMIC_SET_OP

 private:
  // Check whether there's enough storage to hold E.
  STATIC_ASSERT(E::kLastValue < (sizeof(base::AtomicWord) * CHAR_BIT));

  V8_INLINE base::AtomicWord ToIntegral() const {
    return base::NoBarrier_Load(&bits_);
  }

  V8_INLINE base::AtomicWord Mask(E element) const {
    return static_cast<base::AtomicWord>(1) << element;
  }

  base::AtomicWord bits_;
};


// Flag using enums atomically.
template <class E>
class AtomicEnumFlag {
 public:
  explicit AtomicEnumFlag(E initial) : value_(initial) {}

  V8_INLINE E Value() { return static_cast<E>(base::NoBarrier_Load(&value_)); }

  V8_INLINE bool TrySetValue(E old_value, E new_value) {
    return base::NoBarrier_CompareAndSwap(
               &value_, static_cast<base::AtomicWord>(old_value),
               static_cast<base::AtomicWord>(new_value)) ==
           static_cast<base::AtomicWord>(old_value);
  }

 private:
  base::AtomicWord value_;
};

}  // namespace internal
}  // namespace v8

#endif  // #define V8_ATOMIC_UTILS_H_
