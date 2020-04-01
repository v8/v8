// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_MEMBER_H_
#define INCLUDE_CPPGC_MEMBER_H_

#include <atomic>
#include <cstddef>

#include "include/v8config.h"

namespace cppgc {

namespace internal {

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
 public:
  EnabledCheckingPolicy();
  void CheckPointer(const void* ptr);

 private:
  void* impl_;
};

class DisabledCheckingPolicy {
 public:
  void CheckPointer(const void* raw) {}
};

#if V8_ENABLE_CHECKS
using DefaultCheckingPolicy = EnabledCheckingPolicy;
#else
using DefaultCheckingPolicy = DisabledCheckingPolicy;
#endif

// Special tag type used to denote some sentinel member. The semantics of the
// sentinel is defined by the embedder.
struct MemberSentinel {};
constexpr MemberSentinel kMemberSentinel;

// The basic class from which all Member classes are 'generated'.
template <typename T, class WeaknessTag, class WriteBarrierPolicy,
          class CheckingPolicy = DefaultCheckingPolicy>
class BasicMember : private CheckingPolicy {
 public:
  constexpr BasicMember() = default;
  constexpr BasicMember(std::nullptr_t) {}  // NOLINT
  constexpr BasicMember(MemberSentinel)     // NOLINT
      : raw_(reinterpret_cast<T*>(kSentinelValue)) {}
  BasicMember(T* raw) : raw_(raw) {  // NOLINT
    InitializingWriteBarrier();
    this->CheckPointer(raw_);
  }
  // TODO(chromium:1056170): Unfortunately, this overload is used ubiquitously
  // in Blink. Reeavalute the possibility to remove it.
  BasicMember(T& raw) : BasicMember(&raw) {}  // NOLINT
  BasicMember(const BasicMember& other) : BasicMember(other.Get()) {}
  // Allow heterogeneous construction.
  template <typename U, typename OtherBarrierPolicy, typename OtherWeaknessTag,
            typename OtherCheckingPolicy>
  BasicMember(const BasicMember<U, OtherWeaknessTag, OtherBarrierPolicy,
                                OtherCheckingPolicy>& other)
      : BasicMember(other.Get()) {}

  BasicMember& operator=(const BasicMember& other) {
    return operator=(other.Get());
  }
  // Allow heterogeneous assignment.
  template <typename U, typename OtherWeaknessTag, typename OtherBarrierPolicy,
            typename OtherCheckingPolicy>
  BasicMember& operator=(
      const BasicMember<U, OtherWeaknessTag, OtherBarrierPolicy,
                        OtherCheckingPolicy>& other) {
    return operator=(other.Get());
  }
  BasicMember& operator=(T* other) {
    SetRawAtomic(other);
    AssigningWriteBarrier();
    this->CheckPointer(Get());
    return *this;
  }
  BasicMember& operator=(std::nullptr_t) {
    Clear();
    return *this;
  }
  BasicMember& operator=(MemberSentinel) {
    SetRawAtomic(reinterpret_cast<T*>(kSentinelValue));
    return *this;
  }

  template <typename U, typename OtherWeaknessTag, typename OtherBarrierPolicy,
            typename OtherCheckingPolicy>
  void Swap(BasicMember<U, OtherWeaknessTag, OtherBarrierPolicy,
                        OtherCheckingPolicy>& other) {
    T* tmp = Get();
    *this = other;
    other = tmp;
  }

  explicit operator bool() const { return Get(); }
  operator T*() const { return Get(); }
  T* operator->() const { return Get(); }
  T& operator*() const { return *Get(); }

  T* Get() const {
    // Executed by the mutator, hence non atomic load.
    return raw_;
  }

  void Clear() { SetRawAtomic(nullptr); }

  T* Release() {
    T* result = Get();
    Clear();
    return result;
  }

 private:
  // Must not be odr-used.
  static constexpr intptr_t kSentinelValue = -1;

  void SetRawAtomic(T* raw) {
    reinterpret_cast<std::atomic<T*>*>(&raw_)->store(raw,
                                                     std::memory_order_relaxed);
  }
  T* GetRawAtomic() const {
    return reinterpret_cast<const std::atomic<T*>*>(&raw_)->load(
        std::memory_order_relaxed);
  }

  void InitializingWriteBarrier() const {
    WriteBarrierPolicy::InitializingBarrier(
        reinterpret_cast<const void*>(&raw_), static_cast<const void*>(raw_));
  }
  void AssigningWriteBarrier() const {
    WriteBarrierPolicy::AssigningBarrier(reinterpret_cast<const void*>(&raw_),
                                         static_cast<const void*>(raw_));
  }

  T* raw_ = nullptr;
};

template <typename T1, typename WeaknessTag1, typename WriteBarrierPolicy1,
          typename CheckingPolicy1, typename T2, typename WeaknessTag2,
          typename WriteBarrierPolicy2, typename CheckingPolicy2>
bool operator==(
    BasicMember<T1, WeaknessTag1, WriteBarrierPolicy1, CheckingPolicy1> member1,
    BasicMember<T2, WeaknessTag2, WriteBarrierPolicy2, CheckingPolicy2>
        member2) {
  return member1.Get() == member2.Get();
}

template <typename T1, typename WeaknessTag1, typename WriteBarrierPolicy1,
          typename CheckingPolicy1, typename T2, typename WeaknessTag2,
          typename WriteBarrierPolicy2, typename CheckingPolicy2>
bool operator!=(
    BasicMember<T1, WeaknessTag1, WriteBarrierPolicy1, CheckingPolicy1> member1,
    BasicMember<T2, WeaknessTag2, WriteBarrierPolicy2, CheckingPolicy2>
        member2) {
  return !(member1 == member2);
}

template <typename T, typename WriteBarrierPolicy>
using BasicStrongMember =
    BasicMember<T, class StrongMemberTag, WriteBarrierPolicy>;

template <typename T, typename WriteBarrierPolicy>
using BasicWeakMember = BasicMember<T, class WeakMemberTag, WriteBarrierPolicy>;

}  // namespace internal

// Members are used in classes to contain strong pointers to other garbage
// collected objects. All Member fields of a class must be traced in the class'
// trace method.
template <typename T>
using Member =
    internal::BasicStrongMember<T, internal::DijkstraWriteBarrierPolicy>;

// WeakMember is similar to Member in that it is used to point to other garbage
// collected objects. However instead of creating a strong pointer to the
// object, the WeakMember creates a weak pointer, which does not keep the
// pointee alive. Hence if all pointers to to a heap allocated object are weak
// the object will be garbage collected. At the time of GC the weak pointers
// will automatically be set to null.
template <typename T>
using WeakMember =
    internal::BasicWeakMember<T, internal::DijkstraWriteBarrierPolicy>;

// UntracedMember is a pointer to an on-heap object that is not traced for some
// reason. Do not use this unless you know what you are doing. Keeping raw
// pointers to on-heap objects is prohibited unless used from stack. Pointee
// must be kept alive through other means.
template <typename T>
using UntracedMember = internal::BasicMember<T, class UntracedMemberTag,
                                             internal::NoWriteBarrierPolicy>;

}  // namespace cppgc

#endif  // INCLUDE_CPPGC_MEMBER_H_
