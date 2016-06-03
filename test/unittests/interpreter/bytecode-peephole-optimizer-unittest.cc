// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/factory.h"
#include "src/interpreter/bytecode-label.h"
#include "src/interpreter/bytecode-peephole-optimizer.h"
#include "src/interpreter/constant-array-builder.h"
#include "src/objects-inl.h"
#include "src/objects.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace interpreter {

class BytecodePeepholeOptimizerTest : public BytecodePipelineStage,
                                      public TestWithIsolateAndZone {
 public:
  BytecodePeepholeOptimizerTest()
      : constant_array_builder_(isolate(), zone()),
        peephole_optimizer_(&constant_array_builder_, this) {}
  ~BytecodePeepholeOptimizerTest() override {}

  void Write(BytecodeNode* node) override {
    write_count_++;
    last_written_.Clone(node);
  }

  void WriteJump(BytecodeNode* node, BytecodeLabel* label) override {
    write_count_++;
    last_written_.Clone(node);
  }

  void BindLabel(BytecodeLabel* label) override {}
  void BindLabel(const BytecodeLabel& target, BytecodeLabel* label) override {}
  Handle<BytecodeArray> ToBytecodeArray(
      int fixed_register_count, int parameter_count,
      Handle<FixedArray> handle_table) override {
    return Handle<BytecodeArray>();
  }

  void Flush() {
    optimizer()->ToBytecodeArray(0, 0, factory()->empty_fixed_array());
  }

  BytecodePeepholeOptimizer* optimizer() { return &peephole_optimizer_; }
  ConstantArrayBuilder* constant_array() { return &constant_array_builder_; }

  int write_count() const { return write_count_; }
  const BytecodeNode& last_written() const { return last_written_; }

 private:
  ConstantArrayBuilder constant_array_builder_;
  BytecodePeepholeOptimizer peephole_optimizer_;

  int write_count_ = 0;
  BytecodeNode last_written_;
};

// Sanity tests.

TEST_F(BytecodePeepholeOptimizerTest, FlushOnJump) {
  CHECK_EQ(write_count(), 0);

  BytecodeNode add(Bytecode::kAdd, Register(0).ToOperand(),
                   OperandScale::kSingle);
  optimizer()->Write(&add);
  CHECK_EQ(write_count(), 0);

  BytecodeLabel target;
  BytecodeNode jump(Bytecode::kJump, 0, OperandScale::kSingle);
  optimizer()->WriteJump(&jump, &target);
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(jump, last_written());
}

TEST_F(BytecodePeepholeOptimizerTest, FlushOnBind) {
  CHECK_EQ(write_count(), 0);

  BytecodeNode add(Bytecode::kAdd, Register(0).ToOperand(),
                   OperandScale::kSingle);
  optimizer()->Write(&add);
  CHECK_EQ(write_count(), 0);

  BytecodeLabel target;
  optimizer()->BindLabel(&target);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(add, last_written());
}

// Nop elimination tests.

TEST_F(BytecodePeepholeOptimizerTest, ElideEmptyNop) {
  BytecodeNode nop(Bytecode::kNop);
  optimizer()->Write(&nop);
  BytecodeNode add(Bytecode::kAdd, Register(0).ToOperand(),
                   OperandScale::kSingle);
  optimizer()->Write(&add);
  Flush();
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(add, last_written());
}

TEST_F(BytecodePeepholeOptimizerTest, ElideExpressionNop) {
  BytecodeNode nop(Bytecode::kNop);
  nop.source_info().Update({3, false});
  optimizer()->Write(&nop);
  BytecodeNode add(Bytecode::kAdd, Register(0).ToOperand(),
                   OperandScale::kSingle);
  optimizer()->Write(&add);
  Flush();
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(add, last_written());
}

TEST_F(BytecodePeepholeOptimizerTest, KeepStatementNop) {
  BytecodeNode nop(Bytecode::kNop);
  nop.source_info().Update({3, true});
  optimizer()->Write(&nop);
  BytecodeNode add(Bytecode::kAdd, Register(0).ToOperand(),
                   OperandScale::kSingle);
  add.source_info().Update({3, false});
  optimizer()->Write(&add);
  Flush();
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(add, last_written());
}

// Tests covering BytecodePeepholeOptimizer::UpdateCurrentBytecode().

TEST_F(BytecodePeepholeOptimizerTest, KeepJumpIfToBooleanTrue) {
  BytecodeNode first(Bytecode::kLdaNull);
  BytecodeNode second(Bytecode::kJumpIfToBooleanTrue, 3, OperandScale::kSingle);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), first);
  Flush();
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(last_written(), second);
}

