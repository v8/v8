// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_V8_HANDLE_BASE_H_
#define INCLUDE_V8_HANDLE_BASE_H_

#include "v8-internal.h"  // NOLINT(build/include_directory)

namespace v8 {

/**
 * A base class for abstract handles containing indirect pointers.
 * These are useful regardless of whether direct local support is enabled.
 */
class IndirectHandleBase {
 public:
  // Returns true if the handle is empty.
  V8_INLINE bool IsEmpty() const { return location_ == nullptr; }

  // Sets the handle to be empty. IsEmpty() will then return true.
  V8_INLINE void Clear() { location_ = nullptr; }

 protected:
  friend class internal::ValueHelper;
  friend class internal::HandleHelper;

  V8_INLINE IndirectHandleBase() = default;
  V8_INLINE IndirectHandleBase(const IndirectHandleBase& other) = default;
  V8_INLINE IndirectHandleBase& operator=(const IndirectHandleBase& that) =
      default;

  V8_INLINE explicit IndirectHandleBase(internal::Address* location)
      : location_(location) {}

  // Returns the address of the actual heap object (tagged).
  // This method must be called only if the handle is not empty, otherwise it
  // will crash.
  V8_INLINE internal::Address ptr() const { return *location_; }

  // Returns a reference to the slot (indirect pointer).
  V8_INLINE internal::Address* const& slot() const { return location_; }
  V8_INLINE internal::Address*& slot() { return location_; }

  // Returns the handler's "value" (direct or indirect pointer, depending on
  // whether direct local support is enabled).
  template <typename T, bool check_null = false>
  V8_INLINE T* value() const {
    return internal::ValueHelper::SlotAsValue<T, check_null>(slot());
  }

 private:
  internal::Address* location_ = nullptr;
};

#ifdef V8_ENABLE_DIRECT_LOCAL

/**
 * A base class for abstract handles containing direct pointers.
 * These are only possible when conservative stack scanning is enabled.
 */
class DirectHandleBase {
 public:
  // Returns true if the handle is empty.
  V8_INLINE bool IsEmpty() const {
    return ptr_ == internal::ValueHelper::kEmpty;
  }

  // Sets the handle to be empty. IsEmpty() will then return true.
  V8_INLINE void Clear() { ptr_ = internal::ValueHelper::kEmpty; }

 protected:
  friend class internal::ValueHelper;
  friend class internal::HandleHelper;

  V8_INLINE DirectHandleBase() = default;
  V8_INLINE DirectHandleBase(const DirectHandleBase& other) = default;
  V8_INLINE DirectHandleBase& operator=(const DirectHandleBase& that) = default;

  V8_INLINE explicit DirectHandleBase(internal::Address ptr) : ptr_(ptr) {}

  // Returns the address of the referenced object.
  V8_INLINE internal::Address ptr() const { return ptr_; }

  // Returns the handler's "value" (direct pointer, as direct local support
  // is guaranteed to be enabled here).
  template <typename T, bool check_null = false>
  V8_INLINE T* value() const {
    return reinterpret_cast<T*>(ptr_);
  }

 private:
  internal::Address ptr_ = internal::ValueHelper::kEmpty;
};

#endif  // V8_ENABLE_DIRECT_LOCAL

}  // namespace v8

#endif  // INCLUDE_V8_HANDLE_BASE_H_
