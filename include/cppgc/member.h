// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_MEMBER_H_
#define INCLUDE_CPPGC_MEMBER_H_

#include <atomic>
#include <cstddef>
#include <type_traits>

#include "cppgc/internal/api-constants.h"
#include "cppgc/internal/pointer-policies.h"
#include "cppgc/sentinel-pointer.h"
#include "cppgc/type-traits.h"
#include "v8config.h"  // NOLINT(build/include_directory)

namespace cppgc {

class Visitor;

namespace internal {

#if defined(CPPGC_POINTER_COMPRESSION)

class CageBaseGlobal final {
 public:
  V8_INLINE static uintptr_t Get() {
    CPPGC_DCHECK(IsBaseConsistent());
    return g_base_;
  }

  V8_INLINE static bool IsSet() {
    CPPGC_DCHECK(IsBaseConsistent());
    return (g_base_ & ~kLowerHalfWordMask) != 0;
  }

 private:
  // We keep the lower halfword as ones to speed up decompression.
  static constexpr uintptr_t kLowerHalfWordMask =
      (api_constants::kCagedHeapReservationAlignment - 1);

  static thread_local V8_EXPORT uintptr_t g_base_
#if !V8_CC_MSVC
      __attribute__((require_constant_initialization))
#endif  // !V8_CC_MSVC
      ;

  CageBaseGlobal() = delete;

  V8_INLINE static bool IsBaseConsistent() {
    return kLowerHalfWordMask == (g_base_ & kLowerHalfWordMask);
  }

  friend class CageBaseGlobalUpdater;
};

class CompressedPointer final {
 public:
  using Storage = uint32_t;

  V8_INLINE CompressedPointer() : value_(0u) {}
  V8_INLINE explicit CompressedPointer(const void* ptr)
      : value_(Compress(ptr)) {}
  V8_INLINE explicit CompressedPointer(std::nullptr_t) : value_(0u) {}
  V8_INLINE explicit CompressedPointer(SentinelPointer)
      : value_(kCompressedSentinel) {}

  V8_INLINE const void* Load() const { return Decompress(value_); }
  V8_INLINE const void* LoadAtomic() const {
    return Decompress(
        reinterpret_cast<const std::atomic<Storage>&>(value_).load(
            std::memory_order_relaxed));
  }

  V8_INLINE void Store(const void* ptr) { value_ = Compress(ptr); }
  V8_INLINE void StoreAtomic(const void* value) {
    reinterpret_cast<std::atomic<Storage>&>(value_).store(
        Compress(value), std::memory_order_relaxed);
  }

  V8_INLINE void Clear() { value_ = 0u; }
  V8_INLINE bool IsCleared() const { return !value_; }

  V8_INLINE friend bool operator==(CompressedPointer a, CompressedPointer b) {
    return a.value_ == b.value_;
  }

  static V8_INLINE Storage Compress(const void* ptr) {
    static_assert(
        SentinelPointer::kSentinelValue == 0b10,
        "The compression scheme relies on the sentinel encoded as 0b10");
    static constexpr size_t kGigaCageMask =
        ~(api_constants::kCagedHeapReservationAlignment - 1);

    CPPGC_DCHECK(CageBaseGlobal::IsSet());
    const uintptr_t base = CageBaseGlobal::Get();
    CPPGC_DCHECK(!ptr || ptr == kSentinelPointer ||
                 (base & kGigaCageMask) ==
                     (reinterpret_cast<uintptr_t>(ptr) & kGigaCageMask));

    const auto uptr = reinterpret_cast<uintptr_t>(ptr);
    // Truncate the pointer and shift right by one.
    auto compressed = static_cast<Storage>(uptr) >> 1;
    // If the pointer is regular, set the most significant bit.
    if (V8_LIKELY(compressed > 1)) {
      CPPGC_DCHECK((reinterpret_cast<uintptr_t>(ptr) &
                    (api_constants::kAllocationGranularity - 1)) == 0);
      compressed |= 0x80000000;
    }
    return compressed;
  }

  static V8_INLINE void* Decompress(Storage ptr) {
    CPPGC_DCHECK(CageBaseGlobal::IsSet());
    const uintptr_t base = CageBaseGlobal::Get();
    // Sign extend the pointer and shift left by one.
    const int64_t mask = static_cast<int64_t>(static_cast<int32_t>(ptr)) << 1;
    return reinterpret_cast<void*>(mask & base);
  }

