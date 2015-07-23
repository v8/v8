// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecodes.h"

namespace v8 {
namespace internal {
namespace interpreter {

// static
const char* Bytecodes::ToString(Bytecode bytecode) {
  switch (bytecode) {
#define CASE(Name, _)       \
    case Bytecode::k##Name: \
      return #Name;
    BYTECODE_LIST(CASE)
#undef CASE
  }
  UNREACHABLE();
  return "";
}


// static
const int Bytecodes::NumberOfArguments(Bytecode bytecode) {
  switch (bytecode) {
#define CASE(Name, arg_count)   \
    case Bytecode::k##Name:     \
      return arg_count;
    BYTECODE_LIST(CASE)
#undef CASE
  }
  UNREACHABLE();
  return 0;
}


// static
const int Bytecodes::Size(Bytecode bytecode) {
  return NumberOfArguments(bytecode) + 1;
}


std::ostream& operator<<(std::ostream& os, const Bytecode& bytecode) {
  return os << Bytecodes::ToString(bytecode);
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
