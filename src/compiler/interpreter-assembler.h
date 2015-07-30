// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_INTERPRETER_CODEGEN_H_
#define V8_COMPILER_INTERPRETER_CODEGEN_H_

// Clients of this interface shouldn't depend on lots of compiler internals.
// Do not include anything from src/compiler here!
#include "src/allocation.h"
#include "src/base/smart-pointers.h"
#include "src/frames.h"
#include "src/interpreter/bytecodes.h"
#include "src/unique.h"

namespace v8 {
namespace internal {

class Isolate;
class Zone;

namespace compiler {

class CallDescriptor;
class CommonOperatorBuilder;
class Graph;
class MachineOperatorBuilder;
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

  // Constants.
  Node* Int32Constant(int value);
  Node* NumberConstant(double value);
  Node* HeapConstant(Unique<HeapObject> object);

  // Returns the bytecode argument |index| for the current bytecode.
  Node* BytecodeArg(int index);

  // Loads from and stores to the interpreter register file.
  Node* LoadRegister(int index);
  Node* LoadRegister(Node* index);
  Node* StoreRegister(Node* value, int index);
  Node* StoreRegister(Node* value, Node* index);

  // Returns from the function.
  void Return();

  // Dispatch to the bytecode.
  void Dispatch();

 protected:
  static const int kFirstRegisterOffsetFromFp =
      -kPointerSize - StandardFrameConstants::kFixedFrameSizeFromFp;

  // TODO(rmcilroy): Increase this when required.
  static const int kMaxRegisterIndex = 255;

  // Close the graph.
  void End();

  // Protected helpers (for testing) which delegate to RawMachineAssembler.
  CallDescriptor* call_descriptor() const;
  Graph* graph();

 private:
  // Returns a tagged pointer to the current function's BytecodeArray object.
  Node* BytecodeArrayPointer();
  // Returns the offset from the BytecodeArrayPointer of the current bytecode.
  Node* BytecodeOffset();
  // Returns a pointer to first entry in the interpreter dispatch table.
  Node* DispatchTablePointer();
  // Returns the frame pointer for the current function.
  Node* FramePointer();

  // Returns the offset of register |index|.
  Node* RegisterFrameOffset(int index);
  Node* RegisterFrameOffset(Node* index);

  // Returns BytecodeOffset() advanced by delta bytecodes. Note: this does not
  // update BytecodeOffset() itself.
  Node* Advance(int delta);

  // Sets the end node of the graph.
  void SetEndInput(Node* input);

  // Private helpers which delegate to RawMachineAssembler.
  Isolate* isolate();
  Schedule* schedule();
  MachineOperatorBuilder* machine();
  CommonOperatorBuilder* common();

  interpreter::Bytecode bytecode_;
  base::SmartPointer<RawMachineAssembler> raw_assembler_;
  Node* end_node_;
  bool code_generated_;

  DISALLOW_COPY_AND_ASSIGN(InterpreterAssembler);
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_INTERPRETER_CODEGEN_H_
