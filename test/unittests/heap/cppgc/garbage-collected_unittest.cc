// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/garbage-collected.h"

#include "include/cppgc/allocation.h"
#include "src/heap/cppgc/heap.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

class GCed : public GarbageCollected<GCed> {};
class NotGCed {};
class Mixin : public GarbageCollectedMixin {
 public:
  using GarbageCollectedMixin::GetObjectStart;
};
class GCedWithMixin : public GarbageCollected<GCedWithMixin>, public Mixin {
  USING_GARBAGE_COLLECTED_MIXIN();
};
class OtherMixin : public GarbageCollectedMixin {};
class MergedMixins : public Mixin, public OtherMixin {
  MERGE_GARBAGE_COLLECTED_MIXINS();
};
class GCWithMergedMixins : public GCed, public MergedMixins {
  USING_GARBAGE_COLLECTED_MIXIN();
};

class GarbageCollectedTestWithHeap : public testing::TestWithHeap {
  void TearDown() override {
    internal::Heap::From(GetHeap())->CollectGarbage();
    TestWithHeap::TearDown();
  }
};

}  // namespace

TEST(GarbageCollectedTest, GarbageCollectedTrait) {
  STATIC_ASSERT(!IsGarbageCollectedType<int>::value);
  STATIC_ASSERT(!IsGarbageCollectedType<NotGCed>::value);
  STATIC_ASSERT(IsGarbageCollectedType<GCed>::value);
  STATIC_ASSERT(IsGarbageCollectedType<Mixin>::value);
  STATIC_ASSERT(IsGarbageCollectedType<GCedWithMixin>::value);
  STATIC_ASSERT(IsGarbageCollectedType<MergedMixins>::value);
  STATIC_ASSERT(IsGarbageCollectedType<GCWithMergedMixins>::value);
}

TEST(GarbageCollectedTest, GarbageCollectedMixinTrait) {
  STATIC_ASSERT(!IsGarbageCollectedMixinType<int>::value);
  STATIC_ASSERT(!IsGarbageCollectedMixinType<GCed>::value);
  STATIC_ASSERT(!IsGarbageCollectedMixinType<NotGCed>::value);
  STATIC_ASSERT(IsGarbageCollectedMixinType<Mixin>::value);
  STATIC_ASSERT(IsGarbageCollectedMixinType<GCedWithMixin>::value);
  STATIC_ASSERT(IsGarbageCollectedMixinType<MergedMixins>::value);
  STATIC_ASSERT(IsGarbageCollectedMixinType<GCWithMergedMixins>::value);
}

#ifdef CPPGC_SUPPORTS_CONSERVATIVE_STACK_SCAN

TEST_F(GarbageCollectedTestWithHeap, GetObjectStartReturnsCorrentAddress) {
  GCed* gced = MakeGarbageCollected<GCed>(GetHeap());
  GCedWithMixin* gced_with_mixin =
      MakeGarbageCollected<GCedWithMixin>(GetHeap());
  EXPECT_EQ(gced_with_mixin,
            static_cast<Mixin*>(gced_with_mixin)->GetObjectStart());
  EXPECT_NE(gced, static_cast<Mixin*>(gced_with_mixin)->GetObjectStart());
}

#endif  // CPPGC_SUPPORTS_CONSERVATIVE_STACK_SCAN

}  // namespace internal
}  // namespace cppgc
