// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/interpreter-generator.h"

#include <array>
#include <tuple>

#include "src/builtins/builtins-arguments-gen.h"
#include "src/builtins/builtins-constructor-gen.h"
#include "src/builtins/builtins-forin-gen.h"
#include "src/code-events.h"
#include "src/code-factory.h"
#include "src/factory.h"
#include "src/ic/accessor-assembler.h"
#include "src/ic/binary-op-assembler.h"
#include "src/interpreter/bytecode-flags.h"
#include "src/interpreter/bytecodes.h"
#include "src/interpreter/interpreter-assembler.h"
#include "src/interpreter/interpreter-intrinsics-generator.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace interpreter {

using compiler::Node;
typedef CodeStubAssembler::Label Label;
typedef CodeStubAssembler::Variable Variable;

class InterpreterGenerator {
 public:
  explicit InterpreterGenerator(Isolate* isolate) : isolate_(isolate) {}

// Bytecode handler generator functions.
#define DECLARE_BYTECODE_HANDLER_GENERATOR(Name, ...) \
  void Do##Name(InterpreterAssembler* assembler);
  BYTECODE_LIST(DECLARE_BYTECODE_HANDLER_GENERATOR)
#undef DECLARE_BYTECODE_HANDLER_GENERATOR

 private:
  typedef Node* (BinaryOpAssembler::*BinaryOpGenerator)(Node* context,
                                                        Node* left, Node* right,
                                                        Node* slot,
                                                        Node* vector);

  // Generates code to perform the binary operation via |generator|.
  void DoBinaryOpWithFeedback(InterpreterAssembler* assembler,
                              BinaryOpGenerator generator);

  // Generates code to perform the |compare_op| comparison while gathering
  // type feedback.
  void DoCompareOpWithFeedback(Token::Value compare_op,
                               InterpreterAssembler* assembler);

  // Generates code to perform the bitwise binary operation corresponding to
  // |bitwise_op| while gathering type feedback.
  void DoBitwiseBinaryOp(Token::Value bitwise_op,
                         InterpreterAssembler* assembler);

  // Generates code to perform the comparison operation associated with
  // |compare_op|.
  void DoCompareOp(Token::Value compare_op, InterpreterAssembler* assembler);

  // Generates code to perform a global store via |ic|.
  void DoStaGlobal(Callable ic, InterpreterAssembler* assembler);

  // Generates code to perform a named property store via |ic|.
  void DoStoreIC(Callable ic, InterpreterAssembler* assembler);

  // Generates code to perform a keyed property store via |ic|.
  void DoKeyedStoreIC(Callable ic, InterpreterAssembler* assembler);

  // Generates code to perform a JS call that collects type feedback.
  void DoJSCall(InterpreterAssembler* assembler, TailCallMode tail_call_mode);

  // Generates code to perform a JS call with a known number of arguments that
  // collects type feedback.
  void DoJSCallN(InterpreterAssembler* assembler, int n);

  // Generates code to perform delete via function_id.
  void DoDelete(Runtime::FunctionId function_id,
                InterpreterAssembler* assembler);

  // Generates code to perform a lookup slot load via |function_id|.
  void DoLdaLookupSlot(Runtime::FunctionId function_id,
                       InterpreterAssembler* assembler);

  // Generates code to perform a lookup slot load via |function_id| that can
  // fast path to a context slot load.
  void DoLdaLookupContextSlot(Runtime::FunctionId function_id,
                              InterpreterAssembler* assembler);

  // Generates code to perform a lookup slot load via |function_id| that can
  // fast path to a global load.
  void DoLdaLookupGlobalSlot(Runtime::FunctionId function_id,
                             InterpreterAssembler* assembler);

  // Generates code to perform a lookup slot store depending on
  // |language_mode|.
  void DoStaLookupSlot(LanguageMode language_mode,
                       InterpreterAssembler* assembler);

  // Generates code to load a global property.
  void BuildLoadGlobalIC(int slot_operand_index, int name_operand_index,
                         TypeofMode typeof_mode,
                         InterpreterAssembler* assembler);

  // Generates code to load a property.
  void BuildLoadIC(int recv_operand_index, int slot_operand_index,
                   int name_operand_index, InterpreterAssembler* assembler);

  // Generates code to prepare the result for ForInPrepare. Cache data
  // are placed into the consecutive series of registers starting at
  // |output_register|.
  void BuildForInPrepareResult(Node* output_register, Node* cache_type,
                               Node* cache_array, Node* cache_length,
                               InterpreterAssembler* assembler);

  // Generates code to perform the unary operation via |callable|.
  Node* BuildUnaryOp(Callable callable, InterpreterAssembler* assembler);

  Isolate* isolate_;
};

Handle<Code> GenerateBytecodeHandler(Isolate* isolate, Bytecode bytecode,
                                     OperandScale operand_scale) {
  Zone zone(isolate->allocator(), ZONE_NAME);
  InterpreterDispatchDescriptor descriptor(isolate);
  compiler::CodeAssemblerState state(
      isolate, &zone, descriptor, Code::ComputeFlags(Code::BYTECODE_HANDLER),
      Bytecodes::ToString(bytecode), Bytecodes::ReturnCount(bytecode));
  InterpreterAssembler assembler(&state, bytecode, operand_scale);
  if (Bytecodes::MakesCallAlongCriticalPath(bytecode)) {
    assembler.SaveBytecodeOffset();
  }
  InterpreterGenerator generator(isolate);

  switch (bytecode) {
#define CALL_GENERATOR(Name, ...)   \
  case Bytecode::k##Name:           \
    generator.Do##Name(&assembler); \
    break;
    BYTECODE_LIST(CALL_GENERATOR);
#undef CALL_GENERATOR
  }

  Handle<Code> code = compiler::CodeAssembler::GenerateCode(&state);
  PROFILE(isolate, CodeCreateEvent(
                       CodeEventListener::BYTECODE_HANDLER_TAG,
                       AbstractCode::cast(*code),
                       Bytecodes::ToString(bytecode, operand_scale).c_str()));
#ifdef ENABLE_DISASSEMBLER
  if (FLAG_trace_ignition_codegen) {
    OFStream os(stdout);
    code->Disassemble(Bytecodes::ToString(bytecode), os);
    os << std::flush;
  }
#endif  // ENABLE_DISASSEMBLER
  return code;
}

#define __ assembler->

// LdaZero
//
// Load literal '0' into the accumulator.
void InterpreterGenerator::DoLdaZero(InterpreterAssembler* assembler) {
  Node* zero_value = __ NumberConstant(0.0);
  __ SetAccumulator(zero_value);
  __ Dispatch();
}

// LdaSmi <imm>
//
// Load an integer literal into the accumulator as a Smi.
void InterpreterGenerator::DoLdaSmi(InterpreterAssembler* assembler) {
  Node* smi_int = __ BytecodeOperandImmSmi(0);
  __ SetAccumulator(smi_int);
  __ Dispatch();
}

// LdaConstant <idx>
//
// Load constant literal at |idx| in the constant pool into the accumulator.
void InterpreterGenerator::DoLdaConstant(InterpreterAssembler* assembler) {
  Node* index = __ BytecodeOperandIdx(0);
  Node* constant = __ LoadConstantPoolEntry(index);
  __ SetAccumulator(constant);
  __ Dispatch();
}

// LdaUndefined
//
// Load Undefined into the accumulator.
void InterpreterGenerator::DoLdaUndefined(InterpreterAssembler* assembler) {
  Node* undefined_value =
      __ HeapConstant(isolate_->factory()->undefined_value());
  __ SetAccumulator(undefined_value);
  __ Dispatch();
}

// LdaNull
//
// Load Null into the accumulator.
void InterpreterGenerator::DoLdaNull(InterpreterAssembler* assembler) {
  Node* null_value = __ HeapConstant(isolate_->factory()->null_value());
  __ SetAccumulator(null_value);
  __ Dispatch();
}

// LdaTheHole
//
// Load TheHole into the accumulator.
void InterpreterGenerator::DoLdaTheHole(InterpreterAssembler* assembler) {
  Node* the_hole_value = __ HeapConstant(isolate_->factory()->the_hole_value());
  __ SetAccumulator(the_hole_value);
  __ Dispatch();
}

// LdaTrue
//
// Load True into the accumulator.
void InterpreterGenerator::DoLdaTrue(InterpreterAssembler* assembler) {
  Node* true_value = __ HeapConstant(isolate_->factory()->true_value());
  __ SetAccumulator(true_value);
  __ Dispatch();
}

// LdaFalse
//
// Load False into the accumulator.
void InterpreterGenerator::DoLdaFalse(InterpreterAssembler* assembler) {
  Node* false_value = __ HeapConstant(isolate_->factory()->false_value());
  __ SetAccumulator(false_value);
  __ Dispatch();
}

// Ldar <src>
//
// Load accumulator with value from register <src>.
void InterpreterGenerator::DoLdar(InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* value = __ LoadRegister(reg_index);
  __ SetAccumulator(value);
  __ Dispatch();
}

// Star <dst>
//
// Store accumulator to register <dst>.
void InterpreterGenerator::DoStar(InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* accumulator = __ GetAccumulator();
  __ StoreRegister(accumulator, reg_index);
  __ Dispatch();
}

// Mov <src> <dst>
//
// Stores the value of register <src> to register <dst>.
void InterpreterGenerator::DoMov(InterpreterAssembler* assembler) {
  Node* src_index = __ BytecodeOperandReg(0);
  Node* src_value = __ LoadRegister(src_index);
  Node* dst_index = __ BytecodeOperandReg(1);
  __ StoreRegister(src_value, dst_index);
  __ Dispatch();
}

void InterpreterGenerator::BuildLoadGlobalIC(int slot_operand_index,
                                             int name_operand_index,
                                             TypeofMode typeof_mode,
                                             InterpreterAssembler* assembler) {
  // Must be kept in sync with AccessorAssembler::LoadGlobalIC.

  // Load the global via the LoadGlobalIC.
  Node* feedback_vector = __ LoadFeedbackVector();
  Node* feedback_slot = __ BytecodeOperandIdx(slot_operand_index);

  AccessorAssembler accessor_asm(assembler->state());

  Label try_handler(assembler, Label::kDeferred),
      miss(assembler, Label::kDeferred);

  // Fast path without frame construction for the data case.
  {
    Label done(assembler);
    Variable var_result(assembler, MachineRepresentation::kTagged);
    ExitPoint exit_point(assembler, &done, &var_result);

    accessor_asm.LoadGlobalIC_TryPropertyCellCase(
        feedback_vector, feedback_slot, &exit_point, &try_handler, &miss,
        CodeStubAssembler::INTPTR_PARAMETERS);

    __ Bind(&done);
    __ SetAccumulator(var_result.value());
    __ Dispatch();
  }

  // Slow path with frame construction.
  {
    Label done(assembler);
    Variable var_result(assembler, MachineRepresentation::kTagged);
    ExitPoint exit_point(assembler, &done, &var_result);

    __ Bind(&try_handler);
    {
      Node* context = __ GetContext();
      Node* smi_slot = __ SmiTag(feedback_slot);
      Node* name_index = __ BytecodeOperandIdx(name_operand_index);
      Node* name = __ LoadConstantPoolEntry(name_index);

      AccessorAssembler::LoadICParameters params(context, nullptr, name,
                                                 smi_slot, feedback_vector);
      accessor_asm.LoadGlobalIC_TryHandlerCase(&params, typeof_mode,
                                               &exit_point, &miss);
    }

    __ Bind(&miss);
    {
      Node* context = __ GetContext();
      Node* smi_slot = __ SmiTag(feedback_slot);
      Node* name_index = __ BytecodeOperandIdx(name_operand_index);
      Node* name = __ LoadConstantPoolEntry(name_index);

      AccessorAssembler::LoadICParameters params(context, nullptr, name,
                                                 smi_slot, feedback_vector);
      accessor_asm.LoadGlobalIC_MissCase(&params, &exit_point);
    }

    __ Bind(&done);
    {
      __ SetAccumulator(var_result.value());
      __ Dispatch();
    }
  }
}

// LdaGlobal <name_index> <slot>
//
// Load the global with name in constant pool entry <name_index> into the
// accumulator using FeedBackVector slot <slot> outside of a typeof.
void InterpreterGenerator::DoLdaGlobal(InterpreterAssembler* assembler) {
  static const int kNameOperandIndex = 0;
  static const int kSlotOperandIndex = 1;

  BuildLoadGlobalIC(kSlotOperandIndex, kNameOperandIndex, NOT_INSIDE_TYPEOF,
                    assembler);
}

// LdaGlobalInsideTypeof <name_index> <slot>
//
// Load the global with name in constant pool entry <name_index> into the
// accumulator using FeedBackVector slot <slot> inside of a typeof.
void InterpreterGenerator::DoLdaGlobalInsideTypeof(
    InterpreterAssembler* assembler) {
  static const int kNameOperandIndex = 0;
  static const int kSlotOperandIndex = 1;

  BuildLoadGlobalIC(kSlotOperandIndex, kNameOperandIndex, INSIDE_TYPEOF,
                    assembler);
}

void InterpreterGenerator::DoStaGlobal(Callable ic,
                                       InterpreterAssembler* assembler) {
  // Get the global object.
  Node* context = __ GetContext();
  Node* native_context = __ LoadNativeContext(context);
  Node* global =
      __ LoadContextElement(native_context, Context::EXTENSION_INDEX);

  // Store the global via the StoreIC.
  Node* code_target = __ HeapConstant(ic.code());
  Node* constant_index = __ BytecodeOperandIdx(0);
  Node* name = __ LoadConstantPoolEntry(constant_index);
  Node* value = __ GetAccumulator();
  Node* raw_slot = __ BytecodeOperandIdx(1);
  Node* smi_slot = __ SmiTag(raw_slot);
  Node* feedback_vector = __ LoadFeedbackVector();
  __ CallStub(ic.descriptor(), code_target, context, global, name, value,
              smi_slot, feedback_vector);
  __ Dispatch();
}

// StaGlobalSloppy <name_index> <slot>
//
// Store the value in the accumulator into the global with name in constant pool
// entry <name_index> using FeedBackVector slot <slot> in sloppy mode.
void InterpreterGenerator::DoStaGlobalSloppy(InterpreterAssembler* assembler) {
  Callable ic = CodeFactory::StoreGlobalICInOptimizedCode(isolate_, SLOPPY);
  DoStaGlobal(ic, assembler);
}

// StaGlobalStrict <name_index> <slot>
//
// Store the value in the accumulator into the global with name in constant pool
// entry <name_index> using FeedBackVector slot <slot> in strict mode.
void InterpreterGenerator::DoStaGlobalStrict(InterpreterAssembler* assembler) {
  Callable ic = CodeFactory::StoreGlobalICInOptimizedCode(isolate_, STRICT);
  DoStaGlobal(ic, assembler);
}

// LdaContextSlot <context> <slot_index> <depth>
//
// Load the object in |slot_index| of the context at |depth| in the context
// chain starting at |context| into the accumulator.
void InterpreterGenerator::DoLdaContextSlot(InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* context = __ LoadRegister(reg_index);
  Node* slot_index = __ BytecodeOperandIdx(1);
  Node* depth = __ BytecodeOperandUImm(2);
  Node* slot_context = __ GetContextAtDepth(context, depth);
  Node* result = __ LoadContextElement(slot_context, slot_index);
  __ SetAccumulator(result);
  __ Dispatch();
}

// LdaImmutableContextSlot <context> <slot_index> <depth>
//
// Load the object in |slot_index| of the context at |depth| in the context
// chain starting at |context| into the accumulator.
void InterpreterGenerator::DoLdaImmutableContextSlot(
    InterpreterAssembler* assembler) {
  // TODO(danno) Share the actual code object rather creating a duplicate one.
  DoLdaContextSlot(assembler);
}

// LdaCurrentContextSlot <slot_index>
//
// Load the object in |slot_index| of the current context into the accumulator.
void InterpreterGenerator::DoLdaCurrentContextSlot(
    InterpreterAssembler* assembler) {
  Node* slot_index = __ BytecodeOperandIdx(0);
  Node* slot_context = __ GetContext();
  Node* result = __ LoadContextElement(slot_context, slot_index);
  __ SetAccumulator(result);
  __ Dispatch();
}

// LdaImmutableCurrentContextSlot <slot_index>
//
// Load the object in |slot_index| of the current context into the accumulator.
void InterpreterGenerator::DoLdaImmutableCurrentContextSlot(
    InterpreterAssembler* assembler) {
  // TODO(danno) Share the actual code object rather creating a duplicate one.
  DoLdaCurrentContextSlot(assembler);
}

