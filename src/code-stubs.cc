// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/bootstrapper.h"
#include "src/code-stubs.h"
#include "src/cpu-profiler.h"
#include "src/factory.h"
#include "src/gdb-jit.h"
#include "src/macro-assembler.h"
#include "src/stub-cache.h"

namespace v8 {
namespace internal {


CodeStubInterfaceDescriptor::CodeStubInterfaceDescriptor()
    : register_param_count_(-1),
      stack_parameter_count_(no_reg),
      hint_stack_parameter_count_(-1),
      function_mode_(NOT_JS_FUNCTION_STUB_MODE),
      deoptimization_handler_(NULL),
      handler_arguments_mode_(DONT_PASS_ARGUMENTS),
      miss_handler_(),
      has_miss_handler_(false) { }


void CodeStubInterfaceDescriptor::Initialize(
    int register_parameter_count,
    Register* registers,
    Address deoptimization_handler,
    Representation* register_param_representations,
    int hint_stack_parameter_count,
    StubFunctionMode function_mode) {
  // CodeStubInterfaceDescriptor owns a copy of the registers array.
  register_param_count_ = register_parameter_count;
  register_params_.Reset(NewArray<Register>(register_parameter_count));
  for (int i = 0; i < register_parameter_count; i++) {
    register_params_[i] = registers[i];
  }

  // If a representations array is specified, then the descriptor owns that as
  // well.
  if (register_param_representations != NULL) {
    register_param_representations_.Reset(
        NewArray<Representation>(register_parameter_count));
    for (int i = 0; i < register_parameter_count; i++) {
      register_param_representations_[i] = register_param_representations[i];
    }
  }

  deoptimization_handler_ = deoptimization_handler;

  hint_stack_parameter_count_ = hint_stack_parameter_count;
  function_mode_ = function_mode;
}


void CodeStubInterfaceDescriptor::Initialize(
    int register_parameter_count,
    Register* registers,
    Register stack_parameter_count,
    Address deoptimization_handler,
    Representation* register_param_representations,
    int hint_stack_parameter_count,
    StubFunctionMode function_mode,
    HandlerArgumentsMode handler_mode) {
  Initialize(register_parameter_count, registers,
             deoptimization_handler,
             register_param_representations,
             hint_stack_parameter_count,
             function_mode);
  stack_parameter_count_ = stack_parameter_count;
  handler_arguments_mode_ = handler_mode;
}


void CallInterfaceDescriptor::Initialize(
    int register_parameter_count,
    Register* registers,
    Representation* param_representations,
    PlatformCallInterfaceDescriptor* platform_descriptor) {
  // CallInterfaceDescriptor owns a copy of the registers array.
  register_param_count_ = register_parameter_count;
  register_params_.Reset(NewArray<Register>(register_parameter_count));
  for (int i = 0; i < register_parameter_count; i++) {
    register_params_[i] = registers[i];
  }

  // Also the register parameter representations.
  param_representations_.Reset(
      NewArray<Representation>(register_parameter_count));
  for (int i = 0; i < register_parameter_count; i++) {
    param_representations_[i] = param_representations[i];
  }

  platform_specific_descriptor_ = platform_descriptor;
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
  IC::RegisterWeakMapDependency(code);
  OStringStream os;
  os << *this;
  PROFILE(isolate(), CodeCreateEvent(Logger::STUB_TAG, *code, os.c_str()));
  GDBJIT(AddCode(GDBJITInterface::STUB, os.c_str(), *code));
  Counters* counters = isolate()->counters();
  counters->total_stubs_code_size()->Increment(code->instruction_size());
}


Code::Kind CodeStub::GetCodeKind() const {
  return Code::STUB;
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
  MacroAssembler masm(isolate(), NULL, 256);

  {
    // Update the static counter each time a new code stub is generated.
    isolate()->counters()->code_stubs()->Increment();

    // Generate the code for the stub.
    masm.set_generating_stub(true);
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
  if (UseSpecialCache()
      ? FindCodeInSpecialCache(&code)
      : FindCodeInCache(&code)) {
    ASSERT(GetCodeKind() == code->kind());
    return Handle<Code>(code);
  }

  {
    HandleScope scope(isolate());

    Handle<Code> new_object = GenerateCode();
    new_object->set_major_key(MajorKey());
    FinishCode(new_object);
    RecordCodeGeneration(new_object);

#ifdef ENABLE_DISASSEMBLER
    if (FLAG_print_code_stubs) {
      CodeTracer::Scope trace_scope(isolate()->GetCodeTracer());
      OFStream os(trace_scope.file());
      OStringStream name;
      name << *this;
      new_object->Disassemble(name.c_str(), os);
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
      heap->public_set_code_stubs(*dict);
    }
    code = *new_object;
  }

  Activate(code);
  ASSERT(!NeedsImmovableCode() ||
         heap->lo_space()->Contains(code) ||
         heap->code_space()->FirstPage()->Contains(code->address()));
  return Handle<Code>(code, isolate());
}


const char* CodeStub::MajorName(CodeStub::Major major_key,
                                bool allow_unknown_keys) {
  switch (major_key) {
#define DEF_CASE(name) case name: return #name "Stub";
    CODE_STUB_LIST(DEF_CASE)
#undef DEF_CASE
    case UninitializedMajorKey: return "<UninitializedMajorKey>Stub";
    default:
      if (!allow_unknown_keys) {
        UNREACHABLE();
      }
      return NULL;
  }
}


void CodeStub::PrintBaseName(OStream& os) const {  // NOLINT
  os << MajorName(MajorKey(), false);
}


void CodeStub::PrintName(OStream& os) const {  // NOLINT
  PrintBaseName(os);
  PrintState(os);
}


// static
void BinaryOpICStub::GenerateAheadOfTime(Isolate* isolate) {
  // Generate the uninitialized versions of the stub.
  for (int op = Token::BIT_OR; op <= Token::MOD; ++op) {
    for (int mode = NO_OVERWRITE; mode <= OVERWRITE_RIGHT; ++mode) {
      BinaryOpICStub stub(isolate,
                          static_cast<Token::Value>(op),
                          static_cast<OverwriteMode>(mode));
      stub.GetCode();
    }
  }

  // Generate special versions of the stub.
  BinaryOpIC::State::GenerateAheadOfTime(isolate, &GenerateAheadOfTime);
}


void BinaryOpICStub::PrintState(OStream& os) const {  // NOLINT
  os << state_;
}


// static
void BinaryOpICStub::GenerateAheadOfTime(Isolate* isolate,
                                         const BinaryOpIC::State& state) {
  BinaryOpICStub stub(isolate, state);
  stub.GetCode();
}


// static
void BinaryOpICWithAllocationSiteStub::GenerateAheadOfTime(Isolate* isolate) {
  // Generate special versions of the stub.
  BinaryOpIC::State::GenerateAheadOfTime(isolate, &GenerateAheadOfTime);
}


void BinaryOpICWithAllocationSiteStub::PrintState(
    OStream& os) const {  // NOLINT
  os << state_;
}


// static
void BinaryOpICWithAllocationSiteStub::GenerateAheadOfTime(
    Isolate* isolate, const BinaryOpIC::State& state) {
  if (state.CouldCreateAllocationMementos()) {
    BinaryOpICWithAllocationSiteStub stub(isolate, state);
    stub.GetCode();
  }
}


void StringAddStub::PrintBaseName(OStream& os) const {  // NOLINT
  os << "StringAddStub";
  if ((flags() & STRING_ADD_CHECK_BOTH) == STRING_ADD_CHECK_BOTH) {
    os << "_CheckBoth";
  } else if ((flags() & STRING_ADD_CHECK_LEFT) == STRING_ADD_CHECK_LEFT) {
    os << "_CheckLeft";
  } else if ((flags() & STRING_ADD_CHECK_RIGHT) == STRING_ADD_CHECK_RIGHT) {
    os << "_CheckRight";
  }
  if (pretenure_flag() == TENURED) {
    os << "_Tenured";
  }
}


InlineCacheState ICCompareStub::GetICState() {
  CompareIC::State state = Max(left_, right_);
  switch (state) {
    case CompareIC::UNINITIALIZED:
      return ::v8::internal::UNINITIALIZED;
    case CompareIC::SMI:
    case CompareIC::NUMBER:
    case CompareIC::INTERNALIZED_STRING:
    case CompareIC::STRING:
    case CompareIC::UNIQUE_NAME:
    case CompareIC::OBJECT:
    case CompareIC::KNOWN_OBJECT:
      return MONOMORPHIC;
    case CompareIC::GENERIC:
      return ::v8::internal::GENERIC;
  }
  UNREACHABLE();
  return ::v8::internal::UNINITIALIZED;
}


void ICCompareStub::AddToSpecialCache(Handle<Code> new_object) {
  ASSERT(*known_map_ != NULL);
  Isolate* isolate = new_object->GetIsolate();
  Factory* factory = isolate->factory();
  return Map::UpdateCodeCache(known_map_,
                              strict() ?
                                  factory->strict_compare_ic_string() :
                                  factory->compare_ic_string(),
                              new_object);
}


bool ICCompareStub::FindCodeInSpecialCache(Code** code_out) {
  Factory* factory = isolate()->factory();
  Code::Flags flags = Code::ComputeFlags(
      GetCodeKind(),
      UNINITIALIZED);
  ASSERT(op_ == Token::EQ || op_ == Token::EQ_STRICT);
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
    Token::Value cached_op;
    ICCompareStub::DecodeMinorKey((*code_out)->stub_info(), NULL, NULL, NULL,
                                  &cached_op);
    ASSERT(op_ == cached_op);
#endif
    return true;
  }
  return false;
}


int ICCompareStub::MinorKey() const {
  return OpField::encode(op_ - Token::EQ) |
         LeftStateField::encode(left_) |
         RightStateField::encode(right_) |
         HandlerStateField::encode(state_);
}


void ICCompareStub::DecodeMinorKey(int minor_key,
                                   CompareIC::State* left_state,
                                   CompareIC::State* right_state,
                                   CompareIC::State* handler_state,
                                   Token::Value* op) {
  if (left_state) {
    *left_state =
        static_cast<CompareIC::State>(LeftStateField::decode(minor_key));
  }
  if (right_state) {
    *right_state =
        static_cast<CompareIC::State>(RightStateField::decode(minor_key));
  }
  if (handler_state) {
    *handler_state =
        static_cast<CompareIC::State>(HandlerStateField::decode(minor_key));
  }
  if (op) {
    *op = static_cast<Token::Value>(OpField::decode(minor_key) + Token::EQ);
  }
}


void ICCompareStub::Generate(MacroAssembler* masm) {
  switch (state_) {
    case CompareIC::UNINITIALIZED:
      GenerateMiss(masm);
      break;
    case CompareIC::SMI:
      GenerateSmis(masm);
      break;
    case CompareIC::NUMBER:
      GenerateNumbers(masm);
      break;
    case CompareIC::STRING:
      GenerateStrings(masm);
      break;
    case CompareIC::INTERNALIZED_STRING:
      GenerateInternalizedStrings(masm);
      break;
    case CompareIC::UNIQUE_NAME:
      GenerateUniqueNames(masm);
      break;
    case CompareIC::OBJECT:
      GenerateObjects(masm);
      break;
    case CompareIC::KNOWN_OBJECT:
      ASSERT(*known_map_ != NULL);
      GenerateKnownObjects(masm);
      break;
    case CompareIC::GENERIC:
      GenerateGeneric(masm);
      break;
  }
}


void CompareNilICStub::UpdateStatus(Handle<Object> object) {
  ASSERT(!state_.Contains(GENERIC));
  State old_state(state_);
  if (object->IsNull()) {
    state_.Add(NULL_TYPE);
  } else if (object->IsUndefined()) {
    state_.Add(UNDEFINED);
  } else if (object->IsUndetectableObject() ||
             object->IsOddball() ||
             !object->IsHeapObject()) {
    state_.RemoveAll();
    state_.Add(GENERIC);
  } else if (IsMonomorphic()) {
    state_.RemoveAll();
    state_.Add(GENERIC);
  } else {
    state_.Add(MONOMORPHIC_MAP);
  }
  TraceTransition(old_state, state_);
}


template<class StateType>
void HydrogenCodeStub::TraceTransition(StateType from, StateType to) {
  // Note: Although a no-op transition is semantically OK, it is hinting at a
  // bug somewhere in our state transition machinery.
  ASSERT(from != to);
  if (!FLAG_trace_ic) return;
  OFStream os(stdout);
  os << "[";
  PrintBaseName(os);
  os << ": " << from << "=>" << to << "]" << endl;
}


void CompareNilICStub::PrintBaseName(OStream& os) const {  // NOLINT
  CodeStub::PrintBaseName(os);
  os << ((nil_value_ == kNullValue) ? "(NullValue)" : "(UndefinedValue)");
}


void CompareNilICStub::PrintState(OStream& os) const {  // NOLINT
  os << state_;
}


// TODO(svenpanne) Make this a real infix_ostream_iterator.
class SimpleListPrinter {
 public:
  explicit SimpleListPrinter(OStream& os) : os_(os), first_(true) {}

