// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/interpreter/bytecode-array-builder.h"
#include "test/cctest/cctest.h"

using namespace v8::internal;
using namespace v8::internal::interpreter;

TEST(AllBytecodesGenerated) {
  InitializedHandleScope handle_scope;
  BytecodeArrayBuilder builder(handle_scope.main_isolate());

  builder.set_locals_count(1);
  CHECK_EQ(builder.locals_count(), 1);

  // Emit constant loads.
  builder.LoadLiteral(Smi::FromInt(0))
      .LoadLiteral(Smi::FromInt(8))
      .LoadUndefined()
      .LoadNull()
      .LoadTheHole()
      .LoadTrue()
      .LoadFalse();

  // Emit accumulator transfers.
  builder.LoadAccumulatorWithRegister(0).StoreAccumulatorInRegister(0);

  // Emit binary operators invocations.
  builder.BinaryOperation(Token::Value::ADD, 0)
      .BinaryOperation(Token::Value::SUB, 0)
      .BinaryOperation(Token::Value::MUL, 0)
      .BinaryOperation(Token::Value::DIV, 0);

  // Emit control flow. Return must be the last instruction.
  builder.Return();

  // Generate BytecodeArray.
  Handle<BytecodeArray> the_array = builder.ToBytecodeArray();
  CHECK_EQ(the_array->frame_size(), builder.locals_count() * kPointerSize);

  // Build scorecard of bytecodes encountered in the BytecodeArray.
  std::vector<int> scorecard(Bytecodes::ToByte(Bytecode::kLast) + 1);
  Bytecode final_bytecode = Bytecode::kLdaZero;
  for (int i = 0; i < the_array->length(); i++) {
    uint8_t code = the_array->get(i);
    scorecard[code] += 1;
    int operands = Bytecodes::NumberOfOperands(Bytecodes::FromByte(code));
    CHECK_LE(operands, Bytecodes::MaximumNumberOfOperands());
    final_bytecode = Bytecodes::FromByte(code);
    i += operands;
  }

  // Check return occurs at the end and only once in the BytecodeArray.
  CHECK_EQ(final_bytecode, Bytecode::kReturn);
  CHECK_EQ(scorecard[Bytecodes::ToByte(final_bytecode)], 1);

#define CHECK_BYTECODE_PRESENT(Name, ...)     \
  /* Check Bytecode is marked in scorecard */ \
  CHECK_GE(scorecard[Bytecodes::ToByte(Bytecode::k##Name)], 1);
  BYTECODE_LIST(CHECK_BYTECODE_PRESENT)
#undef CHECK_BYTECODE_PRESENT
}


TEST(FrameSizesLookGood) {
  for (int locals = 0; locals < 5; locals++) {
    for (int temps = 0; temps < 3; temps++) {
      InitializedHandleScope handle_scope;
      BytecodeArrayBuilder builder(handle_scope.main_isolate());
      builder.set_locals_count(locals);
      builder.Return();

      TemporaryRegisterScope temporaries(&builder);
      for (int i = 0; i < temps; i++) {
        temporaries.NewRegister();
      }

      Handle<BytecodeArray> the_array = builder.ToBytecodeArray();
      int total_registers = locals + temps;
      CHECK_EQ(the_array->frame_size(), total_registers * kPointerSize);
    }
  }
}


TEST(TemporariesRecycled) {
  InitializedHandleScope handle_scope;
  BytecodeArrayBuilder builder(handle_scope.main_isolate());
  builder.set_locals_count(0);
  builder.Return();

  int first;
  {
    TemporaryRegisterScope temporaries(&builder);
    first = temporaries.NewRegister();
    temporaries.NewRegister();
    temporaries.NewRegister();
    temporaries.NewRegister();
  }

  int second;
  {
    TemporaryRegisterScope temporaries(&builder);
    second = temporaries.NewRegister();
  }

  CHECK_EQ(first, second);
}
