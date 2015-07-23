// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/interpreter-assembler.h"

#include <ostream>

#include "src/compiler/graph.h"
#include "src/compiler/instruction-selector.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-type.h"
#include "src/compiler/pipeline.h"
#include "src/compiler/raw-machine-assembler.h"
#include "src/compiler/schedule.h"
#include "src/frames.h"
#include "src/interpreter/bytecodes.h"
#include "src/macro-assembler.h"
#include "src/zone.h"

namespace v8 {
namespace internal {
namespace compiler {


InterpreterAssembler::InterpreterAssembler(Isolate* isolate, Zone* zone,
                                           interpreter::Bytecode bytecode)
    : bytecode_(bytecode),
      raw_assembler_(new RawMachineAssembler(
          isolate, new (zone) Graph(zone),
          Linkage::GetInterpreterDispatchDescriptor(zone), kMachPtr,
          InstructionSelector::SupportedMachineOperatorFlags())),
      end_node_(nullptr),
      code_generated_(false) {}


InterpreterAssembler::~InterpreterAssembler() {}


Handle<Code> InterpreterAssembler::GenerateCode() {
  DCHECK(!code_generated_);

  End();

  Schedule* schedule = raw_assembler_->Export();
  // TODO(rmcilroy): use a non-testing code generator.
  Handle<Code> code = Pipeline::GenerateCodeForTesting(
      isolate(), raw_assembler_->call_descriptor(), graph(), schedule);

#ifdef ENABLE_DISASSEMBLER
  if (FLAG_trace_ignition_codegen) {
    OFStream os(stdout);
    code->Disassemble(interpreter::Bytecodes::ToString(bytecode_), os);
    os << std::flush;
  }
#endif

  code_generated_ = true;
  return code;
}


Node* InterpreterAssembler::BytecodePointer() {
  return raw_assembler_->Parameter(Linkage::kInterpreterBytecodeParameter);
}


Node* InterpreterAssembler::DispatchTablePointer() {
  return raw_assembler_->Parameter(Linkage::kInterpreterDispatchTableParameter);
}


Node* InterpreterAssembler::FramePointer() {
  return raw_assembler_->LoadFramePointer();
}


Node* InterpreterAssembler::RegisterFrameOffset(int index) {
  DCHECK_LE(index, kMaxRegisterIndex);
  return Int32Constant(kFirstRegisterOffsetFromFp -
                       (index << kPointerSizeLog2));
}


Node* InterpreterAssembler::RegisterFrameOffset(Node* index) {
  return raw_assembler_->Int32Sub(
      Int32Constant(kFirstRegisterOffsetFromFp),
      raw_assembler_->Word32Shl(index, Int32Constant(kPointerSizeLog2)));
}


Node* InterpreterAssembler::BytecodeArg(int delta) {
  DCHECK_LT(delta, interpreter::Bytecodes::NumberOfArguments(bytecode_));
  return raw_assembler_->Load(kMachUint8, BytecodePointer(),
                              Int32Constant(1 + delta));
}


Node* InterpreterAssembler::LoadRegister(int index) {
  return raw_assembler_->Load(kMachPtr, FramePointer(),
                              RegisterFrameOffset(index));
}


Node* InterpreterAssembler::LoadRegister(Node* index) {
  return raw_assembler_->Load(kMachPtr, FramePointer(),
                              RegisterFrameOffset(index));
}


Node* InterpreterAssembler::StoreRegister(Node* value, int index) {
  return raw_assembler_->Store(kMachPtr, FramePointer(),
                               RegisterFrameOffset(index), value);
}


Node* InterpreterAssembler::StoreRegister(Node* value, Node* index) {
  return raw_assembler_->Store(kMachPtr, FramePointer(),
                               RegisterFrameOffset(index), value);
}


Node* InterpreterAssembler::Advance(int delta) {
  return raw_assembler_->IntPtrAdd(BytecodePointer(), Int32Constant(delta));
}


void InterpreterAssembler::Dispatch() {
  Node* new_bytecode_pointer = Advance(interpreter::Bytecodes::Size(bytecode_));
  Node* target_bytecode =
      raw_assembler_->Load(kMachUint8, new_bytecode_pointer);

  // TODO(rmcilroy): Create a code target dispatch table to avoid conversion
  // from code object on every dispatch.
  Node* target_code_object = raw_assembler_->Load(
      kMachPtr, DispatchTablePointer(),
      raw_assembler_->Word32Shl(target_bytecode,
                                Int32Constant(kPointerSizeLog2)));

  // If the order of the parameters you need to change the call signature below.
  STATIC_ASSERT(0 == Linkage::kInterpreterBytecodeParameter);
  STATIC_ASSERT(1 == Linkage::kInterpreterDispatchTableParameter);
  Node* tail_call = graph()->NewNode(common()->TailCall(call_descriptor()),
                                     target_code_object, new_bytecode_pointer,
                                     DispatchTablePointer(), graph()->start(),
                                     graph()->start());
  schedule()->AddTailCall(raw_assembler_->CurrentBlock(), tail_call);

  // This should always be the end node.
  SetEndInput(tail_call);
}


void InterpreterAssembler::SetEndInput(Node* input) {
  DCHECK(!end_node_);
  end_node_ = input;
}


void InterpreterAssembler::End() {
  DCHECK(end_node_);
  // TODO(rmcilroy): Support more than 1 end input.
  Node* end = graph()->NewNode(common()->End(1), end_node_);
  graph()->SetEnd(end);
}


// RawMachineAssembler delegate helpers:
Isolate* InterpreterAssembler::isolate() { return raw_assembler_->isolate(); }


Graph* InterpreterAssembler::graph() { return raw_assembler_->graph(); }


CallDescriptor* InterpreterAssembler::call_descriptor() const {
  return raw_assembler_->call_descriptor();
}


Schedule* InterpreterAssembler::schedule() {
  return raw_assembler_->schedule();
}


MachineOperatorBuilder* InterpreterAssembler::machine() {
  return raw_assembler_->machine();
}


CommonOperatorBuilder* InterpreterAssembler::common() {
  return raw_assembler_->common();
}


Node* InterpreterAssembler::Int32Constant(int value) {
  return raw_assembler_->Int32Constant(value);
}


Node* InterpreterAssembler::NumberConstant(double value) {
  return raw_assembler_->NumberConstant(value);
}


}  // namespace interpreter
}  // namespace internal
}  // namespace v8
