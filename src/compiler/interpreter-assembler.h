// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_INTERPRETER_ASSEMBLER_H_
#define V8_COMPILER_INTERPRETER_ASSEMBLER_H_

// Clients of this interface shouldn't depend on lots of compiler internals.
// Do not include anything from src/compiler here!
#include "src/allocation.h"
#include "src/base/smart-pointers.h"
#include "src/builtins.h"
#include "src/frames.h"
#include "src/interpreter/bytecodes.h"
#include "src/unique.h"

namespace v8 {
namespace internal {

class Isolate;
class Zone;

namespace compiler {

class CallDescriptor;
class Graph;
class Node;
class Operator;
class RawMachineAssembler;
class Schedule;

class InterpreterAssembler {
 public:
  InterpreterAssembler(Isolate* isolate, Zone* zone,
                       interpreter::Bytecode bytecode);
  virtual ~InterpreterAssembler();

  Handle<Code> GenerateCode();

  // Returns the Imm8 immediate for bytecode operand |operand_index| in the
  // current bytecode.
  Node* BytecodeOperandImm8(int operand_index);
  // Returns the register index for bytecode operand |operand_index| in the
  // current bytecode.
  Node* BytecodeOperandReg(int operand_index);

  // Accumulator.
  Node* GetAccumulator();
  void SetAccumulator(Node* value);

  // Loads from and stores to the interpreter register file.
  Node* LoadRegister(Node* reg_index);
  Node* StoreRegister(Node* value, Node* reg_index);

  // Constants.
  Node* Int32Constant(int value);
  Node* IntPtrConstant(intptr_t value);
  Node* NumberConstant(double value);
  Node* HeapConstant(Unique<HeapObject> object);

  // Tag and untag Smi values.
  Node* SmiTag(Node* value);
  Node* SmiUntag(Node* value);

  // Load a field from an object on the heap.
  Node* LoadObjectField(Node* object, int offset);

  // Load |slot_index| from the current context.
  Node* LoadContextSlot(int slot_index);

  // Call JS builtin.
  Node* CallJSBuiltin(Builtins::JavaScript builtin, Node* receiver);
  Node* CallJSBuiltin(Builtins::JavaScript builtin, Node* receiver, Node* arg1);

  // Returns from the function.
  void Return();

  // Dispatch to the bytecode.
  void Dispatch();

 protected:
  // Close the graph.
  void End();

  // Protected helpers (for testing) which delegate to RawMachineAssembler.
  CallDescriptor* call_descriptor() const;
  Graph* graph();

 private:
  // Returns a raw pointer to start of the register file on the stack.
  Node* RegisterFileRawPointer();
  // Returns a tagged pointer to the current function's BytecodeArray object.
  Node* BytecodeArrayTaggedPointer();
  // Returns the offset from the BytecodeArrayPointer of the current bytecode.
  Node* BytecodeOffset();
  // Returns a raw pointer to first entry in the interpreter dispatch table.
  Node* DispatchTableRawPointer();
  // Returns a tagged pointer to the current context.
  Node* ContextTaggedPointer();

  // Returns the offset of register |index| relative to RegisterFilePointer().
  Node* RegisterFrameOffset(Node* index);

  Node* SmiShiftBitsConstant();
  Node* BytecodeOperand(int operand_index);
  Node* BytecodeOperandSignExtended(int operand_index);

  Node* CallJSBuiltin(Builtins::JavaScript builtin, Node* receiver,
                      Node** js_args, int js_arg_count);

  // Returns BytecodeOffset() advanced by delta bytecodes. Note: this does not
  // update BytecodeOffset() itself.
  Node* Advance(int delta);

  // Sets the end node of the graph.
  void SetEndInput(Node* input);

  // Private helpers which delegate to RawMachineAssembler.
  Isolate* isolate();
  Schedule* schedule();
  Zone* zone();

  interpreter::Bytecode bytecode_;
  base::SmartPointer<RawMachineAssembler> raw_assembler_;
  Node* end_node_;
  Node* accumulator_;
  bool code_generated_;

  DISALLOW_COPY_AND_ASSIGN(InterpreterAssembler);
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_INTERPRETER_ASSEMBLER_H_
