// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/code-assembler.h"

#include <ostream>

#include "src/code-factory.h"
#include "src/compiler/graph.h"
#include "src/compiler/instruction-selector.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/pipeline.h"
#include "src/compiler/raw-machine-assembler.h"
#include "src/compiler/schedule.h"
#include "src/frames.h"
#include "src/interface-descriptors.h"
#include "src/interpreter/bytecodes.h"
#include "src/machine-type.h"
#include "src/macro-assembler.h"
#include "src/utils.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

CodeAssemblerState::CodeAssemblerState(
    Isolate* isolate, Zone* zone, const CallInterfaceDescriptor& descriptor,
    Code::Flags flags, const char* name, size_t result_size)
    : CodeAssemblerState(
          isolate, zone,
          Linkage::GetStubCallDescriptor(
              isolate, zone, descriptor, descriptor.GetStackParameterCount(),
              CallDescriptor::kNoFlags, Operator::kNoProperties,
              MachineType::AnyTagged(), result_size),
          flags, name) {}

CodeAssemblerState::CodeAssemblerState(Isolate* isolate, Zone* zone,
                                       int parameter_count, Code::Flags flags,
                                       const char* name)
    : CodeAssemblerState(isolate, zone,
                         Linkage::GetJSCallDescriptor(
                             zone, false, parameter_count,
                             Code::ExtractKindFromFlags(flags) == Code::BUILTIN
                                 ? CallDescriptor::kPushArgumentCount
                                 : CallDescriptor::kNoFlags),
                         flags, name) {}

CodeAssemblerState::CodeAssemblerState(Isolate* isolate, Zone* zone,
                                       CallDescriptor* call_descriptor,
                                       Code::Flags flags, const char* name)
    : raw_assembler_(new RawMachineAssembler(
          isolate, new (zone) Graph(zone), call_descriptor,
          MachineType::PointerRepresentation(),
          InstructionSelector::SupportedMachineOperatorFlags(),
          InstructionSelector::AlignmentRequirements())),
      flags_(flags),
      name_(name),
      code_generated_(false),
      variables_(zone) {}

CodeAssemblerState::~CodeAssemblerState() {}

CodeAssembler::~CodeAssembler() {}

void CodeAssembler::CallPrologue() {}

void CodeAssembler::CallEpilogue() {}

// static
Handle<Code> CodeAssembler::GenerateCode(CodeAssemblerState* state) {
  DCHECK(!state->code_generated_);

  RawMachineAssembler* rasm = state->raw_assembler_.get();
  Schedule* schedule = rasm->Export();
  Handle<Code> code = Pipeline::GenerateCodeForCodeStub(
      rasm->isolate(), rasm->call_descriptor(), rasm->graph(), schedule,
      state->flags_, state->name_);

  state->code_generated_ = true;
  return code;
}

bool CodeAssembler::Is64() const { return raw_assembler()->machine()->Is64(); }

bool CodeAssembler::IsFloat64RoundUpSupported() const {
  return raw_assembler()->machine()->Float64RoundUp().IsSupported();
}

bool CodeAssembler::IsFloat64RoundDownSupported() const {
  return raw_assembler()->machine()->Float64RoundDown().IsSupported();
}

bool CodeAssembler::IsFloat64RoundTiesEvenSupported() const {
  return raw_assembler()->machine()->Float64RoundTiesEven().IsSupported();
}

bool CodeAssembler::IsFloat64RoundTruncateSupported() const {
  return raw_assembler()->machine()->Float64RoundTruncate().IsSupported();
}

Node* CodeAssembler::Int32Constant(int32_t value) {
  return raw_assembler()->Int32Constant(value);
}

Node* CodeAssembler::Int64Constant(int64_t value) {
  return raw_assembler()->Int64Constant(value);
}

Node* CodeAssembler::IntPtrConstant(intptr_t value) {
  return raw_assembler()->IntPtrConstant(value);
}

Node* CodeAssembler::NumberConstant(double value) {
  return raw_assembler()->NumberConstant(value);
}

Node* CodeAssembler::SmiConstant(Smi* value) {
  return BitcastWordToTaggedSigned(IntPtrConstant(bit_cast<intptr_t>(value)));
}

Node* CodeAssembler::SmiConstant(int value) {
  return SmiConstant(Smi::FromInt(value));
}

Node* CodeAssembler::HeapConstant(Handle<HeapObject> object) {
  return raw_assembler()->HeapConstant(object);
}

Node* CodeAssembler::BooleanConstant(bool value) {
  return raw_assembler()->BooleanConstant(value);
}

Node* CodeAssembler::ExternalConstant(ExternalReference address) {
  return raw_assembler()->ExternalConstant(address);
}

Node* CodeAssembler::Float64Constant(double value) {
  return raw_assembler()->Float64Constant(value);
}

Node* CodeAssembler::NaNConstant() {
  return LoadRoot(Heap::kNanValueRootIndex);
}

bool CodeAssembler::ToInt32Constant(Node* node, int32_t& out_value) {
  Int64Matcher m(node);
  if (m.HasValue() &&
      m.IsInRange(std::numeric_limits<int32_t>::min(),
                  std::numeric_limits<int32_t>::max())) {
    out_value = static_cast<int32_t>(m.Value());
    return true;
  }

  return false;
}