TEST_F(BytecodePeepholeOptimizerTest, ElideJumpIfToBooleanTrue) {
  BytecodeNode first(Bytecode::kLdaTrue);
  BytecodeNode second(Bytecode::kJumpIfToBooleanTrue, 3, OperandScale::kSingle);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), first);
  Flush();
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(last_written().bytecode(), Bytecode::kJumpIfTrue);
  CHECK_EQ(last_written().operand(0), second.operand(0));
}

TEST_F(BytecodePeepholeOptimizerTest, KeepToBooleanLogicalNot) {
  BytecodeNode first(Bytecode::kLdaNull);
  BytecodeNode second(Bytecode::kToBooleanLogicalNot);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), first);
  Flush();
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(last_written(), second);
}

TEST_F(BytecodePeepholeOptimizerTest, ElideToBooleanLogicalNot) {
  BytecodeNode first(Bytecode::kLdaTrue);
  BytecodeNode second(Bytecode::kToBooleanLogicalNot);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), first);
  Flush();
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(last_written().bytecode(), Bytecode::kLogicalNot);
}

// Tests covering BytecodePeepholeOptimizer::CanElideCurrent().

TEST_F(BytecodePeepholeOptimizerTest, StarRxLdarRy) {
  BytecodeNode first(Bytecode::kStar, Register(0).ToOperand(),
                     OperandScale::kSingle);
  BytecodeNode second(Bytecode::kLdar, Register(1).ToOperand(),
                      OperandScale::kSingle);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), first);
  Flush();
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(last_written(), second);
}

TEST_F(BytecodePeepholeOptimizerTest, StarRxLdarRx) {
  BytecodeLabel label;
  BytecodeNode first(Bytecode::kStar, Register(0).ToOperand(),
                     OperandScale::kSingle);
  BytecodeNode second(Bytecode::kLdar, Register(0).ToOperand(),
                      OperandScale::kSingle);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), first);
  Flush();
  CHECK_EQ(write_count(), 1);
}

TEST_F(BytecodePeepholeOptimizerTest, StarRxLdarRxStatement) {
  BytecodeNode first(Bytecode::kStar, Register(0).ToOperand(),
                     OperandScale::kSingle);
  BytecodeNode second(Bytecode::kLdar, Register(0).ToOperand(),
                      OperandScale::kSingle);
  second.source_info().Update({0, true});
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), first);
  Flush();
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(last_written().bytecode(), Bytecode::kNop);
  CHECK_EQ(last_written().source_info(), second.source_info());
}

TEST_F(BytecodePeepholeOptimizerTest, StarRxLdarRxStatementStarRy) {
  BytecodeLabel label;
  BytecodeNode first(Bytecode::kStar, Register(0).ToOperand(),
                     OperandScale::kSingle);
  BytecodeNode second(Bytecode::kLdar, Register(0).ToOperand(),
                      OperandScale::kSingle);
  BytecodeNode third(Bytecode::kStar, Register(3).ToOperand(),
                     OperandScale::kSingle);
  second.source_info().Update({0, true});
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), first);
  optimizer()->Write(&third);
  CHECK_EQ(write_count(), 1);
  Flush();
  CHECK_EQ(write_count(), 2);
  // Source position should move |second| to |third| when |second| is elided.
  third.source_info().Update(second.source_info());
  CHECK_EQ(last_written(), third);
}

TEST_F(BytecodePeepholeOptimizerTest, LdarToName) {
  BytecodeNode first(Bytecode::kLdar, Register(0).ToOperand(),
                     OperandScale::kSingle);
  BytecodeNode second(Bytecode::kToName);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), first);
  Flush();
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(last_written(), second);
}

TEST_F(BytecodePeepholeOptimizerTest, ToNameToName) {
  BytecodeNode first(Bytecode::kToName);
  BytecodeNode second(Bytecode::kToName);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), first);
  Flush();
  CHECK_EQ(write_count(), 1);
}

TEST_F(BytecodePeepholeOptimizerTest, TypeOfToName) {
  BytecodeNode first(Bytecode::kTypeOf);
  BytecodeNode second(Bytecode::kToName);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), first);
  Flush();
  CHECK_EQ(write_count(), 1);
}

