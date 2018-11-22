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
#include "src/code-tracer.h"
#include "src/counters.h"
#include "src/gdb-jit.h"
#include "src/heap/heap-inl.h"
#include "src/ic/ic-stats.h"
#include "src/ic/ic.h"
#include "src/macro-assembler.h"
#include "src/objects-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/tracing/tracing-category-observer.h"

namespace v8 {
namespace internal {

using compiler::CodeAssemblerState;

CodeStubDescriptor::CodeStubDescriptor(CodeStub* stub)
    : isolate_(stub->isolate()),
      call_descriptor_(stub->GetCallInterfaceDescriptor()),
      stack_parameter_count_(no_reg),
      hint_stack_parameter_count_(-1),
      function_mode_(NOT_JS_FUNCTION_STUB_MODE),
      deoptimization_handler_(kNullAddress),
      miss_handler_(),
      has_miss_handler_(false) {}

CodeStubDescriptor::CodeStubDescriptor(Isolate* isolate, uint32_t stub_key)
    : isolate_(isolate),
      stack_parameter_count_(no_reg),
      hint_stack_parameter_count_(-1),
      function_mode_(NOT_JS_FUNCTION_STUB_MODE),
      deoptimization_handler_(kNullAddress),
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

bool CodeStub::FindCodeInCache(Code* code_out) {
  SimpleNumberDictionary* stubs = isolate()->heap()->code_stubs();
  int index = stubs->FindEntry(isolate(), GetKey());
  if (index != SimpleNumberDictionary::kNotFound) {
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
  counters->total_stubs_code_size()->Increment(code->raw_instruction_size());
#ifdef DEBUG
  code->VerifyEmbeddedObjects(isolate());
#endif
}


void CodeStub::DeleteStubFromCacheForTesting() {
  Heap* heap = isolate_->heap();
  Handle<SimpleNumberDictionary> dict(heap->code_stubs(), isolate());
  int entry = dict->FindEntry(isolate(), GetKey());
  DCHECK_NE(SimpleNumberDictionary::kNotFound, entry);
  dict = SimpleNumberDictionary::DeleteEntry(isolate(), dict, entry);
  heap->SetRootCodeStubs(*dict);
}

Handle<Code> PlatformCodeStub::GenerateCode() {
  Factory* factory = isolate()->factory();

  // Generate the new code.
  // TODO(yangguo): remove this once we can serialize IC stubs.
  AssemblerOptions options = AssemblerOptions::Default(isolate(), true);
  MacroAssembler masm(isolate(), options, nullptr, 256,
                      CodeObjectRequired::kYes);

  {
    // Update the static counter each time a new code stub is generated.
    isolate()->counters()->code_stubs()->Increment();

    // Generate the code for the stub.
    NoCurrentFrameScope scope(&masm);
    Generate(&masm);
  }

  // Generate the handler table.
  int handler_table_offset = GenerateHandlerTable(&masm);

  // Create the code object.
  CodeDesc desc;
  masm.GetCode(isolate(), &desc);
  // Copy the generated code into a heap object.
  Handle<Code> new_object = factory->NewCode(
      desc, Code::STUB, masm.CodeObject(), Builtins::kNoBuiltinId,
      MaybeHandle<ByteArray>(), DeoptimizationData::Empty(isolate()),
      NeedsImmovableCode(), GetKey(), false, 0, 0, handler_table_offset);
  return new_object;
}


Handle<Code> CodeStub::GetCode() {
  Heap* heap = isolate()->heap();
  Code code;
  if (FindCodeInCache(&code)) {
    DCHECK(code->is_stub());
    return handle(code, isolate_);
  }

  {
    HandleScope scope(isolate());
    // Canonicalize handles, so that we can share constant pool entries pointing
    // to code targets without dereferencing their handles.
    CanonicalHandleScope canonical(isolate());

    Handle<Code> new_object = GenerateCode();
    DCHECK_EQ(GetKey(), new_object->stub_key());
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

    // Update the dictionary and the root in Heap.
    Handle<SimpleNumberDictionary> dict = SimpleNumberDictionary::Set(
        isolate(), handle(heap->code_stubs(), isolate_), GetKey(), new_object);
    heap->SetRootCodeStubs(*dict);
    code = *new_object;
  }

  DCHECK(!NeedsImmovableCode() || heap->IsImmovable(code));
  return Handle<Code>(code, isolate());
}

CodeStub::Major CodeStub::GetMajorKey(const Code code_stub) {
  return MajorKeyFromKey(code_stub->stub_key());
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
  }
  return nullptr;
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

int PlatformCodeStub::GenerateHandlerTable(MacroAssembler* masm) { return 0; }

static void InitializeDescriptorDispatchedCall(CodeStub* stub,
                                               void** value_out) {
  CodeStubDescriptor* descriptor_out =
      reinterpret_cast<CodeStubDescriptor*>(value_out);
  descriptor_out->set_call_descriptor(stub->GetCallInterfaceDescriptor());
}


void CodeStub::InitializeDescriptor(Isolate* isolate, uint32_t key,
                                    CodeStubDescriptor* desc) {
  void** value_out = reinterpret_cast<void**>(desc);
  Dispatch(isolate, key, value_out, &InitializeDescriptorDispatchedCall);
}


void CodeStub::GetCodeDispatchCall(CodeStub* stub, void** value_out) {
  Handle<Code>* code_out = reinterpret_cast<Handle<Code>*>(value_out);
  *code_out = stub->GetCode();
}


MaybeHandle<Code> CodeStub::GetCode(Isolate* isolate, uint32_t key) {
  HandleScope scope(isolate);
  Handle<Code> code;
  void** value_out = reinterpret_cast<void**>(&code);
  Dispatch(isolate, key, value_out, &GetCodeDispatchCall);
  return scope.CloseAndEscape(code);
}

Handle<Code> TurboFanCodeStub::GenerateCode() {
  const char* name = CodeStub::MajorName(MajorKey());
  Zone zone(isolate()->allocator(), ZONE_NAME);
  CallInterfaceDescriptor descriptor(GetCallInterfaceDescriptor());
  compiler::CodeAssemblerState state(
      isolate(), &zone, descriptor, Code::STUB, name,
      PoisoningMitigationLevel::kDontPoison, GetKey());
  GenerateAssembly(&state);
  return compiler::CodeAssembler::GenerateCode(
      &state, AssemblerOptions::Default(isolate()));
}

namespace {

class HandlerStubAssembler : public CodeStubAssembler {
 public:
  explicit HandlerStubAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  // Essentially turns runtime elements kinds (TNode<Int32T>) into
  // compile-time types (int) by dispatching over the runtime type and
  // emitting a specialized copy of the given case function for each elements
  // kind. Use with caution. This produces a *lot* of code.
  typedef std::function<void(ElementsKind)> ElementsKindSwitchCase;
  void DispatchByElementsKind(TNode<Int32T> elements_kind,
                              const ElementsKindSwitchCase& case_function);

  // Dispatches over all possible combinations of {from,to} elements kinds.
  typedef std::function<void(ElementsKind, ElementsKind)>
      ElementsKindTransitionSwitchCase;
  void DispatchForElementsKindTransition(
      TNode<Int32T> from_kind, TNode<Int32T> to_kind,
      const ElementsKindTransitionSwitchCase& case_function);
};

// All possible fast-to-fast transitions. Transitions to dictionary mode are not
// handled by ElementsTransitionAndStore.
#define ELEMENTS_KIND_TRANSITIONS(V)               \
  V(PACKED_SMI_ELEMENTS, HOLEY_SMI_ELEMENTS)       \
  V(PACKED_SMI_ELEMENTS, PACKED_DOUBLE_ELEMENTS)   \
  V(PACKED_SMI_ELEMENTS, HOLEY_DOUBLE_ELEMENTS)    \
  V(PACKED_SMI_ELEMENTS, PACKED_ELEMENTS)          \
  V(PACKED_SMI_ELEMENTS, HOLEY_ELEMENTS)           \
  V(HOLEY_SMI_ELEMENTS, HOLEY_DOUBLE_ELEMENTS)     \
  V(HOLEY_SMI_ELEMENTS, HOLEY_ELEMENTS)            \
  V(PACKED_DOUBLE_ELEMENTS, HOLEY_DOUBLE_ELEMENTS) \
  V(PACKED_DOUBLE_ELEMENTS, PACKED_ELEMENTS)       \
  V(PACKED_DOUBLE_ELEMENTS, HOLEY_ELEMENTS)        \
  V(HOLEY_DOUBLE_ELEMENTS, HOLEY_ELEMENTS)         \
  V(PACKED_ELEMENTS, HOLEY_ELEMENTS)

void HandlerStubAssembler::DispatchForElementsKindTransition(
    TNode<Int32T> from_kind, TNode<Int32T> to_kind,
    const ElementsKindTransitionSwitchCase& case_function) {
  STATIC_ASSERT(sizeof(ElementsKind) == sizeof(uint8_t));

  Label next(this), if_unknown_type(this, Label::kDeferred);

  int32_t combined_elements_kinds[] = {
#define ELEMENTS_KINDS_CASE(FROM, TO) (FROM << kBitsPerByte) | TO,
      ELEMENTS_KIND_TRANSITIONS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE
  };

#define ELEMENTS_KINDS_CASE(FROM, TO) Label if_##FROM##_##TO(this);
  ELEMENTS_KIND_TRANSITIONS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE

  Label* elements_kind_labels[] = {
#define ELEMENTS_KINDS_CASE(FROM, TO) &if_##FROM##_##TO,
      ELEMENTS_KIND_TRANSITIONS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE
  };
  STATIC_ASSERT(arraysize(combined_elements_kinds) ==
                arraysize(elements_kind_labels));

  TNode<Word32T> combined_elements_kind =
      Word32Or(Word32Shl(from_kind, Int32Constant(kBitsPerByte)), to_kind);

  Switch(combined_elements_kind, &if_unknown_type, combined_elements_kinds,
         elements_kind_labels, arraysize(combined_elements_kinds));

#define ELEMENTS_KINDS_CASE(FROM, TO) \
  BIND(&if_##FROM##_##TO);            \
  {                                   \
    case_function(FROM, TO);          \
    Goto(&next);                      \
  }
  ELEMENTS_KIND_TRANSITIONS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE

  BIND(&if_unknown_type);
  Unreachable();

  BIND(&next);
}

#undef ELEMENTS_KIND_TRANSITIONS

}  // namespace

TF_STUB(ElementsTransitionAndStoreStub, HandlerStubAssembler) {
  Node* receiver = Parameter(Descriptor::kReceiver);
  Node* key = Parameter(Descriptor::kName);
  Node* value = Parameter(Descriptor::kValue);
  Node* map = Parameter(Descriptor::kMap);
  Node* slot = Parameter(Descriptor::kSlot);
  Node* vector = Parameter(Descriptor::kVector);
  Node* context = Parameter(Descriptor::kContext);

  Comment("ElementsTransitionAndStoreStub: store_mode=%d", stub->store_mode());

  Label miss(this);

  if (FLAG_trace_elements_transitions) {
    // Tracing elements transitions is the job of the runtime.
    Goto(&miss);
  } else {
    // TODO(v8:8481): Pass from_kind and to_kind in feedback vector slots.
    DispatchForElementsKindTransition(
        LoadElementsKind(receiver), LoadMapElementsKind(map),
        [=, &miss](ElementsKind from_kind, ElementsKind to_kind) {
          TransitionElementsKind(receiver, map, from_kind, to_kind, &miss);
          EmitElementStore(receiver, key, value, to_kind, stub->store_mode(),
                           &miss, context);
        });
    Return(value);
  }

  BIND(&miss);
  TailCallRuntime(Runtime::kElementsTransitionAndStoreIC_Miss, context,
                  receiver, key, value, map, slot, vector);
}

int JSEntryStub::GenerateHandlerTable(MacroAssembler* masm) {
  int handler_table_offset = HandlerTable::EmitReturnTableStart(masm, 1);
  HandlerTable::EmitReturnEntry(masm, 0, handler_offset_);
  return handler_table_offset;
}

namespace {

// All elements kinds handled by EmitElementStore. Specifically, this includes
// fast elements and fixed typed array elements.
#define ELEMENTS_KINDS(V)   \
  V(PACKED_SMI_ELEMENTS)    \
  V(HOLEY_SMI_ELEMENTS)     \
  V(PACKED_ELEMENTS)        \
  V(HOLEY_ELEMENTS)         \
  V(PACKED_DOUBLE_ELEMENTS) \
  V(HOLEY_DOUBLE_ELEMENTS)  \
  V(UINT8_ELEMENTS)         \
  V(INT8_ELEMENTS)          \
  V(UINT16_ELEMENTS)        \
  V(INT16_ELEMENTS)         \
  V(UINT32_ELEMENTS)        \
  V(INT32_ELEMENTS)         \
  V(FLOAT32_ELEMENTS)       \
  V(FLOAT64_ELEMENTS)       \
  V(UINT8_CLAMPED_ELEMENTS) \
  V(BIGUINT64_ELEMENTS)     \
  V(BIGINT64_ELEMENTS)

void HandlerStubAssembler::DispatchByElementsKind(
    TNode<Int32T> elements_kind, const ElementsKindSwitchCase& case_function) {
  Label next(this), if_unknown_type(this, Label::kDeferred);

  int32_t elements_kinds[] = {
#define ELEMENTS_KINDS_CASE(KIND) KIND,
      ELEMENTS_KINDS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE
  };

#define ELEMENTS_KINDS_CASE(KIND) Label if_##KIND(this);
  ELEMENTS_KINDS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE

  Label* elements_kind_labels[] = {
#define ELEMENTS_KINDS_CASE(KIND) &if_##KIND,
      ELEMENTS_KINDS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE
  };
  STATIC_ASSERT(arraysize(elements_kinds) == arraysize(elements_kind_labels));

  Switch(elements_kind, &if_unknown_type, elements_kinds, elements_kind_labels,
         arraysize(elements_kinds));

#define ELEMENTS_KINDS_CASE(KIND) \
  BIND(&if_##KIND);               \
  {                               \
    case_function(KIND);          \
    Goto(&next);                  \
  }
  ELEMENTS_KINDS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE

  BIND(&if_unknown_type);
  Unreachable();

  BIND(&next);
}

#undef ELEMENTS_KINDS

}  // namespace

TF_STUB(StoreFastElementStub, HandlerStubAssembler) {
  Comment("StoreFastElementStub: store_mode=%d", stub->store_mode());

  Node* receiver = Parameter(Descriptor::kReceiver);
  Node* key = Parameter(Descriptor::kName);
  Node* value = Parameter(Descriptor::kValue);
  Node* slot = Parameter(Descriptor::kSlot);
  Node* vector = Parameter(Descriptor::kVector);
  Node* context = Parameter(Descriptor::kContext);

  Label miss(this);

  // TODO(v8:8481): Pass elements_kind in feedback vector slots.
  DispatchByElementsKind(LoadElementsKind(receiver),
                         [=, &miss](ElementsKind elements_kind) {
                           EmitElementStore(receiver, key, value, elements_kind,
                                            stub->store_mode(), &miss, context);
                         });
  Return(value);

  BIND(&miss);
  TailCallRuntime(Runtime::kKeyedStoreIC_Miss, context, value, slot, vector,
                  receiver, key);
}

// static
void StoreFastElementStub::GenerateAheadOfTime(Isolate* isolate) {
  StoreFastElementStub(isolate, STANDARD_STORE).GetCode();
  StoreFastElementStub(isolate, STORE_AND_GROW_NO_TRANSITION_HANDLE_COW)
      .GetCode();
}


void ProfileEntryHookStub::EntryHookTrampoline(intptr_t function,
                                               intptr_t stack_pointer,
                                               Isolate* isolate) {
  FunctionEntryHook entry_hook = isolate->function_entry_hook();
  DCHECK_NOT_NULL(entry_hook);
  entry_hook(function, stack_pointer);
}

void CodeStub::GenerateStubsAheadOfTime(Isolate* isolate) {
  StoreFastElementStub::GenerateAheadOfTime(isolate);
}

}  // namespace internal
}  // namespace v8