bool CodeAssembler::ToInt64Constant(Node* node, int64_t& out_value) {
  Int64Matcher m(node);
  if (m.HasValue()) out_value = m.Value();
  return m.HasValue();
}

bool CodeAssembler::ToSmiConstant(Node* node, Smi*& out_value) {
  if (node->opcode() == IrOpcode::kBitcastWordToTaggedSigned) {
    node = node->InputAt(0);
  } else {
    return false;
  }
  IntPtrMatcher m(node);
  if (m.HasValue()) {
    out_value = Smi::cast(bit_cast<Object*>(m.Value()));
    return true;
  }
  return false;
}

bool CodeAssembler::ToIntPtrConstant(Node* node, intptr_t& out_value) {
  IntPtrMatcher m(node);
  if (m.HasValue()) out_value = m.Value();
  return m.HasValue();
}

Node* CodeAssembler::Parameter(int value) {
  return raw_assembler()->Parameter(value);
}

void CodeAssembler::Return(Node* value) {
  return raw_assembler()->Return(value);
}

void CodeAssembler::PopAndReturn(Node* pop, Node* value) {
  return raw_assembler()->PopAndReturn(pop, value);
}

void CodeAssembler::DebugBreak() { raw_assembler()->DebugBreak(); }

void CodeAssembler::Comment(const char* format, ...) {
  if (!FLAG_code_comments) return;
  char buffer[4 * KB];
  StringBuilder builder(buffer, arraysize(buffer));
  va_list arguments;
  va_start(arguments, format);
  builder.AddFormattedList(format, arguments);
  va_end(arguments);

  // Copy the string before recording it in the assembler to avoid
  // issues when the stack allocated buffer goes out of scope.
  const int prefix_len = 2;
  int length = builder.position() + 1;
  char* copy = reinterpret_cast<char*>(malloc(length + prefix_len));
  MemCopy(copy + prefix_len, builder.Finalize(), length);
  copy[0] = ';';
  copy[1] = ' ';
  raw_assembler()->Comment(copy);
}

void CodeAssembler::Bind(CodeAssembler::Label* label) { return label->Bind(); }

Node* CodeAssembler::LoadFramePointer() {
  return raw_assembler()->LoadFramePointer();
}

Node* CodeAssembler::LoadParentFramePointer() {
  return raw_assembler()->LoadParentFramePointer();
}

Node* CodeAssembler::LoadStackPointer() {
  return raw_assembler()->LoadStackPointer();
}

#define DEFINE_CODE_ASSEMBLER_BINARY_OP(name)   \
  Node* CodeAssembler::name(Node* a, Node* b) { \
    return raw_assembler()->name(a, b);         \
  }
CODE_ASSEMBLER_BINARY_OP_LIST(DEFINE_CODE_ASSEMBLER_BINARY_OP)
#undef DEFINE_CODE_ASSEMBLER_BINARY_OP

Node* CodeAssembler::WordShl(Node* value, int shift) {
  return (shift != 0) ? raw_assembler()->WordShl(value, IntPtrConstant(shift))
                      : value;
}

Node* CodeAssembler::WordShr(Node* value, int shift) {
  return (shift != 0) ? raw_assembler()->WordShr(value, IntPtrConstant(shift))
                      : value;
}

Node* CodeAssembler::Word32Shr(Node* value, int shift) {
  return (shift != 0) ? raw_assembler()->Word32Shr(value, Int32Constant(shift))
                      : value;
}

Node* CodeAssembler::ChangeUint32ToWord(Node* value) {
  if (raw_assembler()->machine()->Is64()) {
    value = raw_assembler()->ChangeUint32ToUint64(value);
  }
  return value;
}

Node* CodeAssembler::ChangeInt32ToIntPtr(Node* value) {
  if (raw_assembler()->machine()->Is64()) {
    value = raw_assembler()->ChangeInt32ToInt64(value);
  }
  return value;
}

Node* CodeAssembler::RoundIntPtrToFloat64(Node* value) {
  if (raw_assembler()->machine()->Is64()) {
    return raw_assembler()->RoundInt64ToFloat64(value);
  }
  return raw_assembler()->ChangeInt32ToFloat64(value);
}

#define DEFINE_CODE_ASSEMBLER_UNARY_OP(name) \
  Node* CodeAssembler::name(Node* a) { return raw_assembler()->name(a); }
CODE_ASSEMBLER_UNARY_OP_LIST(DEFINE_CODE_ASSEMBLER_UNARY_OP)
#undef DEFINE_CODE_ASSEMBLER_UNARY_OP

Node* CodeAssembler::Load(MachineType rep, Node* base) {
  return raw_assembler()->Load(rep, base);
}

Node* CodeAssembler::Load(MachineType rep, Node* base, Node* offset) {
  return raw_assembler()->Load(rep, base, offset);
}

Node* CodeAssembler::AtomicLoad(MachineType rep, Node* base, Node* offset) {
  return raw_assembler()->AtomicLoad(rep, base, offset);
}

