// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/code-stubs.h"

#include <sstream>

#include "src/arguments.h"
#include "src/assembler-inl.h"
#include "src/ast/ast.h"
#include "src/bootstrapper.h"
#include "src/code-factory.h"
#include "src/code-stub-assembler.h"
#include "src/code-stubs-utils.h"
#include "src/counters.h"
#include "src/factory.h"
#include "src/gdb-jit.h"
#include "src/heap/heap-inl.h"
#include "src/ic/ic-stats.h"
#include "src/ic/ic.h"
#include "src/macro-assembler.h"
#include "src/objects-inl.h"
#include "src/tracing/tracing-category-observer.h"

namespace v8 {
namespace internal {

using compiler::CodeAssemblerState;

RUNTIME_FUNCTION(UnexpectedStubMiss) {
  FATAL("Unexpected deopt of a stub");
  return Smi::kZero;
}

CodeStubDescriptor::CodeStubDescriptor(CodeStub* stub)
    : isolate_(stub->isolate()),
      call_descriptor_(stub->GetCallInterfaceDescriptor()),
      stack_parameter_count_(no_reg),
      hint_stack_parameter_count_(-1),
      function_mode_(NOT_JS_FUNCTION_STUB_MODE),
      deoptimization_handler_(NULL),
      miss_handler_(),
      has_miss_handler_(false) {
  stub->InitializeDescriptor(this);
}

CodeStubDescriptor::CodeStubDescriptor(Isolate* isolate, uint32_t stub_key)
    : isolate_(isolate),
      stack_parameter_count_(no_reg),
      hint_stack_parameter_count_(-1),
      function_mode_(NOT_JS_FUNCTION_STUB_MODE),
      deoptimization_handler_(NULL),
      miss_handler_(),
      has_miss_handler_(false) {
  CodeStub::InitializeDescriptor(isolate, stub_key, this);
}


void CodeStubDescriptor::Initialize(Address deoptimization_handler,
                                    int hint_stack_parameter_count,
                                    StubFunctionMode function_mode) {
  deoptimization_handler_ = deoptimization_handler;
  hint_stack_parameter_count_ = hint_stack_parameter_count;
  function_mode_ = function_mode;
}


void CodeStubDescriptor::Initialize(Register stack_parameter_count,
                                    Address deoptimization_handler,
                                    int hint_stack_parameter_count,
                                    StubFunctionMode function_mode) {
  Initialize(deoptimization_handler, hint_stack_parameter_count, function_mode);
  stack_parameter_count_ = stack_parameter_count;
}


bool CodeStub::FindCodeInCache(Code** code_out) {
  UnseededNumberDictionary* stubs = isolate()->heap()->code_stubs();
  int index = stubs->FindEntry(isolate(), GetKey());
  if (index != UnseededNumberDictionary::kNotFound) {
    *code_out = Code::cast(stubs->ValueAt(index));
    return true;
  }
  return false;
}


void CodeStub::RecordCodeGeneration(Handle<Code> code) {
  std::ostringstream os;
  os << *this;
  PROFILE(isolate(),
          CodeCreateEvent(CodeEventListener::STUB_TAG,
                          AbstractCode::cast(*code), os.str().c_str()));
  Counters* counters = isolate()->counters();
  counters->total_stubs_code_size()->Increment(code->instruction_size());
#ifdef DEBUG
  code->VerifyEmbeddedObjects();
#endif
}


Code::Kind CodeStub::GetCodeKind() const {
  return Code::STUB;
}


Code::Flags CodeStub::GetCodeFlags() const {
  return Code::ComputeFlags(GetCodeKind(), GetExtraICState());
}

Handle<Code> CodeStub::GetCodeCopy(const FindAndReplacePattern& pattern) {
  Handle<Code> ic = GetCode();
  ic = isolate()->factory()->CopyCode(ic);
  ic->FindAndReplace(pattern);
  RecordCodeGeneration(ic);
  return ic;
}

void CodeStub::DeleteStubFromCacheForTesting() {
  Heap* heap = isolate_->heap();
  Handle<UnseededNumberDictionary> dict(heap->code_stubs());
  dict = UnseededNumberDictionary::DeleteKey(dict, GetKey());
  heap->SetRootCodeStubs(*dict);
}

Handle<Code> PlatformCodeStub::GenerateCode() {
  Factory* factory = isolate()->factory();

  // Generate the new code.
  MacroAssembler masm(isolate(), NULL, 256, CodeObjectRequired::kYes);

  {
    // Update the static counter each time a new code stub is generated.
    isolate()->counters()->code_stubs()->Increment();

    // Generate the code for the stub.
    masm.set_generating_stub(true);
    // TODO(yangguo): remove this once we can serialize IC stubs.
    masm.enable_serializer();
    NoCurrentFrameScope scope(&masm);
    Generate(&masm);
  }

  // Create the code object.
  CodeDesc desc;
  masm.GetCode(&desc);
  // Copy the generated code into a heap object.
  Code::Flags flags = Code::ComputeFlags(GetCodeKind(), GetExtraICState());
  Handle<Code> new_object = factory->NewCode(
      desc, flags, masm.CodeObject(), NeedsImmovableCode());
  return new_object;
}


Handle<Code> CodeStub::GetCode() {
  Heap* heap = isolate()->heap();
  Code* code;
  if (UseSpecialCache() ? FindCodeInSpecialCache(&code)
                        : FindCodeInCache(&code)) {
    DCHECK(GetCodeKind() == code->kind());
    return Handle<Code>(code);
  }

  {
    HandleScope scope(isolate());

    Handle<Code> new_object = GenerateCode();
    new_object->set_stub_key(GetKey());
    FinishCode(new_object);
    RecordCodeGeneration(new_object);

#ifdef ENABLE_DISASSEMBLER
    if (FLAG_print_code_stubs) {
      CodeTracer::Scope trace_scope(isolate()->GetCodeTracer());
      OFStream os(trace_scope.file());
      std::ostringstream name;
      name << *this;
      new_object->Disassemble(name.str().c_str(), os);
      os << "\n";
    }
#endif

    if (UseSpecialCache()) {
      AddToSpecialCache(new_object);
    } else {
      // Update the dictionary and the root in Heap.
      Handle<UnseededNumberDictionary> dict =
          UnseededNumberDictionary::AtNumberPut(
              Handle<UnseededNumberDictionary>(heap->code_stubs()),
              GetKey(),
              new_object);
      heap->SetRootCodeStubs(*dict);
    }
    code = *new_object;
  }

  Activate(code);
  DCHECK(!NeedsImmovableCode() || Heap::IsImmovable(code) ||
         heap->code_space()->FirstPage()->Contains(code->address()));
  return Handle<Code>(code, isolate());
}


const char* CodeStub::MajorName(CodeStub::Major major_key) {
  switch (major_key) {
#define DEF_CASE(name) case name: return #name "Stub";
    CODE_STUB_LIST(DEF_CASE)
#undef DEF_CASE
    case NoCache:
      return "<NoCache>Stub";
    case NUMBER_OF_IDS:
      UNREACHABLE();
      return NULL;
  }
  return NULL;
}


void CodeStub::PrintBaseName(std::ostream& os) const {  // NOLINT
  os << MajorName(MajorKey());
}


void CodeStub::PrintName(std::ostream& os) const {  // NOLINT
  PrintBaseName(os);
  PrintState(os);
}


void CodeStub::Dispatch(Isolate* isolate, uint32_t key, void** value_out,
                        DispatchedCall call) {
  switch (MajorKeyFromKey(key)) {
#define DEF_CASE(NAME)             \
  case NAME: {                     \
    NAME##Stub stub(key, isolate); \
    CodeStub* pstub = &stub;       \
    call(pstub, value_out);        \
    break;                         \
  }
    CODE_STUB_LIST(DEF_CASE)
#undef DEF_CASE
    case NUMBER_OF_IDS:
    case NoCache:
      UNREACHABLE();
      break;
  }
}


static void InitializeDescriptorDispatchedCall(CodeStub* stub,
                                               void** value_out) {
  CodeStubDescriptor* descriptor_out =
      reinterpret_cast<CodeStubDescriptor*>(value_out);
  stub->InitializeDescriptor(descriptor_out);
  descriptor_out->set_call_descriptor(stub->GetCallInterfaceDescriptor());
}


void CodeStub::InitializeDescriptor(Isolate* isolate, uint32_t key,
                                    CodeStubDescriptor* desc) {
  void** value_out = reinterpret_cast<void**>(desc);
  Dispatch(isolate, key, value_out, &InitializeDescriptorDispatchedCall);
}


void CodeStub::GetCodeDispatchCall(CodeStub* stub, void** value_out) {
  Handle<Code>* code_out = reinterpret_cast<Handle<Code>*>(value_out);
  // Code stubs with special cache cannot be recreated from stub key.
  *code_out = stub->UseSpecialCache() ? Handle<Code>() : stub->GetCode();
}


MaybeHandle<Code> CodeStub::GetCode(Isolate* isolate, uint32_t key) {
  HandleScope scope(isolate);
  Handle<Code> code;
  void** value_out = reinterpret_cast<void**>(&code);
  Dispatch(isolate, key, value_out, &GetCodeDispatchCall);
  return scope.CloseAndEscape(code);
}


// static
void BinaryOpICStub::GenerateAheadOfTime(Isolate* isolate) {
  if (FLAG_minimal) return;
  // Generate the uninitialized versions of the stub.
  for (int op = Token::BIT_OR; op <= Token::MOD; ++op) {
    BinaryOpICStub stub(isolate, static_cast<Token::Value>(op));
    stub.GetCode();
  }

  // Generate special versions of the stub.
  BinaryOpICState::GenerateAheadOfTime(isolate, &GenerateAheadOfTime);
}


void BinaryOpICStub::PrintState(std::ostream& os) const {  // NOLINT
  os << state();
}


// static
void BinaryOpICStub::GenerateAheadOfTime(Isolate* isolate,
                                         const BinaryOpICState& state) {
  if (FLAG_minimal) return;
  BinaryOpICStub stub(isolate, state);
  stub.GetCode();
}


// static
void BinaryOpICWithAllocationSiteStub::GenerateAheadOfTime(Isolate* isolate) {
  // Generate special versions of the stub.
  BinaryOpICState::GenerateAheadOfTime(isolate, &GenerateAheadOfTime);
}


void BinaryOpICWithAllocationSiteStub::PrintState(
    std::ostream& os) const {  // NOLINT
  os << state();
}


// static
void BinaryOpICWithAllocationSiteStub::GenerateAheadOfTime(
    Isolate* isolate, const BinaryOpICState& state) {
  if (state.CouldCreateAllocationMementos()) {
    BinaryOpICWithAllocationSiteStub stub(isolate, state);
    stub.GetCode();
  }
}

void StringAddStub::PrintBaseName(std::ostream& os) const {  // NOLINT
  os << "StringAddStub_" << flags() << "_" << pretenure_flag();
}

TF_STUB(StringAddStub, CodeStubAssembler) {
  StringAddFlags flags = stub->flags();
  PretenureFlag pretenure_flag = stub->pretenure_flag();

  Node* left = Parameter(Descriptor::kLeft);
  Node* right = Parameter(Descriptor::kRight);
  Node* context = Parameter(Descriptor::kContext);

  if ((flags & STRING_ADD_CHECK_LEFT) != 0) {
    DCHECK((flags & STRING_ADD_CONVERT) != 0);
    // TODO(danno): The ToString and JSReceiverToPrimitive below could be
    // combined to avoid duplicate smi and instance type checks.
    left = ToString(context, JSReceiverToPrimitive(context, left));
  }
  if ((flags & STRING_ADD_CHECK_RIGHT) != 0) {
    DCHECK((flags & STRING_ADD_CONVERT) != 0);
    // TODO(danno): The ToString and JSReceiverToPrimitive below could be
    // combined to avoid duplicate smi and instance type checks.
    right = ToString(context, JSReceiverToPrimitive(context, right));
  }

  if ((flags & STRING_ADD_CHECK_BOTH) == 0) {
    CodeStubAssembler::AllocationFlag allocation_flags =
        (pretenure_flag == TENURED) ? CodeStubAssembler::kPretenured
                                    : CodeStubAssembler::kNone;
    Return(StringAdd(context, left, right, allocation_flags));
  } else {
    Callable callable = CodeFactory::StringAdd(isolate(), STRING_ADD_CHECK_NONE,
                                               pretenure_flag);
    TailCallStub(callable, context, left, right);
  }
}

InlineCacheState CompareICStub::GetICState() const {
  CompareICState::State state = Max(left(), right());
  switch (state) {
    case CompareICState::UNINITIALIZED:
      return ::v8::internal::UNINITIALIZED;
    case CompareICState::BOOLEAN:
    case CompareICState::SMI:
    case CompareICState::NUMBER:
    case CompareICState::INTERNALIZED_STRING:
    case CompareICState::STRING:
    case CompareICState::UNIQUE_NAME:
    case CompareICState::RECEIVER:
    case CompareICState::KNOWN_RECEIVER:
      return MONOMORPHIC;
    case CompareICState::GENERIC:
      return ::v8::internal::GENERIC;
  }
  UNREACHABLE();
  return ::v8::internal::UNINITIALIZED;
}


Condition CompareICStub::GetCondition() const {
  return CompareIC::ComputeCondition(op());
}


void CompareICStub::Generate(MacroAssembler* masm) {
  switch (state()) {
    case CompareICState::UNINITIALIZED:
      GenerateMiss(masm);
      break;
    case CompareICState::BOOLEAN:
      GenerateBooleans(masm);
      break;
    case CompareICState::SMI:
      GenerateSmis(masm);
      break;
    case CompareICState::NUMBER:
      GenerateNumbers(masm);
      break;
    case CompareICState::STRING:
      GenerateStrings(masm);
      break;
    case CompareICState::INTERNALIZED_STRING:
      GenerateInternalizedStrings(masm);
      break;
    case CompareICState::UNIQUE_NAME:
      GenerateUniqueNames(masm);
      break;
    case CompareICState::RECEIVER:
      GenerateReceivers(masm);
      break;
    case CompareICState::KNOWN_RECEIVER:
      DCHECK(*known_map_ != NULL);
      GenerateKnownReceivers(masm);
      break;
    case CompareICState::GENERIC:
      GenerateGeneric(masm);
      break;
  }
}

Handle<Code> TurboFanCodeStub::GenerateCode() {
  const char* name = CodeStub::MajorName(MajorKey());
  Zone zone(isolate()->allocator(), ZONE_NAME);
  CallInterfaceDescriptor descriptor(GetCallInterfaceDescriptor());
  compiler::CodeAssemblerState state(isolate(), &zone, descriptor,
                                     GetCodeFlags(), name);
  GenerateAssembly(&state);
  return compiler::CodeAssembler::GenerateCode(&state);
}

TF_STUB(ElementsTransitionAndStoreStub, CodeStubAssembler) {
  Node* receiver = Parameter(Descriptor::kReceiver);
  Node* key = Parameter(Descriptor::kName);
  Node* value = Parameter(Descriptor::kValue);
  Node* map = Parameter(Descriptor::kMap);
  Node* slot = Parameter(Descriptor::kSlot);
  Node* vector = Parameter(Descriptor::kVector);
  Node* context = Parameter(Descriptor::kContext);

  Comment(
      "ElementsTransitionAndStoreStub: from_kind=%s, to_kind=%s,"
      " is_jsarray=%d, store_mode=%d",
      ElementsKindToString(stub->from_kind()),
      ElementsKindToString(stub->to_kind()), stub->is_jsarray(),
      stub->store_mode());

  Label miss(this);

  if (FLAG_trace_elements_transitions) {
    // Tracing elements transitions is the job of the runtime.
    Goto(&miss);
  } else {
    TransitionElementsKind(receiver, map, stub->from_kind(), stub->to_kind(),
                           stub->is_jsarray(), &miss);
    EmitElementStore(receiver, key, value, stub->is_jsarray(), stub->to_kind(),
                     stub->store_mode(), &miss);
    Return(value);
  }

  Bind(&miss);
  {
    Comment("Miss");
    TailCallRuntime(Runtime::kElementsTransitionAndStoreIC_Miss, context,
                    receiver, key, value, map, slot, vector);
  }
}

// TODO(ishell): move to builtins.
TF_STUB(AllocateHeapNumberStub, CodeStubAssembler) {
  Node* result = AllocateHeapNumber();
  Return(result);
}

// TODO(ishell): move to builtins-handler-gen.
TF_STUB(StringLengthStub, CodeStubAssembler) {
  Node* value = Parameter(Descriptor::kReceiver);
  Node* string = LoadJSValueValue(value);
  Node* result = LoadStringLength(string);
  Return(result);
}

#define BINARY_OP_STUB(StubName)                                           \
  TF_STUB(StubName, CodeStubAssembler) {                                   \
    Return(StubName::Generate(                                             \
        this, Parameter(Descriptor::kLeft), Parameter(Descriptor::kRight), \
        ChangeUint32ToWord(Parameter(Descriptor::kSlot)),                  \
        Parameter(Descriptor::kVector), Parameter(Descriptor::kContext))); \
  }

// TODO(ishell): don't have to be stubs. Move generators to BinaryOpICAssembler.
BINARY_OP_STUB(AddWithFeedbackStub)
BINARY_OP_STUB(SubtractWithFeedbackStub)
BINARY_OP_STUB(MultiplyWithFeedbackStub)
BINARY_OP_STUB(DivideWithFeedbackStub)
BINARY_OP_STUB(ModulusWithFeedbackStub)
#undef BINARY_OP_STUB

// static
compiler::Node* AddWithFeedbackStub::Generate(CodeStubAssembler* assembler,
                                              compiler::Node* lhs,
                                              compiler::Node* rhs,
                                              compiler::Node* slot_id,
                                              compiler::Node* feedback_vector,
                                              compiler::Node* context) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  // Shared entry for floating point addition.
  Label do_fadd(assembler), if_lhsisnotnumber(assembler, Label::kDeferred),
      check_rhsisoddball(assembler, Label::kDeferred),
      call_with_oddball_feedback(assembler), call_with_any_feedback(assembler),
      call_add_stub(assembler), end(assembler);
  Variable var_fadd_lhs(assembler, MachineRepresentation::kFloat64),
      var_fadd_rhs(assembler, MachineRepresentation::kFloat64),
      var_type_feedback(assembler, MachineRepresentation::kTaggedSigned),
      var_result(assembler, MachineRepresentation::kTagged);