// StaContextSlot <context> <slot_index> <depth>
//
// Stores the object in the accumulator into |slot_index| of the context at
// |depth| in the context chain starting at |context|.
void InterpreterGenerator::DoStaContextSlot(InterpreterAssembler* assembler) {
  Node* value = __ GetAccumulator();
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* context = __ LoadRegister(reg_index);
  Node* slot_index = __ BytecodeOperandIdx(1);
  Node* depth = __ BytecodeOperandUImm(2);
  Node* slot_context = __ GetContextAtDepth(context, depth);
  __ StoreContextElement(slot_context, slot_index, value);
  __ Dispatch();
}

// StaCurrentContextSlot <slot_index>
//
// Stores the object in the accumulator into |slot_index| of the current
// context.
void InterpreterGenerator::DoStaCurrentContextSlot(
    InterpreterAssembler* assembler) {
  Node* value = __ GetAccumulator();
  Node* slot_index = __ BytecodeOperandIdx(0);
  Node* slot_context = __ GetContext();
  __ StoreContextElement(slot_context, slot_index, value);
  __ Dispatch();
}

void InterpreterGenerator::DoLdaLookupSlot(Runtime::FunctionId function_id,
                                           InterpreterAssembler* assembler) {
  Node* name_index = __ BytecodeOperandIdx(0);
  Node* name = __ LoadConstantPoolEntry(name_index);
  Node* context = __ GetContext();
  Node* result = __ CallRuntime(function_id, context, name);
  __ SetAccumulator(result);
  __ Dispatch();
}

// LdaLookupSlot <name_index>
//
// Lookup the object with the name in constant pool entry |name_index|
// dynamically.
void InterpreterGenerator::DoLdaLookupSlot(InterpreterAssembler* assembler) {
  DoLdaLookupSlot(Runtime::kLoadLookupSlot, assembler);
}

// LdaLookupSlotInsideTypeof <name_index>
//
// Lookup the object with the name in constant pool entry |name_index|
// dynamically without causing a NoReferenceError.
void InterpreterGenerator::DoLdaLookupSlotInsideTypeof(
    InterpreterAssembler* assembler) {
  DoLdaLookupSlot(Runtime::kLoadLookupSlotInsideTypeof, assembler);
}

void InterpreterGenerator::DoLdaLookupContextSlot(
    Runtime::FunctionId function_id, InterpreterAssembler* assembler) {
  Node* context = __ GetContext();
  Node* name_index = __ BytecodeOperandIdx(0);
  Node* slot_index = __ BytecodeOperandIdx(1);
  Node* depth = __ BytecodeOperandUImm(2);

  Label slowpath(assembler, Label::kDeferred);

  // Check for context extensions to allow the fast path.
  __ GotoIfHasContextExtensionUpToDepth(context, depth, &slowpath);

  // Fast path does a normal load context.
  {
    Node* slot_context = __ GetContextAtDepth(context, depth);
    Node* result = __ LoadContextElement(slot_context, slot_index);
    __ SetAccumulator(result);
    __ Dispatch();
  }

  // Slow path when we have to call out to the runtime.
  __ Bind(&slowpath);
  {
    Node* name = __ LoadConstantPoolEntry(name_index);
    Node* result = __ CallRuntime(function_id, context, name);
    __ SetAccumulator(result);
    __ Dispatch();
  }
}

// LdaLookupSlot <name_index>
//
// Lookup the object with the name in constant pool entry |name_index|
// dynamically.
void InterpreterGenerator::DoLdaLookupContextSlot(
    InterpreterAssembler* assembler) {
  DoLdaLookupContextSlot(Runtime::kLoadLookupSlot, assembler);
}

// LdaLookupSlotInsideTypeof <name_index>
//
// Lookup the object with the name in constant pool entry |name_index|
// dynamically without causing a NoReferenceError.
void InterpreterGenerator::DoLdaLookupContextSlotInsideTypeof(
    InterpreterAssembler* assembler) {
  DoLdaLookupContextSlot(Runtime::kLoadLookupSlotInsideTypeof, assembler);
}

void InterpreterGenerator::DoLdaLookupGlobalSlot(
    Runtime::FunctionId function_id, InterpreterAssembler* assembler) {
  Node* context = __ GetContext();
  Node* depth = __ BytecodeOperandUImm(2);

  Label slowpath(assembler, Label::kDeferred);

  // Check for context extensions to allow the fast path
  __ GotoIfHasContextExtensionUpToDepth(context, depth, &slowpath);

  // Fast path does a normal load global
  {
    static const int kNameOperandIndex = 0;
    static const int kSlotOperandIndex = 1;

    TypeofMode typeof_mode = function_id == Runtime::kLoadLookupSlotInsideTypeof
                                 ? INSIDE_TYPEOF
                                 : NOT_INSIDE_TYPEOF;

    BuildLoadGlobalIC(kSlotOperandIndex, kNameOperandIndex, typeof_mode,
                      assembler);
  }

  // Slow path when we have to call out to the runtime
  __ Bind(&slowpath);
  {
    Node* name_index = __ BytecodeOperandIdx(0);
    Node* name = __ LoadConstantPoolEntry(name_index);
    Node* result = __ CallRuntime(function_id, context, name);
    __ SetAccumulator(result);
    __ Dispatch();
  }
}

// LdaLookupGlobalSlot <name_index> <feedback_slot> <depth>
//
// Lookup the object with the name in constant pool entry |name_index|
// dynamically.
void InterpreterGenerator::DoLdaLookupGlobalSlot(
    InterpreterAssembler* assembler) {
  DoLdaLookupGlobalSlot(Runtime::kLoadLookupSlot, assembler);
}

// LdaLookupGlobalSlotInsideTypeof <name_index> <feedback_slot> <depth>
//
// Lookup the object with the name in constant pool entry |name_index|
// dynamically without causing a NoReferenceError.
void InterpreterGenerator::DoLdaLookupGlobalSlotInsideTypeof(
    InterpreterAssembler* assembler) {
  DoLdaLookupGlobalSlot(Runtime::kLoadLookupSlotInsideTypeof, assembler);
}

void InterpreterGenerator::DoStaLookupSlot(LanguageMode language_mode,
                                           InterpreterAssembler* assembler) {
  Node* value = __ GetAccumulator();
  Node* index = __ BytecodeOperandIdx(0);
  Node* name = __ LoadConstantPoolEntry(index);
  Node* context = __ GetContext();
  Node* result = __ CallRuntime(is_strict(language_mode)
                                    ? Runtime::kStoreLookupSlot_Strict
                                    : Runtime::kStoreLookupSlot_Sloppy,
                                context, name, value);
  __ SetAccumulator(result);
  __ Dispatch();
}

// StaLookupSlotSloppy <name_index>
//
// Store the object in accumulator to the object with the name in constant
// pool entry |name_index| in sloppy mode.
void InterpreterGenerator::DoStaLookupSlotSloppy(
    InterpreterAssembler* assembler) {
  DoStaLookupSlot(LanguageMode::SLOPPY, assembler);
}

// StaLookupSlotStrict <name_index>
//
// Store the object in accumulator to the object with the name in constant
// pool entry |name_index| in strict mode.
void InterpreterGenerator::DoStaLookupSlotStrict(
    InterpreterAssembler* assembler) {
  DoStaLookupSlot(LanguageMode::STRICT, assembler);
}

void InterpreterGenerator::BuildLoadIC(int recv_operand_index,
                                       int slot_operand_index,
                                       int name_operand_index,
                                       InterpreterAssembler* assembler) {
  __ Comment("BuildLoadIC");

  // Load vector and slot.
  Node* feedback_vector = __ LoadFeedbackVector();
  Node* feedback_slot = __ BytecodeOperandIdx(slot_operand_index);
  Node* smi_slot = __ SmiTag(feedback_slot);

  // Load receiver.
  Node* register_index = __ BytecodeOperandReg(recv_operand_index);
  Node* recv = __ LoadRegister(register_index);

  // Load the name.
  // TODO(jgruber): Not needed for monomorphic smi handler constant/field case.
  Node* constant_index = __ BytecodeOperandIdx(name_operand_index);
  Node* name = __ LoadConstantPoolEntry(constant_index);

  Node* context = __ GetContext();

  Label done(assembler);
  Variable var_result(assembler, MachineRepresentation::kTagged);
  ExitPoint exit_point(assembler, &done, &var_result);

  AccessorAssembler::LoadICParameters params(context, recv, name, smi_slot,
                                             feedback_vector);
  AccessorAssembler accessor_asm(assembler->state());
  accessor_asm.LoadIC_BytecodeHandler(&params, &exit_point);

  __ Bind(&done);
  {
    __ SetAccumulator(var_result.value());
    __ Dispatch();
  }
}

// LdaNamedProperty <object> <name_index> <slot>
//
// Calls the LoadIC at FeedBackVector slot <slot> for <object> and the name at
// constant pool entry <name_index>.
void InterpreterGenerator::DoLdaNamedProperty(InterpreterAssembler* assembler) {
  static const int kRecvOperandIndex = 0;
  static const int kNameOperandIndex = 1;
  static const int kSlotOperandIndex = 2;

  BuildLoadIC(kRecvOperandIndex, kSlotOperandIndex, kNameOperandIndex,
              assembler);
}

// KeyedLoadIC <object> <slot>
//
// Calls the KeyedLoadIC at FeedBackVector slot <slot> for <object> and the key
// in the accumulator.
void InterpreterGenerator::DoLdaKeyedProperty(InterpreterAssembler* assembler) {
  Callable ic = CodeFactory::KeyedLoadICInOptimizedCode(isolate_);
  Node* code_target = __ HeapConstant(ic.code());
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* object = __ LoadRegister(reg_index);
  Node* name = __ GetAccumulator();
  Node* raw_slot = __ BytecodeOperandIdx(1);
  Node* smi_slot = __ SmiTag(raw_slot);
  Node* feedback_vector = __ LoadFeedbackVector();
  Node* context = __ GetContext();
  Node* result = __ CallStub(ic.descriptor(), code_target, context, object,
                             name, smi_slot, feedback_vector);
  __ SetAccumulator(result);
  __ Dispatch();
}

void InterpreterGenerator::DoStoreIC(Callable ic,
                                     InterpreterAssembler* assembler) {
  Node* code_target = __ HeapConstant(ic.code());
  Node* object_reg_index = __ BytecodeOperandReg(0);
  Node* object = __ LoadRegister(object_reg_index);
  Node* constant_index = __ BytecodeOperandIdx(1);
  Node* name = __ LoadConstantPoolEntry(constant_index);
  Node* value = __ GetAccumulator();
  Node* raw_slot = __ BytecodeOperandIdx(2);
  Node* smi_slot = __ SmiTag(raw_slot);
  Node* feedback_vector = __ LoadFeedbackVector();
  Node* context = __ GetContext();
  __ CallStub(ic.descriptor(), code_target, context, object, name, value,
              smi_slot, feedback_vector);
  __ Dispatch();
}

// StaNamedPropertySloppy <object> <name_index> <slot>
//
// Calls the sloppy mode StoreIC at FeedBackVector slot <slot> for <object> and
// the name in constant pool entry <name_index> with the value in the
// accumulator.
void InterpreterGenerator::DoStaNamedPropertySloppy(
    InterpreterAssembler* assembler) {
  Callable ic = CodeFactory::StoreICInOptimizedCode(isolate_, SLOPPY);
  DoStoreIC(ic, assembler);
}

// StaNamedPropertyStrict <object> <name_index> <slot>
//
// Calls the strict mode StoreIC at FeedBackVector slot <slot> for <object> and
// the name in constant pool entry <name_index> with the value in the
// accumulator.
void InterpreterGenerator::DoStaNamedPropertyStrict(
    InterpreterAssembler* assembler) {
  Callable ic = CodeFactory::StoreICInOptimizedCode(isolate_, STRICT);
  DoStoreIC(ic, assembler);
}

// StaNamedOwnProperty <object> <name_index> <slot>
//
// Calls the StoreOwnIC at FeedBackVector slot <slot> for <object> and
// the name in constant pool entry <name_index> with the value in the
// accumulator.
void InterpreterGenerator::DoStaNamedOwnProperty(
    InterpreterAssembler* assembler) {
  Callable ic = CodeFactory::StoreOwnICInOptimizedCode(isolate_);
  DoStoreIC(ic, assembler);
}

void InterpreterGenerator::DoKeyedStoreIC(Callable ic,
                                          InterpreterAssembler* assembler) {
  Node* code_target = __ HeapConstant(ic.code());
  Node* object_reg_index = __ BytecodeOperandReg(0);
  Node* object = __ LoadRegister(object_reg_index);
  Node* name_reg_index = __ BytecodeOperandReg(1);
  Node* name = __ LoadRegister(name_reg_index);
  Node* value = __ GetAccumulator();
  Node* raw_slot = __ BytecodeOperandIdx(2);
  Node* smi_slot = __ SmiTag(raw_slot);
  Node* feedback_vector = __ LoadFeedbackVector();
  Node* context = __ GetContext();
  __ CallStub(ic.descriptor(), code_target, context, object, name, value,
              smi_slot, feedback_vector);
  __ Dispatch();
}

// StaKeyedPropertySloppy <object> <key> <slot>
//
// Calls the sloppy mode KeyStoreIC at FeedBackVector slot <slot> for <object>
// and the key <key> with the value in the accumulator.
void InterpreterGenerator::DoStaKeyedPropertySloppy(
    InterpreterAssembler* assembler) {
  Callable ic = CodeFactory::KeyedStoreICInOptimizedCode(isolate_, SLOPPY);
  DoKeyedStoreIC(ic, assembler);
}

// StaKeyedPropertyStrict <object> <key> <slot>
//
// Calls the strict mode KeyStoreIC at FeedBackVector slot <slot> for <object>
// and the key <key> with the value in the accumulator.
void InterpreterGenerator::DoStaKeyedPropertyStrict(
    InterpreterAssembler* assembler) {
  Callable ic = CodeFactory::KeyedStoreICInOptimizedCode(isolate_, STRICT);
  DoKeyedStoreIC(ic, assembler);
}

// StaDataPropertyInLiteral <object> <name> <flags>
//
// Define a property <name> with value from the accumulator in <object>.
// Property attributes and whether set_function_name are stored in
// DataPropertyInLiteralFlags <flags>.
//
// This definition is not observable and is used only for definitions
// in object or class literals.
void InterpreterGenerator::DoStaDataPropertyInLiteral(
    InterpreterAssembler* assembler) {
  Node* object = __ LoadRegister(__ BytecodeOperandReg(0));
  Node* name = __ LoadRegister(__ BytecodeOperandReg(1));
  Node* value = __ GetAccumulator();
  Node* flags = __ SmiFromWord32(__ BytecodeOperandFlag(2));
  Node* vector_index = __ SmiTag(__ BytecodeOperandIdx(3));

  Node* feedback_vector = __ LoadFeedbackVector();
  Node* context = __ GetContext();

  __ CallRuntime(Runtime::kDefineDataPropertyInLiteral, context, object, name,
                 value, flags, feedback_vector, vector_index);
  __ Dispatch();
}

void InterpreterGenerator::DoCollectTypeProfile(
    InterpreterAssembler* assembler) {
  Node* position = __ BytecodeOperandImmSmi(0);
  Node* value = __ GetAccumulator();

  Node* feedback_vector = __ LoadFeedbackVector();
  Node* context = __ GetContext();

  __ CallRuntime(Runtime::kCollectTypeProfile, context, position, value,
                 feedback_vector);
  __ Dispatch();
}

// LdaModuleVariable <cell_index> <depth>
//
// Load the contents of a module variable into the accumulator.  The variable is
// identified by <cell_index>.  <depth> is the depth of the current context
// relative to the module context.
void InterpreterGenerator::DoLdaModuleVariable(
    InterpreterAssembler* assembler) {
  Node* cell_index = __ BytecodeOperandImmIntPtr(0);
  Node* depth = __ BytecodeOperandUImm(1);

  Node* module_context = __ GetContextAtDepth(__ GetContext(), depth);
  Node* module =
      __ LoadContextElement(module_context, Context::EXTENSION_INDEX);

  Label if_export(assembler), if_import(assembler), end(assembler);
  __ Branch(__ IntPtrGreaterThan(cell_index, __ IntPtrConstant(0)), &if_export,
            &if_import);

  __ Bind(&if_export);
  {
    Node* regular_exports =
        __ LoadObjectField(module, Module::kRegularExportsOffset);
    // The actual array index is (cell_index - 1).
    Node* export_index = __ IntPtrSub(cell_index, __ IntPtrConstant(1));
    Node* cell = __ LoadFixedArrayElement(regular_exports, export_index);
    __ SetAccumulator(__ LoadObjectField(cell, Cell::kValueOffset));
    __ Goto(&end);
  }

  __ Bind(&if_import);
  {
    Node* regular_imports =
        __ LoadObjectField(module, Module::kRegularImportsOffset);
    // The actual array index is (-cell_index - 1).
    Node* import_index = __ IntPtrSub(__ IntPtrConstant(-1), cell_index);
    Node* cell = __ LoadFixedArrayElement(regular_imports, import_index);
    __ SetAccumulator(__ LoadObjectField(cell, Cell::kValueOffset));
    __ Goto(&end);
  }

  __ Bind(&end);
  __ Dispatch();
}

