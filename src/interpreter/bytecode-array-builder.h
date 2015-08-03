// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODE_ARRAY_BUILDER_H_
#define V8_INTERPRETER_BYTECODE_ARRAY_BUILDER_H_

#include <vector>

#include "src/ast.h"
#include "src/interpreter/bytecodes.h"

namespace v8 {
namespace internal {

class Isolate;

namespace interpreter {

class BytecodeArrayBuilder {
 public:
  explicit BytecodeArrayBuilder(Isolate* isolate);
  Handle<BytecodeArray> ToBytecodeArray();

  // Set number of locals required for bytecode array.
  void set_locals_count(int number_of_locals);
  int locals_count() const;

  // Constant loads to accumulator
  BytecodeArrayBuilder& LoadLiteral(v8::internal::Smi* value);
  BytecodeArrayBuilder& LoadUndefined();
  BytecodeArrayBuilder& LoadNull();
  BytecodeArrayBuilder& LoadTheHole();
  BytecodeArrayBuilder& LoadTrue();
  BytecodeArrayBuilder& LoadFalse();

  // Register-accumulator transfers
  BytecodeArrayBuilder& LoadAccumulatorWithRegister(int reg);
  BytecodeArrayBuilder& StoreAccumulatorInRegister(int reg);

  // Operators
  BytecodeArrayBuilder& BinaryOperation(Token::Value binop, int reg);

  // Flow Control
  BytecodeArrayBuilder& Return();

 private:
  static Bytecode BytecodeForBinaryOperation(Token::Value op);

  void Output(Bytecode bytecode, uint8_t r0, uint8_t r1, uint8_t r2);
  void Output(Bytecode bytecode, uint8_t r0, uint8_t r1);
  void Output(Bytecode bytecode, uint8_t r0);
  void Output(Bytecode bytecode);

  bool OperandIsValid(Bytecode bytecode, int operand_index,
                      uint8_t operand_value) const;

  int BorrowTemporaryRegister();
  void ReturnTemporaryRegister(int reg);

  Isolate* isolate_;
  std::vector<uint8_t> bytecodes_;
  bool bytecode_generated_;

  int local_register_count_;
  int temporary_register_count_;
  int temporary_register_next_;

  friend class TemporaryRegisterScope;
  DISALLOW_IMPLICIT_CONSTRUCTORS(BytecodeArrayBuilder);
};

// A stack-allocated class than allows the instantiator to allocate
// temporary registers that are cleaned up when scope is closed.
class TemporaryRegisterScope {
 public:
  explicit TemporaryRegisterScope(BytecodeArrayBuilder* builder);
  ~TemporaryRegisterScope();
  int NewRegister();

 private:
  void* operator new(size_t size);
  void operator delete(void* p);

  BytecodeArrayBuilder* builder_;
  int count_;
  int register_;

  DISALLOW_COPY_AND_ASSIGN(TemporaryRegisterScope);
};


}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODE_ARRAY_BUILDER_H_