 private:
  static constexpr Storage kCompressedSentinel =
      SentinelPointer::kSentinelValue >> 1;
  // All constructors initialize `value_`. Do not add a default value here as it
  // results in a non-atomic write on some builds, even when the atomic version
  // of the constructor is used.
  Storage value_;
};

#endif  // defined(CPPGC_POINTER_COMPRESSION)

class RawPointer final {
 public:
  using Storage = uintptr_t;

  RawPointer() : ptr_(nullptr) {}
  explicit RawPointer(const void* ptr) : ptr_(ptr) {}

  V8_INLINE const void* Load() const { return ptr_; }
  V8_INLINE const void* LoadAtomic() const {
    return reinterpret_cast<const std::atomic<const void*>&>(ptr_).load(
        std::memory_order_relaxed);
  }

  V8_INLINE void Store(const void* ptr) { ptr_ = ptr; }
  V8_INLINE void StoreAtomic(const void* ptr) {
    reinterpret_cast<std::atomic<const void*>&>(ptr_).store(
        ptr, std::memory_order_relaxed);
  }

  V8_INLINE void Clear() { ptr_ = nullptr; }
  V8_INLINE bool IsCleared() const { return !ptr_; }

  V8_INLINE friend bool operator==(RawPointer a, RawPointer b) {
    return a.ptr_ == b.ptr_;
  }

 private:
  // All constructors initialize `ptr_`. Do not add a default value here as it
  // results in a non-atomic write on some builds, even when the atomic version
  // of the constructor is used.
  const void* ptr_;
};

// MemberBase always refers to the object as const object and defers to
// BasicMember on casting to the right type as needed.
class MemberBase {
 protected:
#if defined(CPPGC_POINTER_COMPRESSION)
  using RawStorage = CompressedPointer;
#else   // !defined(CPPGC_POINTER_COMPRESSION)
  using RawStorage = RawPointer;
#endif  // !defined(CPPGC_POINTER_COMPRESSION)

  struct AtomicInitializerTag {};

  MemberBase() = default;
  explicit MemberBase(const void* value) : raw_(value) {}
  MemberBase(const void* value, AtomicInitializerTag) { SetRawAtomic(value); }

  explicit MemberBase(RawStorage raw) : raw_(raw) {}
  explicit MemberBase(std::nullptr_t) : raw_(nullptr) {}
  explicit MemberBase(SentinelPointer s) : raw_(s) {}

  const void** GetRawSlot() const {
    return reinterpret_cast<const void**>(const_cast<MemberBase*>(this));
  }
  const void* GetRaw() const { return raw_.Load(); }
  void SetRaw(void* value) { raw_.Store(value); }

  const void* GetRawAtomic() const { return raw_.LoadAtomic(); }
  void SetRawAtomic(const void* value) { raw_.StoreAtomic(value); }

  RawStorage GetRawStorage() const { return raw_; }
  void SetRawStorageAtomic(RawStorage other) {
    reinterpret_cast<std::atomic<RawStorage>&>(raw_).store(
        other, std::memory_order_relaxed);
  }

  bool IsCleared() const { return raw_.IsCleared(); }

  void ClearFromGC() const { raw_.Clear(); }

 private:
  mutable RawStorage raw_;
};

// The basic class from which all Member classes are 'generated'.
template <typename T, typename WeaknessTag, typename WriteBarrierPolicy,
          typename CheckingPolicy>
class BasicMember final : private MemberBase, private CheckingPolicy {
 public:
  using PointeeType = T;