// StaModuleVariable <cell_index> <depth>
//
// Store accumulator to the module variable identified by <cell_index>.
// <depth> is the depth of the current context relative to the module context.
void InterpreterGenerator::DoStaModuleVariable(
    InterpreterAssembler* assembler) {
  Node* value = __ GetAccumulator();
  Node* cell_index = __ BytecodeOperandImmIntPtr(0);
  Node* depth = __ BytecodeOperandUImm(1);

  Node* module_context = __ GetContextAtDepth(__ GetContext(), depth);
  Node* module =
      __ LoadContextElement(module_context, Context::EXTENSION_INDEX);

  Label if_export(assembler), if_import(assembler), end(assembler);
  __ Branch(__ IntPtrGreaterThan(cell_index, __ IntPtrConstant(0)), &if_export,
            &if_import);

  __ Bind(&if_export);
  {
    Node* regular_exports =
        __ LoadObjectField(module, Module::kRegularExportsOffset);
    // The actual array index is (cell_index - 1).
    Node* export_index = __ IntPtrSub(cell_index, __ IntPtrConstant(1));
    Node* cell = __ LoadFixedArrayElement(regular_exports, export_index);
    __ StoreObjectField(cell, Cell::kValueOffset, value);
    __ Goto(&end);
  }

  __ Bind(&if_import);
  {
    // Not supported (probably never).
    __ Abort(kUnsupportedModuleOperation);
    __ Goto(&end);
  }

  __ Bind(&end);
  __ Dispatch();
}

// PushContext <context>
//
// Saves the current context in <context>, and pushes the accumulator as the
// new current context.
void InterpreterGenerator::DoPushContext(InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* new_context = __ GetAccumulator();
  Node* old_context = __ GetContext();
  __ StoreRegister(old_context, reg_index);
  __ SetContext(new_context);
  __ Dispatch();
}

// PopContext <context>
//
// Pops the current context and sets <context> as the new context.
void InterpreterGenerator::DoPopContext(InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* context = __ LoadRegister(reg_index);
  __ SetContext(context);
  __ Dispatch();
}

// TODO(mythria): Remove this function once all CompareOps record type feedback.
void InterpreterGenerator::DoCompareOp(Token::Value compare_op,
                                       InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* lhs = __ LoadRegister(reg_index);
  Node* rhs = __ GetAccumulator();
  Node* context = __ GetContext();
  Node* result;
  switch (compare_op) {
    case Token::IN:
      result = assembler->HasProperty(rhs, lhs, context);
      break;
    case Token::INSTANCEOF:
      result = assembler->InstanceOf(lhs, rhs, context);
      break;
    default:
      UNREACHABLE();
  }
  __ SetAccumulator(result);
  __ Dispatch();
}

void InterpreterGenerator::DoBinaryOpWithFeedback(
    InterpreterAssembler* assembler, BinaryOpGenerator generator) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* lhs = __ LoadRegister(reg_index);
  Node* rhs = __ GetAccumulator();
  Node* context = __ GetContext();
  Node* slot_index = __ BytecodeOperandIdx(1);
  Node* feedback_vector = __ LoadFeedbackVector();

  BinaryOpAssembler binop_asm(assembler->state());
  Node* result =
      (binop_asm.*generator)(context, lhs, rhs, slot_index, feedback_vector);
  __ SetAccumulator(result);
  __ Dispatch();
}

void InterpreterGenerator::DoCompareOpWithFeedback(
    Token::Value compare_op, InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* lhs = __ LoadRegister(reg_index);
  Node* rhs = __ GetAccumulator();
  Node* context = __ GetContext();
  Node* slot_index = __ BytecodeOperandIdx(1);
  Node* feedback_vector = __ LoadFeedbackVector();

  Variable var_type_feedback(assembler, MachineRepresentation::kTaggedSigned);
  Label lhs_is_not_smi(assembler), lhs_is_not_number(assembler),
      lhs_is_not_string(assembler), gather_rhs_type(assembler),
      update_feedback(assembler), do_compare(assembler);

  __ GotoIfNot(__ TaggedIsSmi(lhs), &lhs_is_not_smi);

  var_type_feedback.Bind(
      __ SmiConstant(CompareOperationFeedback::kSignedSmall));
  __ Goto(&gather_rhs_type);

  __ Bind(&lhs_is_not_smi);
  {
    Node* lhs_map = __ LoadMap(lhs);
    __ GotoIfNot(__ IsHeapNumberMap(lhs_map), &lhs_is_not_number);

    var_type_feedback.Bind(__ SmiConstant(CompareOperationFeedback::kNumber));
    __ Goto(&gather_rhs_type);

    __ Bind(&lhs_is_not_number);
    {
      Node* lhs_instance_type = __ LoadInstanceType(lhs);
      if (Token::IsOrderedRelationalCompareOp(compare_op)) {
        Label lhs_is_not_oddball(assembler);
        __ GotoIfNot(
            __ Word32Equal(lhs_instance_type, __ Int32Constant(ODDBALL_TYPE)),
            &lhs_is_not_oddball);

        var_type_feedback.Bind(
            __ SmiConstant(CompareOperationFeedback::kNumberOrOddball));
        __ Goto(&gather_rhs_type);

        __ Bind(&lhs_is_not_oddball);
      }

      Label lhs_is_not_string(assembler);
      __ GotoIfNot(__ IsStringInstanceType(lhs_instance_type),
                   &lhs_is_not_string);

      if (Token::IsOrderedRelationalCompareOp(compare_op)) {
        var_type_feedback.Bind(
            __ SmiConstant(CompareOperationFeedback::kString));
      } else {
        var_type_feedback.Bind(__ SelectSmiConstant(
            __ Word32Equal(
                __ Word32And(lhs_instance_type,
                             __ Int32Constant(kIsNotInternalizedMask)),
                __ Int32Constant(kInternalizedTag)),
            CompareOperationFeedback::kInternalizedString,
            CompareOperationFeedback::kString));
      }
      __ Goto(&gather_rhs_type);

      __ Bind(&lhs_is_not_string);
      if (Token::IsEqualityOp(compare_op)) {
        var_type_feedback.Bind(
            __ SelectSmiConstant(__ IsJSReceiverInstanceType(lhs_instance_type),
                                 CompareOperationFeedback::kReceiver,
                                 CompareOperationFeedback::kAny));
      } else {
        var_type_feedback.Bind(__ SmiConstant(CompareOperationFeedback::kAny));
      }
      __ Goto(&gather_rhs_type);
    }
  }

  __ Bind(&gather_rhs_type);
  {
    Label rhs_is_not_smi(assembler), rhs_is_not_number(assembler);

    __ GotoIfNot(__ TaggedIsSmi(rhs), &rhs_is_not_smi);

    var_type_feedback.Bind(
        __ SmiOr(var_type_feedback.value(),
                 __ SmiConstant(CompareOperationFeedback::kSignedSmall)));
    __ Goto(&update_feedback);

    __ Bind(&rhs_is_not_smi);
    {
      Node* rhs_map = __ LoadMap(rhs);
      __ GotoIfNot(__ IsHeapNumberMap(rhs_map), &rhs_is_not_number);

      var_type_feedback.Bind(
          __ SmiOr(var_type_feedback.value(),
                   __ SmiConstant(CompareOperationFeedback::kNumber)));
      __ Goto(&update_feedback);

      __ Bind(&rhs_is_not_number);
      {
        Node* rhs_instance_type = __ LoadInstanceType(rhs);
        if (Token::IsOrderedRelationalCompareOp(compare_op)) {
          Label rhs_is_not_oddball(assembler);
          __ GotoIfNot(
              __ Word32Equal(rhs_instance_type, __ Int32Constant(ODDBALL_TYPE)),
              &rhs_is_not_oddball);

          var_type_feedback.Bind(__ SmiOr(
              var_type_feedback.value(),
              __ SmiConstant(CompareOperationFeedback::kNumberOrOddball)));
          __ Goto(&update_feedback);

          __ Bind(&rhs_is_not_oddball);
        }

        Label rhs_is_not_string(assembler);
        __ GotoIfNot(__ IsStringInstanceType(rhs_instance_type),
                     &rhs_is_not_string);

        if (Token::IsOrderedRelationalCompareOp(compare_op)) {
          var_type_feedback.Bind(
              __ SmiOr(var_type_feedback.value(),
                       __ SmiConstant(CompareOperationFeedback::kString)));
        } else {
          var_type_feedback.Bind(__ SmiOr(
              var_type_feedback.value(),
              __ SelectSmiConstant(
                  __ Word32Equal(
                      __ Word32And(rhs_instance_type,
                                   __ Int32Constant(kIsNotInternalizedMask)),
                      __ Int32Constant(kInternalizedTag)),
                  CompareOperationFeedback::kInternalizedString,
                  CompareOperationFeedback::kString)));
        }
        __ Goto(&update_feedback);

        __ Bind(&rhs_is_not_string);
        if (Token::IsEqualityOp(compare_op)) {
          var_type_feedback.Bind(
              __ SmiOr(var_type_feedback.value(),
                       __ SelectSmiConstant(
                           __ IsJSReceiverInstanceType(rhs_instance_type),
                           CompareOperationFeedback::kReceiver,
                           CompareOperationFeedback::kAny)));
        } else {
          var_type_feedback.Bind(
              __ SmiConstant(CompareOperationFeedback::kAny));
        }
        __ Goto(&update_feedback);
      }
    }
  }

  __ Bind(&update_feedback);
  {
    __ UpdateFeedback(var_type_feedback.value(), feedback_vector, slot_index);
    __ Goto(&do_compare);
  }

  __ Bind(&do_compare);
  Node* result;
  switch (compare_op) {
    case Token::EQ:
      result = assembler->Equal(lhs, rhs, context);
      break;
    case Token::EQ_STRICT:
      result = assembler->StrictEqual(lhs, rhs);
      break;
    case Token::LT:
      result = assembler->RelationalComparison(CodeStubAssembler::kLessThan,
                                               lhs, rhs, context);
      break;
    case Token::GT:
      result = assembler->RelationalComparison(CodeStubAssembler::kGreaterThan,
                                               lhs, rhs, context);
      break;
    case Token::LTE:
      result = assembler->RelationalComparison(
          CodeStubAssembler::kLessThanOrEqual, lhs, rhs, context);
      break;
    case Token::GTE:
      result = assembler->RelationalComparison(
          CodeStubAssembler::kGreaterThanOrEqual, lhs, rhs, context);
      break;
    default:
      UNREACHABLE();
  }
  __ SetAccumulator(result);
  __ Dispatch();
}

// Add <src>
//
// Add register <src> to accumulator.
void InterpreterGenerator::DoAdd(InterpreterAssembler* assembler) {
  DoBinaryOpWithFeedback(assembler,
                         &BinaryOpAssembler::Generate_AddWithFeedback);
}

// Sub <src>
//
// Subtract register <src> from accumulator.
void InterpreterGenerator::DoSub(InterpreterAssembler* assembler) {
  DoBinaryOpWithFeedback(assembler,
                         &BinaryOpAssembler::Generate_SubtractWithFeedback);
}

// Mul <src>
//
// Multiply accumulator by register <src>.
void InterpreterGenerator::DoMul(InterpreterAssembler* assembler) {
  DoBinaryOpWithFeedback(assembler,
                         &BinaryOpAssembler::Generate_MultiplyWithFeedback);
}

// Div <src>
//
// Divide register <src> by accumulator.
void InterpreterGenerator::DoDiv(InterpreterAssembler* assembler) {
  DoBinaryOpWithFeedback(assembler,
                         &BinaryOpAssembler::Generate_DivideWithFeedback);
}

// Mod <src>
//
// Modulo register <src> by accumulator.
void InterpreterGenerator::DoMod(InterpreterAssembler* assembler) {
  DoBinaryOpWithFeedback(assembler,
                         &BinaryOpAssembler::Generate_ModulusWithFeedback);
}

void InterpreterGenerator::DoBitwiseBinaryOp(Token::Value bitwise_op,
                                             InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* lhs = __ LoadRegister(reg_index);
  Node* rhs = __ GetAccumulator();
  Node* context = __ GetContext();
  Node* slot_index = __ BytecodeOperandIdx(1);
  Node* feedback_vector = __ LoadFeedbackVector();

  Variable var_lhs_type_feedback(assembler,
                                 MachineRepresentation::kTaggedSigned),
      var_rhs_type_feedback(assembler, MachineRepresentation::kTaggedSigned);
  Node* lhs_value = __ TruncateTaggedToWord32WithFeedback(
      context, lhs, &var_lhs_type_feedback);
  Node* rhs_value = __ TruncateTaggedToWord32WithFeedback(
      context, rhs, &var_rhs_type_feedback);
  Node* result = nullptr;

  switch (bitwise_op) {
    case Token::BIT_OR: {
      Node* value = __ Word32Or(lhs_value, rhs_value);
      result = __ ChangeInt32ToTagged(value);
    } break;
    case Token::BIT_AND: {
      Node* value = __ Word32And(lhs_value, rhs_value);
      result = __ ChangeInt32ToTagged(value);
    } break;
    case Token::BIT_XOR: {
      Node* value = __ Word32Xor(lhs_value, rhs_value);
      result = __ ChangeInt32ToTagged(value);
    } break;
    case Token::SHL: {
      Node* value = __ Word32Shl(
          lhs_value, __ Word32And(rhs_value, __ Int32Constant(0x1f)));
      result = __ ChangeInt32ToTagged(value);
    } break;
    case Token::SHR: {
      Node* value = __ Word32Shr(
          lhs_value, __ Word32And(rhs_value, __ Int32Constant(0x1f)));
      result = __ ChangeUint32ToTagged(value);
    } break;
    case Token::SAR: {
      Node* value = __ Word32Sar(
          lhs_value, __ Word32And(rhs_value, __ Int32Constant(0x1f)));
      result = __ ChangeInt32ToTagged(value);
    } break;
    default:
      UNREACHABLE();
  }

  Node* result_type = __ SelectSmiConstant(
      __ TaggedIsSmi(result), BinaryOperationFeedback::kSignedSmall,
      BinaryOperationFeedback::kNumber);

  if (FLAG_debug_code) {
    Label ok(assembler);
    __ GotoIf(__ TaggedIsSmi(result), &ok);
    Node* result_map = __ LoadMap(result);
    __ AbortIfWordNotEqual(result_map, __ HeapNumberMapConstant(),
                           kExpectedHeapNumber);
    __ Goto(&ok);
    __ Bind(&ok);
  }

  Node* input_feedback =
      __ SmiOr(var_lhs_type_feedback.value(), var_rhs_type_feedback.value());
  __ UpdateFeedback(__ SmiOr(result_type, input_feedback), feedback_vector,
                    slot_index);
  __ SetAccumulator(result);
  __ Dispatch();
}

// BitwiseOr <src>
//
// BitwiseOr register <src> to accumulator.
void InterpreterGenerator::DoBitwiseOr(InterpreterAssembler* assembler) {
  DoBitwiseBinaryOp(Token::BIT_OR, assembler);
}

// BitwiseXor <src>
//
// BitwiseXor register <src> to accumulator.
void InterpreterGenerator::DoBitwiseXor(InterpreterAssembler* assembler) {
  DoBitwiseBinaryOp(Token::BIT_XOR, assembler);
}

// BitwiseAnd <src>
//
// BitwiseAnd register <src> to accumulator.
void InterpreterGenerator::DoBitwiseAnd(InterpreterAssembler* assembler) {
  DoBitwiseBinaryOp(Token::BIT_AND, assembler);
}

// ShiftLeft <src>
//
// Left shifts register <src> by the count specified in the accumulator.
// Register <src> is converted to an int32 and the accumulator to uint32
// before the operation. 5 lsb bits from the accumulator are used as count
// i.e. <src> << (accumulator & 0x1F).
void InterpreterGenerator::DoShiftLeft(InterpreterAssembler* assembler) {
  DoBitwiseBinaryOp(Token::SHL, assembler);
}

// ShiftRight <src>
//
// Right shifts register <src> by the count specified in the accumulator.
// Result is sign extended. Register <src> is converted to an int32 and the
// accumulator to uint32 before the operation. 5 lsb bits from the accumulator
// are used as count i.e. <src> >> (accumulator & 0x1F).
void InterpreterGenerator::DoShiftRight(InterpreterAssembler* assembler) {
  DoBitwiseBinaryOp(Token::SAR, assembler);
}

// ShiftRightLogical <src>
//
// Right Shifts register <src> by the count specified in the accumulator.
// Result is zero-filled. The accumulator and register <src> are converted to
// uint32 before the operation 5 lsb bits from the accumulator are used as
// count i.e. <src> << (accumulator & 0x1F).
void InterpreterGenerator::DoShiftRightLogical(
    InterpreterAssembler* assembler) {
  DoBitwiseBinaryOp(Token::SHR, assembler);
}

