// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef CPPGC_SUPPORTS_CONSERVATIVE_STACK_SCAN

#include "src/heap/cppgc/heap.h"
#include "include/cppgc/allocation.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

class GCHeapTest : public testing::TestWithHeap {
 public:
  void ConservativeGC() {
    internal::Heap::From(GetHeap())->CollectGarbage(
        {Heap::GCConfig::StackState::kNonEmpty});
  }
  void PreciseGC() {
    internal::Heap::From(GetHeap())->CollectGarbage(
        {Heap::GCConfig::StackState::kEmpty});
  }
};

class Foo : public GarbageCollected<Foo> {
 public:
  static size_t destructor_callcount;

  Foo() { destructor_callcount = 0; }
  ~Foo() { destructor_callcount++; }
};

size_t Foo::destructor_callcount;

}  // namespace

TEST_F(GCHeapTest, PreciseGCReclaimsObjectOnStack) {
  Foo* volatile do_not_acces = MakeGarbageCollected<Foo>(GetHeap());
  USE(do_not_acces);
  EXPECT_EQ(0u, Foo::destructor_callcount);
  PreciseGC();
  EXPECT_EQ(1u, Foo::destructor_callcount);
}

TEST_F(GCHeapTest, ConservaitveGCRetainsObjectOnStack) {
  Foo* volatile do_not_acces = MakeGarbageCollected<Foo>(GetHeap());
  USE(do_not_acces);
  EXPECT_EQ(0u, Foo::destructor_callcount);
  ConservativeGC();
  EXPECT_EQ(0u, Foo::destructor_callcount);
  PreciseGC();
  EXPECT_EQ(1u, Foo::destructor_callcount);
}

}  // namespace internal
}  // namespace cppgc

#endif  // CPPGC_SUPPORTS_CONSERVATIVE_STACK_SCAN