  constexpr BasicMember() = default;
  constexpr BasicMember(std::nullptr_t) {}           // NOLINT
  BasicMember(SentinelPointer s) : MemberBase(s) {}  // NOLINT
  BasicMember(T* raw) : MemberBase(raw) {            // NOLINT
    InitializingWriteBarrier();
    this->CheckPointer(Get());
  }
  BasicMember(T& raw) : BasicMember(&raw) {}  // NOLINT
  // Atomic ctor. Using the AtomicInitializerTag forces BasicMember to
  // initialize using atomic assignments. This is required for preventing
  // data races with concurrent marking.
  using AtomicInitializerTag = MemberBase::AtomicInitializerTag;
  BasicMember(std::nullptr_t, AtomicInitializerTag atomic)
      : MemberBase(nullptr, atomic) {}
  BasicMember(SentinelPointer s, AtomicInitializerTag atomic)
      : MemberBase(s, atomic) {}
  BasicMember(T* raw, AtomicInitializerTag atomic) : MemberBase(raw, atomic) {
    InitializingWriteBarrier();
    this->CheckPointer(Get());
  }
  BasicMember(T& raw, AtomicInitializerTag atomic)
      : BasicMember(&raw, atomic) {}
  // Copy ctor.
  BasicMember(const BasicMember& other) : BasicMember(other.GetRawStorage()) {}
  // Allow heterogeneous construction.
  template <typename U, typename OtherBarrierPolicy, typename OtherWeaknessTag,
            typename OtherCheckingPolicy,
            typename = std::enable_if_t<std::is_base_of<T, U>::value>>
  BasicMember(  // NOLINT
      const BasicMember<U, OtherWeaknessTag, OtherBarrierPolicy,
                        OtherCheckingPolicy>& other)
      : BasicMember(other.GetRawStorage()) {}
  // Move ctor.
  BasicMember(BasicMember&& other) noexcept
      : BasicMember(other.GetRawStorage()) {
    other.Clear();
  }
  // Allow heterogeneous move construction.
  template <typename U, typename OtherBarrierPolicy, typename OtherWeaknessTag,
            typename OtherCheckingPolicy,
            typename = std::enable_if_t<std::is_base_of<T, U>::value>>
  BasicMember(BasicMember<U, OtherWeaknessTag, OtherBarrierPolicy,
                          OtherCheckingPolicy>&& other) noexcept
      : BasicMember(other.GetRawStorage()) {
    other.Clear();
  }
  // Construction from Persistent.
  template <typename U, typename PersistentWeaknessPolicy,
            typename PersistentLocationPolicy,
            typename PersistentCheckingPolicy,
            typename = std::enable_if_t<std::is_base_of<T, U>::value>>
  BasicMember(const BasicPersistent<U, PersistentWeaknessPolicy,
                                    PersistentLocationPolicy,
                                    PersistentCheckingPolicy>& p)
      : BasicMember(p.Get()) {}

