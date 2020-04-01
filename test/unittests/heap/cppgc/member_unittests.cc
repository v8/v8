// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include "include/cppgc/garbage-collected.h"
#include "include/cppgc/member.h"
#include "include/cppgc/type_traits.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

struct GCed : GarbageCollected<GCed> {};
struct DerivedGCed : GCed {};

// Compile tests.
static_assert(!IsWeakV<Member<GCed>>, "Member is always strong.");
static_assert(IsWeakV<WeakMember<GCed>>, "WeakMember is always weak.");

struct CustomWriteBarrierPolicy {
  static size_t InitializingWriteBarriersTriggered;
  static size_t AssigningWriteBarriersTriggered;
  static void InitializingBarrier(const void* slot, const void* value) {
    ++InitializingWriteBarriersTriggered;
  }
  static void AssigningBarrier(const void* slot, const void* value) {
    ++AssigningWriteBarriersTriggered;
  }
};
size_t CustomWriteBarrierPolicy::InitializingWriteBarriersTriggered = 0;
size_t CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered = 0;

using MemberWithCustomBarrier =
    BasicStrongMember<GCed, CustomWriteBarrierPolicy>;

struct CustomCheckingPolicy {
  static std::array<GCed, 10> Array;
  static size_t ChecksTriggered;
  void CheckPointer(const void* ptr) {
    EXPECT_LE(Array.data(), ptr);
    EXPECT_GT(Array.data() + Array.size(), ptr);
    ++ChecksTriggered;
  }
};
std::array<GCed, 10> CustomCheckingPolicy::Array;
size_t CustomCheckingPolicy::ChecksTriggered = 0;

using MemberWithCustomChecking =
    internal::BasicMember<GCed, class StrongMemberTag,
                          DijkstraWriteBarrierPolicy, CustomCheckingPolicy>;

template <template <typename> class Member>
void EmptyTest() {
  {
    Member<GCed> empty;
    EXPECT_EQ(nullptr, empty.Get());
    EXPECT_EQ(nullptr, empty.Release());
  }
  {
    Member<GCed> empty = nullptr;
    EXPECT_EQ(nullptr, empty.Get());
    EXPECT_EQ(nullptr, empty.Release());
  }
}

TEST(MemberTest, Empty) {
  EmptyTest<Member>();
  EmptyTest<WeakMember>();
  EmptyTest<UntracedMember>();
}

template <template <typename> class Member>
void ClearTest() {
  GCed gced;
  Member<GCed> member = &gced;
  EXPECT_NE(nullptr, member.Get());
  member.Clear();
  EXPECT_EQ(nullptr, member.Get());
}

TEST(MemberTest, Clear) {
  ClearTest<Member>();
  ClearTest<WeakMember>();
  ClearTest<UntracedMember>();
}

template <template <typename> class Member>
void ReleaseTest() {
  GCed gced;
  Member<GCed> member = &gced;
  EXPECT_NE(nullptr, member.Get());
  GCed* raw = member.Release();
  EXPECT_EQ(&gced, raw);
  EXPECT_EQ(nullptr, member.Get());
}

TEST(MemberTest, Release) {
  ReleaseTest<Member>();
  ReleaseTest<WeakMember>();
  ReleaseTest<UntracedMember>();
}

template <template <typename> class Member1, template <typename> class Member2>
void SwapTest() {
  GCed gced1, gced2;
  Member1<GCed> member1 = &gced1;
  Member2<GCed> member2 = &gced2;
  EXPECT_EQ(&gced1, member1.Get());
  EXPECT_EQ(&gced2, member2.Get());
  member1.Swap(member2);
  EXPECT_EQ(&gced2, member1.Get());
  EXPECT_EQ(&gced1, member2.Get());
}

TEST(MemberTest, Swap) {
  SwapTest<Member, Member>();
  SwapTest<Member, WeakMember>();
  SwapTest<Member, UntracedMember>();
  SwapTest<WeakMember, Member>();
  SwapTest<WeakMember, WeakMember>();
  SwapTest<WeakMember, UntracedMember>();
  SwapTest<UntracedMember, Member>();
  SwapTest<UntracedMember, WeakMember>();
  SwapTest<UntracedMember, UntracedMember>();
}