  // Check if the {lhs} is a Smi or a HeapObject.
  Label if_lhsissmi(assembler), if_lhsisnotsmi(assembler);
  assembler->Branch(assembler->TaggedIsSmi(lhs), &if_lhsissmi, &if_lhsisnotsmi);

  assembler->Bind(&if_lhsissmi);
  {
    // Check if the {rhs} is also a Smi.
    Label if_rhsissmi(assembler), if_rhsisnotsmi(assembler);
    assembler->Branch(assembler->TaggedIsSmi(rhs), &if_rhsissmi,
                      &if_rhsisnotsmi);

    assembler->Bind(&if_rhsissmi);
    {
      // Try fast Smi addition first.
      Node* pair =
          assembler->IntPtrAddWithOverflow(assembler->BitcastTaggedToWord(lhs),
                                           assembler->BitcastTaggedToWord(rhs));
      Node* overflow = assembler->Projection(1, pair);

      // Check if the Smi additon overflowed.
      Label if_overflow(assembler), if_notoverflow(assembler);
      assembler->Branch(overflow, &if_overflow, &if_notoverflow);

      assembler->Bind(&if_overflow);
      {
        var_fadd_lhs.Bind(assembler->SmiToFloat64(lhs));
        var_fadd_rhs.Bind(assembler->SmiToFloat64(rhs));
        assembler->Goto(&do_fadd);
      }

      assembler->Bind(&if_notoverflow);
      {
        var_type_feedback.Bind(
            assembler->SmiConstant(BinaryOperationFeedback::kSignedSmall));
        var_result.Bind(assembler->BitcastWordToTaggedSigned(
            assembler->Projection(0, pair)));
        assembler->Goto(&end);
      }
    }

    assembler->Bind(&if_rhsisnotsmi);
    {
      // Load the map of {rhs}.
      Node* rhs_map = assembler->LoadMap(rhs);

      // Check if the {rhs} is a HeapNumber.
      assembler->GotoIfNot(assembler->IsHeapNumberMap(rhs_map),
                           &check_rhsisoddball);

      var_fadd_lhs.Bind(assembler->SmiToFloat64(lhs));
      var_fadd_rhs.Bind(assembler->LoadHeapNumberValue(rhs));
      assembler->Goto(&do_fadd);
    }
  }

