// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "src/compiler/control-builders.h"
#include "src/compiler/generic-node-inl.h"
#include "src/compiler/node-properties-inl.h"
#include "src/compiler/pipeline.h"
#include "src/compiler/simplified-lowering.h"
#include "src/compiler/simplified-node-factory.h"
#include "src/compiler/typer.h"
#include "src/compiler/verifier.h"
#include "src/execution.h"
#include "src/parser.h"
#include "src/rewriter.h"
#include "src/scopes.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/cctest/compiler/graph-builder-tester.h"
#include "test/cctest/compiler/value-helper.h"

using namespace v8::internal;
using namespace v8::internal::compiler;

// TODO(titzer): rename this to VMLoweringTester
template <typename ReturnType>
class SimplifiedGraphBuilderTester : public GraphBuilderTester<ReturnType> {
 public:
  SimplifiedGraphBuilderTester(MachineRepresentation p0 = kMachineLast,
                               MachineRepresentation p1 = kMachineLast,
                               MachineRepresentation p2 = kMachineLast,
                               MachineRepresentation p3 = kMachineLast,
                               MachineRepresentation p4 = kMachineLast)
      : GraphBuilderTester<ReturnType>(p0, p1, p2, p3, p4),
        typer(this->zone()),
        source_positions(this->graph()),
        jsgraph(this->graph(), this->common(), &typer),
        lowering(&jsgraph, &source_positions) {}

  Typer typer;
  SourcePositionTable source_positions;
  JSGraph jsgraph;
  SimplifiedLowering lowering;

  // Close graph and lower one node.
  void Lower(Node* node) {
    this->End();
    if (node == NULL) {
      lowering.LowerAllNodes();
    } else {
      lowering.Lower(node);
    }
  }

  // Close graph and lower all nodes.
  void LowerAllNodes() { Lower(NULL); }

  void StoreFloat64(Node* node, double* ptr) {
    Node* ptr_node = this->PointerConstant(ptr);
    this->Store(kMachineFloat64, ptr_node, node);
  }

  Node* LoadInt32(int32_t* ptr) {
    Node* ptr_node = this->PointerConstant(ptr);
    return this->Load(kMachineWord32, ptr_node);
  }

  Node* LoadUint32(uint32_t* ptr) {
    Node* ptr_node = this->PointerConstant(ptr);
    return this->Load(kMachineWord32, ptr_node);
  }

  Node* LoadFloat64(double* ptr) {
    Node* ptr_node = this->PointerConstant(ptr);
    return this->Load(kMachineFloat64, ptr_node);
  }

  Factory* factory() { return this->isolate()->factory(); }
  Heap* heap() { return this->isolate()->heap(); }
};


// TODO(dcarney): find a home for these functions.
namespace {

FieldAccess ForJSObjectMap() {
  FieldAccess access = {JSObject::kMapOffset, Handle<Name>(), Type::Any(),
                        kMachineTagged};
  return access;
}


FieldAccess ForJSObjectProperties() {
  FieldAccess access = {JSObject::kPropertiesOffset, Handle<Name>(),
                        Type::Any(), kMachineTagged};
  return access;
}


FieldAccess ForArrayBufferBackingStore() {
  FieldAccess access = {
      JSArrayBuffer::kBackingStoreOffset, Handle<Name>(), Type::UntaggedPtr(),
      MachineOperatorBuilder::pointer_rep(),
  };
  return access;
}


ElementAccess ForFixedArrayElement() {
  ElementAccess access = {FixedArray::kHeaderSize, Type::Any(), kMachineTagged};
  return access;
}


ElementAccess ForBackingStoreElement(MachineRepresentation rep) {
  ElementAccess access = {kNonHeapObjectHeaderSize, Type::Any(), rep};
  return access;
}
}


// Create a simple JSObject with a unique map.
static Handle<JSObject> TestObject() {
  static int index = 0;
  char buffer[50];
  v8::base::OS::SNPrintF(buffer, 50, "({'a_%d':1})", index++);
  return Handle<JSObject>::cast(v8::Utils::OpenHandle(*CompileRun(buffer)));
}


TEST(RunLoadMap) {
  SimplifiedGraphBuilderTester<Object*> t(kMachineTagged);
  FieldAccess access = ForJSObjectMap();
  Node* load = t.LoadField(access, t.Parameter(0));
  t.Return(load);

  t.LowerAllNodes();

  if (!Pipeline::SupportedTarget()) return;

  Handle<JSObject> src = TestObject();
  Handle<Map> src_map(src->map());
  Object* result = t.Call(*src);
  CHECK_EQ(*src_map, result);
}


TEST(RunStoreMap) {
  SimplifiedGraphBuilderTester<int32_t> t(kMachineTagged, kMachineTagged);
  FieldAccess access = ForJSObjectMap();
  t.StoreField(access, t.Parameter(1), t.Parameter(0));
  t.Return(t.Int32Constant(0));

  t.LowerAllNodes();

  if (!Pipeline::SupportedTarget()) return;

  Handle<JSObject> src = TestObject();
  Handle<Map> src_map(src->map());
  Handle<JSObject> dst = TestObject();
  CHECK(src->map() != dst->map());
  t.Call(*src_map, *dst);
  CHECK(*src_map == dst->map());
}