template <template <typename> class Member1, template <typename> class Member2>
void HeterogeneousConversionTest() {
  {
    GCed gced;
    Member1<GCed> member1 = &gced;
    Member2<GCed> member2 = member1;
    EXPECT_EQ(member1.Get(), member2.Get());
  }
  {
    DerivedGCed gced;
    Member1<DerivedGCed> member1 = &gced;
    Member2<GCed> member2 = member1;
    EXPECT_EQ(member1.Get(), member2.Get());
  }
  {
    GCed gced;
    Member1<GCed> member1 = &gced;
    Member2<GCed> member2;
    member2 = member1;
    EXPECT_EQ(member1.Get(), member2.Get());
  }
  {
    DerivedGCed gced;
    Member1<DerivedGCed> member1 = &gced;
    Member2<GCed> member2;
    member2 = member1;
    EXPECT_EQ(member1.Get(), member2.Get());
  }
}

TEST(MemberTest, HeterogeneousInterface) {
  HeterogeneousConversionTest<Member, Member>();
  HeterogeneousConversionTest<Member, WeakMember>();
  HeterogeneousConversionTest<Member, UntracedMember>();
  HeterogeneousConversionTest<WeakMember, Member>();
  HeterogeneousConversionTest<WeakMember, WeakMember>();
  HeterogeneousConversionTest<WeakMember, UntracedMember>();
  HeterogeneousConversionTest<UntracedMember, Member>();
  HeterogeneousConversionTest<UntracedMember, WeakMember>();
  HeterogeneousConversionTest<UntracedMember, UntracedMember>();
}

template <template <typename> class Member1, template <typename> class Member2>
void EqualityTest() {
  {
    GCed gced;
    Member1<GCed> member1 = &gced;
    Member2<GCed> member2 = &gced;
    EXPECT_TRUE(member1 == member2);
    EXPECT_FALSE(member1 != member2);
    member2 = member1;
    EXPECT_TRUE(member1 == member2);
    EXPECT_FALSE(member1 != member2);
  }
  {
    GCed gced1;
    GCed gced2;
    Member1<GCed> member1 = &gced1;
    Member2<GCed> member2 = &gced2;
    EXPECT_TRUE(member1 != member2);
    EXPECT_FALSE(member1 == member2);
  }
}

TEST(MemberTest, EqualityTest) {
  EqualityTest<Member, Member>();
  EqualityTest<Member, WeakMember>();
  EqualityTest<Member, UntracedMember>();
  EqualityTest<WeakMember, Member>();
  EqualityTest<WeakMember, WeakMember>();
  EqualityTest<WeakMember, UntracedMember>();
  EqualityTest<UntracedMember, Member>();
  EqualityTest<UntracedMember, WeakMember>();
  EqualityTest<UntracedMember, UntracedMember>();
}

TEST(MemberTest, WriteBarrierTriggered) {
  CustomWriteBarrierPolicy::InitializingWriteBarriersTriggered = 0;
  CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered = 0;
  GCed gced;
  MemberWithCustomBarrier member1 = &gced;
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::InitializingWriteBarriersTriggered);
  EXPECT_EQ(0u, CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered);
  member1 = &gced;
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::InitializingWriteBarriersTriggered);
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered);
  member1 = nullptr;
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::InitializingWriteBarriersTriggered);
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered);
  MemberWithCustomBarrier member2 = nullptr;
  // No initializing barriers for std::nullptr_t.
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::InitializingWriteBarriersTriggered);
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered);
  member2 = kMemberSentinel;
  // No initializing barriers for member sentinel.
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::InitializingWriteBarriersTriggered);
  EXPECT_EQ(1u, CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered);
  member2.Swap(member1);
  EXPECT_EQ(3u, CustomWriteBarrierPolicy::AssigningWriteBarriersTriggered);
}

TEST(MemberTest, CheckingPolocy) {
  CustomCheckingPolicy::ChecksTriggered = 0u;
  MemberWithCustomChecking member;
  for (GCed& item : CustomCheckingPolicy::Array) {
    member = &item;
  }
  EXPECT_EQ(CustomCheckingPolicy::Array.size(),
            CustomCheckingPolicy::ChecksTriggered);
}
}  // namespace internal
}  // namespace cppgc
