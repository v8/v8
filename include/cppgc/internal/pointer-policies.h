// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_INTERNAL_POINTER_POLICIES_H_
#define INCLUDE_CPPGC_INTERNAL_POINTER_POLICIES_H_

#include <cstdint>

#include "include/v8config.h"

namespace cppgc {
namespace internal {

// Tags to distinguish between strong and weak member types.
class StrongMemberTag;
class WeakMemberTag;
class UntracedMemberTag;

struct DijkstraWriteBarrierPolicy {
  static void InitializingBarrier(const void*, const void*) {
    // Since in initializing writes the source object is always white, having no
    // barrier doesn't break the tri-color invariant.
  }
  static void AssigningBarrier(const void*, const void*) {
    // TODO(chromium:1056170): Add actual implementation.
  }
};

struct NoWriteBarrierPolicy {
  static void InitializingBarrier(const void*, const void*) {}
  static void AssigningBarrier(const void*, const void*) {}
};

class V8_EXPORT EnabledCheckingPolicy {
 protected:
  EnabledCheckingPolicy();
  void CheckPointer(const void* ptr);

 private:
  void* impl_;
};

class DisabledCheckingPolicy {
 protected:
  void CheckPointer(const void* raw) {}
};

#if V8_ENABLE_CHECKS
using DefaultCheckingPolicy = EnabledCheckingPolicy;
#else
using DefaultCheckingPolicy = DisabledCheckingPolicy;
#endif

// Special tag type used to denote some sentinel member. The semantics of the
// sentinel is defined by the embedder.
struct SentinelPointer {
  template <typename T>
  operator T*() const {  // NOLINT
    static constexpr intptr_t kSentinelValue = -1;
    return reinterpret_cast<T*>(kSentinelValue);
  }
  // Hidden friends.
  friend bool operator==(SentinelPointer, SentinelPointer) { return true; }
  friend bool operator!=(SentinelPointer, SentinelPointer) { return false; }
};

}  // namespace internal

constexpr internal::SentinelPointer kSentinelPointer;

}  // namespace cppgc

#endif  // INCLUDE_CPPGC_INTERNAL_POINTER_POLICIES_H_