  assembler->Bind(&if_lhsisnotsmi);
  {
    // Load the map of {lhs}.
    Node* lhs_map = assembler->LoadMap(lhs);

    // Check if {lhs} is a HeapNumber.
    assembler->GotoIfNot(assembler->IsHeapNumberMap(lhs_map),
                         &if_lhsisnotnumber);

    // Check if the {rhs} is Smi.
    Label if_rhsissmi(assembler), if_rhsisnotsmi(assembler);
    assembler->Branch(assembler->TaggedIsSmi(rhs), &if_rhsissmi,
                      &if_rhsisnotsmi);

    assembler->Bind(&if_rhsissmi);
    {
      var_fadd_lhs.Bind(assembler->LoadHeapNumberValue(lhs));
      var_fadd_rhs.Bind(assembler->SmiToFloat64(rhs));
      assembler->Goto(&do_fadd);
    }

    assembler->Bind(&if_rhsisnotsmi);
    {
      // Load the map of {rhs}.
      Node* rhs_map = assembler->LoadMap(rhs);

      // Check if the {rhs} is a HeapNumber.
      assembler->GotoIfNot(assembler->IsHeapNumberMap(rhs_map),
                           &check_rhsisoddball);

      var_fadd_lhs.Bind(assembler->LoadHeapNumberValue(lhs));
      var_fadd_rhs.Bind(assembler->LoadHeapNumberValue(rhs));
      assembler->Goto(&do_fadd);
    }
  }

  assembler->Bind(&do_fadd);
  {
    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kNumber));
    Node* value =
        assembler->Float64Add(var_fadd_lhs.value(), var_fadd_rhs.value());
    Node* result = assembler->AllocateHeapNumberWithValue(value);
    var_result.Bind(result);
    assembler->Goto(&end);
  }

  assembler->Bind(&if_lhsisnotnumber);
  {
    // No checks on rhs are done yet. We just know lhs is not a number or Smi.
    Label if_lhsisoddball(assembler), if_lhsisnotoddball(assembler);
    Node* lhs_instance_type = assembler->LoadInstanceType(lhs);
    Node* lhs_is_oddball = assembler->Word32Equal(
        lhs_instance_type, assembler->Int32Constant(ODDBALL_TYPE));
    assembler->Branch(lhs_is_oddball, &if_lhsisoddball, &if_lhsisnotoddball);

    assembler->Bind(&if_lhsisoddball);
    {
      assembler->GotoIf(assembler->TaggedIsSmi(rhs),
                        &call_with_oddball_feedback);

      // Load the map of the {rhs}.
      Node* rhs_map = assembler->LoadMap(rhs);

      // Check if {rhs} is a HeapNumber.
      assembler->Branch(assembler->IsHeapNumberMap(rhs_map),
                        &call_with_oddball_feedback, &check_rhsisoddball);
    }

    assembler->Bind(&if_lhsisnotoddball);
    {
      // Exit unless {lhs} is a string
      assembler->GotoIfNot(assembler->IsStringInstanceType(lhs_instance_type),
                           &call_with_any_feedback);

      // Check if the {rhs} is a smi, and exit the string check early if it is.
      assembler->GotoIf(assembler->TaggedIsSmi(rhs), &call_with_any_feedback);

      Node* rhs_instance_type = assembler->LoadInstanceType(rhs);

      // Exit unless {rhs} is a string. Since {lhs} is a string we no longer
      // need an Oddball check.
      assembler->GotoIfNot(assembler->IsStringInstanceType(rhs_instance_type),
                           &call_with_any_feedback);

      var_type_feedback.Bind(
          assembler->SmiConstant(BinaryOperationFeedback::kString));
      Callable callable = CodeFactory::StringAdd(
          assembler->isolate(), STRING_ADD_CHECK_NONE, NOT_TENURED);
      var_result.Bind(assembler->CallStub(callable, context, lhs, rhs));

      assembler->Goto(&end);
    }
  }

  assembler->Bind(&check_rhsisoddball);
  {
    // Check if rhs is an oddball. At this point we know lhs is either a
    // Smi or number or oddball and rhs is not a number or Smi.
    Node* rhs_instance_type = assembler->LoadInstanceType(rhs);
    Node* rhs_is_oddball = assembler->Word32Equal(
        rhs_instance_type, assembler->Int32Constant(ODDBALL_TYPE));
    assembler->Branch(rhs_is_oddball, &call_with_oddball_feedback,
                      &call_with_any_feedback);
  }

  assembler->Bind(&call_with_oddball_feedback);
  {
    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
    assembler->Goto(&call_add_stub);
  }

  assembler->Bind(&call_with_any_feedback);
  {
    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kAny));
    assembler->Goto(&call_add_stub);
  }

  assembler->Bind(&call_add_stub);
  {
    Callable callable = CodeFactory::Add(assembler->isolate());
    var_result.Bind(assembler->CallStub(callable, context, lhs, rhs));
    assembler->Goto(&end);
  }

  assembler->Bind(&end);
  assembler->UpdateFeedback(var_type_feedback.value(), feedback_vector,
                            slot_id);
  return var_result.value();
}

// static
compiler::Node* SubtractWithFeedbackStub::Generate(
    CodeStubAssembler* assembler, compiler::Node* lhs, compiler::Node* rhs,
    compiler::Node* slot_id, compiler::Node* feedback_vector,
    compiler::Node* context) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  // Shared entry for floating point subtraction.
  Label do_fsub(assembler), end(assembler), call_subtract_stub(assembler),
      if_lhsisnotnumber(assembler), check_rhsisoddball(assembler),
      call_with_any_feedback(assembler);
  Variable var_fsub_lhs(assembler, MachineRepresentation::kFloat64),
      var_fsub_rhs(assembler, MachineRepresentation::kFloat64),
      var_type_feedback(assembler, MachineRepresentation::kTaggedSigned),
      var_result(assembler, MachineRepresentation::kTagged);

  // Check if the {lhs} is a Smi or a HeapObject.
  Label if_lhsissmi(assembler), if_lhsisnotsmi(assembler);
  assembler->Branch(assembler->TaggedIsSmi(lhs), &if_lhsissmi, &if_lhsisnotsmi);

  assembler->Bind(&if_lhsissmi);
  {
    // Check if the {rhs} is also a Smi.
    Label if_rhsissmi(assembler), if_rhsisnotsmi(assembler);
    assembler->Branch(assembler->TaggedIsSmi(rhs), &if_rhsissmi,
                      &if_rhsisnotsmi);

    assembler->Bind(&if_rhsissmi);
    {
      // Try a fast Smi subtraction first.
      Node* pair =
          assembler->IntPtrSubWithOverflow(assembler->BitcastTaggedToWord(lhs),
                                           assembler->BitcastTaggedToWord(rhs));
      Node* overflow = assembler->Projection(1, pair);

      // Check if the Smi subtraction overflowed.
      Label if_overflow(assembler), if_notoverflow(assembler);
      assembler->Branch(overflow, &if_overflow, &if_notoverflow);

      assembler->Bind(&if_overflow);
      {
        // lhs, rhs - smi and result - number. combined - number.
        // The result doesn't fit into Smi range.
        var_fsub_lhs.Bind(assembler->SmiToFloat64(lhs));
        var_fsub_rhs.Bind(assembler->SmiToFloat64(rhs));
        assembler->Goto(&do_fsub);
      }

      assembler->Bind(&if_notoverflow);
      // lhs, rhs, result smi. combined - smi.
      var_type_feedback.Bind(
          assembler->SmiConstant(BinaryOperationFeedback::kSignedSmall));
      var_result.Bind(
          assembler->BitcastWordToTaggedSigned(assembler->Projection(0, pair)));
      assembler->Goto(&end);
    }

    assembler->Bind(&if_rhsisnotsmi);
    {
      // Load the map of the {rhs}.
      Node* rhs_map = assembler->LoadMap(rhs);

      // Check if {rhs} is a HeapNumber.
      assembler->GotoIfNot(assembler->IsHeapNumberMap(rhs_map),
                           &check_rhsisoddball);

      // Perform a floating point subtraction.
      var_fsub_lhs.Bind(assembler->SmiToFloat64(lhs));
      var_fsub_rhs.Bind(assembler->LoadHeapNumberValue(rhs));
      assembler->Goto(&do_fsub);
    }
  }

  assembler->Bind(&if_lhsisnotsmi);
  {
    // Load the map of the {lhs}.
    Node* lhs_map = assembler->LoadMap(lhs);

    // Check if the {lhs} is a HeapNumber.
    assembler->GotoIfNot(assembler->IsHeapNumberMap(lhs_map),
                         &if_lhsisnotnumber);

    // Check if the {rhs} is a Smi.
    Label if_rhsissmi(assembler), if_rhsisnotsmi(assembler);
    assembler->Branch(assembler->TaggedIsSmi(rhs), &if_rhsissmi,
                      &if_rhsisnotsmi);

    assembler->Bind(&if_rhsissmi);
    {
      // Perform a floating point subtraction.
      var_fsub_lhs.Bind(assembler->LoadHeapNumberValue(lhs));
      var_fsub_rhs.Bind(assembler->SmiToFloat64(rhs));
      assembler->Goto(&do_fsub);
    }

    assembler->Bind(&if_rhsisnotsmi);
    {
      // Load the map of the {rhs}.
      Node* rhs_map = assembler->LoadMap(rhs);

      // Check if the {rhs} is a HeapNumber.
      assembler->GotoIfNot(assembler->IsHeapNumberMap(rhs_map),
                           &check_rhsisoddball);

      // Perform a floating point subtraction.
      var_fsub_lhs.Bind(assembler->LoadHeapNumberValue(lhs));
      var_fsub_rhs.Bind(assembler->LoadHeapNumberValue(rhs));
      assembler->Goto(&do_fsub);
    }
  }

  assembler->Bind(&do_fsub);
  {
    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kNumber));
    Node* lhs_value = var_fsub_lhs.value();
    Node* rhs_value = var_fsub_rhs.value();
    Node* value = assembler->Float64Sub(lhs_value, rhs_value);
    var_result.Bind(assembler->AllocateHeapNumberWithValue(value));
    assembler->Goto(&end);
  }

  assembler->Bind(&if_lhsisnotnumber);
  {
    // No checks on rhs are done yet. We just know lhs is not a number or Smi.
    // Check if lhs is an oddball.
    Node* lhs_instance_type = assembler->LoadInstanceType(lhs);
    Node* lhs_is_oddball = assembler->Word32Equal(
        lhs_instance_type, assembler->Int32Constant(ODDBALL_TYPE));
    assembler->GotoIfNot(lhs_is_oddball, &call_with_any_feedback);

    Label if_rhsissmi(assembler), if_rhsisnotsmi(assembler);
    assembler->Branch(assembler->TaggedIsSmi(rhs), &if_rhsissmi,
                      &if_rhsisnotsmi);

    assembler->Bind(&if_rhsissmi);
    {
      var_type_feedback.Bind(
          assembler->SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
      assembler->Goto(&call_subtract_stub);
    }

    assembler->Bind(&if_rhsisnotsmi);
    {
      // Load the map of the {rhs}.
      Node* rhs_map = assembler->LoadMap(rhs);

      // Check if {rhs} is a HeapNumber.
      assembler->GotoIfNot(assembler->IsHeapNumberMap(rhs_map),
                           &check_rhsisoddball);

      var_type_feedback.Bind(
          assembler->SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
      assembler->Goto(&call_subtract_stub);
    }
  }

  assembler->Bind(&check_rhsisoddball);
  {
    // Check if rhs is an oddball. At this point we know lhs is either a
    // Smi or number or oddball and rhs is not a number or Smi.
    Node* rhs_instance_type = assembler->LoadInstanceType(rhs);
    Node* rhs_is_oddball = assembler->Word32Equal(
        rhs_instance_type, assembler->Int32Constant(ODDBALL_TYPE));
    assembler->GotoIfNot(rhs_is_oddball, &call_with_any_feedback);

    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
    assembler->Goto(&call_subtract_stub);
  }

  assembler->Bind(&call_with_any_feedback);
  {
    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kAny));
    assembler->Goto(&call_subtract_stub);
  }

  assembler->Bind(&call_subtract_stub);
  {
    Callable callable = CodeFactory::Subtract(assembler->isolate());
    var_result.Bind(assembler->CallStub(callable, context, lhs, rhs));
    assembler->Goto(&end);
  }

  assembler->Bind(&end);
  assembler->UpdateFeedback(var_type_feedback.value(), feedback_vector,
                            slot_id);
  return var_result.value();
}