TEST_F(BytecodePeepholeOptimizerTest, LdaConstantStringToName) {
  Handle<Object> word =
      isolate()->factory()->NewStringFromStaticChars("optimizing");
  size_t index = constant_array()->Insert(word);
  BytecodeNode first(Bytecode::kLdaConstant, static_cast<uint32_t>(index),
                     OperandScale::kSingle);
  BytecodeNode second(Bytecode::kToName);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), first);
  Flush();
  CHECK_EQ(write_count(), 1);
}

TEST_F(BytecodePeepholeOptimizerTest, LdaConstantNumberToName) {
  Handle<Object> word = isolate()->factory()->NewNumber(0.380);
  size_t index = constant_array()->Insert(word);
  BytecodeNode first(Bytecode::kLdaConstant, static_cast<uint32_t>(index),
                     OperandScale::kSingle);
  BytecodeNode second(Bytecode::kToName);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), first);
  Flush();
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(last_written(), second);
}

// Tests covering BytecodePeepholeOptimizer::CanElideLast().

TEST_F(BytecodePeepholeOptimizerTest, LdaTrueLdaFalse) {
  BytecodeNode first(Bytecode::kLdaTrue);
  BytecodeNode second(Bytecode::kLdaFalse);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 0);
  Flush();
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), second);
}

TEST_F(BytecodePeepholeOptimizerTest, LdaTrueStatementLdaFalse) {
  BytecodeNode first(Bytecode::kLdaTrue);
  first.source_info().Update({3, false});
  BytecodeNode second(Bytecode::kLdaFalse);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 0);
  Flush();
  CHECK_EQ(write_count(), 1);
  second.source_info().Update(first.source_info());
  CHECK_EQ(last_written(), second);
}

TEST_F(BytecodePeepholeOptimizerTest, NopStackCheck) {
  BytecodeNode first(Bytecode::kNop);
  BytecodeNode second(Bytecode::kStackCheck);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 0);
  Flush();
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written(), second);
}

TEST_F(BytecodePeepholeOptimizerTest, NopStatementStackCheck) {
  BytecodeNode first(Bytecode::kNop);
  first.source_info().Update({3, false});
  BytecodeNode second(Bytecode::kStackCheck);
  optimizer()->Write(&first);
  CHECK_EQ(write_count(), 0);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 0);
  Flush();
  CHECK_EQ(write_count(), 1);
  second.source_info().Update(first.source_info());
  CHECK_EQ(last_written(), second);
}

// Tests covering BytecodePeepholeOptimizer::UpdateLastAndCurrentBytecodes().

TEST_F(BytecodePeepholeOptimizerTest, MergeLoadICStar) {
  const uint32_t operands[] = {
      static_cast<uint32_t>(Register(31).ToOperand()), 32, 33,
      static_cast<uint32_t>(Register(256).ToOperand())};
  const int expected_operand_count = static_cast<int>(arraysize(operands));

  BytecodeNode first(Bytecode::kLdaNamedProperty, operands[0], operands[1],
                     operands[2], OperandScale::kSingle);
  BytecodeNode second(Bytecode::kStar, operands[3], OperandScale::kDouble);
  BytecodeNode third(Bytecode::kReturn);
  optimizer()->Write(&first);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written().bytecode(), Bytecode::kLdrNamedProperty);
  CHECK_EQ(last_written().operand_count(), expected_operand_count);
  for (int i = 0; i < expected_operand_count; ++i) {
    CHECK_EQ(last_written().operand(i), operands[i]);
  }
  CHECK_EQ(last_written().operand_scale(),
           std::max(first.operand_scale(), second.operand_scale()));
  optimizer()->Write(&third);
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(last_written().bytecode(), Bytecode::kLdar);
  CHECK_EQ(last_written().operand(0), operands[expected_operand_count - 1]);
  Flush();
  CHECK_EQ(last_written().bytecode(), third.bytecode());
}

TEST_F(BytecodePeepholeOptimizerTest, MergeLdaKeyedPropertyStar) {
  const uint32_t operands[] = {static_cast<uint32_t>(Register(31).ToOperand()),
                               9999997,
                               static_cast<uint32_t>(Register(1).ToOperand())};
  const int expected_operand_count = static_cast<int>(arraysize(operands));

  BytecodeNode first(Bytecode::kLdaKeyedProperty, operands[0], operands[1],
                     OperandScale::kQuadruple);
  BytecodeNode second(Bytecode::kStar, operands[2], OperandScale::kSingle);
  BytecodeNode third(Bytecode::kReturn);
  optimizer()->Write(&first);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written().bytecode(), Bytecode::kLdrKeyedProperty);
  CHECK_EQ(last_written().operand_count(), expected_operand_count);
  for (int i = 0; i < expected_operand_count; ++i) {
    CHECK_EQ(last_written().operand(i), operands[i]);
  }
  CHECK_EQ(last_written().operand_scale(),
           std::max(first.operand_scale(), second.operand_scale()));
  optimizer()->Write(&third);
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(last_written().bytecode(), Bytecode::kLdar);
  CHECK_EQ(last_written().operand(0), operands[expected_operand_count - 1]);
  Flush();
  CHECK_EQ(last_written().bytecode(), third.bytecode());
}

