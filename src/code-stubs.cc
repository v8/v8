// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/code-stubs.h"

#include <sstream>

#include "src/bootstrapper.h"
#include "src/compiler/code-stub-assembler.h"
#include "src/factory.h"
#include "src/gdb-jit.h"
#include "src/ic/handler-compiler.h"
#include "src/ic/ic.h"
#include "src/macro-assembler.h"
#include "src/parsing/parser.h"
#include "src/profiler/cpu-profiler.h"

namespace v8 {
namespace internal {


RUNTIME_FUNCTION(UnexpectedStubMiss) {
  FATAL("Unexpected deopt of a stub");
  return Smi::FromInt(0);
}


CodeStubDescriptor::CodeStubDescriptor(CodeStub* stub)
    : call_descriptor_(stub->GetCallInterfaceDescriptor()),
      stack_parameter_count_(no_reg),
      hint_stack_parameter_count_(-1),
      function_mode_(NOT_JS_FUNCTION_STUB_MODE),
      deoptimization_handler_(NULL),
      miss_handler_(),
      has_miss_handler_(false) {
  stub->InitializeDescriptor(this);
}


CodeStubDescriptor::CodeStubDescriptor(Isolate* isolate, uint32_t stub_key)
    : stack_parameter_count_(no_reg),
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
  int index = stubs->FindEntry(GetKey());
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
          CodeCreateEvent(Logger::STUB_TAG, AbstractCode::cast(*code),
                          os.str().c_str()));
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
  return Code::ComputeFlags(GetCodeKind(), GetICState(), GetExtraICState(),
                            GetStubType());
}


