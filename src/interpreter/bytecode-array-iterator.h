// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODE_ARRAY_ITERATOR_H_
#define V8_INTERPRETER_BYTECODE_ARRAY_ITERATOR_H_

#include "src/handles.h"
#include "src/interpreter/bytecodes.h"
#include "src/objects.h"

namespace v8 {
namespace internal {
namespace interpreter {

class BytecodeArrayIterator {
 public:
  explicit BytecodeArrayIterator(Handle<BytecodeArray> bytecode_array);

  void Advance();
  bool done() const;
  Bytecode current_bytecode() const;
  const Handle<BytecodeArray>& bytecode_array() const {
    return bytecode_array_;
  }

  int8_t GetSmi8Operand(int operand_index) const;
  int GetIndexOperand(int operand_index) const;
  Register GetRegisterOperand(int operand_index) const;
  Handle<Object> GetConstantForIndexOperand(int operand_index) const;

 private:
  uint8_t GetOperand(int operand_index, OperandType operand_type) const;

  Handle<BytecodeArray> bytecode_array_;
  int bytecode_offset_;

  DISALLOW_COPY_AND_ASSIGN(BytecodeArrayIterator);
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODE_GRAPH_ITERATOR_H_