// AddSmi <imm> <reg>
//
// Adds an immediate value <imm> to register <reg>. For this
// operation <reg> is the lhs operand and <imm> is the <rhs> operand.
void InterpreterGenerator::DoAddSmi(InterpreterAssembler* assembler) {
  Variable var_result(assembler, MachineRepresentation::kTagged);
  Label fastpath(assembler), slowpath(assembler, Label::kDeferred),
      end(assembler);

  Node* reg_index = __ BytecodeOperandReg(1);
  Node* left = __ LoadRegister(reg_index);
  Node* right = __ BytecodeOperandImmSmi(0);
  Node* slot_index = __ BytecodeOperandIdx(2);
  Node* feedback_vector = __ LoadFeedbackVector();

  // {right} is known to be a Smi.
  // Check if the {left} is a Smi take the fast path.
  __ Branch(__ TaggedIsSmi(left), &fastpath, &slowpath);
  __ Bind(&fastpath);
  {
    // Try fast Smi addition first.
    Node* pair = __ IntPtrAddWithOverflow(__ BitcastTaggedToWord(left),
                                          __ BitcastTaggedToWord(right));
    Node* overflow = __ Projection(1, pair);

    // Check if the Smi additon overflowed.
    Label if_notoverflow(assembler);
    __ Branch(overflow, &slowpath, &if_notoverflow);
    __ Bind(&if_notoverflow);
    {
      __ UpdateFeedback(__ SmiConstant(BinaryOperationFeedback::kSignedSmall),
                        feedback_vector, slot_index);
      var_result.Bind(__ BitcastWordToTaggedSigned(__ Projection(0, pair)));
      __ Goto(&end);
    }
  }
  __ Bind(&slowpath);
  {
    Node* context = __ GetContext();
    // TODO(ishell): pass slot as word-size value.
    var_result.Bind(__ CallBuiltin(Builtins::kAddWithFeedback, context, left,
                                   right, __ TruncateWordToWord32(slot_index),
                                   feedback_vector));
    __ Goto(&end);
  }
  __ Bind(&end);
  {
    __ SetAccumulator(var_result.value());
    __ Dispatch();
  }
}

// SubSmi <imm> <reg>
//
// Subtracts an immediate value <imm> to register <reg>. For this
// operation <reg> is the lhs operand and <imm> is the rhs operand.
void InterpreterGenerator::DoSubSmi(InterpreterAssembler* assembler) {
  Variable var_result(assembler, MachineRepresentation::kTagged);
  Label fastpath(assembler), slowpath(assembler, Label::kDeferred),
      end(assembler);

  Node* reg_index = __ BytecodeOperandReg(1);
  Node* left = __ LoadRegister(reg_index);
  Node* right = __ BytecodeOperandImmSmi(0);
  Node* slot_index = __ BytecodeOperandIdx(2);
  Node* feedback_vector = __ LoadFeedbackVector();

  // {right} is known to be a Smi.
  // Check if the {left} is a Smi take the fast path.
  __ Branch(__ TaggedIsSmi(left), &fastpath, &slowpath);
  __ Bind(&fastpath);
  {
    // Try fast Smi subtraction first.
    Node* pair = __ IntPtrSubWithOverflow(__ BitcastTaggedToWord(left),
                                          __ BitcastTaggedToWord(right));
    Node* overflow = __ Projection(1, pair);

    // Check if the Smi subtraction overflowed.
    Label if_notoverflow(assembler);
    __ Branch(overflow, &slowpath, &if_notoverflow);
    __ Bind(&if_notoverflow);
    {
      __ UpdateFeedback(__ SmiConstant(BinaryOperationFeedback::kSignedSmall),
                        feedback_vector, slot_index);
      var_result.Bind(__ BitcastWordToTaggedSigned(__ Projection(0, pair)));
      __ Goto(&end);
    }
  }
  __ Bind(&slowpath);
  {
    Node* context = __ GetContext();
    // TODO(ishell): pass slot as word-size value.
    var_result.Bind(
        __ CallBuiltin(Builtins::kSubtractWithFeedback, context, left, right,
                       __ TruncateWordToWord32(slot_index), feedback_vector));
    __ Goto(&end);
  }
  __ Bind(&end);
  {
    __ SetAccumulator(var_result.value());
    __ Dispatch();
  }
}

// BitwiseOr <imm> <reg>
//
// BitwiseOr <reg> with <imm>. For this operation <reg> is the lhs
// operand and <imm> is the rhs operand.
void InterpreterGenerator::DoBitwiseOrSmi(InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(1);
  Node* left = __ LoadRegister(reg_index);
  Node* right = __ BytecodeOperandImmSmi(0);
  Node* context = __ GetContext();
  Node* slot_index = __ BytecodeOperandIdx(2);
  Node* feedback_vector = __ LoadFeedbackVector();
  Variable var_lhs_type_feedback(assembler,
                                 MachineRepresentation::kTaggedSigned);
  Node* lhs_value = __ TruncateTaggedToWord32WithFeedback(
      context, left, &var_lhs_type_feedback);
  Node* rhs_value = __ SmiToWord32(right);
  Node* value = __ Word32Or(lhs_value, rhs_value);
  Node* result = __ ChangeInt32ToTagged(value);
  Node* result_type = __ SelectSmiConstant(
      __ TaggedIsSmi(result), BinaryOperationFeedback::kSignedSmall,
      BinaryOperationFeedback::kNumber);
  __ UpdateFeedback(__ SmiOr(result_type, var_lhs_type_feedback.value()),
                    feedback_vector, slot_index);
  __ SetAccumulator(result);
  __ Dispatch();
}

// BitwiseAnd <imm> <reg>
//
// BitwiseAnd <reg> with <imm>. For this operation <reg> is the lhs
// operand and <imm> is the rhs operand.
void InterpreterGenerator::DoBitwiseAndSmi(InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(1);
  Node* left = __ LoadRegister(reg_index);
  Node* right = __ BytecodeOperandImmSmi(0);
  Node* context = __ GetContext();
  Node* slot_index = __ BytecodeOperandIdx(2);
  Node* feedback_vector = __ LoadFeedbackVector();
  Variable var_lhs_type_feedback(assembler,
                                 MachineRepresentation::kTaggedSigned);
  Node* lhs_value = __ TruncateTaggedToWord32WithFeedback(
      context, left, &var_lhs_type_feedback);
  Node* rhs_value = __ SmiToWord32(right);
  Node* value = __ Word32And(lhs_value, rhs_value);
  Node* result = __ ChangeInt32ToTagged(value);
  Node* result_type = __ SelectSmiConstant(
      __ TaggedIsSmi(result), BinaryOperationFeedback::kSignedSmall,
      BinaryOperationFeedback::kNumber);
  __ UpdateFeedback(__ SmiOr(result_type, var_lhs_type_feedback.value()),
                    feedback_vector, slot_index);
  __ SetAccumulator(result);
  __ Dispatch();
}

// ShiftLeftSmi <imm> <reg>
//
// Left shifts register <src> by the count specified in <imm>.
// Register <src> is converted to an int32 before the operation. The 5
// lsb bits from <imm> are used as count i.e. <src> << (<imm> & 0x1F).
void InterpreterGenerator::DoShiftLeftSmi(InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(1);
  Node* left = __ LoadRegister(reg_index);
  Node* right = __ BytecodeOperandImmSmi(0);
  Node* context = __ GetContext();
  Node* slot_index = __ BytecodeOperandIdx(2);
  Node* feedback_vector = __ LoadFeedbackVector();
  Variable var_lhs_type_feedback(assembler,
                                 MachineRepresentation::kTaggedSigned);
  Node* lhs_value = __ TruncateTaggedToWord32WithFeedback(
      context, left, &var_lhs_type_feedback);
  Node* rhs_value = __ SmiToWord32(right);
  Node* shift_count = __ Word32And(rhs_value, __ Int32Constant(0x1f));
  Node* value = __ Word32Shl(lhs_value, shift_count);
  Node* result = __ ChangeInt32ToTagged(value);
  Node* result_type = __ SelectSmiConstant(
      __ TaggedIsSmi(result), BinaryOperationFeedback::kSignedSmall,
      BinaryOperationFeedback::kNumber);
  __ UpdateFeedback(__ SmiOr(result_type, var_lhs_type_feedback.value()),
                    feedback_vector, slot_index);
  __ SetAccumulator(result);
  __ Dispatch();
}

// ShiftRightSmi <imm> <reg>
//
// Right shifts register <src> by the count specified in <imm>.
// Register <src> is converted to an int32 before the operation. The 5
// lsb bits from <imm> are used as count i.e. <src> << (<imm> & 0x1F).
void InterpreterGenerator::DoShiftRightSmi(InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(1);
  Node* left = __ LoadRegister(reg_index);
  Node* right = __ BytecodeOperandImmSmi(0);
  Node* context = __ GetContext();
  Node* slot_index = __ BytecodeOperandIdx(2);
  Node* feedback_vector = __ LoadFeedbackVector();
  Variable var_lhs_type_feedback(assembler,
                                 MachineRepresentation::kTaggedSigned);
  Node* lhs_value = __ TruncateTaggedToWord32WithFeedback(
      context, left, &var_lhs_type_feedback);
  Node* rhs_value = __ SmiToWord32(right);
  Node* shift_count = __ Word32And(rhs_value, __ Int32Constant(0x1f));
  Node* value = __ Word32Sar(lhs_value, shift_count);
  Node* result = __ ChangeInt32ToTagged(value);
  Node* result_type = __ SelectSmiConstant(
      __ TaggedIsSmi(result), BinaryOperationFeedback::kSignedSmall,
      BinaryOperationFeedback::kNumber);
  __ UpdateFeedback(__ SmiOr(result_type, var_lhs_type_feedback.value()),
                    feedback_vector, slot_index);
  __ SetAccumulator(result);
  __ Dispatch();
}

Node* InterpreterGenerator::BuildUnaryOp(Callable callable,
                                         InterpreterAssembler* assembler) {
  Node* target = __ HeapConstant(callable.code());
  Node* accumulator = __ GetAccumulator();
  Node* context = __ GetContext();
  return __ CallStub(callable.descriptor(), target, context, accumulator);
}

// ToName
//
// Convert the object referenced by the accumulator to a name.
void InterpreterGenerator::DoToName(InterpreterAssembler* assembler) {
  Node* object = __ GetAccumulator();
  Node* context = __ GetContext();
  Node* result = __ ToName(context, object);
  __ StoreRegister(result, __ BytecodeOperandReg(0));
  __ Dispatch();
}

// ToNumber
//
// Convert the object referenced by the accumulator to a number.
void InterpreterGenerator::DoToNumber(InterpreterAssembler* assembler) {
  Node* object = __ GetAccumulator();
  Node* context = __ GetContext();
  Node* result = __ ToNumber(context, object);
  __ StoreRegister(result, __ BytecodeOperandReg(0));
  __ Dispatch();
}

// ToObject
//
// Convert the object referenced by the accumulator to a JSReceiver.
void InterpreterGenerator::DoToObject(InterpreterAssembler* assembler) {
  Node* result = BuildUnaryOp(CodeFactory::ToObject(isolate_), assembler);
  __ StoreRegister(result, __ BytecodeOperandReg(0));
  __ Dispatch();
}

// Inc
//
// Increments value in the accumulator by one.
void InterpreterGenerator::DoInc(InterpreterAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Node* value = __ GetAccumulator();
  Node* context = __ GetContext();
  Node* slot_index = __ BytecodeOperandIdx(0);
  Node* feedback_vector = __ LoadFeedbackVector();

  // Shared entry for floating point increment.
  Label do_finc(assembler), end(assembler);
  Variable var_finc_value(assembler, MachineRepresentation::kFloat64);

  // We might need to try again due to ToNumber conversion.
  Variable value_var(assembler, MachineRepresentation::kTagged);
  Variable result_var(assembler, MachineRepresentation::kTagged);
  Variable var_type_feedback(assembler, MachineRepresentation::kTaggedSigned);
  Variable* loop_vars[] = {&value_var, &var_type_feedback};
  Label start(assembler, 2, loop_vars);
  value_var.Bind(value);
  var_type_feedback.Bind(
      assembler->SmiConstant(BinaryOperationFeedback::kNone));
  assembler->Goto(&start);
  assembler->Bind(&start);
  {
    value = value_var.value();

    Label if_issmi(assembler), if_isnotsmi(assembler);
    assembler->Branch(assembler->TaggedIsSmi(value), &if_issmi, &if_isnotsmi);

    assembler->Bind(&if_issmi);
    {
      // Try fast Smi addition first.
      Node* one = assembler->SmiConstant(Smi::FromInt(1));
      Node* pair = assembler->IntPtrAddWithOverflow(
          assembler->BitcastTaggedToWord(value),
          assembler->BitcastTaggedToWord(one));
      Node* overflow = assembler->Projection(1, pair);

      // Check if the Smi addition overflowed.
      Label if_overflow(assembler), if_notoverflow(assembler);
      assembler->Branch(overflow, &if_overflow, &if_notoverflow);

      assembler->Bind(&if_notoverflow);
      var_type_feedback.Bind(assembler->SmiOr(
          var_type_feedback.value(),
          assembler->SmiConstant(BinaryOperationFeedback::kSignedSmall)));
      result_var.Bind(
          assembler->BitcastWordToTaggedSigned(assembler->Projection(0, pair)));
      assembler->Goto(&end);

      assembler->Bind(&if_overflow);
      {
        var_finc_value.Bind(assembler->SmiToFloat64(value));
        assembler->Goto(&do_finc);
      }
    }

    assembler->Bind(&if_isnotsmi);
    {
      // Check if the value is a HeapNumber.
      Label if_valueisnumber(assembler),
          if_valuenotnumber(assembler, Label::kDeferred);
      Node* value_map = assembler->LoadMap(value);
      assembler->Branch(assembler->IsHeapNumberMap(value_map),
                        &if_valueisnumber, &if_valuenotnumber);

      assembler->Bind(&if_valueisnumber);
      {
        // Load the HeapNumber value.
        var_finc_value.Bind(assembler->LoadHeapNumberValue(value));
        assembler->Goto(&do_finc);
      }

      assembler->Bind(&if_valuenotnumber);
      {
        // We do not require an Or with earlier feedback here because once we
        // convert the value to a number, we cannot reach this path. We can
        // only reach this path on the first pass when the feedback is kNone.
        CSA_ASSERT(assembler,
                   assembler->SmiEqual(
                       var_type_feedback.value(),
                       assembler->SmiConstant(BinaryOperationFeedback::kNone)));

        Label if_valueisoddball(assembler), if_valuenotoddball(assembler);
        Node* instance_type = assembler->LoadMapInstanceType(value_map);
        Node* is_oddball = assembler->Word32Equal(
            instance_type, assembler->Int32Constant(ODDBALL_TYPE));
        assembler->Branch(is_oddball, &if_valueisoddball, &if_valuenotoddball);

        assembler->Bind(&if_valueisoddball);
        {
          // Convert Oddball to Number and check again.
          value_var.Bind(
              assembler->LoadObjectField(value, Oddball::kToNumberOffset));
          var_type_feedback.Bind(assembler->SmiConstant(
              BinaryOperationFeedback::kNumberOrOddball));
          assembler->Goto(&start);
        }

        assembler->Bind(&if_valuenotoddball);
        {
          // Convert to a Number first and try again.
          Callable callable =
              CodeFactory::NonNumberToNumber(assembler->isolate());
          var_type_feedback.Bind(
              assembler->SmiConstant(BinaryOperationFeedback::kAny));
          value_var.Bind(assembler->CallStub(callable, context, value));
          assembler->Goto(&start);
        }
      }
    }
  }

  assembler->Bind(&do_finc);
  {
    Node* finc_value = var_finc_value.value();
    Node* one = assembler->Float64Constant(1.0);
    Node* finc_result = assembler->Float64Add(finc_value, one);
    var_type_feedback.Bind(assembler->SmiOr(
        var_type_feedback.value(),
        assembler->SmiConstant(BinaryOperationFeedback::kNumber)));
    result_var.Bind(assembler->AllocateHeapNumberWithValue(finc_result));
    assembler->Goto(&end);
  }

  assembler->Bind(&end);
  assembler->UpdateFeedback(var_type_feedback.value(), feedback_vector,
                            slot_index);

  __ SetAccumulator(result_var.value());
  __ Dispatch();
}

