// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/interpreter.h"

#include "src/compiler.h"
#include "src/compiler/interpreter-assembler.h"
#include "src/factory.h"
#include "src/interpreter/bytecodes.h"
#include "src/zone.h"

namespace v8 {
namespace internal {
namespace interpreter {

using compiler::Node;
#define __ assembler->


Interpreter::Interpreter(Isolate* isolate) : isolate_(isolate) {}


void Interpreter::Initialize(bool create_heap_objects) {
  DCHECK(FLAG_ignition);
  if (create_heap_objects) {
    Zone zone;
    HandleScope scope(isolate_);
    Handle<FixedArray> handler_table = isolate_->factory()->NewFixedArray(
        static_cast<int>(Bytecode::kLast) + 1, TENURED);
    // We rely on the interpreter handler table being immovable, so check that
    // it was allocated on the first page (which is always immovable).
    DCHECK(isolate_->heap()->old_space()->FirstPage()->Contains(
        handler_table->address()));
    isolate_->heap()->public_set_interpreter_table(*handler_table);

#define GENERATE_CODE(Name, ...)                                    \
  {                                                                 \
    compiler::InterpreterAssembler assembler(isolate_, &zone,       \
                                             Bytecode::k##Name);    \
    Do##Name(&assembler);                                           \
    Handle<Code> code = assembler.GenerateCode();                   \
    handler_table->set(static_cast<int>(Bytecode::k##Name), *code); \
  }
    BYTECODE_LIST(GENERATE_CODE)
#undef GENERATE_CODE
  }
}


// LdaZero
//
// Load literal '0' into the accumulator.
void Interpreter::DoLdaZero(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Implement.
}


// LdaSmi8 <imm8>
//
// Load an 8-bit integer literal into the accumulator as a Smi.
void Interpreter::DoLdaSmi8(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Implement 8-bit integer to SMI promotion.
}


// LdaUndefined
//
// Load Undefined into the accumulator.
void Interpreter::DoLdaUndefined(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Implement.
}


// LdaNull
//
// Load Null into the accumulator.
void Interpreter::DoLdaNull(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Implement.
}


// LdaTheHole
//
// Load TheHole into the accumulator.
void Interpreter::DoLdaTheHole(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Implement.
}


// LdaTrue
//
// Load True into the accumulator.
void Interpreter::DoLdaTrue(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Implement.
}


// LdaFalse
//
// Load False into the accumulator.
void Interpreter::DoLdaFalse(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Implement.
}


// Ldar <src>
//
// Load accumulator with value from register <src>.
void Interpreter::DoLdar(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Implement.
}


// Star <dst>
//
// Store accumulator to register <dst>.
void Interpreter::DoStar(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Implement.
}


// Add <src>
//
// Add register <src> to accumulator.
void Interpreter::DoAdd(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Implement.
}


// Sub <src>
//
// Subtract register <src> from accumulator.
void Interpreter::DoSub(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Implement.
}


// Mul <src>
//
// Multiply accumulator by register <src>.
void Interpreter::DoMul(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Implement add register to accumulator.
}


// Div <src>
//
// Divide register <src> by accumulator.
void Interpreter::DoDiv(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Implement.
}


// Return
//
// Return the value in register 0.
void Interpreter::DoReturn(compiler::InterpreterAssembler* assembler) {
  __ Return();
}


}  // namespace interpreter
}  // namespace internal
}  // namespace v8
