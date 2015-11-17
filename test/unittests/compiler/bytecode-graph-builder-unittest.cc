// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "src/compiler/bytecode-graph-builder.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/instruction.h"
#include "src/compiler/instruction-selector.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/linkage.h"
#include "src/interpreter/bytecode-array-builder.h"
#include "src/parser.h"
#include "test/unittests/compiler/compiler-test-utils.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"
#include "test/unittests/test-utils.h"

using ::testing::_;

namespace v8 {
namespace internal {
namespace compiler {

static const LanguageMode kLanguageModes[] = {LanguageMode::SLOPPY,
                                              LanguageMode::STRICT};

Handle<TypeFeedbackVector> NewTypeFeedbackVector(Isolate* isolate,
                                                 FeedbackVectorSpec* spec) {
  Handle<TypeFeedbackMetadata> vector_metadata =
      TypeFeedbackMetadata::New(isolate, spec);
  return TypeFeedbackVector::New(isolate, vector_metadata);
}


class BytecodeGraphBuilderTest : public TestWithIsolateAndZone {
 public:
  BytecodeGraphBuilderTest() {}

  Graph* GetCompletedGraph(Handle<BytecodeArray> bytecode_array,
                           MaybeHandle<TypeFeedbackVector> feedback_vector =
                               MaybeHandle<TypeFeedbackVector>(),
                           LanguageMode language_mode = LanguageMode::SLOPPY);

  Matcher<Node*> IsUndefinedConstant();
  Matcher<Node*> IsNullConstant();
  Matcher<Node*> IsTheHoleConstant();
  Matcher<Node*> IsFalseConstant();
  Matcher<Node*> IsTrueConstant();
  Matcher<Node*> IsIntPtrConstant(int value);
  Matcher<Node*> IsFeedbackVector(Node* effect, Node* control);