// Dec
//
// Decrements value in the accumulator by one.
void InterpreterGenerator::DoDec(InterpreterAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Node* value = __ GetAccumulator();
  Node* context = __ GetContext();
  Node* slot_index = __ BytecodeOperandIdx(0);
  Node* feedback_vector = __ LoadFeedbackVector();

  // Shared entry for floating point decrement.
  Label do_fdec(assembler), end(assembler);
  Variable var_fdec_value(assembler, MachineRepresentation::kFloat64);

  // We might need to try again due to ToNumber conversion.
  Variable value_var(assembler, MachineRepresentation::kTagged);
  Variable result_var(assembler, MachineRepresentation::kTagged);
  Variable var_type_feedback(assembler, MachineRepresentation::kTaggedSigned);
  Variable* loop_vars[] = {&value_var, &var_type_feedback};
  Label start(assembler, 2, loop_vars);
  var_type_feedback.Bind(
      assembler->SmiConstant(BinaryOperationFeedback::kNone));
  value_var.Bind(value);
  assembler->Goto(&start);
  assembler->Bind(&start);
  {
    value = value_var.value();

    Label if_issmi(assembler), if_isnotsmi(assembler);
    assembler->Branch(assembler->TaggedIsSmi(value), &if_issmi, &if_isnotsmi);

    assembler->Bind(&if_issmi);
    {
      // Try fast Smi subtraction first.
      Node* one = assembler->SmiConstant(Smi::FromInt(1));
      Node* pair = assembler->IntPtrSubWithOverflow(
          assembler->BitcastTaggedToWord(value),
          assembler->BitcastTaggedToWord(one));
      Node* overflow = assembler->Projection(1, pair);

      // Check if the Smi subtraction overflowed.
      Label if_overflow(assembler), if_notoverflow(assembler);
      assembler->Branch(overflow, &if_overflow, &if_notoverflow);

      assembler->Bind(&if_notoverflow);
      var_type_feedback.Bind(assembler->SmiOr(
          var_type_feedback.value(),
          assembler->SmiConstant(BinaryOperationFeedback::kSignedSmall)));
      result_var.Bind(
          assembler->BitcastWordToTaggedSigned(assembler->Projection(0, pair)));
      assembler->Goto(&end);

      assembler->Bind(&if_overflow);
      {
        var_fdec_value.Bind(assembler->SmiToFloat64(value));
        assembler->Goto(&do_fdec);
      }
    }

    assembler->Bind(&if_isnotsmi);
    {
      // Check if the value is a HeapNumber.
      Label if_valueisnumber(assembler),
          if_valuenotnumber(assembler, Label::kDeferred);
      Node* value_map = assembler->LoadMap(value);
      assembler->Branch(assembler->IsHeapNumberMap(value_map),
                        &if_valueisnumber, &if_valuenotnumber);

      assembler->Bind(&if_valueisnumber);
      {
        // Load the HeapNumber value.
        var_fdec_value.Bind(assembler->LoadHeapNumberValue(value));
        assembler->Goto(&do_fdec);
      }

      assembler->Bind(&if_valuenotnumber);
      {
        // We do not require an Or with earlier feedback here because once we
        // convert the value to a number, we cannot reach this path. We can
        // only reach this path on the first pass when the feedback is kNone.
        CSA_ASSERT(assembler,
                   assembler->SmiEqual(
                       var_type_feedback.value(),
                       assembler->SmiConstant(BinaryOperationFeedback::kNone)));

        Label if_valueisoddball(assembler), if_valuenotoddball(assembler);
        Node* instance_type = assembler->LoadMapInstanceType(value_map);
        Node* is_oddball = assembler->Word32Equal(
            instance_type, assembler->Int32Constant(ODDBALL_TYPE));
        assembler->Branch(is_oddball, &if_valueisoddball, &if_valuenotoddball);

        assembler->Bind(&if_valueisoddball);
        {
          // Convert Oddball to Number and check again.
          value_var.Bind(
              assembler->LoadObjectField(value, Oddball::kToNumberOffset));
          var_type_feedback.Bind(assembler->SmiConstant(
              BinaryOperationFeedback::kNumberOrOddball));
          assembler->Goto(&start);
        }

        assembler->Bind(&if_valuenotoddball);
        {
          // Convert to a Number first and try again.
          Callable callable =
              CodeFactory::NonNumberToNumber(assembler->isolate());
          var_type_feedback.Bind(
              assembler->SmiConstant(BinaryOperationFeedback::kAny));
          value_var.Bind(assembler->CallStub(callable, context, value));
          assembler->Goto(&start);
        }
      }
    }
  }

  assembler->Bind(&do_fdec);
  {
    Node* fdec_value = var_fdec_value.value();
    Node* one = assembler->Float64Constant(1.0);
    Node* fdec_result = assembler->Float64Sub(fdec_value, one);
    var_type_feedback.Bind(assembler->SmiOr(
        var_type_feedback.value(),
        assembler->SmiConstant(BinaryOperationFeedback::kNumber)));
    result_var.Bind(assembler->AllocateHeapNumberWithValue(fdec_result));
    assembler->Goto(&end);
  }

  assembler->Bind(&end);
  assembler->UpdateFeedback(var_type_feedback.value(), feedback_vector,
                            slot_index);

  __ SetAccumulator(result_var.value());
  __ Dispatch();
}

// LogicalNot
//
// Perform logical-not on the accumulator, first casting the
// accumulator to a boolean value if required.
// ToBooleanLogicalNot
void InterpreterGenerator::DoToBooleanLogicalNot(
    InterpreterAssembler* assembler) {
  Node* value = __ GetAccumulator();
  Variable result(assembler, MachineRepresentation::kTagged);
  Label if_true(assembler), if_false(assembler), end(assembler);
  Node* true_value = __ BooleanConstant(true);
  Node* false_value = __ BooleanConstant(false);
  __ BranchIfToBooleanIsTrue(value, &if_true, &if_false);
  __ Bind(&if_true);
  {
    result.Bind(false_value);
    __ Goto(&end);
  }
  __ Bind(&if_false);
  {
    result.Bind(true_value);
    __ Goto(&end);
  }
  __ Bind(&end);
  __ SetAccumulator(result.value());
  __ Dispatch();
}

// LogicalNot
//
// Perform logical-not on the accumulator, which must already be a boolean
// value.
void InterpreterGenerator::DoLogicalNot(InterpreterAssembler* assembler) {
  Node* value = __ GetAccumulator();
  Variable result(assembler, MachineRepresentation::kTagged);
  Label if_true(assembler), if_false(assembler), end(assembler);
  Node* true_value = __ BooleanConstant(true);
  Node* false_value = __ BooleanConstant(false);
  __ Branch(__ WordEqual(value, true_value), &if_true, &if_false);
  __ Bind(&if_true);
  {
    result.Bind(false_value);
    __ Goto(&end);
  }
  __ Bind(&if_false);
  {
    if (FLAG_debug_code) {
      __ AbortIfWordNotEqual(value, false_value,
                             BailoutReason::kExpectedBooleanValue);
    }
    result.Bind(true_value);
    __ Goto(&end);
  }
  __ Bind(&end);
  __ SetAccumulator(result.value());
  __ Dispatch();
}

// TypeOf
//
// Load the accumulator with the string representating type of the
// object in the accumulator.
void InterpreterGenerator::DoTypeOf(InterpreterAssembler* assembler) {
  Node* value = __ GetAccumulator();
  Node* result = assembler->Typeof(value);
  __ SetAccumulator(result);
  __ Dispatch();
}

void InterpreterGenerator::DoDelete(Runtime::FunctionId function_id,
                                    InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* object = __ LoadRegister(reg_index);
  Node* key = __ GetAccumulator();
  Node* context = __ GetContext();
  Node* result = __ CallRuntime(function_id, context, object, key);
  __ SetAccumulator(result);
  __ Dispatch();
}

// DeletePropertyStrict
//
// Delete the property specified in the accumulator from the object
// referenced by the register operand following strict mode semantics.
void InterpreterGenerator::DoDeletePropertyStrict(
    InterpreterAssembler* assembler) {
  DoDelete(Runtime::kDeleteProperty_Strict, assembler);
}

// DeletePropertySloppy
//
// Delete the property specified in the accumulator from the object
// referenced by the register operand following sloppy mode semantics.
void InterpreterGenerator::DoDeletePropertySloppy(
    InterpreterAssembler* assembler) {
  DoDelete(Runtime::kDeleteProperty_Sloppy, assembler);
}

// GetSuperConstructor
//
// Get the super constructor from the object referenced by the accumulator.
// The result is stored in register |reg|.
void InterpreterGenerator::DoGetSuperConstructor(
    InterpreterAssembler* assembler) {
  Node* active_function = __ GetAccumulator();
  Node* context = __ GetContext();
  Node* result = __ GetSuperConstructor(active_function, context);
  Node* reg = __ BytecodeOperandReg(0);
  __ StoreRegister(result, reg);
  __ Dispatch();
}

void InterpreterGenerator::DoJSCall(InterpreterAssembler* assembler,
                                    TailCallMode tail_call_mode) {
  Node* function_reg = __ BytecodeOperandReg(0);
  Node* function = __ LoadRegister(function_reg);
  Node* receiver_reg = __ BytecodeOperandReg(1);
  Node* receiver_arg = __ RegisterLocation(receiver_reg);
  Node* receiver_args_count = __ BytecodeOperandCount(2);
  Node* receiver_count = __ Int32Constant(1);
  Node* args_count = __ Int32Sub(receiver_args_count, receiver_count);
  Node* slot_id = __ BytecodeOperandIdx(3);
  Node* feedback_vector = __ LoadFeedbackVector();
  Node* context = __ GetContext();
  Node* result =
      __ CallJSWithFeedback(function, context, receiver_arg, args_count,
                            slot_id, feedback_vector, tail_call_mode);
  __ SetAccumulator(result);
  __ Dispatch();
}

void InterpreterGenerator::DoJSCallN(InterpreterAssembler* assembler,
                                     int arg_count) {
  const int kReceiverOperandIndex = 1;
  const int kReceiverOperandCount = 1;
  const int kSlotOperandIndex =
      kReceiverOperandIndex + kReceiverOperandCount + arg_count;
  const int kBoilerplatParameterCount = 7;
  const int kReceiverParameterIndex = 5;

  Node* function_reg = __ BytecodeOperandReg(0);
  Node* function = __ LoadRegister(function_reg);
  std::array<Node*, Bytecodes::kMaxOperands + kBoilerplatParameterCount> temp;
  Callable call_ic = CodeFactory::CallIC(isolate_);
  temp[0] = __ HeapConstant(call_ic.code());
  temp[1] = function;
  temp[2] = __ Int32Constant(arg_count);
  temp[3] = __ BytecodeOperandIdxInt32(kSlotOperandIndex);
  temp[4] = __ LoadFeedbackVector();
  for (int i = 0; i < (arg_count + kReceiverOperandCount); ++i) {
    Node* reg = __ BytecodeOperandReg(i + kReceiverOperandIndex);
    temp[kReceiverParameterIndex + i] = __ LoadRegister(reg);
  }
  temp[kReceiverParameterIndex + arg_count + kReceiverOperandCount] =
      __ GetContext();
  Node* result = __ CallStubN(call_ic.descriptor(), 1,
                              arg_count + kBoilerplatParameterCount, &temp[0]);
  __ SetAccumulator(result);
  __ Dispatch();
}

// Call <callable> <receiver> <arg_count> <feedback_slot_id>
//
// Call a JSfunction or Callable in |callable| with the |receiver| and
// |arg_count| arguments in subsequent registers. Collect type feedback
// into |feedback_slot_id|
void InterpreterGenerator::DoCall(InterpreterAssembler* assembler) {
  DoJSCall(assembler, TailCallMode::kDisallow);
}

void InterpreterGenerator::DoCall0(InterpreterAssembler* assembler) {
  DoJSCallN(assembler, 0);
}

void InterpreterGenerator::DoCall1(InterpreterAssembler* assembler) {
  DoJSCallN(assembler, 1);
}

void InterpreterGenerator::DoCall2(InterpreterAssembler* assembler) {
  DoJSCallN(assembler, 2);
}

void InterpreterGenerator::DoCallProperty(InterpreterAssembler* assembler) {
  // Same as Call
  UNREACHABLE();
}

void InterpreterGenerator::DoCallProperty0(InterpreterAssembler* assembler) {
  // Same as Call0
  UNREACHABLE();
}

void InterpreterGenerator::DoCallProperty1(InterpreterAssembler* assembler) {
  // Same as Call1
  UNREACHABLE();
}

void InterpreterGenerator::DoCallProperty2(InterpreterAssembler* assembler) {
  // Same as Call2
  UNREACHABLE();
}

// TailCall <callable> <receiver> <arg_count> <feedback_slot_id>
//
// Tail call a JSfunction or Callable in |callable| with the |receiver| and
// |arg_count| arguments in subsequent registers. Collect type feedback
// into |feedback_slot_id|
void InterpreterGenerator::DoTailCall(InterpreterAssembler* assembler) {
  DoJSCall(assembler, TailCallMode::kAllow);
}

// CallRuntime <function_id> <first_arg> <arg_count>
//
// Call the runtime function |function_id| with the first argument in
// register |first_arg| and |arg_count| arguments in subsequent
// registers.
void InterpreterGenerator::DoCallRuntime(InterpreterAssembler* assembler) {
  Node* function_id = __ BytecodeOperandRuntimeId(0);
  Node* first_arg_reg = __ BytecodeOperandReg(1);
  Node* first_arg = __ RegisterLocation(first_arg_reg);
  Node* args_count = __ BytecodeOperandCount(2);
  Node* context = __ GetContext();
  Node* result = __ CallRuntimeN(function_id, context, first_arg, args_count);
  __ SetAccumulator(result);
  __ Dispatch();
}

// InvokeIntrinsic <function_id> <first_arg> <arg_count>
//
// Implements the semantic equivalent of calling the runtime function
// |function_id| with the first argument in |first_arg| and |arg_count|
// arguments in subsequent registers.
void InterpreterGenerator::DoInvokeIntrinsic(InterpreterAssembler* assembler) {
  Node* function_id = __ BytecodeOperandIntrinsicId(0);
  Node* first_arg_reg = __ BytecodeOperandReg(1);
  Node* arg_count = __ BytecodeOperandCount(2);
  Node* context = __ GetContext();
  Node* result = GenerateInvokeIntrinsic(assembler, function_id, context,
                                         first_arg_reg, arg_count);
  __ SetAccumulator(result);
  __ Dispatch();
}

// CallRuntimeForPair <function_id> <first_arg> <arg_count> <first_return>
//
// Call the runtime function |function_id| which returns a pair, with the
// first argument in register |first_arg| and |arg_count| arguments in
// subsequent registers. Returns the result in <first_return> and
// <first_return + 1>
void InterpreterGenerator::DoCallRuntimeForPair(
    InterpreterAssembler* assembler) {
  // Call the runtime function.
  Node* function_id = __ BytecodeOperandRuntimeId(0);
  Node* first_arg_reg = __ BytecodeOperandReg(1);
  Node* first_arg = __ RegisterLocation(first_arg_reg);
  Node* args_count = __ BytecodeOperandCount(2);
  Node* context = __ GetContext();
  Node* result_pair =
      __ CallRuntimeN(function_id, context, first_arg, args_count, 2);
  // Store the results in <first_return> and <first_return + 1>
  Node* first_return_reg = __ BytecodeOperandReg(3);
  Node* second_return_reg = __ NextRegister(first_return_reg);
  Node* result0 = __ Projection(0, result_pair);
  Node* result1 = __ Projection(1, result_pair);
  __ StoreRegister(result0, first_return_reg);
  __ StoreRegister(result1, second_return_reg);
  __ Dispatch();
}

// CallJSRuntime <context_index> <receiver> <arg_count>
//
// Call the JS runtime function that has the |context_index| with the receiver
// in register |receiver| and |arg_count| arguments in subsequent registers.
void InterpreterGenerator::DoCallJSRuntime(InterpreterAssembler* assembler) {
  Node* context_index = __ BytecodeOperandIdx(0);
  Node* receiver_reg = __ BytecodeOperandReg(1);
  Node* first_arg = __ RegisterLocation(receiver_reg);
  Node* receiver_args_count = __ BytecodeOperandCount(2);
  Node* receiver_count = __ Int32Constant(1);
  Node* args_count = __ Int32Sub(receiver_args_count, receiver_count);

  // Get the function to call from the native context.
  Node* context = __ GetContext();
  Node* native_context = __ LoadNativeContext(context);
  Node* function = __ LoadContextElement(native_context, context_index);

  // Call the function.
  Node* result = __ CallJS(function, context, first_arg, args_count,
                           TailCallMode::kDisallow);
  __ SetAccumulator(result);
  __ Dispatch();
}