Node* CodeAssembler::LoadRoot(Heap::RootListIndex root_index) {
  if (isolate()->heap()->RootCanBeTreatedAsConstant(root_index)) {
    Handle<Object> root = isolate()->heap()->root_handle(root_index);
    if (root->IsSmi()) {
      return SmiConstant(Smi::cast(*root));
    } else {
      return HeapConstant(Handle<HeapObject>::cast(root));
    }
  }

  Node* roots_array_start =
      ExternalConstant(ExternalReference::roots_array_start(isolate()));
  return Load(MachineType::AnyTagged(), roots_array_start,
              IntPtrConstant(root_index * kPointerSize));
}

Node* CodeAssembler::Store(Node* base, Node* value) {
  return raw_assembler()->Store(MachineRepresentation::kTagged, base, value,
                                kFullWriteBarrier);
}

Node* CodeAssembler::Store(Node* base, Node* offset, Node* value) {
  return raw_assembler()->Store(MachineRepresentation::kTagged, base, offset,
                                value, kFullWriteBarrier);
}

Node* CodeAssembler::StoreWithMapWriteBarrier(Node* base, Node* offset,
                                              Node* value) {
  return raw_assembler()->Store(MachineRepresentation::kTagged, base, offset,
                                value, kMapWriteBarrier);
}

Node* CodeAssembler::StoreNoWriteBarrier(MachineRepresentation rep, Node* base,
                                         Node* value) {
  return raw_assembler()->Store(rep, base, value, kNoWriteBarrier);
}

Node* CodeAssembler::StoreNoWriteBarrier(MachineRepresentation rep, Node* base,
                                         Node* offset, Node* value) {
  return raw_assembler()->Store(rep, base, offset, value, kNoWriteBarrier);
}

Node* CodeAssembler::AtomicStore(MachineRepresentation rep, Node* base,
                                 Node* offset, Node* value) {
  return raw_assembler()->AtomicStore(rep, base, offset, value);
}

Node* CodeAssembler::StoreRoot(Heap::RootListIndex root_index, Node* value) {
  DCHECK(Heap::RootCanBeWrittenAfterInitialization(root_index));
  Node* roots_array_start =
      ExternalConstant(ExternalReference::roots_array_start(isolate()));
  return StoreNoWriteBarrier(MachineRepresentation::kTagged, roots_array_start,
                             IntPtrConstant(root_index * kPointerSize), value);
}

Node* CodeAssembler::Retain(Node* value) {
  return raw_assembler()->Retain(value);
}

Node* CodeAssembler::Projection(int index, Node* value) {
  return raw_assembler()->Projection(index, value);
}

void CodeAssembler::GotoIfException(Node* node, Label* if_exception,
                                    Variable* exception_var) {
  Label success(this), exception(this, Label::kDeferred);
  success.MergeVariables();
  exception.MergeVariables();
  DCHECK(!node->op()->HasProperty(Operator::kNoThrow));

  raw_assembler()->Continuations(node, success.label_, exception.label_);

  Bind(&exception);
  const Operator* op = raw_assembler()->common()->IfException();
  Node* exception_value = raw_assembler()->AddNode(op, node, node);
  if (exception_var != nullptr) {
    exception_var->Bind(exception_value);
  }
  Goto(if_exception);

  Bind(&success);
}

Node* CodeAssembler::CallN(CallDescriptor* descriptor, Node* code_target,
                           Node** args) {
  CallPrologue();
  Node* return_value = raw_assembler()->CallN(descriptor, code_target, args);
  CallEpilogue();
  return return_value;
}

Node* CodeAssembler::TailCallN(CallDescriptor* descriptor, Node* code_target,
                               Node** args) {
  return raw_assembler()->TailCallN(descriptor, code_target, args);
}

Node* CodeAssembler::CallRuntime(Runtime::FunctionId function_id,
                                 Node* context) {
  CallPrologue();
  Node* return_value = raw_assembler()->CallRuntime0(function_id, context);
  CallEpilogue();
  return return_value;
}

Node* CodeAssembler::CallRuntime(Runtime::FunctionId function_id, Node* context,
                                 Node* arg1) {
  CallPrologue();
  Node* return_value =
      raw_assembler()->CallRuntime1(function_id, arg1, context);
  CallEpilogue();
  return return_value;
}

Node* CodeAssembler::CallRuntime(Runtime::FunctionId function_id, Node* context,
                                 Node* arg1, Node* arg2) {
  CallPrologue();
  Node* return_value =
      raw_assembler()->CallRuntime2(function_id, arg1, arg2, context);
  CallEpilogue();
  return return_value;
}

Node* CodeAssembler::CallRuntime(Runtime::FunctionId function_id, Node* context,
                                 Node* arg1, Node* arg2, Node* arg3) {
  CallPrologue();
  Node* return_value =
      raw_assembler()->CallRuntime3(function_id, arg1, arg2, arg3, context);
  CallEpilogue();
  return return_value;
}

Node* CodeAssembler::CallRuntime(Runtime::FunctionId function_id, Node* context,
                                 Node* arg1, Node* arg2, Node* arg3,
                                 Node* arg4) {
  CallPrologue();
  Node* return_value = raw_assembler()->CallRuntime4(function_id, arg1, arg2,
                                                     arg3, arg4, context);
  CallEpilogue();
  return return_value;
}

Node* CodeAssembler::CallRuntime(Runtime::FunctionId function_id, Node* context,
                                 Node* arg1, Node* arg2, Node* arg3, Node* arg4,
                                 Node* arg5) {
  CallPrologue();
  Node* return_value = raw_assembler()->CallRuntime5(function_id, arg1, arg2,
                                                     arg3, arg4, arg5, context);
  CallEpilogue();
  return return_value;
}