// static
compiler::Node* MultiplyWithFeedbackStub::Generate(
    CodeStubAssembler* assembler, compiler::Node* lhs, compiler::Node* rhs,
    compiler::Node* slot_id, compiler::Node* feedback_vector,
    compiler::Node* context) {
  using compiler::Node;
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;

  // Shared entry point for floating point multiplication.
  Label do_fmul(assembler), if_lhsisnotnumber(assembler, Label::kDeferred),
      check_rhsisoddball(assembler, Label::kDeferred),
      call_with_oddball_feedback(assembler), call_with_any_feedback(assembler),
      call_multiply_stub(assembler), end(assembler);
  Variable var_lhs_float64(assembler, MachineRepresentation::kFloat64),
      var_rhs_float64(assembler, MachineRepresentation::kFloat64),
      var_result(assembler, MachineRepresentation::kTagged),
      var_type_feedback(assembler, MachineRepresentation::kTaggedSigned);

  Label lhs_is_smi(assembler), lhs_is_not_smi(assembler);
  assembler->Branch(assembler->TaggedIsSmi(lhs), &lhs_is_smi, &lhs_is_not_smi);

  assembler->Bind(&lhs_is_smi);
  {
    Label rhs_is_smi(assembler), rhs_is_not_smi(assembler);
    assembler->Branch(assembler->TaggedIsSmi(rhs), &rhs_is_smi,
                      &rhs_is_not_smi);

    assembler->Bind(&rhs_is_smi);
    {
      // Both {lhs} and {rhs} are Smis. The result is not necessarily a smi,
      // in case of overflow.
      var_result.Bind(assembler->SmiMul(lhs, rhs));
      var_type_feedback.Bind(assembler->SelectSmiConstant(
          assembler->TaggedIsSmi(var_result.value()),
          BinaryOperationFeedback::kSignedSmall,
          BinaryOperationFeedback::kNumber));
      assembler->Goto(&end);
    }

    assembler->Bind(&rhs_is_not_smi);
    {
      Node* rhs_map = assembler->LoadMap(rhs);

      // Check if {rhs} is a HeapNumber.
      assembler->GotoIfNot(assembler->IsHeapNumberMap(rhs_map),
                           &check_rhsisoddball);

      // Convert {lhs} to a double and multiply it with the value of {rhs}.
      var_lhs_float64.Bind(assembler->SmiToFloat64(lhs));
      var_rhs_float64.Bind(assembler->LoadHeapNumberValue(rhs));
      assembler->Goto(&do_fmul);
    }
  }

  assembler->Bind(&lhs_is_not_smi);
  {
    Node* lhs_map = assembler->LoadMap(lhs);

    // Check if {lhs} is a HeapNumber.
    assembler->GotoIfNot(assembler->IsHeapNumberMap(lhs_map),
                         &if_lhsisnotnumber);

    // Check if {rhs} is a Smi.
    Label rhs_is_smi(assembler), rhs_is_not_smi(assembler);
    assembler->Branch(assembler->TaggedIsSmi(rhs), &rhs_is_smi,
                      &rhs_is_not_smi);

    assembler->Bind(&rhs_is_smi);
    {
      // Convert {rhs} to a double and multiply it with the value of {lhs}.
      var_lhs_float64.Bind(assembler->LoadHeapNumberValue(lhs));
      var_rhs_float64.Bind(assembler->SmiToFloat64(rhs));
      assembler->Goto(&do_fmul);
    }

    assembler->Bind(&rhs_is_not_smi);
    {
      Node* rhs_map = assembler->LoadMap(rhs);

      // Check if {rhs} is a HeapNumber.
      assembler->GotoIfNot(assembler->IsHeapNumberMap(rhs_map),
                           &check_rhsisoddball);

      // Both {lhs} and {rhs} are HeapNumbers. Load their values and
      // multiply them.
      var_lhs_float64.Bind(assembler->LoadHeapNumberValue(lhs));
      var_rhs_float64.Bind(assembler->LoadHeapNumberValue(rhs));
      assembler->Goto(&do_fmul);
    }
  }

  assembler->Bind(&do_fmul);
  {
    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kNumber));
    Node* value =
        assembler->Float64Mul(var_lhs_float64.value(), var_rhs_float64.value());
    Node* result = assembler->AllocateHeapNumberWithValue(value);
    var_result.Bind(result);
    assembler->Goto(&end);
  }

  assembler->Bind(&if_lhsisnotnumber);
  {
    // No checks on rhs are done yet. We just know lhs is not a number or Smi.
    // Check if lhs is an oddball.
    Node* lhs_instance_type = assembler->LoadInstanceType(lhs);
    Node* lhs_is_oddball = assembler->Word32Equal(
        lhs_instance_type, assembler->Int32Constant(ODDBALL_TYPE));
    assembler->GotoIfNot(lhs_is_oddball, &call_with_any_feedback);

    assembler->GotoIf(assembler->TaggedIsSmi(rhs), &call_with_oddball_feedback);

    // Load the map of the {rhs}.
    Node* rhs_map = assembler->LoadMap(rhs);

    // Check if {rhs} is a HeapNumber.
    assembler->Branch(assembler->IsHeapNumberMap(rhs_map),
                      &call_with_oddball_feedback, &check_rhsisoddball);
  }

  assembler->Bind(&check_rhsisoddball);
  {
    // Check if rhs is an oddball. At this point we know lhs is either a
    // Smi or number or oddball and rhs is not a number or Smi.
    Node* rhs_instance_type = assembler->LoadInstanceType(rhs);
    Node* rhs_is_oddball = assembler->Word32Equal(
        rhs_instance_type, assembler->Int32Constant(ODDBALL_TYPE));
    assembler->Branch(rhs_is_oddball, &call_with_oddball_feedback,
                      &call_with_any_feedback);
  }

  assembler->Bind(&call_with_oddball_feedback);
  {
    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
    assembler->Goto(&call_multiply_stub);
  }

  assembler->Bind(&call_with_any_feedback);
  {
    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kAny));
    assembler->Goto(&call_multiply_stub);
  }

  assembler->Bind(&call_multiply_stub);
  {
    Callable callable = CodeFactory::Multiply(assembler->isolate());
    var_result.Bind(assembler->CallStub(callable, context, lhs, rhs));
    assembler->Goto(&end);
  }

  assembler->Bind(&end);
  assembler->UpdateFeedback(var_type_feedback.value(), feedback_vector,
                            slot_id);
  return var_result.value();
}