TEST(RunLoadProperties) {
  SimplifiedGraphBuilderTester<Object*> t(kMachineTagged);
  FieldAccess access = ForJSObjectProperties();
  Node* load = t.LoadField(access, t.Parameter(0));
  t.Return(load);

  t.LowerAllNodes();

  if (!Pipeline::SupportedTarget()) return;

  Handle<JSObject> src = TestObject();
  Handle<FixedArray> src_props(src->properties());
  Object* result = t.Call(*src);
  CHECK_EQ(*src_props, result);
}


TEST(RunLoadStoreMap) {
  SimplifiedGraphBuilderTester<Object*> t(kMachineTagged, kMachineTagged);
  FieldAccess access = ForJSObjectMap();
  Node* load = t.LoadField(access, t.Parameter(0));
  t.StoreField(access, t.Parameter(1), load);
  t.Return(load);

  t.LowerAllNodes();

  if (!Pipeline::SupportedTarget()) return;

  Handle<JSObject> src = TestObject();
  Handle<Map> src_map(src->map());
  Handle<JSObject> dst = TestObject();
  CHECK(src->map() != dst->map());
  Object* result = t.Call(*src, *dst);
  CHECK(result->IsMap());
  CHECK_EQ(*src_map, result);
  CHECK(*src_map == dst->map());
}


TEST(RunLoadStoreFixedArrayIndex) {
  SimplifiedGraphBuilderTester<Object*> t(kMachineTagged);
  ElementAccess access = ForFixedArrayElement();
  Node* load = t.LoadElement(access, t.Parameter(0), t.Int32Constant(0));
  t.StoreElement(access, t.Parameter(0), t.Int32Constant(1), load);
  t.Return(load);

  t.LowerAllNodes();

  if (!Pipeline::SupportedTarget()) return;

  Handle<FixedArray> array = t.factory()->NewFixedArray(2);
  Handle<JSObject> src = TestObject();
  Handle<JSObject> dst = TestObject();
  array->set(0, *src);
  array->set(1, *dst);
  Object* result = t.Call(*array);
  CHECK_EQ(*src, result);
  CHECK_EQ(*src, array->get(0));
  CHECK_EQ(*src, array->get(1));
}


TEST(RunLoadStoreArrayBuffer) {
  SimplifiedGraphBuilderTester<int32_t> t(kMachineTagged);
  const int index = 12;
  FieldAccess access = ForArrayBufferBackingStore();
  Node* backing_store = t.LoadField(access, t.Parameter(0));
  ElementAccess buffer_access = ForBackingStoreElement(kMachineWord8);
  Node* load =
      t.LoadElement(buffer_access, backing_store, t.Int32Constant(index));
  t.StoreElement(buffer_access, backing_store, t.Int32Constant(index + 1),
                 load);
  t.Return(load);

  t.LowerAllNodes();

  if (!Pipeline::SupportedTarget()) return;

  Handle<JSArrayBuffer> array = t.factory()->NewJSArrayBuffer();
  const int array_length = 2 * index;
  Runtime::SetupArrayBufferAllocatingData(t.isolate(), array, array_length);
  uint8_t* data = reinterpret_cast<uint8_t*>(array->backing_store());
  for (int i = 0; i < array_length; i++) {
    data[i] = i;
  }
  int32_t result = t.Call(*array);
  CHECK_EQ(index, result);
  for (int i = 0; i < array_length; i++) {
    uint8_t expected = i;
    if (i == (index + 1)) expected = result;
    CHECK_EQ(data[i], expected);
  }
}


TEST(RunCopyFixedArray) {
  SimplifiedGraphBuilderTester<int32_t> t(kMachineTagged, kMachineTagged);

  const int kArraySize = 15;
  Node* one = t.Int32Constant(1);
  Node* index = t.Int32Constant(0);
  Node* limit = t.Int32Constant(kArraySize);
  t.environment()->Push(index);
  {
    LoopBuilder loop(&t);
    loop.BeginLoop();
    // Loop exit condition.
    index = t.environment()->Top();
    Node* condition = t.Int32LessThan(index, limit);
    loop.BreakUnless(condition);
    // src[index] = dst[index].
    index = t.environment()->Pop();
    ElementAccess access = ForFixedArrayElement();
    Node* src = t.Parameter(0);
    Node* load = t.LoadElement(access, src, index);
    Node* dst = t.Parameter(1);
    t.StoreElement(access, dst, index, load);
    // index++
    index = t.Int32Add(index, one);
    t.environment()->Push(index);
    // continue.
    loop.EndBody();
    loop.EndLoop();
  }
  index = t.environment()->Pop();
  t.Return(index);

  t.LowerAllNodes();

  if (!Pipeline::SupportedTarget()) return;

  Handle<FixedArray> src = t.factory()->NewFixedArray(kArraySize);
  Handle<FixedArray> src_copy = t.factory()->NewFixedArray(kArraySize);
  Handle<FixedArray> dst = t.factory()->NewFixedArray(kArraySize);
  for (int i = 0; i < kArraySize; i++) {
    src->set(i, *TestObject());
    src_copy->set(i, src->get(i));
    dst->set(i, *TestObject());
    CHECK_NE(src_copy->get(i), dst->get(i));
  }
  CHECK_EQ(kArraySize, t.Call(*src, *dst));
  for (int i = 0; i < kArraySize; i++) {
    CHECK_EQ(src_copy->get(i), dst->get(i));
  }
}
