// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecodes.h"

namespace v8 {
namespace internal {
namespace interpreter {

// Maximum number of operands a bytecode may have.
static const int kMaxOperands = 3;

// kBytecodeTable relies on kNone being the same as zero to detect length.
STATIC_ASSERT(static_cast<int>(OperandType::kNone) == 0);

static const OperandType kBytecodeTable[][kMaxOperands] = {
#define DECLARE_OPERAND(_, ...) \
  { __VA_ARGS__ }               \
  ,
    BYTECODE_LIST(DECLARE_OPERAND)
#undef DECLARE_OPERAND
};


// static
const char* Bytecodes::ToString(Bytecode bytecode) {
  switch (bytecode) {
#define CASE(Name, ...)   \
  case Bytecode::k##Name: \
    return #Name;
    BYTECODE_LIST(CASE)
#undef CASE
  }
  UNREACHABLE();
  return "";
}


// static
uint8_t Bytecodes::ToByte(Bytecode bytecode) {
  return static_cast<uint8_t>(bytecode);
}


// static
Bytecode Bytecodes::FromByte(uint8_t value) {
  Bytecode bytecode = static_cast<Bytecode>(value);
  DCHECK(bytecode <= Bytecode::kLast);
  return bytecode;
}


// static
int Bytecodes::NumberOfOperands(Bytecode bytecode) {
  DCHECK(bytecode <= Bytecode::kLast);
  int count;
  uint8_t row = ToByte(bytecode);
  for (count = 0; count < kMaxOperands; count++) {
    if (kBytecodeTable[row][count] == OperandType::kNone) {
      break;
    }
  }
  return count;
}


// static
OperandType Bytecodes::GetOperandType(Bytecode bytecode, int i) {
  DCHECK(bytecode <= Bytecode::kLast && i < NumberOfOperands(bytecode));
  return kBytecodeTable[ToByte(bytecode)][i];
}


// static
int Bytecodes::Size(Bytecode bytecode) {
  return 1 + NumberOfOperands(bytecode);
}


// static
int Bytecodes::MaximumNumberOfOperands() { return kMaxOperands; }


// static
int Bytecodes::MaximumSize() { return 1 + kMaxOperands; }


std::ostream& operator<<(std::ostream& os, const Bytecode& bytecode) {
  return os << Bytecodes::ToString(bytecode);
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