// static
compiler::Node* DivideWithFeedbackStub::Generate(
    CodeStubAssembler* assembler, compiler::Node* dividend,
    compiler::Node* divisor, compiler::Node* slot_id,
    compiler::Node* feedback_vector, compiler::Node* context) {
  using compiler::Node;
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;

  // Shared entry point for floating point division.
  Label do_fdiv(assembler), dividend_is_not_number(assembler, Label::kDeferred),
      check_divisor_for_oddball(assembler, Label::kDeferred),
      call_with_oddball_feedback(assembler), call_with_any_feedback(assembler),
      call_divide_stub(assembler), end(assembler);
  Variable var_dividend_float64(assembler, MachineRepresentation::kFloat64),
      var_divisor_float64(assembler, MachineRepresentation::kFloat64),
      var_result(assembler, MachineRepresentation::kTagged),
      var_type_feedback(assembler, MachineRepresentation::kTaggedSigned);

  Label dividend_is_smi(assembler), dividend_is_not_smi(assembler);
  assembler->Branch(assembler->TaggedIsSmi(dividend), &dividend_is_smi,
                    &dividend_is_not_smi);

  assembler->Bind(&dividend_is_smi);
  {
    Label divisor_is_smi(assembler), divisor_is_not_smi(assembler);
    assembler->Branch(assembler->TaggedIsSmi(divisor), &divisor_is_smi,
                      &divisor_is_not_smi);

    assembler->Bind(&divisor_is_smi);
    {
      Label bailout(assembler);

      // Do floating point division if {divisor} is zero.
      assembler->GotoIf(
          assembler->WordEqual(divisor, assembler->SmiConstant(0)), &bailout);

      // Do floating point division {dividend} is zero and {divisor} is
      // negative.
      Label dividend_is_zero(assembler), dividend_is_not_zero(assembler);
      assembler->Branch(
          assembler->WordEqual(dividend, assembler->SmiConstant(0)),
          &dividend_is_zero, &dividend_is_not_zero);

      assembler->Bind(&dividend_is_zero);
      {
        assembler->GotoIf(
            assembler->SmiLessThan(divisor, assembler->SmiConstant(0)),
            &bailout);
        assembler->Goto(&dividend_is_not_zero);
      }
      assembler->Bind(&dividend_is_not_zero);

      Node* untagged_divisor = assembler->SmiToWord32(divisor);
      Node* untagged_dividend = assembler->SmiToWord32(dividend);

      // Do floating point division if {dividend} is kMinInt (or kMinInt - 1
      // if the Smi size is 31) and {divisor} is -1.
      Label divisor_is_minus_one(assembler),
          divisor_is_not_minus_one(assembler);
      assembler->Branch(assembler->Word32Equal(untagged_divisor,
                                               assembler->Int32Constant(-1)),
                        &divisor_is_minus_one, &divisor_is_not_minus_one);

      assembler->Bind(&divisor_is_minus_one);
      {
        assembler->GotoIf(
            assembler->Word32Equal(
                untagged_dividend,
                assembler->Int32Constant(kSmiValueSize == 32 ? kMinInt
                                                             : (kMinInt >> 1))),
            &bailout);
        assembler->Goto(&divisor_is_not_minus_one);
      }
      assembler->Bind(&divisor_is_not_minus_one);

      Node* untagged_result =
          assembler->Int32Div(untagged_dividend, untagged_divisor);
      Node* truncated = assembler->Int32Mul(untagged_result, untagged_divisor);
      // Do floating point division if the remainder is not 0.
      assembler->GotoIf(assembler->Word32NotEqual(untagged_dividend, truncated),
                        &bailout);
      var_type_feedback.Bind(
          assembler->SmiConstant(BinaryOperationFeedback::kSignedSmall));
      var_result.Bind(assembler->SmiFromWord32(untagged_result));
      assembler->Goto(&end);

      // Bailout: convert {dividend} and {divisor} to double and do double
      // division.
      assembler->Bind(&bailout);
      {
        var_dividend_float64.Bind(assembler->SmiToFloat64(dividend));
        var_divisor_float64.Bind(assembler->SmiToFloat64(divisor));
        assembler->Goto(&do_fdiv);
      }
    }

    assembler->Bind(&divisor_is_not_smi);
    {
      Node* divisor_map = assembler->LoadMap(divisor);

      // Check if {divisor} is a HeapNumber.
      assembler->GotoIfNot(assembler->IsHeapNumberMap(divisor_map),
                           &check_divisor_for_oddball);

      // Convert {dividend} to a double and divide it with the value of
      // {divisor}.
      var_dividend_float64.Bind(assembler->SmiToFloat64(dividend));
      var_divisor_float64.Bind(assembler->LoadHeapNumberValue(divisor));
      assembler->Goto(&do_fdiv);
    }

    assembler->Bind(&dividend_is_not_smi);
    {
      Node* dividend_map = assembler->LoadMap(dividend);

      // Check if {dividend} is a HeapNumber.
      assembler->GotoIfNot(assembler->IsHeapNumberMap(dividend_map),
                           &dividend_is_not_number);

      // Check if {divisor} is a Smi.
      Label divisor_is_smi(assembler), divisor_is_not_smi(assembler);
      assembler->Branch(assembler->TaggedIsSmi(divisor), &divisor_is_smi,
                        &divisor_is_not_smi);

      assembler->Bind(&divisor_is_smi);
      {
        // Convert {divisor} to a double and use it for a floating point
        // division.
        var_dividend_float64.Bind(assembler->LoadHeapNumberValue(dividend));
        var_divisor_float64.Bind(assembler->SmiToFloat64(divisor));
        assembler->Goto(&do_fdiv);
      }

      assembler->Bind(&divisor_is_not_smi);
      {
        Node* divisor_map = assembler->LoadMap(divisor);

        // Check if {divisor} is a HeapNumber.
        assembler->GotoIfNot(assembler->IsHeapNumberMap(divisor_map),
                             &check_divisor_for_oddball);

        // Both {dividend} and {divisor} are HeapNumbers. Load their values
        // and divide them.
        var_dividend_float64.Bind(assembler->LoadHeapNumberValue(dividend));
        var_divisor_float64.Bind(assembler->LoadHeapNumberValue(divisor));
        assembler->Goto(&do_fdiv);
      }
    }
  }

  assembler->Bind(&do_fdiv);
  {
    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kNumber));
    Node* value = assembler->Float64Div(var_dividend_float64.value(),
                                        var_divisor_float64.value());
    var_result.Bind(assembler->AllocateHeapNumberWithValue(value));
    assembler->Goto(&end);
  }

  assembler->Bind(&dividend_is_not_number);
  {
    // We just know dividend is not a number or Smi. No checks on divisor yet.
    // Check if dividend is an oddball.
    Node* dividend_instance_type = assembler->LoadInstanceType(dividend);
    Node* dividend_is_oddball = assembler->Word32Equal(
        dividend_instance_type, assembler->Int32Constant(ODDBALL_TYPE));
    assembler->GotoIfNot(dividend_is_oddball, &call_with_any_feedback);

    assembler->GotoIf(assembler->TaggedIsSmi(divisor),
                      &call_with_oddball_feedback);

    // Load the map of the {divisor}.
    Node* divisor_map = assembler->LoadMap(divisor);

    // Check if {divisor} is a HeapNumber.
    assembler->Branch(assembler->IsHeapNumberMap(divisor_map),
                      &call_with_oddball_feedback, &check_divisor_for_oddball);
  }

  assembler->Bind(&check_divisor_for_oddball);
  {
    // Check if divisor is an oddball. At this point we know dividend is either
    // a Smi or number or oddball and divisor is not a number or Smi.
    Node* divisor_instance_type = assembler->LoadInstanceType(divisor);
    Node* divisor_is_oddball = assembler->Word32Equal(
        divisor_instance_type, assembler->Int32Constant(ODDBALL_TYPE));
    assembler->Branch(divisor_is_oddball, &call_with_oddball_feedback,
                      &call_with_any_feedback);
  }

  assembler->Bind(&call_with_oddball_feedback);
  {
    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
    assembler->Goto(&call_divide_stub);
  }

  assembler->Bind(&call_with_any_feedback);
  {
    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kAny));
    assembler->Goto(&call_divide_stub);
  }

  assembler->Bind(&call_divide_stub);
  {
    Callable callable = CodeFactory::Divide(assembler->isolate());
    var_result.Bind(assembler->CallStub(callable, context, dividend, divisor));
    assembler->Goto(&end);
  }

  assembler->Bind(&end);
  assembler->UpdateFeedback(var_type_feedback.value(), feedback_vector,
                            slot_id);
  return var_result.value();
}

// static
compiler::Node* ModulusWithFeedbackStub::Generate(
    CodeStubAssembler* assembler, compiler::Node* dividend,
    compiler::Node* divisor, compiler::Node* slot_id,
    compiler::Node* feedback_vector, compiler::Node* context) {
  using compiler::Node;
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;

  // Shared entry point for floating point division.
  Label do_fmod(assembler), dividend_is_not_number(assembler, Label::kDeferred),
      check_divisor_for_oddball(assembler, Label::kDeferred),
      call_with_oddball_feedback(assembler), call_with_any_feedback(assembler),
      call_modulus_stub(assembler), end(assembler);
  Variable var_dividend_float64(assembler, MachineRepresentation::kFloat64),
      var_divisor_float64(assembler, MachineRepresentation::kFloat64),
      var_result(assembler, MachineRepresentation::kTagged),
      var_type_feedback(assembler, MachineRepresentation::kTaggedSigned);

  Label dividend_is_smi(assembler), dividend_is_not_smi(assembler);
  assembler->Branch(assembler->TaggedIsSmi(dividend), &dividend_is_smi,
                    &dividend_is_not_smi);

  assembler->Bind(&dividend_is_smi);
  {
    Label divisor_is_smi(assembler), divisor_is_not_smi(assembler);
    assembler->Branch(assembler->TaggedIsSmi(divisor), &divisor_is_smi,
                      &divisor_is_not_smi);

    assembler->Bind(&divisor_is_smi);
    {
      var_result.Bind(assembler->SmiMod(dividend, divisor));
      var_type_feedback.Bind(assembler->SelectSmiConstant(
          assembler->TaggedIsSmi(var_result.value()),
          BinaryOperationFeedback::kSignedSmall,
          BinaryOperationFeedback::kNumber));
      assembler->Goto(&end);
    }

    assembler->Bind(&divisor_is_not_smi);
    {
      Node* divisor_map = assembler->LoadMap(divisor);

      // Check if {divisor} is a HeapNumber.
      assembler->GotoIfNot(assembler->IsHeapNumberMap(divisor_map),
                           &check_divisor_for_oddball);

      // Convert {dividend} to a double and divide it with the value of
      // {divisor}.
      var_dividend_float64.Bind(assembler->SmiToFloat64(dividend));
      var_divisor_float64.Bind(assembler->LoadHeapNumberValue(divisor));
      assembler->Goto(&do_fmod);
    }
  }

  assembler->Bind(&dividend_is_not_smi);
  {
    Node* dividend_map = assembler->LoadMap(dividend);

    // Check if {dividend} is a HeapNumber.
    assembler->GotoIfNot(assembler->IsHeapNumberMap(dividend_map),
                         &dividend_is_not_number);

    // Check if {divisor} is a Smi.
    Label divisor_is_smi(assembler), divisor_is_not_smi(assembler);
    assembler->Branch(assembler->TaggedIsSmi(divisor), &divisor_is_smi,
                      &divisor_is_not_smi);

    assembler->Bind(&divisor_is_smi);
    {
      // Convert {divisor} to a double and use it for a floating point
      // division.
      var_dividend_float64.Bind(assembler->LoadHeapNumberValue(dividend));
      var_divisor_float64.Bind(assembler->SmiToFloat64(divisor));
      assembler->Goto(&do_fmod);
    }

    assembler->Bind(&divisor_is_not_smi);
    {
      Node* divisor_map = assembler->LoadMap(divisor);

      // Check if {divisor} is a HeapNumber.
      assembler->GotoIfNot(assembler->IsHeapNumberMap(divisor_map),
                           &check_divisor_for_oddball);

      // Both {dividend} and {divisor} are HeapNumbers. Load their values
      // and divide them.
      var_dividend_float64.Bind(assembler->LoadHeapNumberValue(dividend));
      var_divisor_float64.Bind(assembler->LoadHeapNumberValue(divisor));
      assembler->Goto(&do_fmod);
    }
  }

  assembler->Bind(&do_fmod);
  {
    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kNumber));
    Node* value = assembler->Float64Mod(var_dividend_float64.value(),
                                        var_divisor_float64.value());
    var_result.Bind(assembler->AllocateHeapNumberWithValue(value));
    assembler->Goto(&end);
  }

  assembler->Bind(&dividend_is_not_number);
  {
    // No checks on divisor yet. We just know dividend is not a number or Smi.
    // Check if dividend is an oddball.
    Node* dividend_instance_type = assembler->LoadInstanceType(dividend);
    Node* dividend_is_oddball = assembler->Word32Equal(
        dividend_instance_type, assembler->Int32Constant(ODDBALL_TYPE));
    assembler->GotoIfNot(dividend_is_oddball, &call_with_any_feedback);

    assembler->GotoIf(assembler->TaggedIsSmi(divisor),
                      &call_with_oddball_feedback);

    // Load the map of the {divisor}.
    Node* divisor_map = assembler->LoadMap(divisor);

    // Check if {divisor} is a HeapNumber.
    assembler->Branch(assembler->IsHeapNumberMap(divisor_map),
                      &call_with_oddball_feedback, &check_divisor_for_oddball);
  }

  assembler->Bind(&check_divisor_for_oddball);
  {
    // Check if divisor is an oddball. At this point we know dividend is either
    // a Smi or number or oddball and divisor is not a number or Smi.
    Node* divisor_instance_type = assembler->LoadInstanceType(divisor);
    Node* divisor_is_oddball = assembler->Word32Equal(
        divisor_instance_type, assembler->Int32Constant(ODDBALL_TYPE));
    assembler->Branch(divisor_is_oddball, &call_with_oddball_feedback,
                      &call_with_any_feedback);
  }

  assembler->Bind(&call_with_oddball_feedback);
  {
    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
    assembler->Goto(&call_modulus_stub);
  }

  assembler->Bind(&call_with_any_feedback);
  {
    var_type_feedback.Bind(
        assembler->SmiConstant(BinaryOperationFeedback::kAny));
    assembler->Goto(&call_modulus_stub);
  }

  assembler->Bind(&call_modulus_stub);
  {
    Callable callable = CodeFactory::Modulus(assembler->isolate());
    var_result.Bind(assembler->CallStub(callable, context, dividend, divisor));
    assembler->Goto(&end);
  }

  assembler->Bind(&end);
  assembler->UpdateFeedback(var_type_feedback.value(), feedback_vector,
                            slot_id);
  return var_result.value();
}