// CallWithSpread <callable> <first_arg> <arg_count>
//
// Call a JSfunction or Callable in |callable| with the receiver in
// |first_arg| and |arg_count - 1| arguments in subsequent registers. The
// final argument is always a spread.
//
void InterpreterGenerator::DoCallWithSpread(InterpreterAssembler* assembler) {
  Node* callable_reg = __ BytecodeOperandReg(0);
  Node* callable = __ LoadRegister(callable_reg);
  Node* receiver_reg = __ BytecodeOperandReg(1);
  Node* receiver_arg = __ RegisterLocation(receiver_reg);
  Node* receiver_args_count = __ BytecodeOperandCount(2);
  Node* receiver_count = __ Int32Constant(1);
  Node* args_count = __ Int32Sub(receiver_args_count, receiver_count);
  Node* context = __ GetContext();

  // Call into Runtime function CallWithSpread which does everything.
  Node* result =
      __ CallJSWithSpread(callable, context, receiver_arg, args_count);
  __ SetAccumulator(result);
  __ Dispatch();
}

// ConstructWithSpread <first_arg> <arg_count>
//
// Call the constructor in |constructor| with the first argument in register
// |first_arg| and |arg_count| arguments in subsequent registers. The final
// argument is always a spread. The new.target is in the accumulator.
//
void InterpreterGenerator::DoConstructWithSpread(
    InterpreterAssembler* assembler) {
  Node* new_target = __ GetAccumulator();
  Node* constructor_reg = __ BytecodeOperandReg(0);
  Node* constructor = __ LoadRegister(constructor_reg);
  Node* first_arg_reg = __ BytecodeOperandReg(1);
  Node* first_arg = __ RegisterLocation(first_arg_reg);
  Node* args_count = __ BytecodeOperandCount(2);
  Node* context = __ GetContext();
  Node* result = __ ConstructWithSpread(constructor, context, new_target,
                                        first_arg, args_count);
  __ SetAccumulator(result);
  __ Dispatch();
}

// Construct <constructor> <first_arg> <arg_count>
//
// Call operator construct with |constructor| and the first argument in
// register |first_arg| and |arg_count| arguments in subsequent
// registers. The new.target is in the accumulator.
//
void InterpreterGenerator::DoConstruct(InterpreterAssembler* assembler) {
  Node* new_target = __ GetAccumulator();
  Node* constructor_reg = __ BytecodeOperandReg(0);
  Node* constructor = __ LoadRegister(constructor_reg);
  Node* first_arg_reg = __ BytecodeOperandReg(1);
  Node* first_arg = __ RegisterLocation(first_arg_reg);
  Node* args_count = __ BytecodeOperandCount(2);
  Node* slot_id = __ BytecodeOperandIdx(3);
  Node* feedback_vector = __ LoadFeedbackVector();
  Node* context = __ GetContext();
  Node* result = __ Construct(constructor, context, new_target, first_arg,
                              args_count, slot_id, feedback_vector);
  __ SetAccumulator(result);
  __ Dispatch();
}

// TestEqual <src>
//
// Test if the value in the <src> register equals the accumulator.
void InterpreterGenerator::DoTestEqual(InterpreterAssembler* assembler) {
  DoCompareOpWithFeedback(Token::Value::EQ, assembler);
}

// TestEqualStrict <src>
//
// Test if the value in the <src> register is strictly equal to the accumulator.
void InterpreterGenerator::DoTestEqualStrict(InterpreterAssembler* assembler) {
  DoCompareOpWithFeedback(Token::Value::EQ_STRICT, assembler);
}

// TestLessThan <src>
//
// Test if the value in the <src> register is less than the accumulator.
void InterpreterGenerator::DoTestLessThan(InterpreterAssembler* assembler) {
  DoCompareOpWithFeedback(Token::Value::LT, assembler);
}

// TestGreaterThan <src>
//
// Test if the value in the <src> register is greater than the accumulator.
void InterpreterGenerator::DoTestGreaterThan(InterpreterAssembler* assembler) {
  DoCompareOpWithFeedback(Token::Value::GT, assembler);
}

// TestLessThanOrEqual <src>
//
// Test if the value in the <src> register is less than or equal to the
// accumulator.
void InterpreterGenerator::DoTestLessThanOrEqual(
    InterpreterAssembler* assembler) {
  DoCompareOpWithFeedback(Token::Value::LTE, assembler);
}

// TestGreaterThanOrEqual <src>
//
// Test if the value in the <src> register is greater than or equal to the
// accumulator.
void InterpreterGenerator::DoTestGreaterThanOrEqual(
    InterpreterAssembler* assembler) {
  DoCompareOpWithFeedback(Token::Value::GTE, assembler);
}

// TestEqualStrictNoFeedback <src>
//
// Test if the value in the <src> register is strictly equal to the accumulator.
// Type feedback is not collected.
void InterpreterGenerator::DoTestEqualStrictNoFeedback(
    InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* lhs = __ LoadRegister(reg_index);
  Node* rhs = __ GetAccumulator();
  // TODO(5310): This is called only when lhs and rhs are Smis (for ex:
  // try-finally or generators) or strings (only when visiting
  // ClassLiteralProperties). We should be able to optimize this and not perform
  // the full strict equality.
  Node* result = assembler->StrictEqual(lhs, rhs);
  __ SetAccumulator(result);
  __ Dispatch();
}

// TestIn <src>
//
// Test if the object referenced by the register operand is a property of the
// object referenced by the accumulator.
void InterpreterGenerator::DoTestIn(InterpreterAssembler* assembler) {
  DoCompareOp(Token::IN, assembler);
}

// TestInstanceOf <src>
//
// Test if the object referenced by the <src> register is an an instance of type
// referenced by the accumulator.
void InterpreterGenerator::DoTestInstanceOf(InterpreterAssembler* assembler) {
  DoCompareOp(Token::INSTANCEOF, assembler);
}

// TestUndetectable <src>
//
// Test if the value in the <src> register equals to null/undefined. This is
// done by checking undetectable bit on the map of the object.
void InterpreterGenerator::DoTestUndetectable(InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* object = __ LoadRegister(reg_index);

  Label not_equal(assembler), end(assembler);
  // If the object is an Smi then return false.
  __ GotoIf(__ TaggedIsSmi(object), &not_equal);

  // If it is a HeapObject, load the map and check for undetectable bit.
  Node* map = __ LoadMap(object);
  Node* map_bitfield = __ LoadMapBitField(map);
  Node* map_undetectable =
      __ Word32And(map_bitfield, __ Int32Constant(1 << Map::kIsUndetectable));
  __ GotoIf(__ Word32Equal(map_undetectable, __ Int32Constant(0)), &not_equal);

  __ SetAccumulator(__ BooleanConstant(true));
  __ Goto(&end);

  __ Bind(&not_equal);
  {
    __ SetAccumulator(__ BooleanConstant(false));
    __ Goto(&end);
  }

  __ Bind(&end);
  __ Dispatch();
}

// TestNull <src>
//
// Test if the value in the <src> register is strictly equal to null.
void InterpreterGenerator::DoTestNull(InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* object = __ LoadRegister(reg_index);
  Node* null_value = __ HeapConstant(isolate_->factory()->null_value());

  Label equal(assembler), end(assembler);
  __ GotoIf(__ WordEqual(object, null_value), &equal);
  __ SetAccumulator(__ BooleanConstant(false));
  __ Goto(&end);

  __ Bind(&equal);
  {
    __ SetAccumulator(__ BooleanConstant(true));
    __ Goto(&end);
  }

  __ Bind(&end);
  __ Dispatch();
}

// TestUndefined <src>
//
// Test if the value in the <src> register is strictly equal to undefined.
void InterpreterGenerator::DoTestUndefined(InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* object = __ LoadRegister(reg_index);
  Node* undefined_value =
      __ HeapConstant(isolate_->factory()->undefined_value());

  Label equal(assembler), end(assembler);
  __ GotoIf(__ WordEqual(object, undefined_value), &equal);
  __ SetAccumulator(__ BooleanConstant(false));
  __ Goto(&end);

  __ Bind(&equal);
  {
    __ SetAccumulator(__ BooleanConstant(true));
    __ Goto(&end);
  }

  __ Bind(&end);
  __ Dispatch();
}

