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


// LoadLiteral0 <dst>
//
// Load literal '0' into the destination register.
void Interpreter::DoLoadLiteral0(compiler::InterpreterAssembler* assembler) {
  Node* register_index = __ BytecodeOperand(0);
  __ StoreRegister(__ NumberConstant(0), register_index);
  __ Dispatch();
}


// LoadSmi8 <dst>, <imm8>
//
// Load an 8-bit integer literal into destination register as a Smi.
void Interpreter::DoLoadSmi8(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Convert an 8-bit integer to a Smi.
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