  void Add(const char* s) {
    if (first_) {
      first_ = false;
    } else {
      os_ << ",";
    }
    os_ << s;
  }

 private:
  OStream& os_;
  bool first_;
};


OStream& operator<<(OStream& os, const CompareNilICStub::State& s) {
  os << "(";
  SimpleListPrinter p(os);
  if (s.IsEmpty()) p.Add("None");
  if (s.Contains(CompareNilICStub::UNDEFINED)) p.Add("Undefined");
  if (s.Contains(CompareNilICStub::NULL_TYPE)) p.Add("Null");
  if (s.Contains(CompareNilICStub::MONOMORPHIC_MAP)) p.Add("MonomorphicMap");
  if (s.Contains(CompareNilICStub::GENERIC)) p.Add("Generic");
  return os << ")";
}


Type* CompareNilICStub::GetType(Zone* zone, Handle<Map> map) {
  if (state_.Contains(CompareNilICStub::GENERIC)) {
    return Type::Any(zone);
  }

  Type* result = Type::None(zone);
  if (state_.Contains(CompareNilICStub::UNDEFINED)) {
    result = Type::Union(result, Type::Undefined(zone), zone);
  }
  if (state_.Contains(CompareNilICStub::NULL_TYPE)) {
    result = Type::Union(result, Type::Null(zone), zone);
  }
  if (state_.Contains(CompareNilICStub::MONOMORPHIC_MAP)) {
    Type* type =
        map.is_null() ? Type::Detectable(zone) : Type::Class(map, zone);
    result = Type::Union(result, type, zone);
  }

  return result;
}


Type* CompareNilICStub::GetInputType(Zone* zone, Handle<Map> map) {
  Type* output_type = GetType(zone, map);
  Type* nil_type =
      nil_value_ == kNullValue ? Type::Null(zone) : Type::Undefined(zone);
  return Type::Union(output_type, nil_type, zone);
}


void CallIC_ArrayStub::PrintState(OStream& os) const {  // NOLINT
  os << state_ << " (Array)";
}


void CallICStub::PrintState(OStream& os) const {  // NOLINT
  os << state_;
}


void InstanceofStub::PrintName(OStream& os) const {  // NOLINT
  os << "InstanceofStub";
  if (HasArgsInRegisters()) os << "_REGS";
  if (HasCallSiteInlineCheck()) os << "_INLINE";
  if (ReturnTrueFalseObject()) os << "_TRUEFALSE";
}


void JSEntryStub::FinishCode(Handle<Code> code) {
  Handle<FixedArray> handler_table =
      code->GetIsolate()->factory()->NewFixedArray(1, TENURED);
  handler_table->set(0, Smi::FromInt(handler_offset_));
  code->set_handler_table(*handler_table);
}


void KeyedLoadFastElementStub::InitializeInterfaceDescriptor(
    CodeStubInterfaceDescriptor* descriptor) {
  Register registers[] = { LoadIC::ReceiverRegister(),
                           LoadIC::NameRegister() };
  STATIC_ASSERT(LoadIC::kRegisterArgumentCount == 2);
  descriptor->Initialize(ARRAY_SIZE(registers), registers,
                         FUNCTION_ADDR(KeyedLoadIC_MissFromStubFailure));
}


void KeyedLoadDictionaryElementStub::InitializeInterfaceDescriptor(
    CodeStubInterfaceDescriptor* descriptor) {
  Register registers[] = { LoadIC::ReceiverRegister(),
                           LoadIC::NameRegister() };
  STATIC_ASSERT(LoadIC::kRegisterArgumentCount == 2);
  descriptor->Initialize(ARRAY_SIZE(registers), registers,
                         FUNCTION_ADDR(KeyedLoadIC_MissFromStubFailure));
}


void KeyedLoadGenericElementStub::InitializeInterfaceDescriptor(
    CodeStubInterfaceDescriptor* descriptor) {
  Register registers[] = { LoadIC::ReceiverRegister(),
                           LoadIC::NameRegister() };
  STATIC_ASSERT(LoadIC::kRegisterArgumentCount == 2);
  descriptor->Initialize(
      ARRAY_SIZE(registers), registers,
      Runtime::FunctionForId(Runtime::kKeyedGetProperty)->entry);
}


void LoadFieldStub::InitializeInterfaceDescriptor(
    CodeStubInterfaceDescriptor* descriptor) {
  Register registers[] = { LoadIC::ReceiverRegister() };
  descriptor->Initialize(ARRAY_SIZE(registers), registers);
}


void KeyedLoadFieldStub::InitializeInterfaceDescriptor(
    CodeStubInterfaceDescriptor* descriptor) {
  Register registers[] = { LoadIC::ReceiverRegister() };
  descriptor->Initialize(ARRAY_SIZE(registers), registers);
}


void StringLengthStub::InitializeInterfaceDescriptor(
    CodeStubInterfaceDescriptor* descriptor) {
  Register registers[] = { LoadIC::ReceiverRegister(),
                           LoadIC::NameRegister() };
  descriptor->Initialize(ARRAY_SIZE(registers), registers);
}


void KeyedStringLengthStub::InitializeInterfaceDescriptor(
    CodeStubInterfaceDescriptor* descriptor) {
  Register registers[] = { LoadIC::ReceiverRegister(),
                           LoadIC::NameRegister() };
  descriptor->Initialize(ARRAY_SIZE(registers), registers);
}


void KeyedStoreFastElementStub::InitializeInterfaceDescriptor(
    CodeStubInterfaceDescriptor* descriptor) {
  Register registers[] = { KeyedStoreIC::ReceiverRegister(),
                           KeyedStoreIC::NameRegister(),
                           KeyedStoreIC::ValueRegister() };
  descriptor->Initialize(
      ARRAY_SIZE(registers), registers,
      FUNCTION_ADDR(KeyedStoreIC_MissFromStubFailure));
}


void StoreGlobalStub::InitializeInterfaceDescriptor(
    CodeStubInterfaceDescriptor* descriptor) {
  Register registers[] = { StoreIC::ReceiverRegister(),
                           StoreIC::NameRegister(),
                           StoreIC::ValueRegister() };
  descriptor->Initialize(ARRAY_SIZE(registers), registers,
                         FUNCTION_ADDR(StoreIC_MissFromStubFailure));
}


void KeyedLoadDictionaryElementPlatformStub::Generate(
    MacroAssembler* masm) {
  KeyedLoadStubCompiler::GenerateLoadDictionaryElement(masm);
}


void CreateAllocationSiteStub::GenerateAheadOfTime(Isolate* isolate) {
  CreateAllocationSiteStub stub(isolate);
  stub.GetCode();
}


void KeyedStoreElementStub::Generate(MacroAssembler* masm) {
  switch (elements_kind_) {
    case FAST_ELEMENTS:
    case FAST_HOLEY_ELEMENTS:
    case FAST_SMI_ELEMENTS:
    case FAST_HOLEY_SMI_ELEMENTS:
    case FAST_DOUBLE_ELEMENTS:
    case FAST_HOLEY_DOUBLE_ELEMENTS:
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype, size) \
    case EXTERNAL_##TYPE##_ELEMENTS:                    \
    case TYPE##_ELEMENTS:

    TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE
      UNREACHABLE();
      break;
    case DICTIONARY_ELEMENTS:
      KeyedStoreStubCompiler::GenerateStoreDictionaryElement(masm);
      break;
    case SLOPPY_ARGUMENTS_ELEMENTS:
      UNREACHABLE();
      break;
  }
}


void ArgumentsAccessStub::PrintName(OStream& os) const {  // NOLINT
  os << "ArgumentsAccessStub_";
  switch (type_) {
    case READ_ELEMENT:
      os << "ReadElement";
      break;
    case NEW_SLOPPY_FAST:
      os << "NewSloppyFast";
      break;
    case NEW_SLOPPY_SLOW:
      os << "NewSloppySlow";
      break;
    case NEW_STRICT:
      os << "NewStrict";
      break;
  }
  return;
}


void CallFunctionStub::PrintName(OStream& os) const {  // NOLINT
  os << "CallFunctionStub_Args" << argc_;
}


void CallConstructStub::PrintName(OStream& os) const {  // NOLINT
  os << "CallConstructStub";
  if (RecordCallTarget()) os << "_Recording";
}


void ArrayConstructorStub::PrintName(OStream& os) const {  // NOLINT
  os << "ArrayConstructorStub";
  switch (argument_count_) {
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


OStream& ArrayConstructorStubBase::BasePrintName(OStream& os,  // NOLINT
                                                 const char* name) const {
  os << name << "_" << ElementsKindToString(elements_kind());
  if (override_mode() == DISABLE_ALLOCATION_SITES) {
    os << "_DISABLE_ALLOCATION_SITES";
  }
  return os;
}


bool ToBooleanStub::UpdateStatus(Handle<Object> object) {
  Types old_types(types_);
  bool to_boolean_value = types_.UpdateStatus(object);
  TraceTransition(old_types, types_);
  return to_boolean_value;
}


void ToBooleanStub::PrintState(OStream& os) const {  // NOLINT
  os << types_;
}


OStream& operator<<(OStream& os, const ToBooleanStub::Types& s) {
  os << "(";
  SimpleListPrinter p(os);
  if (s.IsEmpty()) p.Add("None");
  if (s.Contains(ToBooleanStub::UNDEFINED)) p.Add("Undefined");
  if (s.Contains(ToBooleanStub::BOOLEAN)) p.Add("Bool");
  if (s.Contains(ToBooleanStub::NULL_TYPE)) p.Add("Null");
  if (s.Contains(ToBooleanStub::SMI)) p.Add("Smi");
  if (s.Contains(ToBooleanStub::SPEC_OBJECT)) p.Add("SpecObject");
  if (s.Contains(ToBooleanStub::STRING)) p.Add("String");
  if (s.Contains(ToBooleanStub::SYMBOL)) p.Add("Symbol");
  if (s.Contains(ToBooleanStub::HEAP_NUMBER)) p.Add("HeapNumber");
  return os << ")";
}


bool ToBooleanStub::Types::UpdateStatus(Handle<Object> object) {
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
  } else if (object->IsSpecObject()) {
    Add(SPEC_OBJECT);
    return !object->IsUndetectableObject();
  } else if (object->IsString()) {
    Add(STRING);
    return !object->IsUndetectableObject() &&
        String::cast(*object)->length() != 0;
  } else if (object->IsSymbol()) {
    Add(SYMBOL);
    return true;
  } else if (object->IsHeapNumber()) {
    ASSERT(!object->IsUndetectableObject());
    Add(HEAP_NUMBER);
    double value = HeapNumber::cast(*object)->value();
    return value != 0 && !std::isnan(value);
  } else {
    // We should never see an internal object at runtime here!
    UNREACHABLE();
    return true;
  }
}


bool ToBooleanStub::Types::NeedsMap() const {
  return Contains(ToBooleanStub::SPEC_OBJECT)
      || Contains(ToBooleanStub::STRING)
      || Contains(ToBooleanStub::SYMBOL)
      || Contains(ToBooleanStub::HEAP_NUMBER);
}


bool ToBooleanStub::Types::CanBeUndetectable() const {
  return Contains(ToBooleanStub::SPEC_OBJECT)
      || Contains(ToBooleanStub::STRING);
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
  ASSERT(entry_hook != NULL);
  entry_hook(function, stack_pointer);
}


static void InstallDescriptor(Isolate* isolate, HydrogenCodeStub* stub) {
  int major_key = stub->MajorKey();
  CodeStubInterfaceDescriptor* descriptor =
      isolate->code_stub_interface_descriptor(major_key);
  if (!descriptor->initialized()) {
    stub->InitializeInterfaceDescriptor(descriptor);
  }
}


void ArrayConstructorStubBase::InstallDescriptors(Isolate* isolate) {
  ArrayNoArgumentConstructorStub stub1(isolate, GetInitialFastElementsKind());
  InstallDescriptor(isolate, &stub1);
  ArraySingleArgumentConstructorStub stub2(isolate,
                                           GetInitialFastElementsKind());
  InstallDescriptor(isolate, &stub2);
  ArrayNArgumentsConstructorStub stub3(isolate, GetInitialFastElementsKind());
  InstallDescriptor(isolate, &stub3);
}


void NumberToStringStub::InstallDescriptors(Isolate* isolate) {
  NumberToStringStub stub(isolate);
  InstallDescriptor(isolate, &stub);
}


void FastNewClosureStub::InstallDescriptors(Isolate* isolate) {
  FastNewClosureStub stub(isolate, STRICT, false);
  InstallDescriptor(isolate, &stub);
}


void FastNewContextStub::InstallDescriptors(Isolate* isolate) {
  FastNewContextStub stub(isolate, FastNewContextStub::kMaximumSlots);
  InstallDescriptor(isolate, &stub);
}


// static
void FastCloneShallowArrayStub::InstallDescriptors(Isolate* isolate) {
  FastCloneShallowArrayStub stub(isolate, DONT_TRACK_ALLOCATION_SITE);
  InstallDescriptor(isolate, &stub);
}


// static
void BinaryOpICStub::InstallDescriptors(Isolate* isolate) {
  BinaryOpICStub stub(isolate, Token::ADD, NO_OVERWRITE);
  InstallDescriptor(isolate, &stub);
}


// static
void BinaryOpWithAllocationSiteStub::InstallDescriptors(Isolate* isolate) {
  BinaryOpWithAllocationSiteStub stub(isolate, Token::ADD, NO_OVERWRITE);
  InstallDescriptor(isolate, &stub);
}


// static
void StringAddStub::InstallDescriptors(Isolate* isolate) {
  StringAddStub stub(isolate, STRING_ADD_CHECK_NONE, NOT_TENURED);
  InstallDescriptor(isolate, &stub);
}


// static
void RegExpConstructResultStub::InstallDescriptors(Isolate* isolate) {
  RegExpConstructResultStub stub(isolate);
  InstallDescriptor(isolate, &stub);
}


// static
void KeyedLoadGenericElementStub::InstallDescriptors(Isolate* isolate) {
  KeyedLoadGenericElementStub stub(isolate);
  InstallDescriptor(isolate, &stub);
}


ArrayConstructorStub::ArrayConstructorStub(Isolate* isolate)
    : PlatformCodeStub(isolate), argument_count_(ANY) {
  ArrayConstructorStubBase::GenerateStubsAheadOfTime(isolate);
}


ArrayConstructorStub::ArrayConstructorStub(Isolate* isolate,
                                           int argument_count)
    : PlatformCodeStub(isolate) {
  if (argument_count == 0) {
    argument_count_ = NONE;
  } else if (argument_count == 1) {
    argument_count_ = ONE;
  } else if (argument_count >= 2) {
    argument_count_ = MORE_THAN_ONE;
  } else {
    UNREACHABLE();
  }
  ArrayConstructorStubBase::GenerateStubsAheadOfTime(isolate);
}


void InternalArrayConstructorStubBase::InstallDescriptors(Isolate* isolate) {
  InternalArrayNoArgumentConstructorStub stub1(isolate, FAST_ELEMENTS);
  InstallDescriptor(isolate, &stub1);
  InternalArraySingleArgumentConstructorStub stub2(isolate, FAST_ELEMENTS);
  InstallDescriptor(isolate, &stub2);
  InternalArrayNArgumentsConstructorStub stub3(isolate, FAST_ELEMENTS);
  InstallDescriptor(isolate, &stub3);
}

InternalArrayConstructorStub::InternalArrayConstructorStub(
    Isolate* isolate) : PlatformCodeStub(isolate) {
  InternalArrayConstructorStubBase::GenerateStubsAheadOfTime(isolate);
}


} }  // namespace v8::internal