// TestTypeOf <literal_flag>
//
// Tests if the object in the <accumulator> is typeof the literal represented
// by |literal_flag|.
void InterpreterGenerator::DoTestTypeOf(InterpreterAssembler* assembler) {
  Node* object = __ GetAccumulator();
  Node* literal_flag = __ BytecodeOperandFlag(0);

#define MAKE_LABEL(name, lower_case) Label if_##lower_case(assembler);
  TYPEOF_LITERAL_LIST(MAKE_LABEL)
#undef MAKE_LABEL

#define LABEL_POINTER(name, lower_case) &if_##lower_case,
  Label* labels[] = {TYPEOF_LITERAL_LIST(LABEL_POINTER)};
#undef LABEL_POINTER

#define CASE(name, lower_case) \
  static_cast<int32_t>(TestTypeOfFlags::LiteralFlag::k##name),
  int32_t cases[] = {TYPEOF_LITERAL_LIST(CASE)};
#undef CASE

  Label if_true(assembler), if_false(assembler), end(assembler),
      abort(assembler, Label::kDeferred);

  __ Switch(literal_flag, &abort, cases, labels, arraysize(cases));

  __ Bind(&abort);
  {
    __ Comment("Abort");
    __ Abort(BailoutReason::kUnexpectedTestTypeofLiteralFlag);
    __ Goto(&if_false);
  }
  __ Bind(&if_number);
  {
    __ Comment("IfNumber");
    __ GotoIfNumber(object, &if_true);
    __ Goto(&if_false);
  }
  __ Bind(&if_string);
  {
    __ Comment("IfString");
    __ GotoIf(__ TaggedIsSmi(object), &if_false);
    __ Branch(__ IsString(object), &if_true, &if_false);
  }
  __ Bind(&if_symbol);
  {
    __ Comment("IfSymbol");
    __ GotoIf(__ TaggedIsSmi(object), &if_false);
    __ Branch(__ IsSymbol(object), &if_true, &if_false);
  }
  __ Bind(&if_boolean);
  {
    __ Comment("IfBoolean");
    __ GotoIf(__ WordEqual(object, __ BooleanConstant(true)), &if_true);
    __ Branch(__ WordEqual(object, __ BooleanConstant(false)), &if_true,
              &if_false);
  }
  __ Bind(&if_undefined);
  {
    __ Comment("IfUndefined");
    __ GotoIf(__ TaggedIsSmi(object), &if_false);
    // Check it is not null and the map has the undetectable bit set.
    __ GotoIf(__ WordEqual(object, __ NullConstant()), &if_false);
    Node* map_bitfield = __ LoadMapBitField(__ LoadMap(object));
    Node* undetectable_bit =
        __ Word32And(map_bitfield, __ Int32Constant(1 << Map::kIsUndetectable));
    __ Branch(__ Word32Equal(undetectable_bit, __ Int32Constant(0)), &if_false,
              &if_true);
  }
  __ Bind(&if_function);
  {
    __ Comment("IfFunction");
    __ GotoIf(__ TaggedIsSmi(object), &if_false);
    // Check if callable bit is set and not undetectable.
    Node* map_bitfield = __ LoadMapBitField(__ LoadMap(object));
    Node* callable_undetectable = __ Word32And(
        map_bitfield,
        __ Int32Constant(1 << Map::kIsUndetectable | 1 << Map::kIsCallable));
    __ Branch(__ Word32Equal(callable_undetectable,
                             __ Int32Constant(1 << Map::kIsCallable)),
              &if_true, &if_false);
  }
  __ Bind(&if_object);
  {
    __ Comment("IfObject");
    __ GotoIf(__ TaggedIsSmi(object), &if_false);

    // If the object is null then return true.
    __ GotoIf(__ WordEqual(object, __ NullConstant()), &if_true);

    // Check if the object is a receiver type and is not undefined or callable.
    Node* map = __ LoadMap(object);
    __ GotoIfNot(__ IsJSReceiverMap(map), &if_false);
    Node* map_bitfield = __ LoadMapBitField(map);
    Node* callable_undetectable = __ Word32And(
        map_bitfield,
        __ Int32Constant(1 << Map::kIsUndetectable | 1 << Map::kIsCallable));
    __ Branch(__ Word32Equal(callable_undetectable, __ Int32Constant(0)),
              &if_true, &if_false);
  }
  __ Bind(&if_other);
  {
    // Typeof doesn't return any other string value.
    __ Goto(&if_false);
  }

  __ Bind(&if_false);
  {
    __ SetAccumulator(__ BooleanConstant(false));
    __ Goto(&end);
  }
  __ Bind(&if_true);
  {
    __ SetAccumulator(__ BooleanConstant(true));
    __ Goto(&end);
  }
  __ Bind(&end);
  __ Dispatch();
}

// Jump <imm>
//
// Jump by number of bytes represented by the immediate operand |imm|.
void InterpreterGenerator::DoJump(InterpreterAssembler* assembler) {
  Node* relative_jump = __ BytecodeOperandUImmWord(0);
  __ Jump(relative_jump);
}

// JumpConstant <idx>
//
// Jump by number of bytes in the Smi in the |idx| entry in the constant pool.
void InterpreterGenerator::DoJumpConstant(InterpreterAssembler* assembler) {
  Node* index = __ BytecodeOperandIdx(0);
  Node* relative_jump = __ LoadAndUntagConstantPoolEntry(index);
  __ Jump(relative_jump);
}

// JumpIfTrue <imm>
//
// Jump by number of bytes represented by an immediate operand if the
// accumulator contains true. This only works for boolean inputs, and
// will misbehave if passed arbitrary input values.
void InterpreterGenerator::DoJumpIfTrue(InterpreterAssembler* assembler) {
  Node* accumulator = __ GetAccumulator();
  Node* relative_jump = __ BytecodeOperandUImmWord(0);
  Node* true_value = __ BooleanConstant(true);
  CSA_ASSERT(assembler, assembler->TaggedIsNotSmi(accumulator));
  CSA_ASSERT(assembler, assembler->IsBoolean(accumulator));
  __ JumpIfWordEqual(accumulator, true_value, relative_jump);
}

// JumpIfTrueConstant <idx>
//
// Jump by number of bytes in the Smi in the |idx| entry in the constant pool
// if the accumulator contains true. This only works for boolean inputs, and
// will misbehave if passed arbitrary input values.
void InterpreterGenerator::DoJumpIfTrueConstant(
    InterpreterAssembler* assembler) {
  Node* accumulator = __ GetAccumulator();
  Node* index = __ BytecodeOperandIdx(0);
  Node* relative_jump = __ LoadAndUntagConstantPoolEntry(index);
  Node* true_value = __ BooleanConstant(true);
  CSA_ASSERT(assembler, assembler->TaggedIsNotSmi(accumulator));
  CSA_ASSERT(assembler, assembler->IsBoolean(accumulator));
  __ JumpIfWordEqual(accumulator, true_value, relative_jump);
}

// JumpIfFalse <imm>
//
// Jump by number of bytes represented by an immediate operand if the
// accumulator contains false. This only works for boolean inputs, and
// will misbehave if passed arbitrary input values.
void InterpreterGenerator::DoJumpIfFalse(InterpreterAssembler* assembler) {
  Node* accumulator = __ GetAccumulator();
  Node* relative_jump = __ BytecodeOperandUImmWord(0);
  Node* false_value = __ BooleanConstant(false);
  CSA_ASSERT(assembler, assembler->TaggedIsNotSmi(accumulator));
  CSA_ASSERT(assembler, assembler->IsBoolean(accumulator));
  __ JumpIfWordEqual(accumulator, false_value, relative_jump);
}

// JumpIfFalseConstant <idx>
//
// Jump by number of bytes in the Smi in the |idx| entry in the constant pool
// if the accumulator contains false. This only works for boolean inputs, and
// will misbehave if passed arbitrary input values.
void InterpreterGenerator::DoJumpIfFalseConstant(
    InterpreterAssembler* assembler) {
  Node* accumulator = __ GetAccumulator();
  Node* index = __ BytecodeOperandIdx(0);
  Node* relative_jump = __ LoadAndUntagConstantPoolEntry(index);
  Node* false_value = __ BooleanConstant(false);
  CSA_ASSERT(assembler, assembler->TaggedIsNotSmi(accumulator));
  CSA_ASSERT(assembler, assembler->IsBoolean(accumulator));
  __ JumpIfWordEqual(accumulator, false_value, relative_jump);
}

// JumpIfToBooleanTrue <imm>
//
// Jump by number of bytes represented by an immediate operand if the object
// referenced by the accumulator is true when the object is cast to boolean.
void InterpreterGenerator::DoJumpIfToBooleanTrue(
    InterpreterAssembler* assembler) {
  Node* value = __ GetAccumulator();
  Node* relative_jump = __ BytecodeOperandUImmWord(0);
  Label if_true(assembler), if_false(assembler);
  __ BranchIfToBooleanIsTrue(value, &if_true, &if_false);
  __ Bind(&if_true);
  __ Jump(relative_jump);
  __ Bind(&if_false);
  __ Dispatch();
}

// JumpIfToBooleanTrueConstant <idx>
//
// Jump by number of bytes in the Smi in the |idx| entry in the constant pool
// if the object referenced by the accumulator is true when the object is cast
// to boolean.
void InterpreterGenerator::DoJumpIfToBooleanTrueConstant(
    InterpreterAssembler* assembler) {
  Node* value = __ GetAccumulator();
  Node* index = __ BytecodeOperandIdx(0);
  Node* relative_jump = __ LoadAndUntagConstantPoolEntry(index);
  Label if_true(assembler), if_false(assembler);
  __ BranchIfToBooleanIsTrue(value, &if_true, &if_false);
  __ Bind(&if_true);
  __ Jump(relative_jump);
  __ Bind(&if_false);
  __ Dispatch();
}

// JumpIfToBooleanFalse <imm>
//
// Jump by number of bytes represented by an immediate operand if the object
// referenced by the accumulator is false when the object is cast to boolean.
void InterpreterGenerator::DoJumpIfToBooleanFalse(
    InterpreterAssembler* assembler) {
  Node* value = __ GetAccumulator();
  Node* relative_jump = __ BytecodeOperandUImmWord(0);
  Label if_true(assembler), if_false(assembler);
  __ BranchIfToBooleanIsTrue(value, &if_true, &if_false);
  __ Bind(&if_true);
  __ Dispatch();
  __ Bind(&if_false);
  __ Jump(relative_jump);
}

// JumpIfToBooleanFalseConstant <idx>
//
// Jump by number of bytes in the Smi in the |idx| entry in the constant pool
// if the object referenced by the accumulator is false when the object is cast
// to boolean.
void InterpreterGenerator::DoJumpIfToBooleanFalseConstant(
    InterpreterAssembler* assembler) {
  Node* value = __ GetAccumulator();
  Node* index = __ BytecodeOperandIdx(0);
  Node* relative_jump = __ LoadAndUntagConstantPoolEntry(index);
  Label if_true(assembler), if_false(assembler);
  __ BranchIfToBooleanIsTrue(value, &if_true, &if_false);
  __ Bind(&if_true);
  __ Dispatch();
  __ Bind(&if_false);
  __ Jump(relative_jump);
}

// JumpIfNull <imm>
//
// Jump by number of bytes represented by an immediate operand if the object
// referenced by the accumulator is the null constant.
void InterpreterGenerator::DoJumpIfNull(InterpreterAssembler* assembler) {
  Node* accumulator = __ GetAccumulator();
  Node* null_value = __ HeapConstant(isolate_->factory()->null_value());
  Node* relative_jump = __ BytecodeOperandUImmWord(0);
  __ JumpIfWordEqual(accumulator, null_value, relative_jump);
}

// JumpIfNullConstant <idx>
//
// Jump by number of bytes in the Smi in the |idx| entry in the constant pool
// if the object referenced by the accumulator is the null constant.
void InterpreterGenerator::DoJumpIfNullConstant(
    InterpreterAssembler* assembler) {
  Node* accumulator = __ GetAccumulator();
  Node* null_value = __ HeapConstant(isolate_->factory()->null_value());
  Node* index = __ BytecodeOperandIdx(0);
  Node* relative_jump = __ LoadAndUntagConstantPoolEntry(index);
  __ JumpIfWordEqual(accumulator, null_value, relative_jump);
}

// JumpIfUndefined <imm>
//
// Jump by number of bytes represented by an immediate operand if the object
// referenced by the accumulator is the undefined constant.
void InterpreterGenerator::DoJumpIfUndefined(InterpreterAssembler* assembler) {
  Node* accumulator = __ GetAccumulator();
  Node* undefined_value =
      __ HeapConstant(isolate_->factory()->undefined_value());
  Node* relative_jump = __ BytecodeOperandUImmWord(0);
  __ JumpIfWordEqual(accumulator, undefined_value, relative_jump);
}

// JumpIfUndefinedConstant <idx>
//
// Jump by number of bytes in the Smi in the |idx| entry in the constant pool
// if the object referenced by the accumulator is the undefined constant.
void InterpreterGenerator::DoJumpIfUndefinedConstant(
    InterpreterAssembler* assembler) {
  Node* accumulator = __ GetAccumulator();
  Node* undefined_value =
      __ HeapConstant(isolate_->factory()->undefined_value());
  Node* index = __ BytecodeOperandIdx(0);
  Node* relative_jump = __ LoadAndUntagConstantPoolEntry(index);
  __ JumpIfWordEqual(accumulator, undefined_value, relative_jump);
}

// JumpIfJSReceiver <imm>
//
// Jump by number of bytes represented by an immediate operand if the object
// referenced by the accumulator is a JSReceiver.
void InterpreterGenerator::DoJumpIfJSReceiver(InterpreterAssembler* assembler) {
  Node* accumulator = __ GetAccumulator();
  Node* relative_jump = __ BytecodeOperandUImmWord(0);

  Label if_object(assembler), if_notobject(assembler, Label::kDeferred),
      if_notsmi(assembler);
  __ Branch(__ TaggedIsSmi(accumulator), &if_notobject, &if_notsmi);

  __ Bind(&if_notsmi);
  __ Branch(__ IsJSReceiver(accumulator), &if_object, &if_notobject);
  __ Bind(&if_object);
  __ Jump(relative_jump);

  __ Bind(&if_notobject);
  __ Dispatch();
}

// JumpIfJSReceiverConstant <idx>
//
// Jump by number of bytes in the Smi in the |idx| entry in the constant pool if
// the object referenced by the accumulator is a JSReceiver.
void InterpreterGenerator::DoJumpIfJSReceiverConstant(
    InterpreterAssembler* assembler) {
  Node* accumulator = __ GetAccumulator();
  Node* index = __ BytecodeOperandIdx(0);
  Node* relative_jump = __ LoadAndUntagConstantPoolEntry(index);

  Label if_object(assembler), if_notobject(assembler), if_notsmi(assembler);
  __ Branch(__ TaggedIsSmi(accumulator), &if_notobject, &if_notsmi);

  __ Bind(&if_notsmi);
  __ Branch(__ IsJSReceiver(accumulator), &if_object, &if_notobject);

  __ Bind(&if_object);
  __ Jump(relative_jump);

  __ Bind(&if_notobject);
  __ Dispatch();
}

// JumpIfNotHole <imm>
//
// Jump by number of bytes represented by an immediate operand if the object
// referenced by the accumulator is the hole.
void InterpreterGenerator::DoJumpIfNotHole(InterpreterAssembler* assembler) {
  Node* accumulator = __ GetAccumulator();
  Node* the_hole_value = __ HeapConstant(isolate_->factory()->the_hole_value());
  Node* relative_jump = __ BytecodeOperandUImmWord(0);
  __ JumpIfWordNotEqual(accumulator, the_hole_value, relative_jump);
}

// JumpIfNotHoleConstant <idx>
//
// Jump by number of bytes in the Smi in the |idx| entry in the constant pool
// if the object referenced by the accumulator is the hole constant.
void InterpreterGenerator::DoJumpIfNotHoleConstant(
    InterpreterAssembler* assembler) {
  Node* accumulator = __ GetAccumulator();
  Node* the_hole_value = __ HeapConstant(isolate_->factory()->the_hole_value());
  Node* index = __ BytecodeOperandIdx(0);
  Node* relative_jump = __ LoadAndUntagConstantPoolEntry(index);
  __ JumpIfWordNotEqual(accumulator, the_hole_value, relative_jump);
}

// JumpLoop <imm> <loop_depth>
//
// Jump by number of bytes represented by the immediate operand |imm|. Also
// performs a loop nesting check and potentially triggers OSR in case the
// current OSR level matches (or exceeds) the specified |loop_depth|.
void InterpreterGenerator::DoJumpLoop(InterpreterAssembler* assembler) {
  Node* relative_jump = __ BytecodeOperandUImmWord(0);
  Node* loop_depth = __ BytecodeOperandImm(1);
  Node* osr_level = __ LoadOSRNestingLevel();

  // Check if OSR points at the given {loop_depth} are armed by comparing it to
  // the current {osr_level} loaded from the header of the BytecodeArray.
  Label ok(assembler), osr_armed(assembler, Label::kDeferred);
  Node* condition = __ Int32GreaterThanOrEqual(loop_depth, osr_level);
  __ Branch(condition, &ok, &osr_armed);

  __ Bind(&ok);
  __ JumpBackward(relative_jump);

  __ Bind(&osr_armed);
  {
    Callable callable = CodeFactory::InterpreterOnStackReplacement(isolate_);
    Node* target = __ HeapConstant(callable.code());
    Node* context = __ GetContext();
    __ CallStub(callable.descriptor(), target, context);
    __ JumpBackward(relative_jump);
  }
}

// CreateRegExpLiteral <pattern_idx> <literal_idx> <flags>
//
// Creates a regular expression literal for literal index <literal_idx> with
// <flags> and the pattern in <pattern_idx>.
void InterpreterGenerator::DoCreateRegExpLiteral(
    InterpreterAssembler* assembler) {
  Node* index = __ BytecodeOperandIdx(0);
  Node* pattern = __ LoadConstantPoolEntry(index);
  Node* literal_index = __ BytecodeOperandIdxSmi(1);
  Node* flags = __ SmiFromWord32(__ BytecodeOperandFlag(2));
  Node* closure = __ LoadRegister(Register::function_closure());
  Node* context = __ GetContext();
  ConstructorBuiltinsAssembler constructor_assembler(assembler->state());
  Node* result = constructor_assembler.EmitFastCloneRegExp(
      closure, literal_index, pattern, flags, context);
  __ SetAccumulator(result);
  __ Dispatch();
}

// CreateArrayLiteral <element_idx> <literal_idx> <flags>
//
// Creates an array literal for literal index <literal_idx> with
// CreateArrayLiteral flags <flags> and constant elements in <element_idx>.
void InterpreterGenerator::DoCreateArrayLiteral(
    InterpreterAssembler* assembler) {
  Node* literal_index = __ BytecodeOperandIdxSmi(1);
  Node* closure = __ LoadRegister(Register::function_closure());
  Node* context = __ GetContext();
  Node* bytecode_flags = __ BytecodeOperandFlag(2);

  Label fast_shallow_clone(assembler),
      call_runtime(assembler, Label::kDeferred);
  __ Branch(__ IsSetWord32<CreateArrayLiteralFlags::FastShallowCloneBit>(
                bytecode_flags),
            &fast_shallow_clone, &call_runtime);

  __ Bind(&fast_shallow_clone);
  {
    ConstructorBuiltinsAssembler constructor_assembler(assembler->state());
    Node* result = constructor_assembler.EmitFastCloneShallowArray(
        closure, literal_index, context, &call_runtime, TRACK_ALLOCATION_SITE);
    __ SetAccumulator(result);
    __ Dispatch();
  }

  __ Bind(&call_runtime);
  {
    Node* flags_raw =
        __ DecodeWordFromWord32<CreateArrayLiteralFlags::FlagsBits>(
            bytecode_flags);
    Node* flags = __ SmiTag(flags_raw);
    Node* index = __ BytecodeOperandIdx(0);
    Node* constant_elements = __ LoadConstantPoolEntry(index);
    Node* result =
        __ CallRuntime(Runtime::kCreateArrayLiteral, context, closure,
                       literal_index, constant_elements, flags);
    __ SetAccumulator(result);
    __ Dispatch();
  }
}

// CreateObjectLiteral <element_idx> <literal_idx> <flags>
//
// Creates an object literal for literal index <literal_idx> with
// CreateObjectLiteralFlags <flags> and constant elements in <element_idx>.
void InterpreterGenerator::DoCreateObjectLiteral(
    InterpreterAssembler* assembler) {
  Node* literal_index = __ BytecodeOperandIdxSmi(1);
  Node* bytecode_flags = __ BytecodeOperandFlag(2);
  Node* closure = __ LoadRegister(Register::function_closure());

  // Check if we can do a fast clone or have to call the runtime.
  Label if_fast_clone(assembler),
      if_not_fast_clone(assembler, Label::kDeferred);
  Node* fast_clone_properties_count = __ DecodeWordFromWord32<
      CreateObjectLiteralFlags::FastClonePropertiesCountBits>(bytecode_flags);
  __ Branch(__ WordNotEqual(fast_clone_properties_count, __ IntPtrConstant(0)),
            &if_fast_clone, &if_not_fast_clone);

  __ Bind(&if_fast_clone);
  {
    // If we can do a fast clone do the fast-path in FastCloneShallowObjectStub.
    ConstructorBuiltinsAssembler constructor_assembler(assembler->state());
    Node* result = constructor_assembler.EmitFastCloneShallowObject(
        &if_not_fast_clone, closure, literal_index,
        fast_clone_properties_count);
    __ StoreRegister(result, __ BytecodeOperandReg(3));
    __ Dispatch();
  }

  __ Bind(&if_not_fast_clone);
  {
    // If we can't do a fast clone, call into the runtime.
    Node* index = __ BytecodeOperandIdx(0);
    Node* constant_elements = __ LoadConstantPoolEntry(index);
    Node* context = __ GetContext();

    Node* flags_raw =
        __ DecodeWordFromWord32<CreateObjectLiteralFlags::FlagsBits>(
            bytecode_flags);
    Node* flags = __ SmiTag(flags_raw);

    Node* result =
        __ CallRuntime(Runtime::kCreateObjectLiteral, context, closure,
                       literal_index, constant_elements, flags);
    __ StoreRegister(result, __ BytecodeOperandReg(3));
    // TODO(klaasb) build a single dispatch once the call is inlined
    __ Dispatch();
  }
}

// CreateClosure <index> <slot> <tenured>
//
// Creates a new closure for SharedFunctionInfo at position |index| in the
// constant pool and with the PretenureFlag <tenured>.
void InterpreterGenerator::DoCreateClosure(InterpreterAssembler* assembler) {
  Node* index = __ BytecodeOperandIdx(0);
  Node* shared = __ LoadConstantPoolEntry(index);
  Node* flags = __ BytecodeOperandFlag(2);
  Node* context = __ GetContext();

  Label call_runtime(assembler, Label::kDeferred);
  __ GotoIfNot(__ IsSetWord32<CreateClosureFlags::FastNewClosureBit>(flags),
               &call_runtime);
  ConstructorBuiltinsAssembler constructor_assembler(assembler->state());
  Node* vector_index = __ BytecodeOperandIdx(1);
  vector_index = __ SmiTag(vector_index);
  Node* feedback_vector = __ LoadFeedbackVector();
  __ SetAccumulator(constructor_assembler.EmitFastNewClosure(
      shared, feedback_vector, vector_index, context));
  __ Dispatch();

  __ Bind(&call_runtime);
  {
    Node* tenured_raw =
        __ DecodeWordFromWord32<CreateClosureFlags::PretenuredBit>(flags);
    Node* tenured = __ SmiTag(tenured_raw);
    feedback_vector = __ LoadFeedbackVector();
    vector_index = __ BytecodeOperandIdx(1);
    vector_index = __ SmiTag(vector_index);
    Node* result =
        __ CallRuntime(Runtime::kInterpreterNewClosure, context, shared,
                       feedback_vector, vector_index, tenured);
    __ SetAccumulator(result);
    __ Dispatch();
  }
}

// CreateBlockContext <index>
//
// Creates a new block context with the scope info constant at |index| and the
// closure in the accumulator.
void InterpreterGenerator::DoCreateBlockContext(
    InterpreterAssembler* assembler) {
  Node* index = __ BytecodeOperandIdx(0);
  Node* scope_info = __ LoadConstantPoolEntry(index);
  Node* closure = __ GetAccumulator();
  Node* context = __ GetContext();
  __ SetAccumulator(
      __ CallRuntime(Runtime::kPushBlockContext, context, scope_info, closure));
  __ Dispatch();
}

// CreateCatchContext <exception> <name_idx> <scope_info_idx>
//
// Creates a new context for a catch block with the |exception| in a register,
// the variable name at |name_idx|, the ScopeInfo at |scope_info_idx|, and the
// closure in the accumulator.
void InterpreterGenerator::DoCreateCatchContext(
    InterpreterAssembler* assembler) {
  Node* exception_reg = __ BytecodeOperandReg(0);
  Node* exception = __ LoadRegister(exception_reg);
  Node* name_idx = __ BytecodeOperandIdx(1);
  Node* name = __ LoadConstantPoolEntry(name_idx);
  Node* scope_info_idx = __ BytecodeOperandIdx(2);
  Node* scope_info = __ LoadConstantPoolEntry(scope_info_idx);
  Node* closure = __ GetAccumulator();
  Node* context = __ GetContext();
  __ SetAccumulator(__ CallRuntime(Runtime::kPushCatchContext, context, name,
                                   exception, scope_info, closure));
  __ Dispatch();
}

// CreateFunctionContext <slots>
//
// Creates a new context with number of |slots| for the function closure.
void InterpreterGenerator::DoCreateFunctionContext(
    InterpreterAssembler* assembler) {
  Node* closure = __ LoadRegister(Register::function_closure());
  Node* slots = __ BytecodeOperandUImm(0);
  Node* context = __ GetContext();
  ConstructorBuiltinsAssembler constructor_assembler(assembler->state());
  __ SetAccumulator(constructor_assembler.EmitFastNewFunctionContext(
      closure, slots, context, FUNCTION_SCOPE));
  __ Dispatch();
}

// CreateEvalContext <slots>
//
// Creates a new context with number of |slots| for an eval closure.
void InterpreterGenerator::DoCreateEvalContext(
    InterpreterAssembler* assembler) {
  Node* closure = __ LoadRegister(Register::function_closure());
  Node* slots = __ BytecodeOperandUImm(0);
  Node* context = __ GetContext();
  ConstructorBuiltinsAssembler constructor_assembler(assembler->state());
  __ SetAccumulator(constructor_assembler.EmitFastNewFunctionContext(
      closure, slots, context, EVAL_SCOPE));
  __ Dispatch();
}

// CreateWithContext <register> <scope_info_idx>
//
// Creates a new context with the ScopeInfo at |scope_info_idx| for a
// with-statement with the object in |register| and the closure in the
// accumulator.
void InterpreterGenerator::DoCreateWithContext(
    InterpreterAssembler* assembler) {
  Node* reg_index = __ BytecodeOperandReg(0);
  Node* object = __ LoadRegister(reg_index);
  Node* scope_info_idx = __ BytecodeOperandIdx(1);
  Node* scope_info = __ LoadConstantPoolEntry(scope_info_idx);
  Node* closure = __ GetAccumulator();
  Node* context = __ GetContext();
  __ SetAccumulator(__ CallRuntime(Runtime::kPushWithContext, context, object,
                                   scope_info, closure));
  __ Dispatch();
}

// CreateMappedArguments
//
// Creates a new mapped arguments object.
void InterpreterGenerator::DoCreateMappedArguments(
    InterpreterAssembler* assembler) {
  Node* closure = __ LoadRegister(Register::function_closure());
  Node* context = __ GetContext();

  Label if_duplicate_parameters(assembler, Label::kDeferred);
  Label if_not_duplicate_parameters(assembler);

  // Check if function has duplicate parameters.
  // TODO(rmcilroy): Remove this check when FastNewSloppyArgumentsStub supports
  // duplicate parameters.
  Node* shared_info =
      __ LoadObjectField(closure, JSFunction::kSharedFunctionInfoOffset);
  Node* compiler_hints = __ LoadObjectField(
      shared_info, SharedFunctionInfo::kHasDuplicateParametersByteOffset,
      MachineType::Uint8());
  Node* duplicate_parameters_bit = __ Int32Constant(
      1 << SharedFunctionInfo::kHasDuplicateParametersBitWithinByte);
  Node* compare = __ Word32And(compiler_hints, duplicate_parameters_bit);
  __ Branch(compare, &if_duplicate_parameters, &if_not_duplicate_parameters);

  __ Bind(&if_not_duplicate_parameters);
  {
    ArgumentsBuiltinsAssembler constructor_assembler(assembler->state());
    Node* result =
        constructor_assembler.EmitFastNewSloppyArguments(context, closure);
    __ SetAccumulator(result);
    __ Dispatch();
  }

  __ Bind(&if_duplicate_parameters);
  {
    Node* result =
        __ CallRuntime(Runtime::kNewSloppyArguments_Generic, context, closure);
    __ SetAccumulator(result);
    __ Dispatch();
  }
}

// CreateUnmappedArguments
//
// Creates a new unmapped arguments object.
void InterpreterGenerator::DoCreateUnmappedArguments(
    InterpreterAssembler* assembler) {
  Node* context = __ GetContext();
  Node* closure = __ LoadRegister(Register::function_closure());
  ArgumentsBuiltinsAssembler builtins_assembler(assembler->state());
  Node* result =
      builtins_assembler.EmitFastNewStrictArguments(context, closure);
  __ SetAccumulator(result);
  __ Dispatch();
}

// CreateRestParameter
//
// Creates a new rest parameter array.
void InterpreterGenerator::DoCreateRestParameter(
    InterpreterAssembler* assembler) {
  Node* closure = __ LoadRegister(Register::function_closure());
  Node* context = __ GetContext();
  ArgumentsBuiltinsAssembler builtins_assembler(assembler->state());
  Node* result = builtins_assembler.EmitFastNewRestParameter(context, closure);
  __ SetAccumulator(result);
  __ Dispatch();
}

// StackCheck
//
// Performs a stack guard check.
void InterpreterGenerator::DoStackCheck(InterpreterAssembler* assembler) {
  Label ok(assembler), stack_check_interrupt(assembler, Label::kDeferred);

  Node* interrupt = __ StackCheckTriggeredInterrupt();
  __ Branch(interrupt, &stack_check_interrupt, &ok);

  __ Bind(&ok);
  __ Dispatch();

  __ Bind(&stack_check_interrupt);
  {
    Node* context = __ GetContext();
    __ CallRuntime(Runtime::kStackGuard, context);
    __ Dispatch();
  }
}

// SetPendingMessage
//
// Sets the pending message to the value in the accumulator, and returns the
// previous pending message in the accumulator.
void InterpreterGenerator::DoSetPendingMessage(
    InterpreterAssembler* assembler) {
  Node* pending_message = __ ExternalConstant(
      ExternalReference::address_of_pending_message_obj(isolate_));
  Node* previous_message =
      __ Load(MachineType::TaggedPointer(), pending_message);
  Node* new_message = __ GetAccumulator();
  __ StoreNoWriteBarrier(MachineRepresentation::kTaggedPointer, pending_message,
                         new_message);
  __ SetAccumulator(previous_message);
  __ Dispatch();
}

// Throw
//
// Throws the exception in the accumulator.
void InterpreterGenerator::DoThrow(InterpreterAssembler* assembler) {
  Node* exception = __ GetAccumulator();
  Node* context = __ GetContext();
  __ CallRuntime(Runtime::kThrow, context, exception);
  // We shouldn't ever return from a throw.
  __ Abort(kUnexpectedReturnFromThrow);
}

// ReThrow
//
// Re-throws the exception in the accumulator.
void InterpreterGenerator::DoReThrow(InterpreterAssembler* assembler) {
  Node* exception = __ GetAccumulator();
  Node* context = __ GetContext();
  __ CallRuntime(Runtime::kReThrow, context, exception);
  // We shouldn't ever return from a throw.
  __ Abort(kUnexpectedReturnFromThrow);
}

// Return
//
// Return the value in the accumulator.
void InterpreterGenerator::DoReturn(InterpreterAssembler* assembler) {
  __ UpdateInterruptBudgetOnReturn();
  Node* accumulator = __ GetAccumulator();
  __ Return(accumulator);
}

// Debugger
//
// Call runtime to handle debugger statement.
void InterpreterGenerator::DoDebugger(InterpreterAssembler* assembler) {
  Node* context = __ GetContext();
  __ CallStub(CodeFactory::HandleDebuggerStatement(isolate_), context);
  __ Dispatch();
}

// DebugBreak
//
// Call runtime to handle a debug break.
#define DEBUG_BREAK(Name, ...)                                                \
  void InterpreterGenerator::Do##Name(InterpreterAssembler* assembler) {      \
    Node* context = __ GetContext();                                          \
    Node* accumulator = __ GetAccumulator();                                  \
    Node* original_handler =                                                  \
        __ CallRuntime(Runtime::kDebugBreakOnBytecode, context, accumulator); \
    __ MaybeDropFrames(context);                                              \
    __ DispatchToBytecodeHandler(original_handler);                           \
  }