  // Copy assignment.
  BasicMember& operator=(const BasicMember& other) {
    return operator=(other.GetRawStorage());
  }
  // Allow heterogeneous copy assignment.
  template <typename U, typename OtherWeaknessTag, typename OtherBarrierPolicy,
            typename OtherCheckingPolicy,
            typename = std::enable_if_t<std::is_base_of<T, U>::value>>
  BasicMember& operator=(
      const BasicMember<U, OtherWeaknessTag, OtherBarrierPolicy,
                        OtherCheckingPolicy>& other) {
    return operator=(other.GetRawStorage());
  }
  // Move assignment.
  BasicMember& operator=(BasicMember&& other) noexcept {
    operator=(other.GetRawStorage());
    other.Clear();
    return *this;
  }
  // Heterogeneous move assignment.
  template <typename U, typename OtherWeaknessTag, typename OtherBarrierPolicy,
            typename OtherCheckingPolicy,
            typename = std::enable_if_t<std::is_base_of<T, U>::value>>
  BasicMember& operator=(BasicMember<U, OtherWeaknessTag, OtherBarrierPolicy,
                                     OtherCheckingPolicy>&& other) noexcept {
    operator=(other.GetRawStorage());
    other.Clear();
    return *this;
  }
  // Assignment from Persistent.
  template <typename U, typename PersistentWeaknessPolicy,
            typename PersistentLocationPolicy,
            typename PersistentCheckingPolicy,
            typename = std::enable_if_t<std::is_base_of<T, U>::value>>
  BasicMember& operator=(
      const BasicPersistent<U, PersistentWeaknessPolicy,
                            PersistentLocationPolicy, PersistentCheckingPolicy>&
          other) {
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
  BasicMember& operator=(SentinelPointer s) {
    SetRawAtomic(s);
    return *this;
  }

  template <typename OtherWeaknessTag, typename OtherBarrierPolicy,
            typename OtherCheckingPolicy>
  void Swap(BasicMember<T, OtherWeaknessTag, OtherBarrierPolicy,
                        OtherCheckingPolicy>& other) {
    auto tmp = GetRawStorage();
    *this = other;
    other = tmp;
  }

  explicit operator bool() const { return !IsCleared(); }
  operator T*() const { return Get(); }
  T* operator->() const { return Get(); }
  T& operator*() const { return *Get(); }

  // CFI cast exemption to allow passing SentinelPointer through T* and support
  // heterogeneous assignments between different Member and Persistent handles
  // based on their actual types.
  V8_CLANG_NO_SANITIZE("cfi-unrelated-cast") T* Get() const {
    // Executed by the mutator, hence non atomic load.
    //
    // The const_cast below removes the constness from MemberBase storage. The
    // following static_cast re-adds any constness if specified through the
    // user-visible template parameter T.
    return static_cast<T*>(const_cast<void*>(MemberBase::GetRaw()));
  }

  void Clear() { SetRawStorageAtomic(RawStorage{}); }

  T* Release() {
    T* result = Get();
    Clear();
    return result;
  }

  const T** GetSlotForTesting() const {
    return reinterpret_cast<const T**>(GetRawSlot());
  }

 private:
  explicit BasicMember(RawStorage raw) : MemberBase(raw) {
    InitializingWriteBarrier();
    this->CheckPointer(Get());
  }

  BasicMember& operator=(RawStorage other) {
    SetRawStorageAtomic(other);
    AssigningWriteBarrier();
    this->CheckPointer(Get());
    return *this;
  }

  const T* GetRawAtomic() const {
    return static_cast<const T*>(MemberBase::GetRawAtomic());
  }

  void InitializingWriteBarrier() const {
    WriteBarrierPolicy::InitializingBarrier(GetRawSlot(), GetRaw());
  }
  void AssigningWriteBarrier() const {
    WriteBarrierPolicy::AssigningBarrier(GetRawSlot(), GetRaw());
  }

  void ClearFromGC() const { MemberBase::ClearFromGC(); }

  T* GetFromGC() const { return Get(); }

  friend class cppgc::Visitor;
  template <typename U>
  friend struct cppgc::TraceTrait;
  template <typename T1, typename WeaknessTag1, typename WriteBarrierPolicy1,
            typename CheckingPolicy1>
  friend class BasicMember;
  template <typename T1, typename WeaknessTag1, typename WriteBarrierPolicy1,
            typename CheckingPolicy1, typename T2, typename WeaknessTag2,
            typename WriteBarrierPolicy2, typename CheckingPolicy2>
  friend bool operator==(
      const BasicMember<T1, WeaknessTag1, WriteBarrierPolicy1, CheckingPolicy1>&
          member1,
      const BasicMember<T2, WeaknessTag2, WriteBarrierPolicy2, CheckingPolicy2>&
          member2);
};

template <typename T1, typename WeaknessTag1, typename WriteBarrierPolicy1,
          typename CheckingPolicy1, typename T2, typename WeaknessTag2,
          typename WriteBarrierPolicy2, typename CheckingPolicy2>
bool operator==(const BasicMember<T1, WeaknessTag1, WriteBarrierPolicy1,
                                  CheckingPolicy1>& member1,
                const BasicMember<T2, WeaknessTag2, WriteBarrierPolicy2,
                                  CheckingPolicy2>& member2) {
  return member1.GetRawStorage() == member2.GetRawStorage();
}

template <typename T1, typename WeaknessTag1, typename WriteBarrierPolicy1,
          typename CheckingPolicy1, typename T2, typename WeaknessTag2,
          typename WriteBarrierPolicy2, typename CheckingPolicy2>
bool operator!=(const BasicMember<T1, WeaknessTag1, WriteBarrierPolicy1,
                                  CheckingPolicy1>& member1,
                const BasicMember<T2, WeaknessTag2, WriteBarrierPolicy2,
                                  CheckingPolicy2>& member2) {
  return !(member1 == member2);
}

template <typename T, typename WriteBarrierPolicy, typename CheckingPolicy>
struct IsWeak<
    internal::BasicMember<T, WeakMemberTag, WriteBarrierPolicy, CheckingPolicy>>
    : std::true_type {};

}  // namespace internal

/**
 * Members are used in classes to contain strong pointers to other garbage
 * collected objects. All Member fields of a class must be traced in the class'
 * trace method.
 */
template <typename T>
using Member = internal::BasicMember<T, internal::StrongMemberTag,
                                     internal::DijkstraWriteBarrierPolicy>;

/**
 * WeakMember is similar to Member in that it is used to point to other garbage
 * collected objects. However instead of creating a strong pointer to the
 * object, the WeakMember creates a weak pointer, which does not keep the
 * pointee alive. Hence if all pointers to to a heap allocated object are weak
 * the object will be garbage collected. At the time of GC the weak pointers
 * will automatically be set to null.
 */
template <typename T>
using WeakMember = internal::BasicMember<T, internal::WeakMemberTag,
                                         internal::DijkstraWriteBarrierPolicy>;

/**
 * UntracedMember is a pointer to an on-heap object that is not traced for some
 * reason. Do not use this unless you know what you are doing. Keeping raw
 * pointers to on-heap objects is prohibited unless used from stack. Pointee
 * must be kept alive through other means.
 */
template <typename T>
using UntracedMember = internal::BasicMember<T, internal::UntracedMemberTag,
                                             internal::NoWriteBarrierPolicy>;

}  // namespace cppgc

#endif  // INCLUDE_CPPGC_MEMBER_H_