Node* CodeAssembler::TailCallRuntime(Runtime::FunctionId function_id,
                                     Node* context) {
  return raw_assembler()->TailCallRuntime0(function_id, context);
}

Node* CodeAssembler::TailCallRuntime(Runtime::FunctionId function_id,
                                     Node* context, Node* arg1) {
  return raw_assembler()->TailCallRuntime1(function_id, arg1, context);
}

Node* CodeAssembler::TailCallRuntime(Runtime::FunctionId function_id,
                                     Node* context, Node* arg1, Node* arg2) {
  return raw_assembler()->TailCallRuntime2(function_id, arg1, arg2, context);
}

Node* CodeAssembler::TailCallRuntime(Runtime::FunctionId function_id,
                                     Node* context, Node* arg1, Node* arg2,
                                     Node* arg3) {
  return raw_assembler()->TailCallRuntime3(function_id, arg1, arg2, arg3,
                                           context);
}

Node* CodeAssembler::TailCallRuntime(Runtime::FunctionId function_id,
                                     Node* context, Node* arg1, Node* arg2,
                                     Node* arg3, Node* arg4) {
  return raw_assembler()->TailCallRuntime4(function_id, arg1, arg2, arg3, arg4,
                                           context);
}

Node* CodeAssembler::TailCallRuntime(Runtime::FunctionId function_id,
                                     Node* context, Node* arg1, Node* arg2,
                                     Node* arg3, Node* arg4, Node* arg5) {
  return raw_assembler()->TailCallRuntime5(function_id, arg1, arg2, arg3, arg4,
                                           arg5, context);
}

Node* CodeAssembler::TailCallRuntime(Runtime::FunctionId function_id,
                                     Node* context, Node* arg1, Node* arg2,
                                     Node* arg3, Node* arg4, Node* arg5,
                                     Node* arg6) {
  return raw_assembler()->TailCallRuntime6(function_id, arg1, arg2, arg3, arg4,
                                           arg5, arg6, context);
}

Node* CodeAssembler::CallStub(Callable const& callable, Node* context,
                              Node* arg1, size_t result_size) {
  Node* target = HeapConstant(callable.code());
  return CallStub(callable.descriptor(), target, context, arg1, result_size);
}

Node* CodeAssembler::CallStub(Callable const& callable, Node* context,
                              Node* arg1, Node* arg2, size_t result_size) {
  Node* target = HeapConstant(callable.code());
  return CallStub(callable.descriptor(), target, context, arg1, arg2,
                  result_size);
}

Node* CodeAssembler::CallStub(Callable const& callable, Node* context,
                              Node* arg1, Node* arg2, Node* arg3,
                              size_t result_size) {
  Node* target = HeapConstant(callable.code());
  return CallStub(callable.descriptor(), target, context, arg1, arg2, arg3,
                  result_size);
}

Node* CodeAssembler::CallStub(Callable const& callable, Node* context,
                              Node* arg1, Node* arg2, Node* arg3, Node* arg4,
                              size_t result_size) {
  Node* target = HeapConstant(callable.code());
  return CallStub(callable.descriptor(), target, context, arg1, arg2, arg3,
                  arg4, result_size);
}

Node* CodeAssembler::CallStubN(Callable const& callable, Node** args,
                               size_t result_size) {
  Node* target = HeapConstant(callable.code());
  return CallStubN(callable.descriptor(), target, args, result_size);
}

Node* CodeAssembler::CallStub(const CallInterfaceDescriptor& descriptor,
                              Node* target, Node* context, size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kNoFlags, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  Node** args = zone()->NewArray<Node*>(1);
  args[0] = context;

  return CallN(call_descriptor, target, args);
}

Node* CodeAssembler::CallStub(const CallInterfaceDescriptor& descriptor,
                              Node* target, Node* context, Node* arg1,
                              size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kNoFlags, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  Node** args = zone()->NewArray<Node*>(2);
  args[0] = arg1;
  args[1] = context;

  return CallN(call_descriptor, target, args);
}

Node* CodeAssembler::CallStub(const CallInterfaceDescriptor& descriptor,
                              Node* target, Node* context, Node* arg1,
                              Node* arg2, size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kNoFlags, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  Node** args = zone()->NewArray<Node*>(3);
  args[0] = arg1;
  args[1] = arg2;
  args[2] = context;

  return CallN(call_descriptor, target, args);
}

Node* CodeAssembler::CallStub(const CallInterfaceDescriptor& descriptor,
                              Node* target, Node* context, Node* arg1,
                              Node* arg2, Node* arg3, size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kNoFlags, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  Node** args = zone()->NewArray<Node*>(4);
  args[0] = arg1;
  args[1] = arg2;
  args[2] = arg3;
  args[3] = context;

  return CallN(call_descriptor, target, args);
}

Node* CodeAssembler::CallStub(const CallInterfaceDescriptor& descriptor,
                              Node* target, Node* context, Node* arg1,
                              Node* arg2, Node* arg3, Node* arg4,
                              size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kNoFlags, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  Node** args = zone()->NewArray<Node*>(5);
  args[0] = arg1;
  args[1] = arg2;
  args[2] = arg3;
  args[3] = arg4;
  args[4] = context;

  return CallN(call_descriptor, target, args);
}