// TODO(ishell): move to builtins.
TF_STUB(NumberToStringStub, CodeStubAssembler) {
  Node* context = Parameter(Descriptor::kContext);
  Node* argument = Parameter(Descriptor::kArgument);
  Return(NumberToString(context, argument));
}

// TODO(ishell): move to builtins.
TF_STUB(SubStringStub, CodeStubAssembler) {
  Node* context = Parameter(Descriptor::kContext);
  Node* string = Parameter(Descriptor::kString);
  Node* from = Parameter(Descriptor::kFrom);
  Node* to = Parameter(Descriptor::kTo);

  Return(SubString(context, string, from, to));
}

// TODO(ishell): move to builtins-handler-gen.
TF_STUB(KeyedLoadSloppyArgumentsStub, CodeStubAssembler) {
  Node* receiver = Parameter(Descriptor::kReceiver);
  Node* key = Parameter(Descriptor::kName);
  Node* slot = Parameter(Descriptor::kSlot);
  Node* vector = Parameter(Descriptor::kVector);
  Node* context = Parameter(Descriptor::kContext);

  Label miss(this);

  Node* result = LoadKeyedSloppyArguments(receiver, key, &miss);
  Return(result);

  Bind(&miss);
  {
    Comment("Miss");
    TailCallRuntime(Runtime::kKeyedLoadIC_Miss, context, receiver, key, slot,
                    vector);
  }
}

// TODO(ishell): move to builtins-handler-gen.
TF_STUB(KeyedStoreSloppyArgumentsStub, CodeStubAssembler) {
  Node* receiver = Parameter(Descriptor::kReceiver);
  Node* key = Parameter(Descriptor::kName);
  Node* value = Parameter(Descriptor::kValue);
  Node* slot = Parameter(Descriptor::kSlot);
  Node* vector = Parameter(Descriptor::kVector);
  Node* context = Parameter(Descriptor::kContext);

  Label miss(this);

  StoreKeyedSloppyArguments(receiver, key, value, &miss);
  Return(value);

  Bind(&miss);
  {
    Comment("Miss");
    TailCallRuntime(Runtime::kKeyedStoreIC_Miss, context, value, slot, vector,
                    receiver, key);
  }
}

TF_STUB(LoadScriptContextFieldStub, CodeStubAssembler) {
  Comment("LoadScriptContextFieldStub: context_index=%d, slot=%d",
          stub->context_index(), stub->slot_index());

  Node* context = Parameter(Descriptor::kContext);

  Node* script_context = LoadScriptContext(context, stub->context_index());
  Node* result = LoadFixedArrayElement(script_context, stub->slot_index());
  Return(result);
}

TF_STUB(StoreScriptContextFieldStub, CodeStubAssembler) {
  Comment("StoreScriptContextFieldStub: context_index=%d, slot=%d",
          stub->context_index(), stub->slot_index());

  Node* value = Parameter(Descriptor::kValue);
  Node* context = Parameter(Descriptor::kContext);

  Node* script_context = LoadScriptContext(context, stub->context_index());
  StoreFixedArrayElement(script_context, IntPtrConstant(stub->slot_index()),
                         value);
  Return(value);
}

// TODO(ishell): move to builtins-handler-gen.
TF_STUB(StoreInterceptorStub, CodeStubAssembler) {
  Node* receiver = Parameter(Descriptor::kReceiver);
  Node* name = Parameter(Descriptor::kName);
  Node* value = Parameter(Descriptor::kValue);
  Node* slot = Parameter(Descriptor::kSlot);
  Node* vector = Parameter(Descriptor::kVector);
  Node* context = Parameter(Descriptor::kContext);
  TailCallRuntime(Runtime::kStorePropertyWithInterceptor, context, value, slot,
                  vector, receiver, name);
}

// TODO(ishell): move to builtins-handler-gen.
TF_STUB(LoadIndexedInterceptorStub, CodeStubAssembler) {
  Node* receiver = Parameter(Descriptor::kReceiver);
  Node* key = Parameter(Descriptor::kName);
  Node* slot = Parameter(Descriptor::kSlot);
  Node* vector = Parameter(Descriptor::kVector);
  Node* context = Parameter(Descriptor::kContext);

  Label if_keyispositivesmi(this), if_keyisinvalid(this);
  Branch(TaggedIsPositiveSmi(key), &if_keyispositivesmi, &if_keyisinvalid);
  Bind(&if_keyispositivesmi);
  TailCallRuntime(Runtime::kLoadElementWithInterceptor, context, receiver, key);

  Bind(&if_keyisinvalid);
  TailCallRuntime(Runtime::kKeyedLoadIC_Miss, context, receiver, key, slot,
                  vector);
}

void CallICStub::PrintState(std::ostream& os) const {  // NOLINT
  os << convert_mode() << ", " << tail_call_mode();
}

