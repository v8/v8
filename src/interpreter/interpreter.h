// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_INTERPRETER_H_
#define V8_INTERPRETER_INTERPRETER_H_

// Clients of this interface shouldn't depend on lots of interpreter internals.
// Do not include anything from src/interpreter other than
// src/interpreter/bytecodes.h here!
#include "src/base/macros.h"
#include "src/interpreter/bytecodes.h"

namespace v8 {
namespace internal {

class Isolate;
class CompilationInfo;

namespace compiler {
class InterpreterAssembler;
}

namespace interpreter {

class Interpreter {
 public:
  explicit Interpreter(Isolate* isolate);
  virtual ~Interpreter() {}

  void Initialize(bool create_heap_objects);

 private:
// Bytecode handler generator functions.
#define DECLARE_BYTECODE_HANDLER_GENERATOR(Name, _) \
  void Do##Name(compiler::InterpreterAssembler* assembler);
  BYTECODE_LIST(DECLARE_BYTECODE_HANDLER_GENERATOR)
#undef DECLARE_BYTECODE_HANDLER_GENERATOR

  Isolate* isolate_;

  DISALLOW_COPY_AND_ASSIGN(Interpreter);
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_INTERPRETER_H_