Node* CodeAssembler::CallStub(const CallInterfaceDescriptor& descriptor,
                              Node* target, Node* context, Node* arg1,
                              Node* arg2, Node* arg3, Node* arg4, Node* arg5,
                              size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kNoFlags, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  Node** args = zone()->NewArray<Node*>(6);
  args[0] = arg1;
  args[1] = arg2;
  args[2] = arg3;
  args[3] = arg4;
  args[4] = arg5;
  args[5] = context;

  return CallN(call_descriptor, target, args);
}

Node* CodeAssembler::CallStub(const CallInterfaceDescriptor& descriptor,
                              Node* target, Node* context, const Arg& arg1,
                              const Arg& arg2, size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kNoFlags, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  const int kArgsCount = 3;
  Node** args = zone()->NewArray<Node*>(kArgsCount);
  DCHECK((std::fill(&args[0], &args[kArgsCount], nullptr), true));
  args[arg1.index] = arg1.value;
  args[arg2.index] = arg2.value;
  args[kArgsCount - 1] = context;
  DCHECK_EQ(0, std::count(&args[0], &args[kArgsCount], nullptr));

  return CallN(call_descriptor, target, args);
}

Node* CodeAssembler::CallStub(const CallInterfaceDescriptor& descriptor,
                              Node* target, Node* context, const Arg& arg1,
                              const Arg& arg2, const Arg& arg3,
                              size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kNoFlags, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  const int kArgsCount = 4;
  Node** args = zone()->NewArray<Node*>(kArgsCount);
  DCHECK((std::fill(&args[0], &args[kArgsCount], nullptr), true));
  args[arg1.index] = arg1.value;
  args[arg2.index] = arg2.value;
  args[arg3.index] = arg3.value;
  args[kArgsCount - 1] = context;
  DCHECK_EQ(0, std::count(&args[0], &args[kArgsCount], nullptr));

  return CallN(call_descriptor, target, args);
}

Node* CodeAssembler::CallStub(const CallInterfaceDescriptor& descriptor,
                              Node* target, Node* context, const Arg& arg1,
                              const Arg& arg2, const Arg& arg3, const Arg& arg4,
                              size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kNoFlags, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  const int kArgsCount = 5;
  Node** args = zone()->NewArray<Node*>(kArgsCount);
  DCHECK((std::fill(&args[0], &args[kArgsCount], nullptr), true));
  args[arg1.index] = arg1.value;
  args[arg2.index] = arg2.value;
  args[arg3.index] = arg3.value;
  args[arg4.index] = arg4.value;
  args[kArgsCount - 1] = context;
  DCHECK_EQ(0, std::count(&args[0], &args[kArgsCount], nullptr));

  return CallN(call_descriptor, target, args);
}

Node* CodeAssembler::CallStub(const CallInterfaceDescriptor& descriptor,
                              Node* target, Node* context, const Arg& arg1,
                              const Arg& arg2, const Arg& arg3, const Arg& arg4,
                              const Arg& arg5, size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kNoFlags, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  const int kArgsCount = 6;
  Node** args = zone()->NewArray<Node*>(kArgsCount);
  DCHECK((std::fill(&args[0], &args[kArgsCount], nullptr), true));
  args[arg1.index] = arg1.value;
  args[arg2.index] = arg2.value;
  args[arg3.index] = arg3.value;
  args[arg4.index] = arg4.value;
  args[arg5.index] = arg5.value;
  args[kArgsCount - 1] = context;
  DCHECK_EQ(0, std::count(&args[0], &args[kArgsCount], nullptr));

  return CallN(call_descriptor, target, args);
}

Node* CodeAssembler::CallStubN(const CallInterfaceDescriptor& descriptor,
                               int js_parameter_count, Node* target,
                               Node** args, size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor,
      descriptor.GetStackParameterCount() + js_parameter_count,
      CallDescriptor::kNoFlags, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  return CallN(call_descriptor, target, args);
}

Node* CodeAssembler::TailCallStub(Callable const& callable, Node* context,
                                  Node* arg1, size_t result_size) {
  Node* target = HeapConstant(callable.code());
  return TailCallStub(callable.descriptor(), target, context, arg1,
                      result_size);
}

Node* CodeAssembler::TailCallStub(Callable const& callable, Node* context,
                                  Node* arg1, Node* arg2, size_t result_size) {
  Node* target = HeapConstant(callable.code());
  return TailCallStub(callable.descriptor(), target, context, arg1, arg2,
                      result_size);
}

Node* CodeAssembler::TailCallStub(Callable const& callable, Node* context,
                                  Node* arg1, Node* arg2, Node* arg3,
                                  size_t result_size) {
  Node* target = HeapConstant(callable.code());
  return TailCallStub(callable.descriptor(), target, context, arg1, arg2, arg3,
                      result_size);
}

Node* CodeAssembler::TailCallStub(Callable const& callable, Node* context,
                                  Node* arg1, Node* arg2, Node* arg3,
                                  Node* arg4, size_t result_size) {
  Node* target = HeapConstant(callable.code());
  return TailCallStub(callable.descriptor(), target, context, arg1, arg2, arg3,
                      arg4, result_size);
}