// TODO(ishell): Move to CallICAssembler.
TF_STUB(CallICStub, CodeStubAssembler) {
  Node* context = Parameter(Descriptor::kContext);
  Node* target = Parameter(Descriptor::kTarget);
  Node* argc = Parameter(Descriptor::kActualArgumentsCount);
  Node* slot = Parameter(Descriptor::kSlot);
  Node* vector = Parameter(Descriptor::kVector);

  // TODO(bmeurer): The slot should actually be an IntPtr, but TurboFan's
  // SimplifiedLowering cannot deal with IntPtr machine type properly yet.
  slot = ChangeInt32ToIntPtr(slot);

  // Static checks to assert it is safe to examine the type feedback element.
  // We don't know that we have a weak cell. We might have a private symbol
  // or an AllocationSite, but the memory is safe to examine.
  // AllocationSite::kTransitionInfoOffset - contains a Smi or pointer to
  // FixedArray.
  // WeakCell::kValueOffset - contains a JSFunction or Smi(0)
  // Symbol::kHashFieldSlot - if the low bit is 1, then the hash is not
  // computed, meaning that it can't appear to be a pointer. If the low bit is
  // 0, then hash is computed, but the 0 bit prevents the field from appearing
  // to be a pointer.
  STATIC_ASSERT(WeakCell::kSize >= kPointerSize);
  STATIC_ASSERT(AllocationSite::kTransitionInfoOffset ==
                    WeakCell::kValueOffset &&
                WeakCell::kValueOffset == Symbol::kHashFieldSlot);

  // Increment the call count.
  // TODO(bmeurer): Would it be beneficial to use Int32Add on 64-bit?
  Comment("increment call count");
  Node* call_count = LoadFixedArrayElement(vector, slot, 1 * kPointerSize);
  Node* new_count = SmiAdd(call_count, SmiConstant(1));
  // Count is Smi, so we don't need a write barrier.
  StoreFixedArrayElement(vector, slot, new_count, SKIP_WRITE_BARRIER,
                         1 * kPointerSize);

  Label call_function(this), extra_checks(this), call(this);

  // The checks. First, does function match the recorded monomorphic target?
  Node* feedback_element = LoadFixedArrayElement(vector, slot);
  Node* feedback_value = LoadWeakCellValueUnchecked(feedback_element);
  Node* is_monomorphic = WordEqual(target, feedback_value);
  GotoIfNot(is_monomorphic, &extra_checks);

  // The compare above could have been a SMI/SMI comparison. Guard against
  // this convincing us that we have a monomorphic JSFunction.
  Node* is_smi = TaggedIsSmi(target);
  Branch(is_smi, &extra_checks, &call_function);

  Bind(&call_function);
  {
    // Call using CallFunction builtin.
    Callable callable = CodeFactory::CallFunction(
        isolate(), stub->convert_mode(), stub->tail_call_mode());
    TailCallStub(callable, context, target, argc);
  }

  Bind(&extra_checks);
  {
    Label check_initialized(this), mark_megamorphic(this),
        create_allocation_site(this, Label::kDeferred),
        create_weak_cell(this, Label::kDeferred);

    Comment("check if megamorphic");
    // Check if it is a megamorphic target.
    Node* is_megamorphic =
        WordEqual(feedback_element,
                  HeapConstant(FeedbackVector::MegamorphicSentinel(isolate())));
    GotoIf(is_megamorphic, &call);

    Comment("check if it is an allocation site");
    GotoIfNot(IsAllocationSiteMap(LoadMap(feedback_element)),
              &check_initialized);

    // If it is not the Array() function, mark megamorphic.
    Node* context_slot = LoadContextElement(LoadNativeContext(context),
                                            Context::ARRAY_FUNCTION_INDEX);
    Node* is_array_function = WordEqual(context_slot, target);
    GotoIfNot(is_array_function, &mark_megamorphic);

    // Call ArrayConstructorStub.
    Callable callable = CodeFactory::ArrayConstructor(isolate());
    TailCallStub(callable, context, target, target, argc, feedback_element);

    Bind(&check_initialized);
    {
      Comment("check if uninitialized");
      // Check if it is uninitialized target first.
      Node* is_uninitialized = WordEqual(
          feedback_element,
          HeapConstant(FeedbackVector::UninitializedSentinel(isolate())));
      GotoIfNot(is_uninitialized, &mark_megamorphic);

      Comment("handle unitinitialized");
      // If it is not a JSFunction mark it as megamorphic.
      Node* is_smi = TaggedIsSmi(target);
      GotoIf(is_smi, &mark_megamorphic);

      // Check if function is an object of JSFunction type.
      Node* is_js_function = IsJSFunction(target);
      GotoIfNot(is_js_function, &mark_megamorphic);

      // Check if it is the Array() function.
      Node* context_slot = LoadContextElement(LoadNativeContext(context),
                                              Context::ARRAY_FUNCTION_INDEX);
      Node* is_array_function = WordEqual(context_slot, target);
      GotoIf(is_array_function, &create_allocation_site);

      // Check if the function belongs to the same native context.
      Node* native_context = LoadNativeContext(
          LoadObjectField(target, JSFunction::kContextOffset));
      Node* is_same_native_context =
          WordEqual(native_context, LoadNativeContext(context));
      Branch(is_same_native_context, &create_weak_cell, &mark_megamorphic);
    }

    Bind(&create_weak_cell);
    {
      // Wrap the {target} in a WeakCell and remember it.
      Comment("create weak cell");
      CreateWeakCellInFeedbackVector(vector, SmiTag(slot), target);

      // Call using CallFunction builtin.
      Goto(&call_function);
    }

    Bind(&create_allocation_site);
    {
      // Create an AllocationSite for the {target}.
      Comment("create allocation site");
      CreateAllocationSiteInFeedbackVector(vector, SmiTag(slot));

      // Call using CallFunction builtin. CallICs have a PREMONOMORPHIC state.
      // They start collecting feedback only when a call is executed the second
      // time. So, do not pass any feedback here.
      Goto(&call_function);
    }

    Bind(&mark_megamorphic);
    {
      // Mark it as a megamorphic.
      // MegamorphicSentinel is created as a part of Heap::InitialObjects
      // and will not move during a GC. So it is safe to skip write barrier.
      DCHECK(Heap::RootIsImmortalImmovable(Heap::kmegamorphic_symbolRootIndex));
      StoreFixedArrayElement(
          vector, slot,
          HeapConstant(FeedbackVector::MegamorphicSentinel(isolate())),
          SKIP_WRITE_BARRIER);
      Goto(&call);
    }
  }

  Bind(&call);
  {
    // Call using call builtin.
    Comment("call using Call builtin");
    Callable callable_call = CodeFactory::Call(isolate(), stub->convert_mode(),
                                               stub->tail_call_mode());
    TailCallStub(callable_call, context, target, argc);
  }
}

TF_STUB(CallICTrampolineStub, CodeStubAssembler) {
  Node* context = Parameter(Descriptor::kContext);
  Node* target = Parameter(Descriptor::kTarget);
  Node* argc = Parameter(Descriptor::kActualArgumentsCount);
  Node* slot = Parameter(Descriptor::kSlot);
  Node* vector = LoadFeedbackVectorForStub();

  Callable callable = CodeFactory::CallIC(isolate(), stub->convert_mode(),
                                          stub->tail_call_mode());
  TailCallStub(callable, context, target, argc, slot, vector);
}

void JSEntryStub::FinishCode(Handle<Code> code) {
  Handle<FixedArray> handler_table =
      code->GetIsolate()->factory()->NewFixedArray(1, TENURED);
  handler_table->set(0, Smi::FromInt(handler_offset_));
  code->set_handler_table(*handler_table);
}

void TransitionElementsKindStub::InitializeDescriptor(
    CodeStubDescriptor* descriptor) {
  descriptor->Initialize(
      Runtime::FunctionForId(Runtime::kTransitionElementsKind)->entry);
}


void AllocateHeapNumberStub::InitializeDescriptor(
    CodeStubDescriptor* descriptor) {
  descriptor->Initialize(
      Runtime::FunctionForId(Runtime::kAllocateHeapNumber)->entry);
}


void ToBooleanICStub::InitializeDescriptor(CodeStubDescriptor* descriptor) {
  descriptor->Initialize(FUNCTION_ADDR(Runtime_ToBooleanIC_Miss));
  descriptor->SetMissHandler(Runtime::kToBooleanIC_Miss);
}


void BinaryOpICStub::InitializeDescriptor(CodeStubDescriptor* descriptor) {
  descriptor->Initialize(FUNCTION_ADDR(Runtime_BinaryOpIC_Miss));
  descriptor->SetMissHandler(Runtime::kBinaryOpIC_Miss);
}


void BinaryOpWithAllocationSiteStub::InitializeDescriptor(
    CodeStubDescriptor* descriptor) {
  descriptor->Initialize(
      FUNCTION_ADDR(Runtime_BinaryOpIC_MissWithAllocationSite));
}

// TODO(ishell): move to builtins.
TF_STUB(GetPropertyStub, CodeStubAssembler) {
  Label call_runtime(this, Label::kDeferred), return_undefined(this), end(this);

  Node* object = Parameter(Descriptor::kObject);
  Node* key = Parameter(Descriptor::kKey);
  Node* context = Parameter(Descriptor::kContext);
  Variable var_result(this, MachineRepresentation::kTagged);

  CodeStubAssembler::LookupInHolder lookup_property_in_holder =
      [=, &var_result, &end](Node* receiver, Node* holder, Node* holder_map,
                             Node* holder_instance_type, Node* unique_name,
                             Label* next_holder, Label* if_bailout) {
        Variable var_value(this, MachineRepresentation::kTagged);
        Label if_found(this);
        TryGetOwnProperty(context, receiver, holder, holder_map,
                          holder_instance_type, unique_name, &if_found,
                          &var_value, next_holder, if_bailout);
        Bind(&if_found);
        {
          var_result.Bind(var_value.value());
          Goto(&end);
        }
      };

  CodeStubAssembler::LookupInHolder lookup_element_in_holder =
      [=](Node* receiver, Node* holder, Node* holder_map,
          Node* holder_instance_type, Node* index, Label* next_holder,
          Label* if_bailout) {
        // Not supported yet.
        Use(next_holder);
        Goto(if_bailout);
      };

  TryPrototypeChainLookup(object, key, lookup_property_in_holder,
                          lookup_element_in_holder, &return_undefined,
                          &call_runtime);

  Bind(&return_undefined);
  {
    var_result.Bind(UndefinedConstant());
    Goto(&end);
  }

  Bind(&call_runtime);
  {
    var_result.Bind(CallRuntime(Runtime::kGetProperty, context, object, key));
    Goto(&end);
  }

  Bind(&end);
  Return(var_result.value());
}

void CreateAllocationSiteStub::GenerateAheadOfTime(Isolate* isolate) {
  CreateAllocationSiteStub stub(isolate);
  stub.GetCode();
}


void CreateWeakCellStub::GenerateAheadOfTime(Isolate* isolate) {
  CreateWeakCellStub stub(isolate);
  stub.GetCode();
}

// TODO(ishell): move to builtins-handler-gen.
TF_STUB(StoreSlowElementStub, CodeStubAssembler) {
  Node* receiver = Parameter(Descriptor::kReceiver);
  Node* name = Parameter(Descriptor::kName);
  Node* value = Parameter(Descriptor::kValue);
  Node* slot = Parameter(Descriptor::kSlot);
  Node* vector = Parameter(Descriptor::kVector);
  Node* context = Parameter(Descriptor::kContext);

  TailCallRuntime(Runtime::kKeyedStoreIC_Slow, context, value, slot, vector,
                  receiver, name);
}

TF_STUB(StoreFastElementStub, CodeStubAssembler) {
  Comment("StoreFastElementStub: js_array=%d, elements_kind=%s, store_mode=%d",
          stub->is_js_array(), ElementsKindToString(stub->elements_kind()),
          stub->store_mode());

  Node* receiver = Parameter(Descriptor::kReceiver);
  Node* key = Parameter(Descriptor::kName);
  Node* value = Parameter(Descriptor::kValue);
  Node* slot = Parameter(Descriptor::kSlot);
  Node* vector = Parameter(Descriptor::kVector);
  Node* context = Parameter(Descriptor::kContext);

  Label miss(this);

  EmitElementStore(receiver, key, value, stub->is_js_array(),
                   stub->elements_kind(), stub->store_mode(), &miss);
  Return(value);

  Bind(&miss);
  {
    Comment("Miss");
    TailCallRuntime(Runtime::kKeyedStoreIC_Miss, context, value, slot, vector,
                    receiver, key);
  }
}

