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
    isolate_->heap()->public_set_interpreter_table(*handler_table);

#define GENERATE_CODE(Name, _)                                     \
    {                                                              \
      compiler::InterpreterAssembler assembler(isolate_, &zone,     \
                                               Bytecode::k##Name); \
      Do##Name(&assembler);                                        \
      handler_table->set(static_cast<int>(Bytecode::k##Name),      \
                         *assembler.GenerateCode());               \
    }
    BYTECODE_LIST(GENERATE_CODE)
#undef GENERATE_CODE
  }
}


// Load literal '0' into the register index specified by the bytecode's
// argument.
void Interpreter::DoLoadLiteral0(compiler::InterpreterAssembler* assembler) {
  Node* register_index = __ BytecodeArg(0);
  __ StoreRegister(__ NumberConstant(0), register_index);
  __ Dispatch();
}


// Return the value in register 0.
void Interpreter::DoReturn(compiler::InterpreterAssembler* assembler) {
  // TODO(rmcilroy) Jump to exit trampoline.
}


}  // namespace interpreter
}  // namespace internal
}  // namespace v8