Node* CodeAssembler::TailCallStub(Callable const& callable, Node* context,
                                  Node* arg1, Node* arg2, Node* arg3,
                                  Node* arg4, Node* arg5, size_t result_size) {
  Node* target = HeapConstant(callable.code());
  return TailCallStub(callable.descriptor(), target, context, arg1, arg2, arg3,
                      arg4, arg5, result_size);
}

Node* CodeAssembler::TailCallStub(const CallInterfaceDescriptor& descriptor,
                                  Node* target, Node* context, Node* arg1,
                                  size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kSupportsTailCalls, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  Node** args = zone()->NewArray<Node*>(2);
  args[0] = arg1;
  args[1] = context;

  return raw_assembler()->TailCallN(call_descriptor, target, args);
}

Node* CodeAssembler::TailCallStub(const CallInterfaceDescriptor& descriptor,
                                  Node* target, Node* context, Node* arg1,
                                  Node* arg2, size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kSupportsTailCalls, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  Node** args = zone()->NewArray<Node*>(3);
  args[0] = arg1;
  args[1] = arg2;
  args[2] = context;

  return raw_assembler()->TailCallN(call_descriptor, target, args);
}

Node* CodeAssembler::TailCallStub(const CallInterfaceDescriptor& descriptor,
                                  Node* target, Node* context, Node* arg1,
                                  Node* arg2, Node* arg3, size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kSupportsTailCalls, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  Node** args = zone()->NewArray<Node*>(4);
  args[0] = arg1;
  args[1] = arg2;
  args[2] = arg3;
  args[3] = context;

  return raw_assembler()->TailCallN(call_descriptor, target, args);
}

Node* CodeAssembler::TailCallStub(const CallInterfaceDescriptor& descriptor,
                                  Node* target, Node* context, Node* arg1,
                                  Node* arg2, Node* arg3, Node* arg4,
                                  size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kSupportsTailCalls, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  Node** args = zone()->NewArray<Node*>(5);
  args[0] = arg1;
  args[1] = arg2;
  args[2] = arg3;
  args[3] = arg4;
  args[4] = context;

  return raw_assembler()->TailCallN(call_descriptor, target, args);
}

Node* CodeAssembler::TailCallStub(const CallInterfaceDescriptor& descriptor,
                                  Node* target, Node* context, Node* arg1,
                                  Node* arg2, Node* arg3, Node* arg4,
                                  Node* arg5, size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kSupportsTailCalls, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  Node** args = zone()->NewArray<Node*>(6);
  args[0] = arg1;
  args[1] = arg2;
  args[2] = arg3;
  args[3] = arg4;
  args[4] = arg5;
  args[5] = context;

  return raw_assembler()->TailCallN(call_descriptor, target, args);
}

Node* CodeAssembler::TailCallStub(const CallInterfaceDescriptor& descriptor,
                                  Node* target, Node* context, Node* arg1,
                                  Node* arg2, Node* arg3, Node* arg4,
                                  Node* arg5, Node* arg6, size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kSupportsTailCalls, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  Node** args = zone()->NewArray<Node*>(7);
  args[0] = arg1;
  args[1] = arg2;
  args[2] = arg3;
  args[3] = arg4;
  args[4] = arg5;
  args[5] = arg6;
  args[6] = context;

  return raw_assembler()->TailCallN(call_descriptor, target, args);
}

Node* CodeAssembler::TailCallStub(const CallInterfaceDescriptor& descriptor,
                                  Node* target, Node* context, const Arg& arg1,
                                  const Arg& arg2, const Arg& arg3,
                                  const Arg& arg4, size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kSupportsTailCalls, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  const int kArgsCount = 5;
  Node** args = zone()->NewArray<Node*>(kArgsCount);
  DCHECK((std::fill(&args[0], &args[kArgsCount], nullptr), true));
  args[arg1.index] = arg1.value;
  args[arg2.index] = arg2.value;
  args[arg3.index] = arg3.value;
  args[arg4.index] = arg4.value;
  args[kArgsCount - 1] = context;
  DCHECK_EQ(0, std::count(&args[0], &args[kArgsCount], nullptr));

  return raw_assembler()->TailCallN(call_descriptor, target, args);
}

Node* CodeAssembler::TailCallStub(const CallInterfaceDescriptor& descriptor,
                                  Node* target, Node* context, const Arg& arg1,
                                  const Arg& arg2, const Arg& arg3,
                                  const Arg& arg4, const Arg& arg5,
                                  size_t result_size) {
  CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
      isolate(), zone(), descriptor, descriptor.GetStackParameterCount(),
      CallDescriptor::kSupportsTailCalls, Operator::kNoProperties,
      MachineType::AnyTagged(), result_size);

  const int kArgsCount = 6;
  Node** args = zone()->NewArray<Node*>(kArgsCount);
  DCHECK((std::fill(&args[0], &args[kArgsCount], nullptr), true));
  args[arg1.index] = arg1.value;
  args[arg2.index] = arg2.value;
  args[arg3.index] = arg3.value;
  args[arg4.index] = arg4.value;
  args[arg5.index] = arg5.value;
  args[kArgsCount - 1] = context;
  DCHECK_EQ(0, std::count(&args[0], &args[kArgsCount], nullptr));

  return raw_assembler()->TailCallN(call_descriptor, target, args);
}