  static Handle<String> GetName(Isolate* isolate, const char* name) {
    Handle<String> result = isolate->factory()->NewStringFromAsciiChecked(name);
    return isolate->factory()->string_table()->LookupString(isolate, result);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(BytecodeGraphBuilderTest);
};


Graph* BytecodeGraphBuilderTest::GetCompletedGraph(
    Handle<BytecodeArray> bytecode_array,
    MaybeHandle<TypeFeedbackVector> feedback_vector,
    LanguageMode language_mode) {
  MachineOperatorBuilder* machine = new (zone()) MachineOperatorBuilder(
      zone(), kMachPtr, InstructionSelector::SupportedMachineOperatorFlags());
  CommonOperatorBuilder* common = new (zone()) CommonOperatorBuilder(zone());
  JSOperatorBuilder* javascript = new (zone()) JSOperatorBuilder(zone());
  Graph* graph = new (zone()) Graph(zone());
  JSGraph* jsgraph = new (zone())
      JSGraph(isolate(), graph, common, javascript, nullptr, machine);

  Handle<String> name = factory()->NewStringFromStaticChars("test");
  Handle<String> script = factory()->NewStringFromStaticChars("test() {}");
  Handle<SharedFunctionInfo> shared_info =
      factory()->NewSharedFunctionInfo(name, MaybeHandle<Code>());
  shared_info->set_script(*factory()->NewScript(script));
  if (!feedback_vector.is_null()) {
    shared_info->set_feedback_vector(*feedback_vector.ToHandleChecked());
  }

  ParseInfo parse_info(zone(), shared_info);
  parse_info.set_language_mode(language_mode);
  CompilationInfo info(&parse_info);
  info.shared_info()->set_function_data(*bytecode_array);

  BytecodeGraphBuilder graph_builder(zone(), &info, jsgraph);
  graph_builder.CreateGraph();
  return graph;
}


Matcher<Node*> BytecodeGraphBuilderTest::IsUndefinedConstant() {
  return IsHeapConstant(factory()->undefined_value());
}


Matcher<Node*> BytecodeGraphBuilderTest::IsNullConstant() {
  return IsHeapConstant(factory()->null_value());
}


Matcher<Node*> BytecodeGraphBuilderTest::IsTheHoleConstant() {
  return IsHeapConstant(factory()->the_hole_value());
}


Matcher<Node*> BytecodeGraphBuilderTest::IsFalseConstant() {
  return IsHeapConstant(factory()->false_value());
}


Matcher<Node*> BytecodeGraphBuilderTest::IsTrueConstant() {
  return IsHeapConstant(factory()->true_value());
}


Matcher<Node*> BytecodeGraphBuilderTest::IsIntPtrConstant(int value) {
  if (kPointerSize == 8) {
    return IsInt64Constant(value);
  } else {
    return IsInt32Constant(value);
  }
}


Matcher<Node*> BytecodeGraphBuilderTest::IsFeedbackVector(Node* effect,
                                                          Node* control) {
  int offset = SharedFunctionInfo::kFeedbackVectorOffset - kHeapObjectTag;
  int offset1 = JSFunction::kSharedFunctionInfoOffset - kHeapObjectTag;

  return IsLoad(kMachAnyTagged,
                IsLoad(kMachAnyTagged,
                       IsParameter(Linkage::kJSFunctionCallClosureParamIndex),
                       IsIntPtrConstant(offset1), effect, control),
                IsIntPtrConstant(offset), effect, control);
}


TEST_F(BytecodeGraphBuilderTest, ReturnUndefined) {
  interpreter::BytecodeArrayBuilder array_builder(isolate(), zone());
  array_builder.set_locals_count(0);
  array_builder.set_context_count(0);
  array_builder.set_parameter_count(1);
  array_builder.LoadUndefined().Return();

  Graph* graph = GetCompletedGraph(array_builder.ToBytecodeArray());
  Node* end = graph->end();
  EXPECT_EQ(1, end->InputCount());
  Node* ret = end->InputAt(0);
  Node* effect = graph->start();
  Node* control = graph->start();
  EXPECT_THAT(ret, IsReturn(IsUndefinedConstant(), effect, control));
}


TEST_F(BytecodeGraphBuilderTest, ReturnNull) {
  interpreter::BytecodeArrayBuilder array_builder(isolate(), zone());
  array_builder.set_locals_count(0);
  array_builder.set_context_count(0);
  array_builder.set_parameter_count(1);
  array_builder.LoadNull().Return();

  Graph* graph = GetCompletedGraph(array_builder.ToBytecodeArray());
  Node* end = graph->end();
  EXPECT_EQ(1, end->InputCount());
  Node* ret = end->InputAt(0);
  EXPECT_THAT(ret, IsReturn(IsNullConstant(), graph->start(), graph->start()));
}


TEST_F(BytecodeGraphBuilderTest, ReturnTheHole) {
  interpreter::BytecodeArrayBuilder array_builder(isolate(), zone());
  array_builder.set_locals_count(0);
  array_builder.set_context_count(0);
  array_builder.set_parameter_count(1);
  array_builder.LoadTheHole().Return();

  Graph* graph = GetCompletedGraph(array_builder.ToBytecodeArray());
  Node* end = graph->end();
  EXPECT_EQ(1, end->InputCount());
  Node* ret = end->InputAt(0);
  Node* effect = graph->start();
  Node* control = graph->start();
  EXPECT_THAT(ret, IsReturn(IsTheHoleConstant(), effect, control));
}


TEST_F(BytecodeGraphBuilderTest, ReturnTrue) {
  interpreter::BytecodeArrayBuilder array_builder(isolate(), zone());
  array_builder.set_locals_count(0);
  array_builder.set_context_count(0);
  array_builder.set_parameter_count(1);
  array_builder.LoadTrue().Return();

  Graph* graph = GetCompletedGraph(array_builder.ToBytecodeArray());
  Node* end = graph->end();
  EXPECT_EQ(1, end->InputCount());
  Node* ret = end->InputAt(0);
  Node* effect = graph->start();
  Node* control = graph->start();
  EXPECT_THAT(ret, IsReturn(IsTrueConstant(), effect, control));
}


TEST_F(BytecodeGraphBuilderTest, ReturnFalse) {
  interpreter::BytecodeArrayBuilder array_builder(isolate(), zone());
  array_builder.set_locals_count(0);
  array_builder.set_context_count(0);
  array_builder.set_parameter_count(1);
  array_builder.LoadFalse().Return();

  Graph* graph = GetCompletedGraph(array_builder.ToBytecodeArray());
  Node* end = graph->end();
  EXPECT_EQ(1, end->InputCount());
  Node* ret = end->InputAt(0);
  Node* effect = graph->start();
  Node* control = graph->start();
  EXPECT_THAT(ret, IsReturn(IsFalseConstant(), effect, control));
}


TEST_F(BytecodeGraphBuilderTest, ReturnInt8) {
  interpreter::BytecodeArrayBuilder array_builder(isolate(), zone());
  static const int kValue = 3;
  array_builder.set_locals_count(0);
  array_builder.set_context_count(0);
  array_builder.set_parameter_count(1);
  array_builder.LoadLiteral(Smi::FromInt(kValue)).Return();

  Graph* graph = GetCompletedGraph(array_builder.ToBytecodeArray());
  Node* end = graph->end();
  EXPECT_EQ(1, end->InputCount());
  Node* ret = end->InputAt(0);
  Node* effect = graph->start();
  Node* control = graph->start();
  EXPECT_THAT(ret, IsReturn(IsNumberConstant(kValue), effect, control));
}


TEST_F(BytecodeGraphBuilderTest, ReturnDouble) {
  interpreter::BytecodeArrayBuilder array_builder(isolate(), zone());
  const double kValue = 0.123456789;
  array_builder.set_locals_count(0);
  array_builder.set_context_count(0);
  array_builder.set_parameter_count(1);
  array_builder.LoadLiteral(factory()->NewHeapNumber(kValue));
  array_builder.Return();

  Graph* graph = GetCompletedGraph(array_builder.ToBytecodeArray());
  Node* end = graph->end();
  EXPECT_EQ(1, end->InputCount());
  Node* ret = end->InputAt(0);
  Node* effect = graph->start();
  Node* control = graph->start();
  EXPECT_THAT(ret, IsReturn(IsNumberConstant(kValue), effect, control));
}


TEST_F(BytecodeGraphBuilderTest, SimpleExpressionWithParameters) {
  interpreter::BytecodeArrayBuilder array_builder(isolate(), zone());
  array_builder.set_locals_count(1);
  array_builder.set_context_count(0);
  array_builder.set_parameter_count(3);
  array_builder.LoadAccumulatorWithRegister(array_builder.Parameter(1))
      .BinaryOperation(Token::Value::ADD, array_builder.Parameter(2),
                       Strength::WEAK)
      .StoreAccumulatorInRegister(interpreter::Register(0))
      .Return();

  Graph* graph = GetCompletedGraph(array_builder.ToBytecodeArray());
  Node* end = graph->end();
  EXPECT_EQ(1, end->InputCount());
  Node* ret = end->InputAt(0);
  // NB binary operation is <reg> <op> <acc>. The register represents
  // the left-hand side, which is why parameters appear in opposite
  // order to construction via the builder.
  EXPECT_THAT(ret, IsReturn(IsJSAdd(IsParameter(2), IsParameter(1)), _, _));
}


TEST_F(BytecodeGraphBuilderTest, SimpleExpressionWithRegister) {
  interpreter::BytecodeArrayBuilder array_builder(isolate(), zone());
  static const int kLeft = -655371;
  static const int kRight = +2000000;
  array_builder.set_locals_count(1);
  array_builder.set_context_count(0);
  array_builder.set_parameter_count(1);
  array_builder.LoadLiteral(Smi::FromInt(kLeft))
      .StoreAccumulatorInRegister(interpreter::Register(0))
      .LoadLiteral(Smi::FromInt(kRight))
      .BinaryOperation(Token::Value::ADD, interpreter::Register(0),
                       Strength::WEAK)
      .Return();

  Graph* graph = GetCompletedGraph(array_builder.ToBytecodeArray());
  Node* end = graph->end();
  EXPECT_EQ(1, end->InputCount());
  Node* ret = end->InputAt(0);
  EXPECT_THAT(
      ret, IsReturn(IsJSAdd(IsNumberConstant(kLeft), IsNumberConstant(kRight)),
                    _, _));
}


TEST_F(BytecodeGraphBuilderTest, NamedLoad) {
  const bool kWideBytecode[] = {false, true};
  TRACED_FOREACH(LanguageMode, language_mode, kLanguageModes) {
    TRACED_FOREACH(bool, wide_bytecode, kWideBytecode) {
      FeedbackVectorSpec feedback_spec(zone());
      if (wide_bytecode) {
        for (int i = 0; i < 128; i++) {
          feedback_spec.AddLoadICSlot();
        }
      }
      FeedbackVectorSlot slot = feedback_spec.AddLoadICSlot();
      Handle<TypeFeedbackVector> vector =
          NewTypeFeedbackVector(isolate(), &feedback_spec);

      interpreter::BytecodeArrayBuilder array_builder(isolate(), zone());
      array_builder.set_locals_count(1);
      array_builder.set_context_count(0);
      array_builder.set_parameter_count(2);

      Handle<Name> name = GetName(isolate(), "val");
      size_t name_index = array_builder.GetConstantPoolEntry(name);

      array_builder.LoadNamedProperty(array_builder.Parameter(1), name_index,
                                      vector->GetIndex(slot), language_mode)
          .Return();
      Graph* graph = GetCompletedGraph(array_builder.ToBytecodeArray(), vector,
                                       language_mode);

      Node* ret = graph->end()->InputAt(0);
      Node* start = graph->start();

      Matcher<Node*> feedback_vector_matcher = IsFeedbackVector(start, start);
      Matcher<Node*> load_named_matcher = IsJSLoadNamed(
          name, IsParameter(1), feedback_vector_matcher, start, start);

      EXPECT_THAT(ret, IsReturn(load_named_matcher, _, _));
    }
  }
}


TEST_F(BytecodeGraphBuilderTest, CallProperty0) {
  FeedbackVectorSpec feedback_spec(zone());
  FeedbackVectorSlot call_slot = feedback_spec.AddCallICSlot();
  FeedbackVectorSlot load_slot = feedback_spec.AddLoadICSlot();
  Handle<TypeFeedbackVector> vector =
      NewTypeFeedbackVector(isolate(), &feedback_spec);

  interpreter::BytecodeArrayBuilder array_builder(isolate(), zone());
  array_builder.set_locals_count(1);
  array_builder.set_context_count(0);
  array_builder.set_parameter_count(2);

  Handle<Name> func_name = GetName(isolate(), "func");
  size_t func_name_index = array_builder.GetConstantPoolEntry(func_name);

  interpreter::Register reg0 = interpreter::Register(0);
  array_builder.LoadNamedProperty(
        array_builder.Parameter(1), func_name_index,
        vector->GetIndex(load_slot), LanguageMode::SLOPPY)
      .StoreAccumulatorInRegister(reg0)
      .Call(reg0, array_builder.Parameter(1), 0, vector->GetIndex(call_slot))
      .Return();

  Graph* graph = GetCompletedGraph(array_builder.ToBytecodeArray(), vector);
  Node* ret = graph->end()->InputAt(0);
  Node* start = graph->start();

  Matcher<Node*> feedback_vector_matcher = IsFeedbackVector(start, start);
  Matcher<Node*> load_named_matcher = IsJSLoadNamed(
      func_name, IsParameter(1), feedback_vector_matcher, start, start);
  std::vector<Matcher<Node*>> call_inputs;
  call_inputs.push_back(load_named_matcher);
  call_inputs.push_back(IsParameter(1));
  Matcher<Node*> call_matcher =
      IsJSCallFunction(call_inputs, load_named_matcher, IsIfSuccess(_));

  EXPECT_THAT(ret, IsReturn(call_matcher, _, _));
}


TEST_F(BytecodeGraphBuilderTest, CallProperty2) {
  FeedbackVectorSpec feedback_spec(zone());
  FeedbackVectorSlot call_slot = feedback_spec.AddCallICSlot();
  FeedbackVectorSlot load_slot = feedback_spec.AddLoadICSlot();
  Handle<TypeFeedbackVector> vector =
      NewTypeFeedbackVector(isolate(), &feedback_spec);

  interpreter::BytecodeArrayBuilder array_builder(isolate(), zone());
  array_builder.set_locals_count(4);
  array_builder.set_context_count(0);
  array_builder.set_parameter_count(4);

  Handle<Name> func_name = GetName(isolate(), "func");
  size_t func_name_index = array_builder.GetConstantPoolEntry(func_name);

  interpreter::Register reg0 = interpreter::Register(0);
  interpreter::Register reg1 = interpreter::Register(1);
  interpreter::Register reg2 = interpreter::Register(2);
  interpreter::Register reg3 = interpreter::Register(3);
  array_builder.LoadNamedProperty(
        array_builder.Parameter(1), func_name_index,
        vector->GetIndex(load_slot), LanguageMode::SLOPPY)
      .StoreAccumulatorInRegister(reg0)
      .LoadAccumulatorWithRegister(array_builder.Parameter(1))
      .StoreAccumulatorInRegister(reg1)
      .LoadAccumulatorWithRegister(array_builder.Parameter(2))
      .StoreAccumulatorInRegister(reg2)
      .LoadAccumulatorWithRegister(array_builder.Parameter(3))
      .StoreAccumulatorInRegister(reg3)
      .Call(reg0, reg1, 2, vector->GetIndex(call_slot))
      .Return();

  Graph* graph = GetCompletedGraph(array_builder.ToBytecodeArray(), vector);
  Node* ret = graph->end()->InputAt(0);
  Node* start = graph->start();

  Matcher<Node*> feedback_vector_matcher = IsFeedbackVector(start, start);
  Matcher<Node*> load_named_matcher = IsJSLoadNamed(
      func_name, IsParameter(1), feedback_vector_matcher, start, start);
  std::vector<Matcher<Node*>> call_inputs;
  call_inputs.push_back(load_named_matcher);
  call_inputs.push_back(IsParameter(1));
  call_inputs.push_back(IsParameter(2));
  call_inputs.push_back(IsParameter(3));
  Matcher<Node*> call_matcher =
      IsJSCallFunction(call_inputs, load_named_matcher, IsIfSuccess(_));

  EXPECT_THAT(ret, IsReturn(call_matcher, _, _));
}


TEST_F(BytecodeGraphBuilderTest, LoadGlobal) {
  const TypeofMode kTypeOfModes[] = {TypeofMode::NOT_INSIDE_TYPEOF,
                                     TypeofMode::INSIDE_TYPEOF};
  const bool kWideBytecode[] = {false, true};
  TRACED_FOREACH(LanguageMode, language_mode, kLanguageModes) {
    TRACED_FOREACH(TypeofMode, typeof_mode, kTypeOfModes) {
      TRACED_FOREACH(bool, wide_bytecode, kWideBytecode) {
        FeedbackVectorSpec feedback_spec(zone());
        if (wide_bytecode) {
          for (int i = 0; i < 128; i++) {
            feedback_spec.AddLoadICSlot();
          }
        }
        FeedbackVectorSlot slot = feedback_spec.AddLoadICSlot();
        Handle<TypeFeedbackVector> vector =
            NewTypeFeedbackVector(isolate(), &feedback_spec);

        interpreter::BytecodeArrayBuilder array_builder(isolate(), zone());
        array_builder.set_locals_count(0);
        array_builder.set_context_count(0);
        array_builder.set_parameter_count(1);

        Handle<Name> name = GetName(isolate(), "global");
        size_t name_index = array_builder.GetConstantPoolEntry(name);

        array_builder.LoadGlobal(name_index, vector->GetIndex(slot),
                                 language_mode, typeof_mode)
            .Return();
        Graph* graph = GetCompletedGraph(array_builder.ToBytecodeArray(),
                                         vector, language_mode);

        Node* ret = graph->end()->InputAt(0);
        Node* start = graph->start();

        Matcher<Node*> feedback_vector_matcher = IsFeedbackVector(start, start);
        Matcher<Node*> load_global_matcher = IsJSLoadGlobal(
            name, typeof_mode, feedback_vector_matcher, start, start);

        EXPECT_THAT(ret, IsReturn(load_global_matcher, _, _));
      }
    }
  }
}


TEST_F(BytecodeGraphBuilderTest, StoreGlobal) {
  const bool kWideBytecode[] = {false, true};
  TRACED_FOREACH(LanguageMode, language_mode, kLanguageModes) {
    TRACED_FOREACH(bool, wide_bytecode, kWideBytecode) {
      FeedbackVectorSpec feedback_spec(zone());
      if (wide_bytecode) {
        for (int i = 0; i < 128; i++) {
          feedback_spec.AddStoreICSlot();
        }
      }
      FeedbackVectorSlot slot = feedback_spec.AddStoreICSlot();
      Handle<TypeFeedbackVector> vector =
          NewTypeFeedbackVector(isolate(), &feedback_spec);

      interpreter::BytecodeArrayBuilder array_builder(isolate(), zone());
      array_builder.set_locals_count(0);
      array_builder.set_context_count(0);
      array_builder.set_parameter_count(1);

      Handle<Name> name = GetName(isolate(), "global");
      size_t name_index = array_builder.GetConstantPoolEntry(name);

      array_builder.LoadLiteral(Smi::FromInt(321))
          .StoreGlobal(name_index, vector->GetIndex(slot), language_mode)
          .Return();
      Graph* graph = GetCompletedGraph(array_builder.ToBytecodeArray(), vector,
                                       language_mode);

      Node* ret = graph->end()->InputAt(0);
      Node* start = graph->start();

      Matcher<Node*> value_matcher = IsNumberConstant(321);
      Matcher<Node*> feedback_vector_matcher = IsFeedbackVector(start, start);
      Matcher<Node*> store_global_matcher = IsJSStoreGlobal(
          name, value_matcher, feedback_vector_matcher, start, start);

      EXPECT_THAT(ret, IsReturn(_, store_global_matcher, _));
    }
  }
}


}  // namespace compiler
}  // namespace internal
}  // namespace v8