TEST_F(BytecodePeepholeOptimizerTest, MergeLdaGlobalStar) {
  const uint32_t operands[] = {54321, 19191,
                               static_cast<uint32_t>(Register(1).ToOperand())};
  const int expected_operand_count = static_cast<int>(arraysize(operands));

  BytecodeNode first(Bytecode::kLdaGlobal, operands[0], operands[1],
                     OperandScale::kDouble);
  BytecodeNode second(Bytecode::kStar, operands[2], OperandScale::kSingle);
  BytecodeNode third(Bytecode::kReturn);
  optimizer()->Write(&first);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written().bytecode(), Bytecode::kLdrGlobal);
  CHECK_EQ(last_written().operand_count(), expected_operand_count);
  for (int i = 0; i < expected_operand_count; ++i) {
    CHECK_EQ(last_written().operand(i), operands[i]);
  }
  CHECK_EQ(last_written().operand_scale(),
           std::max(first.operand_scale(), second.operand_scale()));
  optimizer()->Write(&third);
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(last_written().bytecode(), Bytecode::kLdar);
  CHECK_EQ(last_written().operand(0), operands[expected_operand_count - 1]);
  Flush();
  CHECK_EQ(last_written().bytecode(), third.bytecode());
}

TEST_F(BytecodePeepholeOptimizerTest, MergeLdaContextSlotStar) {
  const uint32_t operands[] = {
      static_cast<uint32_t>(Register(200000).ToOperand()), 55005500,
      static_cast<uint32_t>(Register(1).ToOperand())};
  const int expected_operand_count = static_cast<int>(arraysize(operands));

  BytecodeNode first(Bytecode::kLdaContextSlot, operands[0], operands[1],
                     OperandScale::kQuadruple);
  BytecodeNode second(Bytecode::kStar, operands[2], OperandScale::kSingle);
  BytecodeNode third(Bytecode::kReturn);
  optimizer()->Write(&first);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written().bytecode(), Bytecode::kLdrContextSlot);
  CHECK_EQ(last_written().operand_count(), expected_operand_count);
  for (int i = 0; i < expected_operand_count; ++i) {
    CHECK_EQ(last_written().operand(i), operands[i]);
  }
  CHECK_EQ(last_written().operand_scale(),
           std::max(first.operand_scale(), second.operand_scale()));
  optimizer()->Write(&third);
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(last_written().bytecode(), Bytecode::kLdar);
  CHECK_EQ(last_written().operand(0), operands[expected_operand_count - 1]);
  Flush();
  CHECK_EQ(last_written().bytecode(), third.bytecode());
}

TEST_F(BytecodePeepholeOptimizerTest, MergeLdaUndefinedStar) {
  const uint32_t operands[] = {
      static_cast<uint32_t>(Register(100000).ToOperand())};
  const int expected_operand_count = static_cast<int>(arraysize(operands));

  BytecodeNode first(Bytecode::kLdaUndefined);
  BytecodeNode second(Bytecode::kStar, operands[0], OperandScale::kQuadruple);
  BytecodeNode third(Bytecode::kReturn);
  optimizer()->Write(&first);
  optimizer()->Write(&second);
  CHECK_EQ(write_count(), 1);
  CHECK_EQ(last_written().bytecode(), Bytecode::kLdrUndefined);
  CHECK_EQ(last_written().operand_count(), expected_operand_count);
  for (int i = 0; i < expected_operand_count; ++i) {
    CHECK_EQ(last_written().operand(i), operands[i]);
  }
  CHECK_EQ(last_written().operand_scale(),
           std::max(first.operand_scale(), second.operand_scale()));
  optimizer()->Write(&third);
  CHECK_EQ(write_count(), 2);
  CHECK_EQ(last_written().bytecode(), Bytecode::kLdar);
  CHECK_EQ(last_written().operand(0), operands[expected_operand_count - 1]);
  Flush();
  CHECK_EQ(last_written().bytecode(), third.bytecode());
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
