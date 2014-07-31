// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "src/compiler/control-builders.h"
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

template <typename ReturnType>
class SimplifiedGraphBuilderTester : public GraphBuilderTester<ReturnType> {
 public:
  SimplifiedGraphBuilderTester(MachineRepresentation p0 = kMachineLast,
                               MachineRepresentation p1 = kMachineLast,
                               MachineRepresentation p2 = kMachineLast,
                               MachineRepresentation p3 = kMachineLast,
                               MachineRepresentation p4 = kMachineLast)
      : GraphBuilderTester<ReturnType>(p0, p1, p2, p3, p4) {}

  // Close graph and lower one node.
  void Lower(Node* node) {
    this->End();
    Typer typer(this->zone());
    CommonOperatorBuilder common(this->zone());
    SourcePositionTable source_positions(this->graph());
    JSGraph jsgraph(this->graph(), &common, &typer);
    SimplifiedLowering lowering(&jsgraph, &source_positions);
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


class SimplifiedGraphBuilderJSTester
    : public SimplifiedGraphBuilderTester<Object*> {
 public:
  SimplifiedGraphBuilderJSTester()
      : SimplifiedGraphBuilderTester<Object*>(),
        f_(v8::Utils::OpenHandle(*v8::Handle<v8::Function>::Cast(CompileRun(
            "(function() { 'use strict'; return 2.7123; })")))),
        swapped_(false) {
    set_current_context(HeapConstant(handle(f_->context())));
  }

  template <typename T>
  T* CallJS() {
    if (!swapped_) {
      Compile();
    }
    Handle<Object>* args = NULL;
    MaybeHandle<Object> result = Execution::Call(
        isolate(), f_, factory()->undefined_value(), 0, args, false);
    return T::cast(*result.ToHandleChecked());
  }

 private:
  void Compile() {
    CompilationInfoWithZone info(f_);
    CHECK(Parser::Parse(&info));
    StrictMode strict_mode = info.function()->strict_mode();
    info.SetStrictMode(strict_mode);
    info.SetOptimizing(BailoutId::None(), Handle<Code>(f_->code()));
    CHECK(Rewriter::Rewrite(&info));
    CHECK(Scope::Analyze(&info));
    CHECK_NE(NULL, info.scope());
    Pipeline pipeline(&info);
    Linkage linkage(&info);
    Handle<Code> code = pipeline.GenerateCodeForMachineGraph(&linkage, graph());
    CHECK(!code.is_null());
    f_->ReplaceCode(*code);
    swapped_ = true;
  }

  Handle<JSFunction> f_;
  bool swapped_;
};


TEST(RunChangeTaggedToInt32) {
  SimplifiedGraphBuilderTester<int32_t> t(kMachineTagged);
  Node* x = t.ChangeTaggedToInt32(t.Parameter(0));
  t.Return(x);

  t.Lower(x);

  // TODO(titzer): remove me.
  return;

  FOR_INT32_INPUTS(i) {
    int32_t input = *i;

    if (Smi::IsValid(input)) {
      int32_t result = t.Call(Smi::FromInt(input));
      CHECK_EQ(input, result);
    }

    {
      Handle<Object> number = t.factory()->NewNumber(input);
      int32_t result = t.Call(*number);
      CHECK_EQ(input, result);
    }

    {
      Handle<HeapNumber> number = t.factory()->NewHeapNumber(input);
      int32_t result = t.Call(*number);
      CHECK_EQ(input, result);
    }
  }
}


TEST(RunChangeTaggedToUint32) {
  SimplifiedGraphBuilderTester<int32_t> t(kMachineTagged);
  Node* x = t.ChangeTaggedToUint32(t.Parameter(0));
  t.Return(x);

  t.Lower(x);

  // TODO(titzer): remove me.
  return;

  FOR_UINT32_INPUTS(i) {
    uint32_t input = *i;

    if (Smi::IsValid(input)) {
      int32_t result = t.Call(Smi::FromInt(input));
      CHECK_EQ(static_cast<int32_t>(input), result);
    }

    {
      Handle<Object> number = t.factory()->NewNumber(input);
      int32_t result = t.Call(*number);
      CHECK_EQ(static_cast<int32_t>(input), result);
    }

    {
      Handle<HeapNumber> number = t.factory()->NewHeapNumber(input);
      int32_t result = t.Call(*number);
      CHECK_EQ(static_cast<int32_t>(input), result);
    }
  }
}


TEST(RunChangeTaggedToFloat64) {
  SimplifiedGraphBuilderTester<int32_t> t(kMachineTagged);
  double result;
  Node* x = t.ChangeTaggedToFloat64(t.Parameter(0));
  t.StoreFloat64(x, &result);
  t.Return(t.Int32Constant(0));

  t.Lower(x);

  // TODO(titzer): remove me.
  return;

  {
    FOR_INT32_INPUTS(i) {
      int32_t input = *i;

      if (Smi::IsValid(input)) {
        t.Call(Smi::FromInt(input));
        CHECK_EQ(input, static_cast<int32_t>(result));
      }

      {
        Handle<Object> number = t.factory()->NewNumber(input);
        t.Call(*number);
        CHECK_EQ(input, static_cast<int32_t>(result));
      }

      {
        Handle<HeapNumber> number = t.factory()->NewHeapNumber(input);
        t.Call(*number);
        CHECK_EQ(input, static_cast<int32_t>(result));
      }
    }
  }

  {
    FOR_FLOAT64_INPUTS(i) {
      double input = *i;
      {
        Handle<Object> number = t.factory()->NewNumber(input);
        t.Call(*number);
        CHECK_EQ(input, result);
      }

      {
        Handle<HeapNumber> number = t.factory()->NewHeapNumber(input);
        t.Call(*number);
        CHECK_EQ(input, result);
      }
    }
  }
}


TEST(RunChangeBoolToBit) {
  SimplifiedGraphBuilderTester<int32_t> t(kMachineTagged);
  Node* x = t.ChangeBoolToBit(t.Parameter(0));
  t.Return(x);

  t.Lower(x);

  if (!Pipeline::SupportedTarget()) return;

  {
    Object* true_obj = t.heap()->true_value();
    int32_t result = t.Call(true_obj);
    CHECK_EQ(1, result);
  }

  {
    Object* false_obj = t.heap()->false_value();
    int32_t result = t.Call(false_obj);
    CHECK_EQ(0, result);
  }
}


TEST(RunChangeBitToBool) {
  SimplifiedGraphBuilderTester<Object*> t(kMachineTagged);
  Node* x = t.ChangeBitToBool(t.Parameter(0));
  t.Return(x);

  t.Lower(x);

  // TODO(titzer): remove me.
  return;

  {
    Object* result = t.Call(1);
    Object* true_obj = t.heap()->true_value();
    CHECK_EQ(true_obj, result);
  }

  {
    Object* result = t.Call(0);
    Object* false_obj = t.heap()->false_value();
    CHECK_EQ(false_obj, result);
  }
}


TEST(RunChangeInt32ToTagged) {
  SimplifiedGraphBuilderJSTester t;
  int32_t input;
  Node* load = t.LoadInt32(&input);
  Node* x = t.ChangeInt32ToTagged(load);
  t.Return(x);

  t.Lower(x);

  // TODO(titzer): remove me.
  return;


  {
    FOR_INT32_INPUTS(i) {
      input = *i;
      HeapNumber* result = t.CallJS<HeapNumber>();
      CHECK_EQ(static_cast<double>(input), result->value());
    }
  }

  {
    FOR_INT32_INPUTS(i) {
      input = *i;
      SimulateFullSpace(CcTest::heap()->new_space());
      HeapNumber* result = t.CallJS<HeapNumber>();
      CHECK_EQ(static_cast<double>(input), result->value());
    }
  }
}


TEST(RunChangeUint32ToTagged) {
  SimplifiedGraphBuilderJSTester t;
  uint32_t input;
  Node* load = t.LoadUint32(&input);
  Node* x = t.ChangeUint32ToTagged(load);
  t.Return(x);

  t.Lower(x);

  // TODO(titzer): remove me.
  return;

  {
    FOR_UINT32_INPUTS(i) {
      input = *i;
      HeapNumber* result = t.CallJS<HeapNumber>();
      double expected = static_cast<double>(input);
      CHECK_EQ(expected, result->value());
    }
  }

  {
    FOR_UINT32_INPUTS(i) {
      input = *i;
      SimulateFullSpace(CcTest::heap()->new_space());
      HeapNumber* result = t.CallJS<HeapNumber>();
      double expected = static_cast<double>(static_cast<uint32_t>(input));
      CHECK_EQ(expected, result->value());
    }
  }
}


TEST(RunChangeFloat64ToTagged) {
  SimplifiedGraphBuilderJSTester t;
  double input;
  Node* load = t.LoadFloat64(&input);
  Node* x = t.ChangeFloat64ToTagged(load);
  t.Return(x);

  t.Lower(x);

  // TODO(titzer): remove me.
  return;

  {
    FOR_FLOAT64_INPUTS(i) {
      input = *i;
      HeapNumber* result = t.CallJS<HeapNumber>();
      CHECK_EQ(input, result->value());
    }
  }
  {
    FOR_FLOAT64_INPUTS(i) {
      input = *i;
      SimulateFullSpace(CcTest::heap()->new_space());
      HeapNumber* result = t.CallJS<HeapNumber>();
      CHECK_EQ(input, result->value());
    }
  }
}


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
