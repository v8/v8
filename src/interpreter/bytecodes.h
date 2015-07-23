// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODES_H_
#define V8_INTERPRETER_BYTECODES_H_

#include <iosfwd>

// Clients of this interface shouldn't depend on lots of interpreter internals.
// Do not include anything from src/interpreter here!
#include "src/utils.h"

namespace v8 {
namespace internal {
namespace interpreter {

// The list of bytecodes which are interpreted by the interpreter.
#define BYTECODE_LIST(V) \
  V(LoadLiteral0, 1)     \
  V(Return, 0)

enum class Bytecode : uint8_t {
#define DECLARE_BYTECODE(Name, _) k##Name,
  BYTECODE_LIST(DECLARE_BYTECODE)
#undef DECLARE_BYTECODE
#define COUNT_BYTECODE(x, _) +1
  // The COUNT_BYTECODE macro will turn this into kLast = -1 +1 +1... which will
  // evaluate to the same value as the last real bytecode.
  kLast = -1 BYTECODE_LIST(COUNT_BYTECODE)
#undef COUNT_BYTECODE
};

class Bytecodes {
 public:
  // Returns string representation of |bytecode|.
  static const char* ToString(Bytecode bytecode);

  // Returns the number of arguments expected by |bytecode|.
  static const int NumberOfArguments(Bytecode bytecode);

  // Returns the size of the bytecode including its arguments.
  static const int Size(Bytecode bytecode);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Bytecodes);
};


std::ostream& operator<<(std::ostream& os, const Bytecode& bytecode);

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODES_H_