Handle<Code> CodeStub::GetCodeCopy(const Code::FindAndReplacePattern& pattern) {
  Handle<Code> ic = GetCode();
  ic = isolate()->factory()->CopyCode(ic);
  ic->FindAndReplace(pattern);
  RecordCodeGeneration(ic);
  return ic;
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
  Code::Flags flags = Code::ComputeFlags(
      GetCodeKind(),
      GetICState(),
      GetExtraICState(),
      GetStubType());
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
  DCHECK(!NeedsImmovableCode() ||
         heap->lo_space()->Contains(code) ||
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


std::ostream& operator<<(std::ostream& os, const StringAddFlags& flags) {
  switch (flags) {
    case STRING_ADD_CHECK_NONE:
      return os << "CheckNone";
    case STRING_ADD_CHECK_LEFT:
      return os << "CheckLeft";
    case STRING_ADD_CHECK_RIGHT:
      return os << "CheckRight";
    case STRING_ADD_CHECK_BOTH:
      return os << "CheckBoth";
    case STRING_ADD_CONVERT_LEFT:
      return os << "ConvertLeft";
    case STRING_ADD_CONVERT_RIGHT:
      return os << "ConvertRight";
    case STRING_ADD_CONVERT:
      break;
  }
  UNREACHABLE();
  return os;
}


void StringAddStub::PrintBaseName(std::ostream& os) const {  // NOLINT
  os << "StringAddStub_" << flags() << "_" << pretenure_flag();
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


void CompareICStub::AddToSpecialCache(Handle<Code> new_object) {
  DCHECK(*known_map_ != NULL);
  Isolate* isolate = new_object->GetIsolate();
  Factory* factory = isolate->factory();
  return Map::UpdateCodeCache(known_map_,
                              strict() ?
                                  factory->strict_compare_ic_string() :
                                  factory->compare_ic_string(),
                              new_object);
}


bool CompareICStub::FindCodeInSpecialCache(Code** code_out) {
  Factory* factory = isolate()->factory();
  Code::Flags flags = Code::ComputeFlags(
      GetCodeKind(),
      UNINITIALIZED);
  Handle<Object> probe(
      known_map_->FindInCodeCache(
        strict() ?
            *factory->strict_compare_ic_string() :
            *factory->compare_ic_string(),
        flags),
      isolate());
  if (probe->IsCode()) {
    *code_out = Code::cast(*probe);
#ifdef DEBUG
    CompareICStub decode((*code_out)->stub_key(), isolate());
    DCHECK(op() == decode.op());
    DCHECK(left() == decode.left());
    DCHECK(right() == decode.right());
    DCHECK(state() == decode.state());
#endif
    return true;
  }
  return false;
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
  Zone zone;
  CallInterfaceDescriptor descriptor(GetCallInterfaceDescriptor());
  compiler::CodeStubAssembler assembler(isolate(), &zone, descriptor,
                                        GetCodeFlags(), name);
  GenerateAssembly(&assembler);
  return assembler.GenerateCode();
}

void AllocateHeapNumberStub::GenerateAssembly(
    compiler::CodeStubAssembler* assembler) const {
  compiler::Node* result = assembler->Allocate(
      HeapNumber::kSize, compiler::CodeStubAssembler::kNone);
  compiler::Node* map_offset =
      assembler->IntPtrConstant(HeapObject::kMapOffset - kHeapObjectTag);
  compiler::Node* map = assembler->IntPtrAdd(result, map_offset);
  assembler->StoreNoWriteBarrier(
      MachineRepresentation::kTagged, map,
      assembler->HeapConstant(isolate()->factory()->heap_number_map()));
  assembler->Return(result);
}

void AllocateMutableHeapNumberStub::GenerateAssembly(
    compiler::CodeStubAssembler* assembler) const {
  compiler::Node* result = assembler->Allocate(
      HeapNumber::kSize, compiler::CodeStubAssembler::kNone);
  compiler::Node* map_offset =
      assembler->IntPtrConstant(HeapObject::kMapOffset - kHeapObjectTag);
  compiler::Node* map = assembler->IntPtrAdd(result, map_offset);
  assembler->StoreNoWriteBarrier(
      MachineRepresentation::kTagged, map,
      assembler->HeapConstant(isolate()->factory()->mutable_heap_number_map()));
  assembler->Return(result);
}

void StringLengthStub::GenerateAssembly(
    compiler::CodeStubAssembler* assembler) const {
  compiler::Node* value = assembler->Parameter(0);
  compiler::Node* string =
      assembler->LoadObjectField(value, JSValue::kValueOffset);
  compiler::Node* result =
      assembler->LoadObjectField(string, String::kLengthOffset);
  assembler->Return(result);
}

void StrictEqualStub::GenerateAssembly(
    compiler::CodeStubAssembler* assembler) const {
  // Here's pseudo-code for the algorithm below:
  //
  // if (lhs == rhs) {
  //   if (lhs->IsHeapNumber()) return HeapNumber::cast(lhs)->value() != NaN;
  //   return true;
  // }
  // if (!lhs->IsSmi()) {
  //   if (lhs->IsHeapNumber()) {
  //     if (rhs->IsSmi()) {
  //       return Smi::cast(rhs)->value() == HeapNumber::cast(lhs)->value();
  //     } else if (rhs->IsHeapNumber()) {
  //       return HeapNumber::cast(rhs)->value() ==
  //       HeapNumber::cast(lhs)->value();
  //     } else {
  //       return false;
  //     }
  //   } else {
  //     if (rhs->IsSmi()) {
  //       return false;
  //     } else {
  //       if (lhs->IsString()) {
  //         if (rhs->IsString()) {
  //           return %StringEqual(lhs, rhs);
  //         } else {
  //           return false;
  //         }
  //       } else if (lhs->IsSimd128()) {
  //         if (rhs->IsSimd128()) {
  //           return %StrictEqual(lhs, rhs);
  //         }
  //       } else {
  //         return false;
  //       }
  //     }
  //   }
  // } else {
  //   if (rhs->IsSmi()) {
  //     return false;
  //   } else {
  //     if (rhs->IsHeapNumber()) {
  //       return Smi::cast(lhs)->value() == HeapNumber::cast(rhs)->value();
  //     } else {
  //       return false;
  //     }
  //   }
  // }

  typedef compiler::CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Node* lhs = assembler->Parameter(0);
  Node* rhs = assembler->Parameter(1);
  Node* context = assembler->Parameter(2);

  Label if_true(assembler), if_false(assembler);

  // Check if {lhs} and {rhs} refer to the same object.
  Label if_same(assembler), if_notsame(assembler);
  assembler->Branch(assembler->WordEqual(lhs, rhs), &if_same, &if_notsame);

  assembler->Bind(&if_same);
  {
    // The {lhs} and {rhs} reference the exact same value, yet we need special
    // treatment for HeapNumber, as NaN is not equal to NaN.
    // TODO(bmeurer): This seems to violate the SIMD.js specification, but it
    // seems to be what is tested in the current SIMD.js testsuite.

    // Check if {lhs} (and therefore {rhs}) is a Smi or a HeapObject.
    Label if_lhsissmi(assembler), if_lhsisnotsmi(assembler);
    assembler->Branch(assembler->WordIsSmi(lhs), &if_lhsissmi, &if_lhsisnotsmi);

    assembler->Bind(&if_lhsisnotsmi);
    {
      // Load the map of {lhs}.
      Node* lhs_map = assembler->LoadObjectField(lhs, HeapObject::kMapOffset);

      // Check if {lhs} (and therefore {rhs}) is a HeapNumber.
      Node* number_map = assembler->HeapNumberMapConstant();
      Label if_lhsisnumber(assembler), if_lhsisnotnumber(assembler);
      assembler->Branch(assembler->WordEqual(lhs_map, number_map),
                        &if_lhsisnumber, &if_lhsisnotnumber);

      assembler->Bind(&if_lhsisnumber);
      {
        // Convert {lhs} (and therefore {rhs}) to floating point value.
        Node* lhs_value = assembler->LoadHeapNumberValue(lhs);

        // Check if the HeapNumber value is a NaN.
        assembler->BranchIfFloat64IsNaN(lhs_value, &if_false, &if_true);
      }

      assembler->Bind(&if_lhsisnotnumber);
      assembler->Goto(&if_true);
    }

    assembler->Bind(&if_lhsissmi);
    assembler->Goto(&if_true);
  }

  assembler->Bind(&if_notsame);
  {
    // The {lhs} and {rhs} reference different objects, yet for Smi, HeapNumber,
    // String and Simd128Value they can still be considered equal.
    Node* number_map = assembler->HeapNumberMapConstant();

    // Check if {lhs} is a Smi or a HeapObject.
    Label if_lhsissmi(assembler), if_lhsisnotsmi(assembler);
    assembler->Branch(assembler->WordIsSmi(lhs), &if_lhsissmi, &if_lhsisnotsmi);

    assembler->Bind(&if_lhsisnotsmi);
    {
      // Load the map of {lhs}.
      Node* lhs_map = assembler->LoadObjectField(lhs, HeapObject::kMapOffset);

      // Check if {lhs} is a HeapNumber.
      Label if_lhsisnumber(assembler), if_lhsisnotnumber(assembler);
      assembler->Branch(assembler->WordEqual(lhs_map, number_map),
                        &if_lhsisnumber, &if_lhsisnotnumber);

      assembler->Bind(&if_lhsisnumber);
      {
        // Check if {rhs} is a Smi or a HeapObject.
        Label if_rhsissmi(assembler), if_rhsisnotsmi(assembler);
        assembler->Branch(assembler->WordIsSmi(rhs), &if_rhsissmi,
                          &if_rhsisnotsmi);

        assembler->Bind(&if_rhsissmi);
        {
          // Convert {lhs} and {rhs} to floating point values.
          Node* lhs_value = assembler->LoadHeapNumberValue(lhs);
          Node* rhs_value = assembler->SmiToFloat64(rhs);

          // Perform a floating point comparison of {lhs} and {rhs}.
          assembler->BranchIfFloat64Equal(lhs_value, rhs_value, &if_true,
                                          &if_false);
        }

        assembler->Bind(&if_rhsisnotsmi);
        {
          // Load the map of {rhs}.
          Node* rhs_map =
              assembler->LoadObjectField(rhs, HeapObject::kMapOffset);

          // Check if {rhs} is also a HeapNumber.
          Label if_rhsisnumber(assembler), if_rhsisnotnumber(assembler);
          assembler->Branch(assembler->WordEqual(rhs_map, number_map),
                            &if_rhsisnumber, &if_rhsisnotnumber);

          assembler->Bind(&if_rhsisnumber);
          {
            // Convert {lhs} and {rhs} to floating point values.
            Node* lhs_value = assembler->LoadHeapNumberValue(lhs);
            Node* rhs_value = assembler->LoadHeapNumberValue(rhs);

            // Perform a floating point comparison of {lhs} and {rhs}.
            assembler->BranchIfFloat64Equal(lhs_value, rhs_value, &if_true,
                                            &if_false);
          }

          assembler->Bind(&if_rhsisnotnumber);
          assembler->Goto(&if_false);
        }
      }

      assembler->Bind(&if_lhsisnotnumber);
      {
        // Check if {rhs} is a Smi or a HeapObject.
        Label if_rhsissmi(assembler), if_rhsisnotsmi(assembler);
        assembler->Branch(assembler->WordIsSmi(rhs), &if_rhsissmi,
                          &if_rhsisnotsmi);

        assembler->Bind(&if_rhsissmi);
        assembler->Goto(&if_false);

        assembler->Bind(&if_rhsisnotsmi);
        {
          // Load the instance type of {lhs}.
          Node* lhs_instance_type = assembler->LoadMapInstanceType(lhs_map);

          // Check if {lhs} is a String.
          Label if_lhsisstring(assembler), if_lhsisnotstring(assembler);
          assembler->Branch(assembler->Int32LessThan(
                                lhs_instance_type,
                                assembler->Int32Constant(FIRST_NONSTRING_TYPE)),
                            &if_lhsisstring, &if_lhsisnotstring);

          assembler->Bind(&if_lhsisstring);
          {
            // Load the instance type of {rhs}.
            Node* rhs_instance_type = assembler->LoadInstanceType(rhs);

            // Check if {rhs} is also a String.
            Label if_rhsisstring(assembler), if_rhsisnotstring(assembler);
            assembler->Branch(assembler->Int32LessThan(
                                  rhs_instance_type, assembler->Int32Constant(
                                                         FIRST_NONSTRING_TYPE)),
                              &if_rhsisstring, &if_rhsisnotstring);

            assembler->Bind(&if_rhsisstring);
            {
              // TODO(bmeurer): Optimize this further once the StringEqual
              // functionality is available in TurboFan land.
              assembler->TailCallRuntime(Runtime::kStringEqual, context, lhs,
                                         rhs);
            }

            assembler->Bind(&if_rhsisnotstring);
            assembler->Goto(&if_false);
          }

          assembler->Bind(&if_lhsisnotstring);
          {
            // Check if {lhs} is a Simd128Value.
            Label if_lhsissimd128value(assembler),
                if_lhsisnotsimd128value(assembler);
            assembler->Branch(assembler->Word32Equal(
                                  lhs_instance_type,
                                  assembler->Int32Constant(SIMD128_VALUE_TYPE)),
                              &if_lhsissimd128value, &if_lhsisnotsimd128value);

            assembler->Bind(&if_lhsissimd128value);
            {
              // TODO(bmeurer): Inline the Simd128Value equality check.
              assembler->TailCallRuntime(Runtime::kStrictEqual, context, lhs,
                                         rhs);
            }

            assembler->Bind(&if_lhsisnotsimd128value);
            assembler->Goto(&if_false);
          }
        }
      }
    }

    assembler->Bind(&if_lhsissmi);
    {
      // We already know that {lhs} and {rhs} are not reference equal, and {lhs}
      // is a Smi; so {lhs} and {rhs} can only be strictly equal if {rhs} is a
      // HeapNumber with an equal floating point value.

      // Check if {rhs} is a Smi or a HeapObject.
      Label if_rhsissmi(assembler), if_rhsisnotsmi(assembler);
      assembler->Branch(assembler->WordIsSmi(rhs), &if_rhsissmi,
                        &if_rhsisnotsmi);

      assembler->Bind(&if_rhsissmi);
      assembler->Goto(&if_false);

      assembler->Bind(&if_rhsisnotsmi);
      {
        // Load the map of the {rhs}.
        Node* rhs_map = assembler->LoadObjectField(rhs, HeapObject::kMapOffset);

        // The {rhs} could be a HeapNumber with the same value as {lhs}.
        Label if_rhsisnumber(assembler), if_rhsisnotnumber(assembler);
        assembler->Branch(assembler->WordEqual(rhs_map, number_map),
                          &if_rhsisnumber, &if_rhsisnotnumber);

        assembler->Bind(&if_rhsisnumber);
        {
          // Convert {lhs} and {rhs} to floating point values.
          Node* lhs_value = assembler->SmiToFloat64(lhs);
          Node* rhs_value = assembler->LoadHeapNumberValue(rhs);

          // Perform a floating point comparison of {lhs} and {rhs}.
          assembler->BranchIfFloat64Equal(lhs_value, rhs_value, &if_true,
                                          &if_false);
        }

        assembler->Bind(&if_rhsisnotnumber);
        assembler->Goto(&if_false);
      }
    }
  }

  assembler->Bind(&if_true);
  assembler->Return(assembler->BooleanConstant(true));

  assembler->Bind(&if_false);
  assembler->Return(assembler->BooleanConstant(false));
}

void ToBooleanStub::GenerateAssembly(
    compiler::CodeStubAssembler* assembler) const {
  typedef compiler::Node Node;
  typedef compiler::CodeStubAssembler::Label Label;

  Node* value = assembler->Parameter(0);
  Label if_valueissmi(assembler), if_valueisnotsmi(assembler);

  // Check if {value} is a Smi or a HeapObject.
  assembler->Branch(assembler->WordIsSmi(value), &if_valueissmi,
                    &if_valueisnotsmi);

  assembler->Bind(&if_valueissmi);
  {
    // The {value} is a Smi, only need to check against zero.
    Label if_valueiszero(assembler), if_valueisnotzero(assembler);
    assembler->Branch(assembler->SmiEqual(value, assembler->SmiConstant(0)),
                      &if_valueiszero, &if_valueisnotzero);

    assembler->Bind(&if_valueiszero);
    assembler->Return(assembler->BooleanConstant(false));

    assembler->Bind(&if_valueisnotzero);
    assembler->Return(assembler->BooleanConstant(true));
  }

  assembler->Bind(&if_valueisnotsmi);
  {
    Label if_valueisstring(assembler), if_valueisheapnumber(assembler),
        if_valueisoddball(assembler), if_valueisother(assembler);

    // The {value} is a HeapObject, load its map.
    Node* value_map = assembler->LoadObjectField(value, HeapObject::kMapOffset);

    // Load the {value}s instance type.
    Node* value_instancetype = assembler->Load(
        MachineType::Uint8(), value_map,
        assembler->IntPtrConstant(Map::kInstanceTypeOffset - kHeapObjectTag));

    // Dispatch based on the instance type; we distinguish all String instance
    // types, the HeapNumber type and the Oddball type.
    size_t const kNumCases = FIRST_NONSTRING_TYPE + 2;
    Label* case_labels[kNumCases];
    int32_t case_values[kNumCases];
    for (int32_t i = 0; i < FIRST_NONSTRING_TYPE; ++i) {
      case_labels[i] = new Label(assembler);
      case_values[i] = i;
    }
    case_labels[FIRST_NONSTRING_TYPE + 0] = &if_valueisheapnumber;
    case_values[FIRST_NONSTRING_TYPE + 0] = HEAP_NUMBER_TYPE;
    case_labels[FIRST_NONSTRING_TYPE + 1] = &if_valueisoddball;
    case_values[FIRST_NONSTRING_TYPE + 1] = ODDBALL_TYPE;
    assembler->Switch(value_instancetype, &if_valueisother, case_values,
                      case_labels, arraysize(case_values));
    for (int32_t i = 0; i < FIRST_NONSTRING_TYPE; ++i) {
      assembler->Bind(case_labels[i]);
      assembler->Goto(&if_valueisstring);
      delete case_labels[i];
    }

    assembler->Bind(&if_valueisstring);
    {
      // Load the string length field of the {value}.
      Node* value_length =
          assembler->LoadObjectField(value, String::kLengthOffset);

      // Check if the {value} is the empty string.
      Label if_valueisempty(assembler), if_valueisnotempty(assembler);
      assembler->Branch(
          assembler->SmiEqual(value_length, assembler->SmiConstant(0)),
          &if_valueisempty, &if_valueisnotempty);

      assembler->Bind(&if_valueisempty);
      assembler->Return(assembler->BooleanConstant(false));

      assembler->Bind(&if_valueisnotempty);
      assembler->Return(assembler->BooleanConstant(true));
    }

    assembler->Bind(&if_valueisheapnumber);
    {
      Node* value_value = assembler->Load(
          MachineType::Float64(), value,
          assembler->IntPtrConstant(HeapNumber::kValueOffset - kHeapObjectTag));

      Label if_valueispositive(assembler), if_valueisnotpositive(assembler),
          if_valueisnegative(assembler), if_valueisnanorzero(assembler);
      assembler->Branch(assembler->Float64LessThan(
                            assembler->Float64Constant(0.0), value_value),
                        &if_valueispositive, &if_valueisnotpositive);

      assembler->Bind(&if_valueispositive);
      assembler->Return(assembler->BooleanConstant(true));

      assembler->Bind(&if_valueisnotpositive);
      assembler->Branch(assembler->Float64LessThan(
                            value_value, assembler->Float64Constant(0.0)),
                        &if_valueisnegative, &if_valueisnanorzero);

      assembler->Bind(&if_valueisnegative);
      assembler->Return(assembler->BooleanConstant(true));

      assembler->Bind(&if_valueisnanorzero);
      assembler->Return(assembler->BooleanConstant(false));
    }

    assembler->Bind(&if_valueisoddball);
    {
      // The {value} is an Oddball, and every Oddball knows its boolean value.
      Node* value_toboolean =
          assembler->LoadObjectField(value, Oddball::kToBooleanOffset);
      assembler->Return(value_toboolean);
    }

    assembler->Bind(&if_valueisother);
    {
      Node* value_map_bitfield = assembler->Load(
          MachineType::Uint8(), value_map,
          assembler->IntPtrConstant(Map::kBitFieldOffset - kHeapObjectTag));
      Node* value_map_undetectable = assembler->Word32And(
          value_map_bitfield,
          assembler->Int32Constant(1 << Map::kIsUndetectable));

      // Check if the {value} is undetectable.
      Label if_valueisundetectable(assembler),
          if_valueisnotundetectable(assembler);
      assembler->Branch(assembler->Word32Equal(value_map_undetectable,
                                               assembler->Int32Constant(0)),
                        &if_valueisnotundetectable, &if_valueisundetectable);

      assembler->Bind(&if_valueisundetectable);
      assembler->Return(assembler->BooleanConstant(false));

      assembler->Bind(&if_valueisnotundetectable);
      assembler->Return(assembler->BooleanConstant(true));
    }
  }
}

template<class StateType>
void HydrogenCodeStub::TraceTransition(StateType from, StateType to) {
  // Note: Although a no-op transition is semantically OK, it is hinting at a
  // bug somewhere in our state transition machinery.
  DCHECK(from != to);
  if (!FLAG_trace_ic) return;
  OFStream os(stdout);
  os << "[";
  PrintBaseName(os);
  os << ": " << from << "=>" << to << "]" << std::endl;
}


// TODO(svenpanne) Make this a real infix_ostream_iterator.
class SimpleListPrinter {
 public:
  explicit SimpleListPrinter(std::ostream& os) : os_(os), first_(true) {}

  void Add(const char* s) {
    if (first_) {
      first_ = false;
    } else {
      os_ << ",";
    }
    os_ << s;
  }

 private:
  std::ostream& os_;
  bool first_;
};


void CallICStub::PrintState(std::ostream& os) const {  // NOLINT
  os << state();
}


void JSEntryStub::FinishCode(Handle<Code> code) {
  Handle<FixedArray> handler_table =
      code->GetIsolate()->factory()->NewFixedArray(1, TENURED);
  handler_table->set(0, Smi::FromInt(handler_offset_));
  code->set_handler_table(*handler_table);
}


void LoadDictionaryElementStub::InitializeDescriptor(
    CodeStubDescriptor* descriptor) {
  descriptor->Initialize(
      FUNCTION_ADDR(Runtime_KeyedLoadIC_MissFromStubFailure));
}


void KeyedLoadGenericStub::InitializeDescriptor(
    CodeStubDescriptor* descriptor) {
  descriptor->Initialize(
      Runtime::FunctionForId(Runtime::kKeyedGetProperty)->entry);
}


void HandlerStub::InitializeDescriptor(CodeStubDescriptor* descriptor) {
  if (kind() == Code::STORE_IC) {
    descriptor->Initialize(FUNCTION_ADDR(Runtime_StoreIC_MissFromStubFailure));
  } else if (kind() == Code::KEYED_LOAD_IC) {
    descriptor->Initialize(
        FUNCTION_ADDR(Runtime_KeyedLoadIC_MissFromStubFailure));
  } else if (kind() == Code::KEYED_STORE_IC) {
    descriptor->Initialize(
        FUNCTION_ADDR(Runtime_KeyedStoreIC_MissFromStubFailure));
  }
}


CallInterfaceDescriptor HandlerStub::GetCallInterfaceDescriptor() const {
  if (kind() == Code::LOAD_IC || kind() == Code::KEYED_LOAD_IC) {
    return LoadWithVectorDescriptor(isolate());
  } else {
    DCHECK(kind() == Code::STORE_IC || kind() == Code::KEYED_STORE_IC);
    return VectorStoreICDescriptor(isolate());
  }
}


void StoreFastElementStub::InitializeDescriptor(
    CodeStubDescriptor* descriptor) {
  descriptor->Initialize(
      FUNCTION_ADDR(Runtime_KeyedStoreIC_MissFromStubFailure));
}


void ElementsTransitionAndStoreStub::InitializeDescriptor(
    CodeStubDescriptor* descriptor) {
  descriptor->Initialize(
      FUNCTION_ADDR(Runtime_ElementsTransitionAndStoreIC_Miss));
}


void ToObjectStub::InitializeDescriptor(CodeStubDescriptor* descriptor) {
  descriptor->Initialize(Runtime::FunctionForId(Runtime::kToObject)->entry);
}


CallInterfaceDescriptor StoreTransitionStub::GetCallInterfaceDescriptor()
    const {
  return VectorStoreTransitionDescriptor(isolate());
}


CallInterfaceDescriptor
ElementsTransitionAndStoreStub::GetCallInterfaceDescriptor() const {
  return VectorStoreTransitionDescriptor(isolate());
}


void FastNewClosureStub::InitializeDescriptor(CodeStubDescriptor* descriptor) {
  descriptor->Initialize(Runtime::FunctionForId(Runtime::kNewClosure)->entry);
}


void FastNewContextStub::InitializeDescriptor(CodeStubDescriptor* d) {}


void TypeofStub::InitializeDescriptor(CodeStubDescriptor* descriptor) {}


void NumberToStringStub::InitializeDescriptor(CodeStubDescriptor* descriptor) {
  NumberToStringDescriptor call_descriptor(isolate());
  descriptor->Initialize(
      Runtime::FunctionForId(Runtime::kNumberToString)->entry);
}


void FastCloneRegExpStub::InitializeDescriptor(CodeStubDescriptor* descriptor) {
  FastCloneRegExpDescriptor call_descriptor(isolate());
  descriptor->Initialize(
      Runtime::FunctionForId(Runtime::kCreateRegExpLiteral)->entry);
}


void FastCloneShallowArrayStub::InitializeDescriptor(
    CodeStubDescriptor* descriptor) {
  FastCloneShallowArrayDescriptor call_descriptor(isolate());
  descriptor->Initialize(
      Runtime::FunctionForId(Runtime::kCreateArrayLiteralStubBailout)->entry);
}


void FastCloneShallowObjectStub::InitializeDescriptor(
    CodeStubDescriptor* descriptor) {
  FastCloneShallowObjectDescriptor call_descriptor(isolate());
  descriptor->Initialize(
      Runtime::FunctionForId(Runtime::kCreateObjectLiteral)->entry);
}


void CreateAllocationSiteStub::InitializeDescriptor(CodeStubDescriptor* d) {}


void CreateWeakCellStub::InitializeDescriptor(CodeStubDescriptor* d) {}


void RegExpConstructResultStub::InitializeDescriptor(
    CodeStubDescriptor* descriptor) {
  descriptor->Initialize(
      Runtime::FunctionForId(Runtime::kRegExpConstructResult)->entry);
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


void AllocateMutableHeapNumberStub::InitializeDescriptor(
    CodeStubDescriptor* descriptor) {
  descriptor->Initialize();
}


void AllocateInNewSpaceStub::InitializeDescriptor(
    CodeStubDescriptor* descriptor) {
  descriptor->Initialize();
}

void ToBooleanICStub::InitializeDescriptor(CodeStubDescriptor* descriptor) {
  descriptor->Initialize(FUNCTION_ADDR(Runtime_ToBooleanIC_Miss));
  descriptor->SetMissHandler(ExternalReference(
      Runtime::FunctionForId(Runtime::kToBooleanIC_Miss), isolate()));
}


void BinaryOpICStub::InitializeDescriptor(CodeStubDescriptor* descriptor) {
  descriptor->Initialize(FUNCTION_ADDR(Runtime_BinaryOpIC_Miss));
  descriptor->SetMissHandler(ExternalReference(
      Runtime::FunctionForId(Runtime::kBinaryOpIC_Miss), isolate()));
}


void BinaryOpWithAllocationSiteStub::InitializeDescriptor(
    CodeStubDescriptor* descriptor) {
  descriptor->Initialize(
      FUNCTION_ADDR(Runtime_BinaryOpIC_MissWithAllocationSite));
}


void StringAddStub::InitializeDescriptor(CodeStubDescriptor* descriptor) {
  descriptor->Initialize(Runtime::FunctionForId(Runtime::kStringAdd)->entry);
}


void GrowArrayElementsStub::InitializeDescriptor(
    CodeStubDescriptor* descriptor) {
  descriptor->Initialize(
      Runtime::FunctionForId(Runtime::kGrowArrayElements)->entry);
}


void TypeofStub::GenerateAheadOfTime(Isolate* isolate) {
  TypeofStub stub(isolate);
  stub.GetCode();
}


void CreateAllocationSiteStub::GenerateAheadOfTime(Isolate* isolate) {
  CreateAllocationSiteStub stub(isolate);
  stub.GetCode();
}


void CreateWeakCellStub::GenerateAheadOfTime(Isolate* isolate) {
  CreateWeakCellStub stub(isolate);
  stub.GetCode();
}


void StoreElementStub::Generate(MacroAssembler* masm) {
  DCHECK_EQ(DICTIONARY_ELEMENTS, elements_kind());
  ElementHandlerCompiler::GenerateStoreSlow(masm);
}


// static
void StoreFastElementStub::GenerateAheadOfTime(Isolate* isolate) {
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


void ArrayConstructorStub::PrintName(std::ostream& os) const {  // NOLINT
  os << "ArrayConstructorStub";
  switch (argument_count()) {
    case ANY:
      os << "_Any";
      break;
    case NONE:
      os << "_None";
      break;
    case ONE:
      os << "_One";
      break;
    case MORE_THAN_ONE:
      os << "_More_Than_One";
      break;
  }
  return;
}


std::ostream& ArrayConstructorStubBase::BasePrintName(
    std::ostream& os,  // NOLINT
    const char* name) const {
  os << name << "_" << ElementsKindToString(elements_kind());
  if (override_mode() == DISABLE_ALLOCATION_SITES) {
    os << "_DISABLE_ALLOCATION_SITES";
  }
  return os;
}

bool ToBooleanICStub::UpdateStatus(Handle<Object> object) {
  Types new_types = types();
  Types old_types = new_types;
  bool to_boolean_value = new_types.UpdateStatus(object);
  TraceTransition(old_types, new_types);
  set_sub_minor_key(TypesBits::update(sub_minor_key(), new_types.ToIntegral()));
  return to_boolean_value;
}

void ToBooleanICStub::PrintState(std::ostream& os) const {  // NOLINT
  os << types();
}

std::ostream& operator<<(std::ostream& os, const ToBooleanICStub::Types& s) {
  os << "(";
  SimpleListPrinter p(os);
  if (s.IsEmpty()) p.Add("None");
  if (s.Contains(ToBooleanICStub::UNDEFINED)) p.Add("Undefined");
  if (s.Contains(ToBooleanICStub::BOOLEAN)) p.Add("Bool");
  if (s.Contains(ToBooleanICStub::NULL_TYPE)) p.Add("Null");
  if (s.Contains(ToBooleanICStub::SMI)) p.Add("Smi");
  if (s.Contains(ToBooleanICStub::SPEC_OBJECT)) p.Add("SpecObject");
  if (s.Contains(ToBooleanICStub::STRING)) p.Add("String");
  if (s.Contains(ToBooleanICStub::SYMBOL)) p.Add("Symbol");
  if (s.Contains(ToBooleanICStub::HEAP_NUMBER)) p.Add("HeapNumber");
  if (s.Contains(ToBooleanICStub::SIMD_VALUE)) p.Add("SimdValue");
  return os << ")";
}

bool ToBooleanICStub::Types::UpdateStatus(Handle<Object> object) {
  if (object->IsUndefined()) {
    Add(UNDEFINED);
    return false;
  } else if (object->IsBoolean()) {
    Add(BOOLEAN);
    return object->IsTrue();
  } else if (object->IsNull()) {
    Add(NULL_TYPE);
    return false;
  } else if (object->IsSmi()) {
    Add(SMI);
    return Smi::cast(*object)->value() != 0;
  } else if (object->IsJSReceiver()) {
    Add(SPEC_OBJECT);
    return !object->IsUndetectableObject();
  } else if (object->IsString()) {
    DCHECK(!object->IsUndetectableObject());
    Add(STRING);
    return String::cast(*object)->length() != 0;
  } else if (object->IsSymbol()) {
    Add(SYMBOL);
    return true;
  } else if (object->IsHeapNumber()) {
    DCHECK(!object->IsUndetectableObject());
    Add(HEAP_NUMBER);
    double value = HeapNumber::cast(*object)->value();
    return value != 0 && !std::isnan(value);
  } else if (object->IsSimd128Value()) {
    Add(SIMD_VALUE);
    return true;
  } else {
    // We should never see an internal object at runtime here!
    UNREACHABLE();
    return true;
  }
}

bool ToBooleanICStub::Types::NeedsMap() const {
  return Contains(ToBooleanICStub::SPEC_OBJECT) ||
         Contains(ToBooleanICStub::STRING) ||
         Contains(ToBooleanICStub::SYMBOL) ||
         Contains(ToBooleanICStub::HEAP_NUMBER) ||
         Contains(ToBooleanICStub::SIMD_VALUE);
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


ArrayConstructorStub::ArrayConstructorStub(Isolate* isolate)
    : PlatformCodeStub(isolate) {
  minor_key_ = ArgumentCountBits::encode(ANY);
  ArrayConstructorStubBase::GenerateStubsAheadOfTime(isolate);
}


ArrayConstructorStub::ArrayConstructorStub(Isolate* isolate,
                                           int argument_count)
    : PlatformCodeStub(isolate) {
  if (argument_count == 0) {
    minor_key_ = ArgumentCountBits::encode(NONE);
  } else if (argument_count == 1) {
    minor_key_ = ArgumentCountBits::encode(ONE);
  } else if (argument_count >= 2) {
    minor_key_ = ArgumentCountBits::encode(MORE_THAN_ONE);
  } else {
    UNREACHABLE();
  }
  ArrayConstructorStubBase::GenerateStubsAheadOfTime(isolate);
}


InternalArrayConstructorStub::InternalArrayConstructorStub(
    Isolate* isolate) : PlatformCodeStub(isolate) {
  InternalArrayConstructorStubBase::GenerateStubsAheadOfTime(isolate);
}


Representation RepresentationFromType(Type* type) {
  if (type->Is(Type::UntaggedIntegral())) {
    return Representation::Integer32();
  }

  if (type->Is(Type::TaggedSigned())) {
    return Representation::Smi();
  }

  if (type->Is(Type::UntaggedPointer())) {
    return Representation::External();
  }

  DCHECK(!type->Is(Type::Untagged()));
  return Representation::Tagged();
}

}  // namespace internal
}  // namespace v8