Node* CodeAssembler::TailCallBytecodeDispatch(
    const CallInterfaceDescriptor& interface_descriptor,
    Node* code_target_address, Node** args) {
  CallDescriptor* descriptor = Linkage::GetBytecodeDispatchCallDescriptor(
      isolate(), zone(), interface_descriptor,
      interface_descriptor.GetStackParameterCount());
  return raw_assembler()->TailCallN(descriptor, code_target_address, args);
}

Node* CodeAssembler::CallJS(Callable const& callable, Node* context,
                            Node* function, Node* receiver,
                            size_t result_size) {
  const int argc = 0;
  Node* target = HeapConstant(callable.code());

  Node** args = zone()->NewArray<Node*>(argc + 4);
  args[0] = function;
  args[1] = Int32Constant(argc);
  args[2] = receiver;
  args[3] = context;

  return CallStubN(callable.descriptor(), argc + 1, target, args, result_size);
}

Node* CodeAssembler::CallJS(Callable const& callable, Node* context,
                            Node* function, Node* receiver, Node* arg1,
                            size_t result_size) {
  const int argc = 1;
  Node* target = HeapConstant(callable.code());

  Node** args = zone()->NewArray<Node*>(argc + 4);
  args[0] = function;
  args[1] = Int32Constant(argc);
  args[2] = receiver;
  args[3] = arg1;
  args[4] = context;

  return CallStubN(callable.descriptor(), argc + 1, target, args, result_size);
}

Node* CodeAssembler::CallJS(Callable const& callable, Node* context,
                            Node* function, Node* receiver, Node* arg1,
                            Node* arg2, size_t result_size) {
  const int argc = 2;
  Node* target = HeapConstant(callable.code());

  Node** args = zone()->NewArray<Node*>(argc + 4);
  args[0] = function;
  args[1] = Int32Constant(argc);
  args[2] = receiver;
  args[3] = arg1;
  args[4] = arg2;
  args[5] = context;

  return CallStubN(callable.descriptor(), argc + 1, target, args, result_size);
}

Node* CodeAssembler::CallJS(Callable const& callable, Node* context,
                            Node* function, Node* receiver, Node* arg1,
                            Node* arg2, Node* arg3, size_t result_size) {
  const int argc = 3;
  Node* target = HeapConstant(callable.code());

  Node** args = zone()->NewArray<Node*>(argc + 4);
  args[0] = function;
  args[1] = Int32Constant(argc);
  args[2] = receiver;
  args[3] = arg1;
  args[4] = arg2;
  args[5] = arg3;
  args[6] = context;

  return CallStubN(callable.descriptor(), argc + 1, target, args, result_size);
}

Node* CodeAssembler::CallCFunction2(MachineType return_type,
                                    MachineType arg0_type,
                                    MachineType arg1_type, Node* function,
                                    Node* arg0, Node* arg1) {
  return raw_assembler()->CallCFunction2(return_type, arg0_type, arg1_type,
                                         function, arg0, arg1);
}

void CodeAssembler::Goto(CodeAssembler::Label* label) {
  label->MergeVariables();
  raw_assembler()->Goto(label->label_);
}

void CodeAssembler::GotoIf(Node* condition, Label* true_label) {
  Label false_label(this);
  Branch(condition, true_label, &false_label);
  Bind(&false_label);
}

void CodeAssembler::GotoUnless(Node* condition, Label* false_label) {
  Label true_label(this);
  Branch(condition, &true_label, false_label);
  Bind(&true_label);
}

void CodeAssembler::Branch(Node* condition, CodeAssembler::Label* true_label,
                           CodeAssembler::Label* false_label) {
  true_label->MergeVariables();
  false_label->MergeVariables();
  return raw_assembler()->Branch(condition, true_label->label_,
                                 false_label->label_);
}

void CodeAssembler::Switch(Node* index, Label* default_label,
                           const int32_t* case_values, Label** case_labels,
                           size_t case_count) {
  RawMachineLabel** labels =
      new (zone()->New(sizeof(RawMachineLabel*) * case_count))
          RawMachineLabel*[case_count];
  for (size_t i = 0; i < case_count; ++i) {
    labels[i] = case_labels[i]->label_;
    case_labels[i]->MergeVariables();
    default_label->MergeVariables();
  }
  return raw_assembler()->Switch(index, default_label->label_, case_values,
                                 labels, case_count);
}

Node* CodeAssembler::Select(Node* condition, Node* true_value,
                            Node* false_value, MachineRepresentation rep) {
  Variable value(this, rep);
  Label vtrue(this), vfalse(this), end(this);
  Branch(condition, &vtrue, &vfalse);

  Bind(&vtrue);
  {
    value.Bind(true_value);
    Goto(&end);
  }
  Bind(&vfalse);
  {
    value.Bind(false_value);
    Goto(&end);
  }

  Bind(&end);
  return value.value();
}

// RawMachineAssembler delegate helpers:
Isolate* CodeAssembler::isolate() const { return raw_assembler()->isolate(); }

Factory* CodeAssembler::factory() const { return isolate()->factory(); }

Zone* CodeAssembler::zone() const { return raw_assembler()->zone(); }