// static
void StoreFastElementStub::GenerateAheadOfTime(Isolate* isolate) {
  if (FLAG_minimal) return;
  StoreFastElementStub(isolate, false, FAST_HOLEY_ELEMENTS, STANDARD_STORE)
      .GetCode();
  StoreFastElementStub(isolate, false, FAST_HOLEY_ELEMENTS,
                       STORE_AND_GROW_NO_TRANSITION).GetCode();
  for (int i = FIRST_FAST_ELEMENTS_KIND; i <= LAST_FAST_ELEMENTS_KIND; i++) {
    ElementsKind kind = static_cast<ElementsKind>(i);
    StoreFastElementStub(isolate, true, kind, STANDARD_STORE).GetCode();
    StoreFastElementStub(isolate, true, kind, STORE_AND_GROW_NO_TRANSITION)
        .GetCode();
  }
}

bool ToBooleanICStub::UpdateStatus(Handle<Object> object) {
  ToBooleanHints old_hints = hints();
  ToBooleanHints new_hints = old_hints;
  bool to_boolean_value = false;  // Dummy initialization.
  if (object->IsUndefined(isolate())) {
    new_hints |= ToBooleanHint::kUndefined;
    to_boolean_value = false;
  } else if (object->IsBoolean()) {
    new_hints |= ToBooleanHint::kBoolean;
    to_boolean_value = object->IsTrue(isolate());
  } else if (object->IsNull(isolate())) {
    new_hints |= ToBooleanHint::kNull;
    to_boolean_value = false;
  } else if (object->IsSmi()) {
    new_hints |= ToBooleanHint::kSmallInteger;
    to_boolean_value = Smi::cast(*object)->value() != 0;
  } else if (object->IsJSReceiver()) {
    new_hints |= ToBooleanHint::kReceiver;
    to_boolean_value = !object->IsUndetectable();
  } else if (object->IsString()) {
    DCHECK(!object->IsUndetectable());
    new_hints |= ToBooleanHint::kString;
    to_boolean_value = String::cast(*object)->length() != 0;
  } else if (object->IsSymbol()) {
    new_hints |= ToBooleanHint::kSymbol;
    to_boolean_value = true;
  } else if (object->IsHeapNumber()) {
    DCHECK(!object->IsUndetectable());
    new_hints |= ToBooleanHint::kHeapNumber;
    double value = HeapNumber::cast(*object)->value();
    to_boolean_value = value != 0 && !std::isnan(value);
  } else {
    // We should never see an internal object at runtime here!
    UNREACHABLE();
    to_boolean_value = true;
  }

  set_sub_minor_key(HintsBits::update(sub_minor_key(), new_hints));
  return to_boolean_value;
}

void ToBooleanICStub::PrintState(std::ostream& os) const {  // NOLINT
  os << hints();
}

void StubFailureTrampolineStub::GenerateAheadOfTime(Isolate* isolate) {
  StubFailureTrampolineStub stub1(isolate, NOT_JS_FUNCTION_STUB_MODE);
  StubFailureTrampolineStub stub2(isolate, JS_FUNCTION_STUB_MODE);
  stub1.GetCode();
  stub2.GetCode();
}


void ProfileEntryHookStub::EntryHookTrampoline(intptr_t function,
                                               intptr_t stack_pointer,
                                               Isolate* isolate) {
  FunctionEntryHook entry_hook = isolate->function_entry_hook();
  DCHECK(entry_hook != NULL);
  entry_hook(function, stack_pointer);
}

// TODO(ishell): move to builtins.
TF_STUB(CreateAllocationSiteStub, CodeStubAssembler) {
  Return(CreateAllocationSiteInFeedbackVector(Parameter(Descriptor::kVector),
                                              Parameter(Descriptor::kSlot)));
}

// TODO(ishell): move to builtins.
TF_STUB(CreateWeakCellStub, CodeStubAssembler) {
  Return(CreateWeakCellInFeedbackVector(Parameter(Descriptor::kVector),
                                        Parameter(Descriptor::kSlot),
                                        Parameter(Descriptor::kValue)));
}

TF_STUB(ArrayNoArgumentConstructorStub, CodeStubAssembler) {
  ElementsKind elements_kind = stub->elements_kind();
  Node* native_context = LoadObjectField(Parameter(Descriptor::kFunction),
                                         JSFunction::kContextOffset);
  bool track_allocation_site =
      AllocationSite::GetMode(elements_kind) == TRACK_ALLOCATION_SITE &&
      stub->override_mode() != DISABLE_ALLOCATION_SITES;
  Node* allocation_site =
      track_allocation_site ? Parameter(Descriptor::kAllocationSite) : nullptr;
  Node* array_map = LoadJSArrayElementsMap(elements_kind, native_context);
  Node* array =
      AllocateJSArray(elements_kind, array_map,
                      IntPtrConstant(JSArray::kPreallocatedArrayElements),
                      SmiConstant(Smi::kZero), allocation_site);
  Return(array);
}

TF_STUB(InternalArrayNoArgumentConstructorStub, CodeStubAssembler) {
  Node* array_map = LoadObjectField(Parameter(Descriptor::kFunction),
                                    JSFunction::kPrototypeOrInitialMapOffset);
  Node* array =
      AllocateJSArray(stub->elements_kind(), array_map,
                      IntPtrConstant(JSArray::kPreallocatedArrayElements),
                      SmiConstant(Smi::kZero));
  Return(array);
}

namespace {

// TODO(ishell): wrap the code into SingleArgumentConstructorAssembler.
template <typename Descriptor>
void SingleArgumentConstructorCommon(CodeStubAssembler* assembler,
                                     ElementsKind elements_kind,
                                     compiler::Node* array_map,
                                     compiler::Node* allocation_site,
                                     AllocationSiteMode mode) {
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;

  Label ok(assembler);
  Label smi_size(assembler);
  Label small_smi_size(assembler);
  Label call_runtime(assembler, Label::kDeferred);

  Node* size = assembler->Parameter(Descriptor::kArraySizeSmiParameter);
  assembler->Branch(assembler->TaggedIsSmi(size), &smi_size, &call_runtime);

  assembler->Bind(&smi_size);

  if (IsFastPackedElementsKind(elements_kind)) {
    Label abort(assembler, Label::kDeferred);
    assembler->Branch(
        assembler->SmiEqual(size, assembler->SmiConstant(Smi::kZero)),
        &small_smi_size, &abort);

    assembler->Bind(&abort);
    Node* reason =
        assembler->SmiConstant(Smi::FromInt(kAllocatingNonEmptyPackedArray));
    Node* context = assembler->Parameter(Descriptor::kContext);
    assembler->TailCallRuntime(Runtime::kAbort, context, reason);
  } else {
    int element_size =
        IsFastDoubleElementsKind(elements_kind) ? kDoubleSize : kPointerSize;
    int max_fast_elements =
        (kMaxRegularHeapObjectSize - FixedArray::kHeaderSize - JSArray::kSize -
         AllocationMemento::kSize) /
        element_size;
    assembler->Branch(
        assembler->SmiAboveOrEqual(
            size, assembler->SmiConstant(Smi::FromInt(max_fast_elements))),
        &call_runtime, &small_smi_size);
  }

  assembler->Bind(&small_smi_size);
  {
    Node* array = assembler->AllocateJSArray(
        elements_kind, array_map, size, size,
        mode == DONT_TRACK_ALLOCATION_SITE ? nullptr : allocation_site,
        CodeStubAssembler::SMI_PARAMETERS);
    assembler->Return(array);
  }

  assembler->Bind(&call_runtime);
  {
    Node* context = assembler->Parameter(Descriptor::kContext);
    Node* function = assembler->Parameter(Descriptor::kFunction);
    Node* array_size = assembler->Parameter(Descriptor::kArraySizeSmiParameter);
    Node* allocation_site = assembler->Parameter(Descriptor::kAllocationSite);
    assembler->TailCallRuntime(Runtime::kNewArray, context, function,
                               array_size, function, allocation_site);
  }
}
}  // namespace

TF_STUB(ArraySingleArgumentConstructorStub, CodeStubAssembler) {
  ElementsKind elements_kind = stub->elements_kind();
  Node* function = Parameter(Descriptor::kFunction);
  Node* native_context = LoadObjectField(function, JSFunction::kContextOffset);
  Node* array_map = LoadJSArrayElementsMap(elements_kind, native_context);
  AllocationSiteMode mode = stub->override_mode() == DISABLE_ALLOCATION_SITES
                                ? DONT_TRACK_ALLOCATION_SITE
                                : AllocationSite::GetMode(elements_kind);
  Node* allocation_site = Parameter(Descriptor::kAllocationSite);
  // TODO(ishell): pass all parameters explicitly.
  SingleArgumentConstructorCommon<Descriptor>(this, elements_kind, array_map,
                                              allocation_site, mode);
}

TF_STUB(InternalArraySingleArgumentConstructorStub, CodeStubAssembler) {
  Node* function = Parameter(Descriptor::kFunction);
  Node* array_map =
      LoadObjectField(function, JSFunction::kPrototypeOrInitialMapOffset);

  // TODO(ishell): pass all parameters explicitly.
  SingleArgumentConstructorCommon<Descriptor>(this, stub->elements_kind(),
                                              array_map, UndefinedConstant(),
                                              DONT_TRACK_ALLOCATION_SITE);
}

TF_STUB(GrowArrayElementsStub, CodeStubAssembler) {
  Label runtime(this, CodeStubAssembler::Label::kDeferred);

  Node* object = Parameter(Descriptor::kObject);
  Node* key = Parameter(Descriptor::kKey);
  Node* context = Parameter(Descriptor::kContext);
  ElementsKind kind = stub->elements_kind();

  Node* elements = LoadElements(object);
  Node* new_elements =
      TryGrowElementsCapacity(object, elements, kind, key, &runtime);
  Return(new_elements);

  Bind(&runtime);
  // TODO(danno): Make this a tail call when the stub is only used from TurboFan
  // code. This musn't be a tail call for now, since the caller site in lithium
  // creates a safepoint. This safepoint musn't have a different number of
  // arguments on the stack in the case that a GC happens from the slow-case
  // allocation path (zero, since all the stubs inputs are in registers) and
  // when the call happens (it would be two in the tail call case due to the
  // tail call pushing the arguments on the stack for the runtime call). By not
  // tail-calling, the runtime call case also has zero arguments on the stack
  // for the stub frame.
  Return(CallRuntime(Runtime::kGrowArrayElements, context, object, key));
}

ArrayConstructorStub::ArrayConstructorStub(Isolate* isolate)
    : PlatformCodeStub(isolate) {}

InternalArrayConstructorStub::InternalArrayConstructorStub(Isolate* isolate)
    : PlatformCodeStub(isolate) {}

}  // namespace internal
}  // namespace v8