DEBUG_BREAK_BYTECODE_LIST(DEBUG_BREAK);
#undef DEBUG_BREAK

void InterpreterGenerator::BuildForInPrepareResult(
    Node* output_register, Node* cache_type, Node* cache_array,
    Node* cache_length, InterpreterAssembler* assembler) {
  __ StoreRegister(cache_type, output_register);
  output_register = __ NextRegister(output_register);
  __ StoreRegister(cache_array, output_register);
  output_register = __ NextRegister(output_register);
  __ StoreRegister(cache_length, output_register);
}

// ForInPrepare <receiver> <cache_info_triple>
//
// Returns state for for..in loop execution based on the object in the register
// |receiver|. The object must not be null or undefined and must have been
// converted to a receiver already.
// The result is output in registers |cache_info_triple| to
// |cache_info_triple + 2|, with the registers holding cache_type, cache_array,
// and cache_length respectively.
void InterpreterGenerator::DoForInPrepare(InterpreterAssembler* assembler) {
  Node* object_register = __ BytecodeOperandReg(0);
  Node* output_register = __ BytecodeOperandReg(1);
  Node* receiver = __ LoadRegister(object_register);
  Node* context = __ GetContext();

  Node* cache_type;
  Node* cache_array;
  Node* cache_length;
  Label call_runtime(assembler, Label::kDeferred),
      nothing_to_iterate(assembler, Label::kDeferred);

  ForInBuiltinsAssembler forin_assembler(assembler->state());
  std::tie(cache_type, cache_array, cache_length) =
      forin_assembler.EmitForInPrepare(receiver, context, &call_runtime,
                                       &nothing_to_iterate);

  BuildForInPrepareResult(output_register, cache_type, cache_array,
                          cache_length, assembler);
  __ Dispatch();

  __ Bind(&call_runtime);
  {
    Node* result_triple =
        __ CallRuntime(Runtime::kForInPrepare, context, receiver);
    Node* cache_type = __ Projection(0, result_triple);
    Node* cache_array = __ Projection(1, result_triple);
    Node* cache_length = __ Projection(2, result_triple);
    BuildForInPrepareResult(output_register, cache_type, cache_array,
                            cache_length, assembler);
    __ Dispatch();
  }
  __ Bind(&nothing_to_iterate);
  {
    // Receiver is null or undefined or descriptors are zero length.
    Node* zero = __ SmiConstant(0);
    BuildForInPrepareResult(output_register, zero, zero, zero, assembler);
    __ Dispatch();
  }
}

// ForInNext <receiver> <index> <cache_info_pair>
//
// Returns the next enumerable property in the the accumulator.
void InterpreterGenerator::DoForInNext(InterpreterAssembler* assembler) {
  Node* receiver_reg = __ BytecodeOperandReg(0);
  Node* receiver = __ LoadRegister(receiver_reg);
  Node* index_reg = __ BytecodeOperandReg(1);
  Node* index = __ LoadRegister(index_reg);
  Node* cache_type_reg = __ BytecodeOperandReg(2);
  Node* cache_type = __ LoadRegister(cache_type_reg);
  Node* cache_array_reg = __ NextRegister(cache_type_reg);
  Node* cache_array = __ LoadRegister(cache_array_reg);

  // Load the next key from the enumeration array.
  Node* key = __ LoadFixedArrayElement(cache_array, index, 0,
                                       CodeStubAssembler::SMI_PARAMETERS);

  // Check if we can use the for-in fast path potentially using the enum cache.
  Label if_fast(assembler), if_slow(assembler, Label::kDeferred);
  Node* receiver_map = __ LoadMap(receiver);
  __ Branch(__ WordEqual(receiver_map, cache_type), &if_fast, &if_slow);
  __ Bind(&if_fast);
  {
    // Enum cache in use for {receiver}, the {key} is definitely valid.
    __ SetAccumulator(key);
    __ Dispatch();
  }
  __ Bind(&if_slow);
  {
    // Record the fact that we hit the for-in slow path.
    Node* vector_index = __ BytecodeOperandIdx(3);
    Node* feedback_vector = __ LoadFeedbackVector();
    Node* megamorphic_sentinel =
        __ HeapConstant(FeedbackVector::MegamorphicSentinel(isolate_));
    __ StoreFixedArrayElement(feedback_vector, vector_index,
                              megamorphic_sentinel, SKIP_WRITE_BARRIER);

    // Need to filter the {key} for the {receiver}.
    Node* context = __ GetContext();
    Callable callable = CodeFactory::ForInFilter(assembler->isolate());
    Node* result = __ CallStub(callable, context, key, receiver);
    __ SetAccumulator(result);
    __ Dispatch();
  }
}

// ForInContinue <index> <cache_length>
//
// Returns false if the end of the enumerable properties has been reached.
void InterpreterGenerator::DoForInContinue(InterpreterAssembler* assembler) {
  Node* index_reg = __ BytecodeOperandReg(0);
  Node* index = __ LoadRegister(index_reg);
  Node* cache_length_reg = __ BytecodeOperandReg(1);
  Node* cache_length = __ LoadRegister(cache_length_reg);

  // Check if {index} is at {cache_length} already.
  Label if_true(assembler), if_false(assembler), end(assembler);
  __ Branch(__ WordEqual(index, cache_length), &if_true, &if_false);
  __ Bind(&if_true);
  {
    __ SetAccumulator(__ BooleanConstant(false));
    __ Goto(&end);
  }
  __ Bind(&if_false);
  {
    __ SetAccumulator(__ BooleanConstant(true));
    __ Goto(&end);
  }
  __ Bind(&end);
  __ Dispatch();
}

// ForInStep <index>
//
// Increments the loop counter in register |index| and stores the result
// in the accumulator.
void InterpreterGenerator::DoForInStep(InterpreterAssembler* assembler) {
  Node* index_reg = __ BytecodeOperandReg(0);
  Node* index = __ LoadRegister(index_reg);
  Node* one = __ SmiConstant(Smi::FromInt(1));
  Node* result = __ SmiAdd(index, one);
  __ SetAccumulator(result);
  __ Dispatch();
}

// Wide
//
// Prefix bytecode indicating next bytecode has wide (16-bit) operands.
void InterpreterGenerator::DoWide(InterpreterAssembler* assembler) {
  __ DispatchWide(OperandScale::kDouble);
}

// ExtraWide
//
// Prefix bytecode indicating next bytecode has extra-wide (32-bit) operands.
void InterpreterGenerator::DoExtraWide(InterpreterAssembler* assembler) {
  __ DispatchWide(OperandScale::kQuadruple);
}

// Illegal
//
// An invalid bytecode aborting execution if dispatched.
void InterpreterGenerator::DoIllegal(InterpreterAssembler* assembler) {
  __ Abort(kInvalidBytecode);
}

// Nop
//
// No operation.
void InterpreterGenerator::DoNop(InterpreterAssembler* assembler) {
  __ Dispatch();
}

// SuspendGenerator <generator>
//
// Exports the register file and stores it into the generator.  Also stores the
// current context, the state given in the accumulator, and the current bytecode
// offset (for debugging purposes) into the generator.
void InterpreterGenerator::DoSuspendGenerator(InterpreterAssembler* assembler) {
  Node* generator_reg = __ BytecodeOperandReg(0);
  Node* generator = __ LoadRegister(generator_reg);

  Label if_stepping(assembler, Label::kDeferred), ok(assembler);
  Node* step_action_address = __ ExternalConstant(
      ExternalReference::debug_last_step_action_address(isolate_));
  Node* step_action = __ Load(MachineType::Int8(), step_action_address);
  STATIC_ASSERT(StepIn > StepNext);
  STATIC_ASSERT(LastStepAction == StepIn);
  Node* step_next = __ Int32Constant(StepNext);
  __ Branch(__ Int32LessThanOrEqual(step_next, step_action), &if_stepping, &ok);
  __ Bind(&ok);

  Node* array =
      __ LoadObjectField(generator, JSGeneratorObject::kRegisterFileOffset);
  Node* context = __ GetContext();
  Node* state = __ GetAccumulator();

  __ ExportRegisterFile(array);
  __ StoreObjectField(generator, JSGeneratorObject::kContextOffset, context);
  __ StoreObjectField(generator, JSGeneratorObject::kContinuationOffset, state);

  Node* offset = __ SmiTag(__ BytecodeOffset());
  __ StoreObjectField(generator, JSGeneratorObject::kInputOrDebugPosOffset,
                      offset);

  __ Dispatch();

  __ Bind(&if_stepping);
  {
    Node* context = __ GetContext();
    __ CallRuntime(Runtime::kDebugRecordGenerator, context, generator);
    __ Goto(&ok);
  }
}

// ResumeGenerator <generator>
//
// Imports the register file stored in the generator. Also loads the
// generator's state and stores it in the accumulator, before overwriting it
// with kGeneratorExecuting.
void InterpreterGenerator::DoResumeGenerator(InterpreterAssembler* assembler) {
  Node* generator_reg = __ BytecodeOperandReg(0);
  Node* generator = __ LoadRegister(generator_reg);

  __ ImportRegisterFile(
      __ LoadObjectField(generator, JSGeneratorObject::kRegisterFileOffset));

  Node* old_state =
      __ LoadObjectField(generator, JSGeneratorObject::kContinuationOffset);
  Node* new_state = __ Int32Constant(JSGeneratorObject::kGeneratorExecuting);
  __ StoreObjectField(generator, JSGeneratorObject::kContinuationOffset,
                      __ SmiTag(new_state));
  __ SetAccumulator(old_state);

  __ Dispatch();
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
