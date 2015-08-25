// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/compiler/interpreter-assembler-unittest.h"

#include "src/compiler/graph.h"
#include "src/compiler/node.h"
#include "src/unique.h"
#include "test/unittests/compiler/compiler-test-utils.h"
#include "test/unittests/compiler/node-test-utils.h"

using ::testing::_;

namespace v8 {
namespace internal {
namespace compiler {

const interpreter::Bytecode kBytecodes[] = {
#define DEFINE_BYTECODE(Name, ...) interpreter::Bytecode::k##Name,
    BYTECODE_LIST(DEFINE_BYTECODE)
#undef DEFINE_BYTECODE
};


Matcher<Node*> IsIntPtrConstant(const intptr_t value) {
  return kPointerSize == 8 ? IsInt64Constant(static_cast<int64_t>(value))
                           : IsInt32Constant(static_cast<int32_t>(value));
}


Matcher<Node*> IsIntPtrAdd(const Matcher<Node*>& lhs_matcher,
                           const Matcher<Node*>& rhs_matcher) {
  return kPointerSize == 8 ? IsInt64Add(lhs_matcher, rhs_matcher)
                           : IsInt32Add(lhs_matcher, rhs_matcher);
}


Matcher<Node*> IsIntPtrSub(const Matcher<Node*>& lhs_matcher,
                           const Matcher<Node*>& rhs_matcher) {
  return kPointerSize == 8 ? IsInt64Sub(lhs_matcher, rhs_matcher)
                           : IsInt32Sub(lhs_matcher, rhs_matcher);
}


Matcher<Node*> IsWordShl(const Matcher<Node*>& lhs_matcher,
                         const Matcher<Node*>& rhs_matcher) {
  return kPointerSize == 8 ? IsWord64Shl(lhs_matcher, rhs_matcher)
                           : IsWord32Shl(lhs_matcher, rhs_matcher);
}


Matcher<Node*> IsWordSar(const Matcher<Node*>& lhs_matcher,
                         const Matcher<Node*>& rhs_matcher) {
  return kPointerSize == 8 ? IsWord64Sar(lhs_matcher, rhs_matcher)
                           : IsWord32Sar(lhs_matcher, rhs_matcher);
}


Matcher<Node*> InterpreterAssemblerTest::InterpreterAssemblerForTest::IsLoad(
    const Matcher<LoadRepresentation>& rep_matcher,
    const Matcher<Node*>& base_matcher, const Matcher<Node*>& index_matcher) {
  return ::i::compiler::IsLoad(rep_matcher, base_matcher, index_matcher,
                               graph()->start(), graph()->start());
}


Matcher<Node*> InterpreterAssemblerTest::InterpreterAssemblerForTest::IsStore(
    const Matcher<StoreRepresentation>& rep_matcher,
    const Matcher<Node*>& base_matcher, const Matcher<Node*>& index_matcher,
    const Matcher<Node*>& value_matcher) {
  return ::i::compiler::IsStore(rep_matcher, base_matcher, index_matcher,
                                value_matcher, graph()->start(),
                                graph()->start());
}


Matcher<Node*>
InterpreterAssemblerTest::InterpreterAssemblerForTest::IsBytecodeOperand(
    int operand) {
  return IsLoad(
      kMachUint8, IsParameter(Linkage::kInterpreterBytecodeArrayParameter),
      IsIntPtrAdd(IsParameter(Linkage::kInterpreterBytecodeOffsetParameter),
                  IsInt32Constant(1 + operand)));
}


Matcher<Node*> InterpreterAssemblerTest::InterpreterAssemblerForTest::
    IsBytecodeOperandSignExtended(int operand) {
  Matcher<Node*> load_matcher = IsLoad(
      kMachInt8, IsParameter(Linkage::kInterpreterBytecodeArrayParameter),
      IsIntPtrAdd(IsParameter(Linkage::kInterpreterBytecodeOffsetParameter),
                  IsInt32Constant(1 + operand)));
  if (kPointerSize == 8) {
    load_matcher = IsChangeInt32ToInt64(load_matcher);
  }
  return load_matcher;
}


Graph*
InterpreterAssemblerTest::InterpreterAssemblerForTest::GetCompletedGraph() {
  End();
  return graph();
}


TARGET_TEST_F(InterpreterAssemblerTest, Dispatch) {
  TRACED_FOREACH(interpreter::Bytecode, bytecode, kBytecodes) {
    InterpreterAssemblerForTest m(this, bytecode);
    m.Dispatch();
    Graph* graph = m.GetCompletedGraph();

    Node* end = graph->end();
    EXPECT_EQ(1, end->InputCount());
    Node* tail_call_node = end->InputAt(0);

    Matcher<Node*> next_bytecode_offset_matcher =
        IsIntPtrAdd(IsParameter(Linkage::kInterpreterBytecodeOffsetParameter),
                    IsInt32Constant(interpreter::Bytecodes::Size(bytecode)));
    Matcher<Node*> target_bytecode_matcher = m.IsLoad(
        kMachUint8, IsParameter(Linkage::kInterpreterBytecodeArrayParameter),
        next_bytecode_offset_matcher);
    Matcher<Node*> code_target_matcher = m.IsLoad(
        kMachPtr, IsParameter(Linkage::kInterpreterDispatchTableParameter),
        IsWord32Shl(target_bytecode_matcher,
                    IsInt32Constant(kPointerSizeLog2)));

    EXPECT_EQ(CallDescriptor::kCallCodeObject, m.call_descriptor()->kind());
    EXPECT_TRUE(m.call_descriptor()->flags() & CallDescriptor::kCanUseRoots);
    EXPECT_THAT(
        tail_call_node,
        IsTailCall(m.call_descriptor(), code_target_matcher,
                   IsParameter(Linkage::kInterpreterAccumulatorParameter),
                   IsParameter(Linkage::kInterpreterRegisterFileParameter),
                   next_bytecode_offset_matcher,
                   IsParameter(Linkage::kInterpreterBytecodeArrayParameter),
                   IsParameter(Linkage::kInterpreterDispatchTableParameter),
                   IsParameter(Linkage::kInterpreterContextParameter),
                   graph->start(), graph->start()));
  }
}


TARGET_TEST_F(InterpreterAssemblerTest, Return) {
  TRACED_FOREACH(interpreter::Bytecode, bytecode, kBytecodes) {
    InterpreterAssemblerForTest m(this, bytecode);
    m.Return();
    Graph* graph = m.GetCompletedGraph();

    Node* end = graph->end();
    EXPECT_EQ(1, end->InputCount());
    Node* tail_call_node = end->InputAt(0);

    EXPECT_EQ(CallDescriptor::kCallCodeObject, m.call_descriptor()->kind());
    EXPECT_TRUE(m.call_descriptor()->flags() & CallDescriptor::kCanUseRoots);
    Matcher<Unique<HeapObject>> exit_trampoline(
        Unique<HeapObject>::CreateImmovable(
            isolate()->builtins()->InterpreterExitTrampoline()));
    EXPECT_THAT(
        tail_call_node,
        IsTailCall(m.call_descriptor(), IsHeapConstant(exit_trampoline),
                   IsParameter(Linkage::kInterpreterAccumulatorParameter),
                   IsParameter(Linkage::kInterpreterRegisterFileParameter),
                   IsParameter(Linkage::kInterpreterBytecodeOffsetParameter),
                   IsParameter(Linkage::kInterpreterBytecodeArrayParameter),
                   IsParameter(Linkage::kInterpreterDispatchTableParameter),
                   IsParameter(Linkage::kInterpreterContextParameter),
                   graph->start(), graph->start()));
  }
}


TARGET_TEST_F(InterpreterAssemblerTest, BytecodeOperand) {
  TRACED_FOREACH(interpreter::Bytecode, bytecode, kBytecodes) {
    InterpreterAssemblerForTest m(this, bytecode);
    int number_of_operands = interpreter::Bytecodes::NumberOfOperands(bytecode);
    for (int i = 0; i < number_of_operands; i++) {
      switch (interpreter::Bytecodes::GetOperandType(bytecode, i)) {
        case interpreter::OperandType::kImm8:
          EXPECT_THAT(m.BytecodeOperandImm8(i),
                      m.IsBytecodeOperandSignExtended(i));
          break;
        case interpreter::OperandType::kReg:
          EXPECT_THAT(m.BytecodeOperandReg(i),
                      m.IsBytecodeOperandSignExtended(i));
          break;
        case interpreter::OperandType::kNone:
          UNREACHABLE();
          break;
      }
    }
  }
}


TARGET_TEST_F(InterpreterAssemblerTest, GetSetAccumulator) {
  TRACED_FOREACH(interpreter::Bytecode, bytecode, kBytecodes) {
    InterpreterAssemblerForTest m(this, bytecode);
    // Should be incoming accumulator if not set.
    EXPECT_THAT(m.GetAccumulator(),
                IsParameter(Linkage::kInterpreterAccumulatorParameter));

    // Should be set by SedtAccumulator.
    Node* accumulator_value_1 = m.Int32Constant(0xdeadbeef);
    m.SetAccumulator(accumulator_value_1);
    EXPECT_THAT(m.GetAccumulator(), accumulator_value_1);
    Node* accumulator_value_2 = m.Int32Constant(42);
    m.SetAccumulator(accumulator_value_2);
    EXPECT_THAT(m.GetAccumulator(), accumulator_value_2);

    // Should be passed to next bytecode handler on dispatch.
    m.Dispatch();
    Graph* graph = m.GetCompletedGraph();

    Node* end = graph->end();
    EXPECT_EQ(1, end->InputCount());
    Node* tail_call_node = end->InputAt(0);

    EXPECT_THAT(tail_call_node,
                IsTailCall(m.call_descriptor(), _, accumulator_value_2, _, _, _,
                           _, graph->start(), graph->start()));
  }
}


TARGET_TEST_F(InterpreterAssemblerTest, LoadRegister) {
  TRACED_FOREACH(interpreter::Bytecode, bytecode, kBytecodes) {
    InterpreterAssemblerForTest m(this, bytecode);
    Node* reg_index_node = m.Int32Constant(44);
    Node* load_reg_node = m.LoadRegister(reg_index_node);
    EXPECT_THAT(
        load_reg_node,
        m.IsLoad(kMachAnyTagged,
                 IsParameter(Linkage::kInterpreterRegisterFileParameter),
                 IsWordShl(reg_index_node, IsInt32Constant(kPointerSizeLog2))));
  }
}


TARGET_TEST_F(InterpreterAssemblerTest, StoreRegister) {
  TRACED_FOREACH(interpreter::Bytecode, bytecode, kBytecodes) {
    InterpreterAssemblerForTest m(this, bytecode);
    Node* store_value = m.Int32Constant(0xdeadbeef);
    Node* reg_index_node = m.Int32Constant(44);
    Node* store_reg_node = m.StoreRegister(store_value, reg_index_node);
    EXPECT_THAT(
        store_reg_node,
        m.IsStore(StoreRepresentation(kMachAnyTagged, kNoWriteBarrier),
                  IsParameter(Linkage::kInterpreterRegisterFileParameter),
                  IsWordShl(reg_index_node, IsInt32Constant(kPointerSizeLog2)),
                  store_value));
  }
}


TARGET_TEST_F(InterpreterAssemblerTest, SmiTag) {
  TRACED_FOREACH(interpreter::Bytecode, bytecode, kBytecodes) {
    InterpreterAssemblerForTest m(this, bytecode);
    Node* value = m.Int32Constant(44);
    EXPECT_THAT(m.SmiTag(value),
                IsWordShl(value, IsInt32Constant(kSmiShiftSize + kSmiTagSize)));
    EXPECT_THAT(m.SmiUntag(value),
                IsWordSar(value, IsInt32Constant(kSmiShiftSize + kSmiTagSize)));
  }
}


TARGET_TEST_F(InterpreterAssemblerTest, LoadContextSlot) {
  TRACED_FOREACH(interpreter::Bytecode, bytecode, kBytecodes) {
    InterpreterAssemblerForTest m(this, bytecode);
    Node* load_context = m.LoadContextSlot(22);
    EXPECT_THAT(load_context,
                m.IsLoad(kMachAnyTagged,
                         IsParameter(Linkage::kInterpreterContextParameter),
                         IsIntPtrConstant(Context::SlotOffset(22))));
  }
}


TARGET_TEST_F(InterpreterAssemblerTest, LoadObjectField) {
  TRACED_FOREACH(interpreter::Bytecode, bytecode, kBytecodes) {
    InterpreterAssemblerForTest m(this, bytecode);
    Node* object = m.IntPtrConstant(0xdeadbeef);
    int offset = 16;
    Node* load_field = m.LoadObjectField(object, offset);
    EXPECT_THAT(load_field,
                m.IsLoad(kMachAnyTagged, object,
                         IsIntPtrConstant(offset - kHeapObjectTag)));
  }
}


TARGET_TEST_F(InterpreterAssemblerTest, CallJSBuiltin) {
  TRACED_FOREACH(interpreter::Bytecode, bytecode, kBytecodes) {
    InterpreterAssemblerForTest m(this, bytecode);
    Node* receiver = m.IntPtrConstant(1234);
    Node* call_js_builtin_0 = m.CallJSBuiltin(Builtins::SUB, receiver);

    Matcher<Node*> load_globals_matcher = m.IsLoad(
        kMachAnyTagged, IsParameter(Linkage::kInterpreterContextParameter),
        IsIntPtrConstant(Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
    Matcher<Node*> load_builtins_matcher = m.IsLoad(
        kMachAnyTagged, load_globals_matcher,
        IsIntPtrConstant(GlobalObject::kBuiltinsOffset - kHeapObjectTag));
    Matcher<Node*> function_matcher =
        m.IsLoad(kMachAnyTagged, load_builtins_matcher,
                 IsIntPtrConstant(
                     JSBuiltinsObject::OffsetOfFunctionWithId(Builtins::SUB) -
                     kHeapObjectTag));
    Matcher<Node*> context_matcher =
        m.IsLoad(kMachAnyTagged, function_matcher,
                 IsIntPtrConstant(JSFunction::kContextOffset - kHeapObjectTag));
    EXPECT_THAT(call_js_builtin_0,
                IsCall(_, function_matcher, receiver, context_matcher,
                       m.graph()->start(), m.graph()->start()));

    Node* arg1 = m.Int32Constant(0xabcd);
    Node* call_js_builtin_1 = m.CallJSBuiltin(Builtins::SUB, receiver, arg1);
    EXPECT_THAT(call_js_builtin_1,
                IsCall(_, function_matcher, receiver, arg1, context_matcher,
                       m.graph()->start(), m.graph()->start()));
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