RawMachineAssembler* CodeAssembler::raw_assembler() const {
  return state_->raw_assembler_.get();
}

// The core implementation of Variable is stored through an indirection so
// that it can outlive the often block-scoped Variable declarations. This is
// needed to ensure that variable binding and merging through phis can
// properly be verified.
class CodeAssembler::Variable::Impl : public ZoneObject {
 public:
  explicit Impl(MachineRepresentation rep) : value_(nullptr), rep_(rep) {}
  Node* value_;
  MachineRepresentation rep_;
};

CodeAssembler::Variable::Variable(CodeAssembler* assembler,
                                  MachineRepresentation rep)
    : impl_(new (assembler->zone()) Impl(rep)), state_(assembler->state_) {
  state_->variables_.insert(impl_);
}

CodeAssembler::Variable::~Variable() { state_->variables_.erase(impl_); }

void CodeAssembler::Variable::Bind(Node* value) { impl_->value_ = value; }

Node* CodeAssembler::Variable::value() const {
  DCHECK_NOT_NULL(impl_->value_);
  return impl_->value_;
}

MachineRepresentation CodeAssembler::Variable::rep() const {
  return impl_->rep_;
}

bool CodeAssembler::Variable::IsBound() const {
  return impl_->value_ != nullptr;
}

CodeAssembler::Label::Label(CodeAssembler* assembler, size_t vars_count,
                            Variable** vars, CodeAssembler::Label::Type type)
    : bound_(false),
      merge_count_(0),
      state_(assembler->state_),
      label_(nullptr) {
  void* buffer = assembler->zone()->New(sizeof(RawMachineLabel));
  label_ = new (buffer)
      RawMachineLabel(type == kDeferred ? RawMachineLabel::kDeferred
                                        : RawMachineLabel::kNonDeferred);
  for (size_t i = 0; i < vars_count; ++i) {
    variable_phis_[vars[i]->impl_] = nullptr;
  }
}

void CodeAssembler::Label::MergeVariables() {
  ++merge_count_;
  for (auto var : state_->variables_) {
    size_t count = 0;
    Node* node = var->value_;
    if (node != nullptr) {
      auto i = variable_merges_.find(var);
      if (i != variable_merges_.end()) {
        i->second.push_back(node);
        count = i->second.size();
      } else {
        count = 1;
        variable_merges_[var] = std::vector<Node*>(1, node);
      }
    }
    // If the following asserts, then you've jumped to a label without a bound
    // variable along that path that expects to merge its value into a phi.
    DCHECK(variable_phis_.find(var) == variable_phis_.end() ||
           count == merge_count_);
    USE(count);

    // If the label is already bound, we already know the set of variables to
    // merge and phi nodes have already been created.
    if (bound_) {
      auto phi = variable_phis_.find(var);
      if (phi != variable_phis_.end()) {
        DCHECK_NOT_NULL(phi->second);
        state_->raw_assembler_->AppendPhiInput(phi->second, node);
      } else {
        auto i = variable_merges_.find(var);
        if (i != variable_merges_.end()) {
          // If the following assert fires, then you've declared a variable that
          // has the same bound value along all paths up until the point you
          // bound this label, but then later merged a path with a new value for
          // the variable after the label bind (it's not possible to add phis to
          // the bound label after the fact, just make sure to list the variable
          // in the label's constructor's list of merged variables).
          DCHECK(find_if(i->second.begin(), i->second.end(),
                         [node](Node* e) -> bool { return node != e; }) ==
                 i->second.end());
        }
      }
    }
  }
}

void CodeAssembler::Label::Bind() {
  DCHECK(!bound_);
  state_->raw_assembler_->Bind(label_);

  // Make sure that all variables that have changed along any path up to this
  // point are marked as merge variables.
  for (auto var : state_->variables_) {
    Node* shared_value = nullptr;
    auto i = variable_merges_.find(var);
    if (i != variable_merges_.end()) {
      for (auto value : i->second) {
        DCHECK(value != nullptr);
        if (value != shared_value) {
          if (shared_value == nullptr) {
            shared_value = value;
          } else {
            variable_phis_[var] = nullptr;
          }
        }
      }
    }
  }

  for (auto var : variable_phis_) {
    CodeAssembler::Variable::Impl* var_impl = var.first;
    auto i = variable_merges_.find(var_impl);
    // If the following assert fires, then a variable that has been marked as
    // being merged at the label--either by explicitly marking it so in the
    // label constructor or by having seen different bound values at branches
    // into the label--doesn't have a bound value along all of the paths that
    // have been merged into the label up to this point.
    DCHECK(i != variable_merges_.end() && i->second.size() == merge_count_);
    Node* phi = state_->raw_assembler_->Phi(
        var.first->rep_, static_cast<int>(merge_count_), &(i->second[0]));
    variable_phis_[var_impl] = phi;
  }

  // Bind all variables to a merge phi, the common value along all paths or
  // null.
  for (auto var : state_->variables_) {
    auto i = variable_phis_.find(var);
    if (i != variable_phis_.end()) {
      var->value_ = i->second;
    } else {
      auto j = variable_merges_.find(var);
      if (j != variable_merges_.end() && j->second.size() == merge_count_) {
        var->value_ = j->second.back();
      } else {
        var->value_ = nullptr;
      }
    }
  }

  bound_ = true;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
