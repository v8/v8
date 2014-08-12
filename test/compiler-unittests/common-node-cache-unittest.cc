// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/common-node-cache.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "test/compiler-unittests/compiler-unittests.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::AllOf;
using testing::IsNull;
using testing::NotNull;
using testing::Pointee;

namespace v8 {
namespace internal {
namespace compiler {

class CommonNodeCacheTest : public CompilerTest {
 public:
  CommonNodeCacheTest() : cache_(zone()), common_(zone()), graph_(zone()) {}
  virtual ~CommonNodeCacheTest() {}

 protected:
  Factory* factory() const { return isolate()->factory(); }
  CommonNodeCache* cache() { return &cache_; }
  CommonOperatorBuilder* common() { return &common_; }
  Graph* graph() { return &graph_; }

 private:
  CommonNodeCache cache_;
  CommonOperatorBuilder common_;
  Graph graph_;
};


TEST_F(CommonNodeCacheTest, FindInt32Constant) {
  Node** l42 = cache()->FindInt32Constant(42);
  ASSERT_THAT(l42, AllOf(NotNull(), Pointee(IsNull())));
  Node* n42 = *l42 = graph()->NewNode(common()->Int32Constant(42));

  Node** l0 = cache()->FindInt32Constant(0);
  ASSERT_THAT(l0, AllOf(NotNull(), Pointee(IsNull())));
  Node* n0 = *l0 = graph()->NewNode(common()->Int32Constant(0));

  EXPECT_THAT(cache()->FindInt32Constant(42), AllOf(l42, Pointee(n42)));
  EXPECT_THAT(cache()->FindInt32Constant(0), AllOf(l0, Pointee(n0)));
  EXPECT_THAT(cache()->FindInt32Constant(42), AllOf(l42, Pointee(n42)));
  EXPECT_THAT(cache()->FindInt32Constant(0), AllOf(l0, Pointee(n0)));
}


TEST_F(CommonNodeCacheTest, FindFloat64Constant) {
  Node** l42 = cache()->FindFloat64Constant(42.0);
  ASSERT_THAT(l42, AllOf(NotNull(), Pointee(IsNull())));
  Node* n42 = *l42 = graph()->NewNode(common()->Float64Constant(42.0));

  Node** l0 = cache()->FindFloat64Constant(0.0);
  ASSERT_THAT(l0, AllOf(NotNull(), Pointee(IsNull())));
  Node* n0 = *l0 = graph()->NewNode(common()->Float64Constant(0.0));

  EXPECT_THAT(cache()->FindFloat64Constant(42.0), AllOf(l42, Pointee(n42)));
  EXPECT_THAT(cache()->FindFloat64Constant(0.0), AllOf(l0, Pointee(n0)));
  EXPECT_THAT(cache()->FindFloat64Constant(42.0), AllOf(l42, Pointee(n42)));
  EXPECT_THAT(cache()->FindFloat64Constant(0.0), AllOf(l0, Pointee(n0)));
}


TEST_F(CommonNodeCacheTest, FindExternalConstant) {
  ExternalReference i = ExternalReference::isolate_address(isolate());
  Node** li = cache()->FindExternalConstant(i);
  ASSERT_THAT(li, AllOf(NotNull(), Pointee(IsNull())));
  Node* ni = *li = graph()->NewNode(common()->ExternalConstant(i));

  ExternalReference m = ExternalReference::address_of_min_int();
  Node** lm = cache()->FindExternalConstant(m);
  ASSERT_THAT(lm, AllOf(NotNull(), Pointee(IsNull())));
  Node* nm = *lm = graph()->NewNode(common()->ExternalConstant(m));

  EXPECT_THAT(cache()->FindExternalConstant(i), AllOf(li, Pointee(ni)));
  EXPECT_THAT(cache()->FindExternalConstant(m), AllOf(lm, Pointee(nm)));
  EXPECT_THAT(cache()->FindExternalConstant(i), AllOf(li, Pointee(ni)));
  EXPECT_THAT(cache()->FindExternalConstant(m), AllOf(lm, Pointee(nm)));
}


TEST_F(CommonNodeCacheTest, FindNumberConstant) {
  Node** l42 = cache()->FindNumberConstant(42.0);
  ASSERT_THAT(l42, AllOf(NotNull(), Pointee(IsNull())));
  Node* n42 = *l42 = graph()->NewNode(common()->NumberConstant(42.0));

  Node** l0 = cache()->FindNumberConstant(0.0);
  ASSERT_THAT(l0, AllOf(NotNull(), Pointee(IsNull())));
  Node* n0 = *l0 = graph()->NewNode(common()->NumberConstant(0.0));

  EXPECT_THAT(cache()->FindNumberConstant(42.0), AllOf(l42, Pointee(n42)));
  EXPECT_THAT(cache()->FindNumberConstant(0.0), AllOf(l0, Pointee(n0)));
  EXPECT_THAT(cache()->FindNumberConstant(42.0), AllOf(l42, Pointee(n42)));
  EXPECT_THAT(cache()->FindNumberConstant(0.0), AllOf(l0, Pointee(n0)));
}


TEST_F(CommonNodeCacheTest, FindHeapConstant) {
  PrintableUnique<HeapObject> n = PrintableUnique<HeapObject>::CreateImmovable(
      zone(), factory()->null_value());
  Node** ln = cache()->FindHeapConstant(n);
  ASSERT_THAT(ln, AllOf(NotNull(), Pointee(IsNull())));
  Node* nn = *ln = graph()->NewNode(common()->HeapConstant(n));

  PrintableUnique<HeapObject> t = PrintableUnique<HeapObject>::CreateImmovable(
      zone(), factory()->true_value());
  Node** lt = cache()->FindHeapConstant(t);
  ASSERT_THAT(lt, AllOf(NotNull(), Pointee(IsNull())));
  Node* nt = *lt = graph()->NewNode(common()->HeapConstant(t));

  EXPECT_THAT(cache()->FindHeapConstant(n), AllOf(ln, Pointee(nn)));
  EXPECT_THAT(cache()->FindHeapConstant(t), AllOf(lt, Pointee(nt)));
  EXPECT_THAT(cache()->FindHeapConstant(n), AllOf(ln, Pointee(nn)));
  EXPECT_THAT(cache()->FindHeapConstant(t), AllOf(lt, Pointee(nt)));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
