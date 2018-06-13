// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-code-manager.h"

#include <iomanip>

#include "src/assembler-inl.h"
#include "src/base/atomic-utils.h"
#include "src/base/macros.h"
#include "src/base/platform/platform.h"
#include "src/codegen.h"
#include "src/disassembler.h"
#include "src/globals.h"
#include "src/macro-assembler-inl.h"
#include "src/macro-assembler.h"
#include "src/objects-inl.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-objects.h"

#define TRACE_HEAP(...)                                   \
  do {                                                    \
    if (FLAG_wasm_trace_native_heap) PrintF(__VA_ARGS__); \
  } while (false)

namespace v8 {
namespace internal {
namespace wasm {

namespace {

// Binary predicate to perform lookups in {NativeModule::owned_code_} with a
// given address into a code object. Use with {std::upper_bound} for example.
struct WasmCodeUniquePtrComparator {
  bool operator()(Address pc, const std::unique_ptr<WasmCode>& code) const {
    DCHECK_NE(kNullAddress, pc);
    DCHECK_NOT_NULL(code);
    return pc < code->instruction_start();
  }
};

#if V8_TARGET_ARCH_X64
#define __ masm->
constexpr bool kModuleCanAllocateMoreMemory = false;

void GenerateJumpTrampoline(MacroAssembler* masm, Address target) {
  __ movq(kScratchRegister, static_cast<uint64_t>(target));
  __ jmp(kScratchRegister);
}
#undef __
#elif V8_TARGET_ARCH_S390X
#define __ masm->
constexpr bool kModuleCanAllocateMoreMemory = false;

void GenerateJumpTrampoline(MacroAssembler* masm, Address target) {
  __ mov(ip, Operand(bit_cast<intptr_t, Address>(target)));
  __ b(ip);
}
#undef __
#elif V8_TARGET_ARCH_ARM64
#define __ masm->
constexpr bool kModuleCanAllocateMoreMemory = false;

void GenerateJumpTrampoline(MacroAssembler* masm, Address target) {
  UseScratchRegisterScope temps(masm);
  Register scratch = temps.AcquireX();
  __ Mov(scratch, static_cast<uint64_t>(target));
  __ Br(scratch);
}
#undef __
#else
const bool kModuleCanAllocateMoreMemory = true;
#endif

void RelocateCode(WasmCode* code, const WasmCode* orig,
                  WasmCode::FlushICache flush_icache) {
  intptr_t delta = code->instruction_start() - orig->instruction_start();
  for (RelocIterator it(code->instructions(), code->reloc_info(),
                        code->constant_pool(), RelocInfo::kApplyMask);
       !it.done(); it.next()) {
    it.rinfo()->apply(delta);
  }
  if (flush_icache) {
    Assembler::FlushICache(code->instructions().start(),
                           code->instructions().size());
  }
}

}  // namespace

void DisjointAllocationPool::Merge(AddressRange range) {
  auto dest_it = ranges_.begin();
  auto dest_end = ranges_.end();

  // Skip over dest ranges strictly before {range}.
  while (dest_it != dest_end && dest_it->end < range.start) ++dest_it;

  // After last dest range: insert and done.
  if (dest_it == dest_end) {
    ranges_.push_back(range);
    return;
  }

  // Adjacent (from below) to dest: merge and done.
  if (dest_it->start == range.end) {
    dest_it->start = range.start;
    return;
  }

  // Before dest: insert and done.
  if (dest_it->start > range.end) {
    ranges_.insert(dest_it, range);
    return;
  }

  // Src is adjacent from above. Merge and check whether the merged range is now
  // adjacent to the next range.
  DCHECK_EQ(dest_it->end, range.start);
  dest_it->end = range.end;
  auto next_dest = dest_it;
  ++next_dest;
  if (next_dest != dest_end && dest_it->end == next_dest->start) {
    dest_it->end = next_dest->end;
    ranges_.erase(next_dest);
  }
}

AddressRange DisjointAllocationPool::Allocate(size_t size) {
  for (auto it = ranges_.begin(), end = ranges_.end(); it != end; ++it) {
    size_t range_size = it->size();
    if (size > range_size) continue;
    AddressRange ret{it->start, it->start + size};
    if (size == range_size) {
      ranges_.erase(it);
    } else {
      it->start += size;
      DCHECK_LT(it->start, it->end);
    }
    return ret;
  }
  return {};
}

Address WasmCode::constant_pool() const {
  if (FLAG_enable_embedded_constant_pool) {
    if (constant_pool_offset_ < instructions().size()) {
      return instruction_start() + constant_pool_offset_;
    }
  }
  return kNullAddress;
}

size_t WasmCode::trap_handler_index() const {
  CHECK(HasTrapHandlerIndex());
  return static_cast<size_t>(trap_handler_index_);
}

void WasmCode::set_trap_handler_index(size_t value) {
  trap_handler_index_ = value;
}

void WasmCode::RegisterTrapHandlerData() {
  if (kind() != wasm::WasmCode::kFunction) return;
  if (HasTrapHandlerIndex()) return;

  Address base = instruction_start();

  size_t size = instructions().size();
  const int index =
      RegisterHandlerData(base, size, protected_instructions().size(),
                          protected_instructions().data());

  // TODO(eholk): if index is negative, fail.
  CHECK_LE(0, index);
  set_trap_handler_index(static_cast<size_t>(index));
}

bool WasmCode::HasTrapHandlerIndex() const { return trap_handler_index_ >= 0; }

void WasmCode::ResetTrapHandlerIndex() { trap_handler_index_ = -1; }

bool WasmCode::ShouldBeLogged(Isolate* isolate) {
  return isolate->logger()->is_listening_to_code_events() ||
         isolate->is_profiling();
}

void WasmCode::LogCode(Isolate* isolate) const {
  DCHECK(ShouldBeLogged(isolate));
  if (index_.IsJust()) {
    uint32_t index = this->index();
    Handle<WasmModuleObject> module_object(native_module()->module_object(),
                                           isolate);
    int name_length;
    Handle<String> name(
        WasmModuleObject::GetFunctionName(isolate, module_object, index));
    auto cname =
        name->ToCString(AllowNullsFlag::DISALLOW_NULLS,
                        RobustnessFlag::ROBUST_STRING_TRAVERSAL, &name_length);
    PROFILE(isolate,
            CodeCreateEvent(CodeEventListener::FUNCTION_TAG, this,
                            {cname.get(), static_cast<size_t>(name_length)}));
    if (!source_positions().is_empty()) {
      LOG_CODE_EVENT(isolate, CodeLinePosInfoRecordEvent(instruction_start(),
                                                         source_positions()));
    }
  }
}

void WasmCode::Validate() const {
#ifdef DEBUG
  // We expect certain relocation info modes to never appear in {WasmCode}
  // objects or to be restricted to a small set of valid values. Hence the
  // iteration below does not use a mask, but visits all relocation data.
  for (RelocIterator it(instructions(), reloc_info(), constant_pool());
       !it.done(); it.next()) {
    RelocInfo::Mode mode = it.rinfo()->rmode();
    switch (mode) {
      case RelocInfo::CODE_TARGET: {
        Address target = it.rinfo()->target_address();
        Code* code = native_module_->ReverseTrampolineLookup(target);
        // TODO(7424): This is by now limited to only contain references to a
        // limited set of builtins. This code will eventually be completely free
        // of {RelocInfo::CODE_TARGET} relocation entries altogether.
        int builtin_index = code->builtin_index();
        CHECK(builtin_index == Builtins::kAllocateHeapNumber ||
              builtin_index == Builtins::kArgumentsAdaptorTrampoline ||
              builtin_index == Builtins::kCall_ReceiverIsAny ||
              builtin_index == Builtins::kToNumber);
        break;
      }
      case RelocInfo::WASM_STUB_CALL: {
        Address target = it.rinfo()->wasm_stub_call_address();
        WasmCode* code = native_module_->Lookup(target);
        CHECK(code && code->kind() == WasmCode::kRuntimeStub);
        CHECK_EQ(target, code->instruction_start());
        break;
      }
      case RelocInfo::WASM_CODE_TABLE_ENTRY:
      case RelocInfo::WASM_CALL:
      case RelocInfo::JS_TO_WASM_CALL:
      case RelocInfo::EXTERNAL_REFERENCE:
      case RelocInfo::INTERNAL_REFERENCE:
      case RelocInfo::INTERNAL_REFERENCE_ENCODED:
      case RelocInfo::OFF_HEAP_TARGET:
      case RelocInfo::COMMENT:
      case RelocInfo::CONST_POOL:
      case RelocInfo::VENEER_POOL:
        // These are OK to appear.
        break;
      default:
        FATAL("Unexpected mode: %d", mode);
    }
  }
#endif
}

void WasmCode::Print(Isolate* isolate) const {
  StdoutStream os;
  os << "--- WebAssembly code ---\n";
  Disassemble(nullptr, isolate, os);
  os << "--- End code ---\n";
}

void WasmCode::Disassemble(const char* name, Isolate* isolate, std::ostream& os,
                           Address current_pc) const {
  if (name) os << "name: " << name << "\n";
  if (index_.IsJust()) os << "index: " << index_.FromJust() << "\n";
  os << "kind: " << GetWasmCodeKindAsString(kind_) << "\n";
  os << "compiler: " << (is_liftoff() ? "Liftoff" : "TurboFan") << "\n";
  size_t body_size = instructions().size();
  os << "Body (size = " << body_size << ")\n";

#ifdef ENABLE_DISASSEMBLER
  size_t instruction_size = body_size;
  if (constant_pool_offset_ && constant_pool_offset_ < instruction_size) {
    instruction_size = constant_pool_offset_;
  }
  if (safepoint_table_offset_ && safepoint_table_offset_ < instruction_size) {
    instruction_size = safepoint_table_offset_;
  }
  DCHECK_LT(0, instruction_size);
  os << "Instructions (size = " << instruction_size << ")\n";
  // TODO(mtrofin): rework the dependency on isolate and code in
  // Disassembler::Decode.
  Disassembler::Decode(isolate, &os, instructions().start(),
                       instructions().start() + instruction_size,
                       CodeReference(this), current_pc);
  os << "\n";

  if (!source_positions().is_empty()) {
    os << "Source positions:\n pc offset  position\n";
    for (SourcePositionTableIterator it(source_positions()); !it.done();
         it.Advance()) {
      os << std::setw(10) << std::hex << it.code_offset() << std::dec
         << std::setw(10) << it.source_position().ScriptOffset()
         << (it.is_statement() ? "  statement" : "") << "\n";
    }
    os << "\n";
  }

  os << "RelocInfo (size = " << reloc_size_ << ")\n";
  for (RelocIterator it(instructions(), reloc_info(), constant_pool());
       !it.done(); it.next()) {
    it.rinfo()->Print(isolate, os);
  }
  os << "\n";
#endif  // ENABLE_DISASSEMBLER
}

const char* GetWasmCodeKindAsString(WasmCode::Kind kind) {
  switch (kind) {
    case WasmCode::kFunction:
      return "wasm function";
    case WasmCode::kWasmToJsWrapper:
      return "wasm-to-js";
    case WasmCode::kLazyStub:
      return "lazy-compile";
    case WasmCode::kRuntimeStub:
      return "runtime-stub";
    case WasmCode::kInterpreterEntry:
      return "interpreter entry";
    case WasmCode::kTrampoline:
      return "trampoline";
  }
  return "unknown kind";
}

WasmCode::~WasmCode() {
  // Depending on finalizer order, the WasmCompiledModule finalizer may be
  // called first, case in which we release here. If the InstanceFinalizer is
  // called first, the handlers will be cleared in Reset, as-if the NativeModule
  // may be later used again (which would be the case if the WasmCompiledModule
  // were still held by a WasmModuleObject)
  if (HasTrapHandlerIndex()) {
    CHECK_LT(trap_handler_index(),
             static_cast<size_t>(std::numeric_limits<int>::max()));
    trap_handler::ReleaseHandlerData(static_cast<int>(trap_handler_index()));
  }
}

base::AtomicNumber<size_t> NativeModule::next_id_;

NativeModule::NativeModule(Isolate* isolate, uint32_t num_functions,
                           uint32_t num_imports, bool can_request_more,
                           VirtualMemory* code_space,
                           WasmCodeManager* code_manager, ModuleEnv& env)
    : instance_id(next_id_.Increment(1)),
      num_functions_(num_functions),
      num_imported_functions_(num_imports),
      compilation_state_(NewCompilationState(isolate, env)),
      free_code_space_({code_space->address(), code_space->end()}),
      wasm_code_manager_(code_manager),
      can_request_more_memory_(can_request_more),
      use_trap_handler_(env.use_trap_handler) {
  if (num_functions > 0) {
    uint32_t num_wasm_functions = num_functions - num_imports;
    code_table_.reset(new WasmCode*[num_wasm_functions]);
    memset(code_table_.get(), 0, num_wasm_functions * sizeof(WasmCode*));
  }
  VirtualMemory my_mem;
  owned_code_space_.push_back(my_mem);
  owned_code_space_.back().TakeControl(code_space);
  owned_code_.reserve(num_functions);
}

void NativeModule::ReserveCodeTableForTesting(uint32_t max_functions) {
  DCHECK_LE(num_functions_, max_functions);
  uint32_t num_wasm = num_functions_ - num_imported_functions_;
  uint32_t max_wasm = max_functions - num_imported_functions_;
  WasmCode** new_table = new WasmCode*[max_wasm];
  memset(new_table, 0, max_wasm * sizeof(*new_table));
  memcpy(new_table, code_table_.get(), num_wasm * sizeof(*new_table));
  code_table_.reset(new_table);
}

void NativeModule::SetNumFunctionsForTesting(uint32_t num_functions) {
  num_functions_ = num_functions;
}

void NativeModule::SetCodeForTesting(uint32_t index, WasmCode* code) {
  DCHECK_LT(index, num_functions_);
  DCHECK_LE(num_imported_functions_, index);
  code_table_[index - num_imported_functions_] = code;
}

WasmCode* NativeModule::AddOwnedCode(
    Vector<const byte> orig_instructions,
    std::unique_ptr<const byte[]> reloc_info, size_t reloc_size,
    std::unique_ptr<const byte[]> source_pos, size_t source_pos_size,
    Maybe<uint32_t> index, WasmCode::Kind kind, size_t constant_pool_offset,
    uint32_t stack_slots, size_t safepoint_table_offset,
    size_t handler_table_offset,
    std::unique_ptr<ProtectedInstructions> protected_instructions,
    WasmCode::Tier tier, WasmCode::FlushICache flush_icache) {
  // both allocation and insertion in owned_code_ happen in the same critical
  // section, thus ensuring owned_code_'s elements are rarely if ever moved.
  base::LockGuard<base::Mutex> lock(&allocation_mutex_);
  Address executable_buffer = AllocateForCode(orig_instructions.size());
  if (executable_buffer == kNullAddress) {
    V8::FatalProcessOutOfMemory(nullptr, "NativeModule::AddOwnedCode");
    UNREACHABLE();
  }
  memcpy(reinterpret_cast<void*>(executable_buffer), orig_instructions.start(),
         orig_instructions.size());
  std::unique_ptr<WasmCode> code(new WasmCode(
      {reinterpret_cast<byte*>(executable_buffer), orig_instructions.size()},
      std::move(reloc_info), reloc_size, std::move(source_pos), source_pos_size,
      this, index, kind, constant_pool_offset, stack_slots,
      safepoint_table_offset, handler_table_offset,
      std::move(protected_instructions), tier));
  WasmCode* ret = code.get();

  // TODO(mtrofin): We allocate in increasing address order, and
  // even if we end up with segmented memory, we may end up only with a few
  // large moves - if, for example, a new segment is below the current ones.
  auto insert_before =
      std::upper_bound(owned_code_.begin(), owned_code_.end(),
                       ret->instruction_start(), WasmCodeUniquePtrComparator());
  owned_code_.insert(insert_before, std::move(code));
  if (flush_icache) {
    Assembler::FlushICache(ret->instructions().start(),
                           ret->instructions().size());
  }
  return ret;
}

WasmCode* NativeModule::AddCodeCopy(Handle<Code> code, WasmCode::Kind kind,
                                    uint32_t index) {
  // TODO(wasm): Adding instance-specific wasm-to-js wrappers as owned code to
  // this NativeModule is a memory leak until the whole NativeModule dies.
  WasmCode* ret = AddAnonymousCode(code, kind);
  ret->index_ = Just(index);
  if (index >= num_imported_functions_) set_code(index, ret);
  return ret;
}

WasmCode* NativeModule::AddInterpreterEntry(Handle<Code> code, uint32_t index) {
  WasmCode* ret = AddAnonymousCode(code, WasmCode::kInterpreterEntry);
  ret->index_ = Just(index);
  return ret;
}

void NativeModule::SetLazyBuiltin(Handle<Code> code) {
  WasmCode* lazy_builtin = AddAnonymousCode(code, WasmCode::kLazyStub);
  for (WasmCode*& code_table_entry : code_table()) {
    code_table_entry = lazy_builtin;
  }
}

void NativeModule::SetRuntimeStubs(Isolate* isolate) {
  DCHECK_NULL(runtime_stub_table_[0]);  // Only called once.
#define COPY_BUILTIN(Name)                                                     \
  runtime_stub_table_[WasmCode::k##Name] =                                     \
      AddAnonymousCode(isolate->builtins()->builtin_handle(Builtins::k##Name), \
                       WasmCode::kRuntimeStub);
#define COPY_BUILTIN_TRAP(Name) COPY_BUILTIN(ThrowWasm##Name)
  WASM_RUNTIME_STUB_LIST(COPY_BUILTIN, COPY_BUILTIN_TRAP);
#undef COPY_BUILTIN_TRAP
#undef COPY_BUILTIN
}

WasmModuleObject* NativeModule::module_object() const {
  DCHECK_NOT_NULL(module_object_);
  return *module_object_;
}

void NativeModule::SetModuleObject(Handle<WasmModuleObject> module_object) {
  DCHECK_NULL(module_object_);
  module_object_ = module_object->GetIsolate()
                       ->global_handles()
                       ->Create(*module_object)
                       .location();
  GlobalHandles::MakeWeak(reinterpret_cast<Object***>(&module_object_));
}

WasmCode* NativeModule::AddAnonymousCode(Handle<Code> code,
                                         WasmCode::Kind kind) {
  std::unique_ptr<byte[]> reloc_info;
  if (code->relocation_size() > 0) {
    reloc_info.reset(new byte[code->relocation_size()]);
    memcpy(reloc_info.get(), code->relocation_start(), code->relocation_size());
  }
  std::unique_ptr<byte[]> source_pos;
  Handle<ByteArray> source_pos_table(code->SourcePositionTable());
  if (source_pos_table->length() > 0) {
    source_pos.reset(new byte[source_pos_table->length()]);
    source_pos_table->copy_out(0, source_pos.get(), source_pos_table->length());
  }
  std::unique_ptr<ProtectedInstructions> protected_instructions(
      new ProtectedInstructions(0));
  Vector<const byte> orig_instructions(
      reinterpret_cast<byte*>(code->InstructionStart()),
      static_cast<size_t>(code->InstructionSize()));
  int stack_slots = code->has_safepoint_info() ? code->stack_slots() : 0;
  int safepoint_table_offset =
      code->has_safepoint_info() ? code->safepoint_table_offset() : 0;
  WasmCode* ret =
      AddOwnedCode(orig_instructions,      // instructions
                   std::move(reloc_info),  // reloc_info
                   static_cast<size_t>(code->relocation_size()),  // reloc_size
                   std::move(source_pos),  // source positions
                   static_cast<size_t>(source_pos_table->length()),
                   Nothing<uint32_t>(),                // index
                   kind,                               // kind
                   code->constant_pool_offset(),       // constant_pool_offset
                   stack_slots,                        // stack_slots
                   safepoint_table_offset,             // safepoint_table_offset
                   code->handler_table_offset(),       // handler_table_offset
                   std::move(protected_instructions),  // protected_instructions
                   WasmCode::kOther,                   // kind
                   WasmCode::kNoFlushICache);          // flush_icache

  // Apply the relocation delta by iterating over the RelocInfo.
  intptr_t delta = ret->instruction_start() - code->InstructionStart();
  int mode_mask = RelocInfo::kApplyMask | RelocInfo::kCodeTargetMask |
                  RelocInfo::ModeMask(RelocInfo::WASM_STUB_CALL);
  RelocIterator orig_it(*code, mode_mask);
  for (RelocIterator it(ret->instructions(), ret->reloc_info(),
                        ret->constant_pool(), mode_mask);
       !it.done(); it.next(), orig_it.next()) {
    RelocInfo::Mode mode = it.rinfo()->rmode();
    if (RelocInfo::IsWasmStubCall(mode)) {
      uint32_t stub_call_tag = it.rinfo()->wasm_stub_call_tag();
      DCHECK_LT(stub_call_tag, WasmCode::kRuntimeStubCount);
      WasmCode* code =
          runtime_stub(static_cast<WasmCode::RuntimeStubId>(stub_call_tag));
      it.rinfo()->set_wasm_stub_call_address(code->instruction_start(),
                                             SKIP_ICACHE_FLUSH);
    } else if (RelocInfo::IsCodeTarget(mode)) {
      Code* call_target =
          Code::GetCodeFromTargetAddress(orig_it.rinfo()->target_address());
      it.rinfo()->set_target_address(
          GetLocalAddressFor(handle(call_target, call_target->GetIsolate())),
          SKIP_WRITE_BARRIER, SKIP_ICACHE_FLUSH);
    } else {
      it.rinfo()->apply(delta);
    }
  }

  // Flush the i-cache here instead of in AddOwnedCode, to include the changes
  // made while iterating over the RelocInfo above.
  Assembler::FlushICache(ret->instructions().start(),
                         ret->instructions().size());
  if (FLAG_print_code || FLAG_print_wasm_code) {
    // TODO(mstarzinger): don't need the isolate here.
    ret->Print(code->GetIsolate());
  }
  ret->Validate();
  return ret;
}

WasmCode* NativeModule::AddCode(
    const CodeDesc& desc, uint32_t frame_slots, uint32_t index,
    size_t safepoint_table_offset, size_t handler_table_offset,
    std::unique_ptr<ProtectedInstructions> protected_instructions,
    Handle<ByteArray> source_pos_table, WasmCode::Tier tier) {
  std::unique_ptr<byte[]> reloc_info;
  if (desc.reloc_size) {
    reloc_info.reset(new byte[desc.reloc_size]);
    memcpy(reloc_info.get(), desc.buffer + desc.buffer_size - desc.reloc_size,
           desc.reloc_size);
  }
  std::unique_ptr<byte[]> source_pos;
  if (source_pos_table->length() > 0) {
    source_pos.reset(new byte[source_pos_table->length()]);
    source_pos_table->copy_out(0, source_pos.get(), source_pos_table->length());
  }
  TurboAssembler* origin = reinterpret_cast<TurboAssembler*>(desc.origin);
  WasmCode* ret = AddOwnedCode(
      {desc.buffer, static_cast<size_t>(desc.instr_size)},
      std::move(reloc_info), static_cast<size_t>(desc.reloc_size),
      std::move(source_pos), static_cast<size_t>(source_pos_table->length()),
      Just(index), WasmCode::kFunction,
      desc.instr_size - desc.constant_pool_size, frame_slots,
      safepoint_table_offset, handler_table_offset,
      std::move(protected_instructions), tier, WasmCode::kNoFlushICache);

  set_code(index, ret);

  // Apply the relocation delta by iterating over the RelocInfo.
  AllowDeferredHandleDereference embedding_raw_address;
  intptr_t delta = ret->instructions().start() - desc.buffer;
  int mode_mask = RelocInfo::kApplyMask | RelocInfo::kCodeTargetMask |
                  RelocInfo::ModeMask(RelocInfo::WASM_STUB_CALL);
  for (RelocIterator it(ret->instructions(), ret->reloc_info(),
                        ret->constant_pool(), mode_mask);
       !it.done(); it.next()) {
    RelocInfo::Mode mode = it.rinfo()->rmode();
    if (RelocInfo::IsWasmStubCall(mode)) {
      uint32_t stub_call_tag = it.rinfo()->wasm_stub_call_tag();
      DCHECK_LT(stub_call_tag, WasmCode::kRuntimeStubCount);
      WasmCode* code =
          runtime_stub(static_cast<WasmCode::RuntimeStubId>(stub_call_tag));
      it.rinfo()->set_wasm_stub_call_address(code->instruction_start(),
                                             SKIP_ICACHE_FLUSH);
    } else if (RelocInfo::IsCodeTarget(mode)) {
      // rewrite code handles to direct pointers to the first instruction in the
      // code object
      Handle<Object> p = it.rinfo()->target_object_handle(origin);
      Code* code = Code::cast(*p);
      it.rinfo()->set_target_address(
          GetLocalAddressFor(handle(code, code->GetIsolate())),
          SKIP_WRITE_BARRIER, SKIP_ICACHE_FLUSH);
    } else {
      it.rinfo()->apply(delta);
    }
  }

  // Flush the i-cache here instead of in AddOwnedCode, to include the changes
  // made while iterating over the RelocInfo above.
  Assembler::FlushICache(ret->instructions().start(),
                         ret->instructions().size());
  if (FLAG_print_code || FLAG_print_wasm_code) {
    // TODO(mstarzinger): don't need the isolate here.
    ret->Print(source_pos_table->GetIsolate());
  }
  ret->Validate();
  return ret;
}

#if V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_S390X || V8_TARGET_ARCH_ARM64
Address NativeModule::CreateTrampolineTo(Handle<Code> code) {
  MacroAssembler masm(code->GetIsolate(), nullptr, 0, CodeObjectRequired::kNo);
  Address dest = code->raw_instruction_start();
  GenerateJumpTrampoline(&masm, dest);
  CodeDesc code_desc;
  masm.GetCode(nullptr, &code_desc);
  Vector<const byte> instructions(code_desc.buffer,
                                  static_cast<size_t>(code_desc.instr_size));
  WasmCode* wasm_code = AddOwnedCode(instructions,           // instructions
                                     nullptr,                // reloc_info
                                     0,                      // reloc_size
                                     nullptr,                // source_pos
                                     0,                      // source_pos_size
                                     Nothing<uint32_t>(),    // index
                                     WasmCode::kTrampoline,  // kind
                                     0,   // constant_pool_offset
                                     0,   // stack_slots
                                     0,   // safepoint_table_offset
                                     0,   // handler_table_offset
                                     {},  // protected_instructions
                                     WasmCode::kOther,         // tier
                                     WasmCode::kFlushICache);  // flush_icache
  Address ret = wasm_code->instruction_start();
  trampolines_.emplace(std::make_pair(dest, ret));
  return ret;
}
#else
Address NativeModule::CreateTrampolineTo(Handle<Code> code) {
  Address ret = code->raw_instruction_start();
  trampolines_.insert(std::make_pair(ret, ret));
  return ret;
}
#endif

Address NativeModule::GetLocalAddressFor(Handle<Code> code) {
  DCHECK(Heap::IsImmovable(*code));

  // Limit calls of {Code} objects on the GC heap to builtins (i.e. disallow
  // calls to {CodeStub} or dynamic code). The serializer depends on this.
  DCHECK(code->is_builtin());

  Address index = code->raw_instruction_start();
  auto trampoline_iter = trampolines_.find(index);
  if (trampoline_iter == trampolines_.end()) {
    return CreateTrampolineTo(code);
  } else {
    return trampoline_iter->second;
  }
}

Code* NativeModule::ReverseTrampolineLookup(Address target) {
  // Uses sub-optimal linear search, but is only used for debugging.
  for (auto pair : trampolines_) {
    if (pair.second == target) {
      return Code::GetCodeFromTargetAddress(pair.first);
    }
  }
  UNREACHABLE();
  return nullptr;
}

Address NativeModule::AllocateForCode(size_t size) {
  // This happens under a lock assumed by the caller.
  size = RoundUp(size, kCodeAlignment);
  AddressRange mem = free_code_space_.Allocate(size);
  if (mem.is_empty()) {
    if (!can_request_more_memory_) return kNullAddress;

    Address hint = owned_code_space_.empty() ? kNullAddress
                                             : owned_code_space_.back().end();
    VirtualMemory empty_mem;
    owned_code_space_.push_back(empty_mem);
    VirtualMemory& new_mem = owned_code_space_.back();
    wasm_code_manager_->TryAllocate(size, &new_mem,
                                    reinterpret_cast<void*>(hint));
    if (!new_mem.IsReserved()) return kNullAddress;
    wasm_code_manager_->AssignRanges(new_mem.address(), new_mem.end(), this);

    free_code_space_.Merge({new_mem.address(), new_mem.end()});
    mem = free_code_space_.Allocate(size);
    if (mem.is_empty()) return kNullAddress;
  }
  Address commit_start = RoundUp(mem.start, AllocatePageSize());
  Address commit_end = RoundUp(mem.end, AllocatePageSize());
  // {commit_start} will be either mem.start or the start of the next page.
  // {commit_end} will be the start of the page after the one in which
  // the allocation ends.
  // We start from an aligned start, and we know we allocated vmem in
  // page multiples.
  // We just need to commit what's not committed. The page in which we
  // start is already committed (or we start at the beginning of a page).
  // The end needs to be committed all through the end of the page.
  if (commit_start < commit_end) {
#if V8_OS_WIN
    // On Windows, we cannot commit a range that straddles different
    // reservations of virtual memory. Because we bump-allocate, and because, if
    // we need more memory, we append that memory at the end of the
    // owned_code_space_ list, we traverse that list in reverse order to find
    // the reservation(s) that guide how to chunk the region to commit.
    for (auto it = owned_code_space_.crbegin(),
              rend = owned_code_space_.crend();
         it != rend && commit_start < commit_end; ++it) {
      if (commit_end > it->end() || it->address() >= commit_end) continue;
      Address start = std::max(commit_start, it->address());
      size_t commit_size = static_cast<size_t>(commit_end - start);
      DCHECK(IsAligned(commit_size, AllocatePageSize()));
      if (!wasm_code_manager_->Commit(start, commit_size)) {
        return kNullAddress;
      }
      committed_code_space_ += commit_size;
      commit_end = start;
    }
#else
    size_t commit_size = static_cast<size_t>(commit_end - commit_start);
    DCHECK(IsAligned(commit_size, AllocatePageSize()));
    if (!wasm_code_manager_->Commit(commit_start, commit_size)) {
      return kNullAddress;
    }
    committed_code_space_ += commit_size;
#endif
  }
  DCHECK(IsAligned(mem.start, kCodeAlignment));
  allocated_code_space_.Merge(std::move(mem));
  TRACE_HEAP("ID: %zu. Code alloc: %p,+%zu\n", instance_id,
             reinterpret_cast<void*>(mem.start), size);
  return mem.start;
}

WasmCode* NativeModule::Lookup(Address pc) {
  if (owned_code_.empty()) return nullptr;
  auto iter = std::upper_bound(owned_code_.begin(), owned_code_.end(), pc,
                               WasmCodeUniquePtrComparator());
  if (iter == owned_code_.begin()) return nullptr;
  --iter;
  WasmCode* candidate = iter->get();
  DCHECK_NOT_NULL(candidate);
  return candidate->contains(pc) ? candidate : nullptr;
}

Address NativeModule::GetCallTargetForFunction(uint32_t func_index) {
  // TODO(clemensh): Introduce a jump table and return a slot of it here.
  WasmCode* wasm_code = code(func_index);
  if (!wasm_code) return kNullAddress;
  if (wasm_code->kind() != WasmCode::kLazyStub) {
    return wasm_code->instruction_start();
  }

  DCHECK_IMPLIES(func_index < num_imported_functions_,
                 !wasm_code->IsAnonymous());
  if (!wasm_code->IsAnonymous()) {
    // If the function wasn't imported, its index should match.
    DCHECK_IMPLIES(func_index >= num_imported_functions_,
                   func_index == wasm_code->index());
    return wasm_code->instruction_start();
  }
  if (lazy_compile_stubs_ == nullptr) {
    lazy_compile_stubs_.reset(new WasmCode*[num_functions_]);
    memset(lazy_compile_stubs_.get(), 0, num_functions_ * sizeof(WasmCode*));
  }
  WasmCode* cloned_code = lazy_compile_stubs_[func_index];
  if (cloned_code == nullptr) {
    cloned_code = CloneCode(wasm_code, WasmCode::kNoFlushICache);
    RelocateCode(cloned_code, wasm_code, WasmCode::kFlushICache);
    cloned_code->index_ = Just(func_index);
    lazy_compile_stubs_[func_index] = cloned_code;
  }
  DCHECK_EQ(func_index, cloned_code->index());
  return cloned_code->instruction_start();
}

WasmCode* NativeModule::CloneCode(const WasmCode* original_code,
                                  WasmCode::FlushICache flush_icache) {
  std::unique_ptr<byte[]> reloc_info;
  if (original_code->reloc_info().size() > 0) {
    reloc_info.reset(new byte[original_code->reloc_info().size()]);
    memcpy(reloc_info.get(), original_code->reloc_info().start(),
           original_code->reloc_info().size());
  }
  std::unique_ptr<byte[]> source_pos;
  if (original_code->source_positions().size() > 0) {
    source_pos.reset(new byte[original_code->source_positions().size()]);
    memcpy(source_pos.get(), original_code->source_positions().start(),
           original_code->source_positions().size());
  }
  DCHECK_EQ(0, original_code->protected_instructions().size());
  std::unique_ptr<ProtectedInstructions> protected_instructions(
      new ProtectedInstructions(0));
  WasmCode* ret = AddOwnedCode(
      original_code->instructions(), std::move(reloc_info),
      original_code->reloc_info().size(), std::move(source_pos),
      original_code->source_positions().size(), original_code->index_,
      original_code->kind(), original_code->constant_pool_offset_,
      original_code->stack_slots(), original_code->safepoint_table_offset_,
      original_code->handler_table_offset_, std::move(protected_instructions),
      original_code->tier(), flush_icache);
  if (!ret->IsAnonymous()) {
    set_code(ret->index(), ret);
  }
  return ret;
}

void NativeModule::UnpackAndRegisterProtectedInstructions() {
  for (WasmCode* wasm_code : code_table()) {
    if (wasm_code == nullptr) continue;
    wasm_code->RegisterTrapHandlerData();
  }
}

void NativeModule::ReleaseProtectedInstructions() {
  for (WasmCode* wasm_code : code_table()) {
    if (wasm_code == nullptr || !wasm_code->HasTrapHandlerIndex()) continue;
    CHECK_LT(wasm_code->trap_handler_index(),
             static_cast<size_t>(std::numeric_limits<int>::max()));
    trap_handler::ReleaseHandlerData(
        static_cast<int>(wasm_code->trap_handler_index()));
    wasm_code->ResetTrapHandlerIndex();
  }
}

void NativeModule::DisableTrapHandler() {
  // Switch {use_trap_handler_} from true to false.
  DCHECK(use_trap_handler_);
  use_trap_handler_ = false;

  // Clear the code table (just to increase the chances to hit an error if we
  // forget to re-add all code).
  uint32_t num_wasm_functions = num_functions_ - num_imported_functions_;
  memset(code_table_.get(), 0, num_wasm_functions * sizeof(WasmCode*));

  // TODO(clemensh): Actually free the owned code, such that the memory can be
  // recycled.
}

NativeModule::~NativeModule() {
  TRACE_HEAP("Deleting native module: %p\n", reinterpret_cast<void*>(this));
  // Clear the handle at the beginning of destructor to make it robust against
  // potential GCs in the rest of the destructor.
  if (module_object_ != nullptr) {
    Isolate* isolate = module_object()->GetIsolate();
    isolate->global_handles()->Destroy(
        reinterpret_cast<Object**>(module_object_));
    module_object_ = nullptr;
  }
  wasm_code_manager_->FreeNativeModule(this);
}

WasmCodeManager::WasmCodeManager(size_t max_committed) {
  DCHECK_LE(max_committed, kMaxWasmCodeMemory);
  remaining_uncommitted_code_space_.store(max_committed);
}

bool WasmCodeManager::Commit(Address start, size_t size) {
  DCHECK(IsAligned(start, AllocatePageSize()));
  DCHECK(IsAligned(size, AllocatePageSize()));
  // Reserve the size. Use CAS loop to avoid underflow on
  // {remaining_uncommitted_}. Temporary underflow would allow concurrent
  // threads to over-commit.
  while (true) {
    size_t old_value = remaining_uncommitted_code_space_.load();
    if (old_value < size) return false;
    if (remaining_uncommitted_code_space_.compare_exchange_weak(
            old_value, old_value - size)) {
      break;
    }
  }
  PageAllocator::Permission permission = FLAG_wasm_write_protect_code_memory
                                             ? PageAllocator::kReadWrite
                                             : PageAllocator::kReadWriteExecute;

  bool ret = SetPermissions(start, size, permission);
  TRACE_HEAP("Setting rw permissions for %p:%p\n",
             reinterpret_cast<void*>(start),
             reinterpret_cast<void*>(start + size));

  if (!ret) {
    // Highly unlikely.
    remaining_uncommitted_code_space_.fetch_add(size);
    return false;
  }
  return ret;
}

void WasmCodeManager::AssignRanges(Address start, Address end,
                                   NativeModule* native_module) {
  lookup_map_.insert(std::make_pair(start, std::make_pair(end, native_module)));
}

void WasmCodeManager::TryAllocate(size_t size, VirtualMemory* ret, void* hint) {
  DCHECK_GT(size, 0);
  size = RoundUp(size, AllocatePageSize());
  if (hint == nullptr) hint = GetRandomMmapAddr();

  if (!AlignedAllocVirtualMemory(size, static_cast<size_t>(AllocatePageSize()),
                                 hint, ret)) {
    DCHECK(!ret->IsReserved());
  }
  TRACE_HEAP("VMem alloc: %p:%p (%zu)\n",
             reinterpret_cast<void*>(ret->address()),
             reinterpret_cast<void*>(ret->end()), ret->size());
}

size_t WasmCodeManager::EstimateNativeModuleSize(const WasmModule* module) {
  constexpr size_t kCodeSizeMultiplier = 4;
  constexpr size_t kImportSize = 32 * kPointerSize;

  size_t estimate =
      AllocatePageSize() /* TODO(titzer): 1 page spot bonus */ +
      sizeof(NativeModule) +
      (sizeof(WasmCode*) * module->functions.size() /* code table size */) +
      (sizeof(WasmCode) * module->functions.size() /* code object size */) +
      (kImportSize * module->num_imported_functions /* import size */);

  for (auto& function : module->functions) {
    estimate += kCodeSizeMultiplier * function.code.length();
  }

  return estimate;
}

std::unique_ptr<NativeModule> WasmCodeManager::NewNativeModule(
    Isolate* isolate, const WasmModule& module, ModuleEnv& env) {
  size_t memory_estimate = EstimateNativeModuleSize(&module);
  return NewNativeModule(
      isolate, memory_estimate, static_cast<uint32_t>(module.functions.size()),
      module.num_imported_functions, kModuleCanAllocateMoreMemory, env);
}

std::unique_ptr<NativeModule> WasmCodeManager::NewNativeModule(
    Isolate* isolate, size_t memory_estimate, uint32_t num_functions,
    uint32_t num_imported_functions, bool can_request_more, ModuleEnv& env) {
  // TODO(titzer): we force a critical memory pressure notification
  // when the code space is almost exhausted, but only upon the next module
  // creation. This is only for one isolate, and it should really do this for
  // all isolates, at the point of commit.
  constexpr size_t kCriticalThreshold = 32 * 1024 * 1024;
  bool force_critical_notification =
      (active_ > 1) &&
      (remaining_uncommitted_code_space_.load() < kCriticalThreshold);

  if (force_critical_notification) {
    (reinterpret_cast<v8::Isolate*>(isolate))
        ->MemoryPressureNotification(MemoryPressureLevel::kCritical);
  }

  VirtualMemory mem;
  // If the code must be contiguous, reserve enough address space up front.
  size_t vmem_size = kRequiresCodeRange ? kMaxWasmCodeMemory : memory_estimate;
  TryAllocate(vmem_size, &mem);
  if (mem.IsReserved()) {
    Address start = mem.address();
    size_t size = mem.size();
    Address end = mem.end();
    std::unique_ptr<NativeModule> ret(
        new NativeModule(isolate, num_functions, num_imported_functions,
                         can_request_more, &mem, this, env));
    TRACE_HEAP("New Module: ID:%zu. Mem: %p,+%zu\n", ret->instance_id,
               reinterpret_cast<void*>(start), size);
    AssignRanges(start, end, ret.get());
    ++active_;
    return ret;
  }

  V8::FatalProcessOutOfMemory(isolate, "WasmCodeManager::NewNativeModule");
  return nullptr;
}

bool NativeModule::SetExecutable(bool executable) {
  if (is_executable_ == executable) return true;
  TRACE_HEAP("Setting module %zu as executable: %d.\n", instance_id,
             executable);
  PageAllocator::Permission permission =
      executable ? PageAllocator::kReadExecute : PageAllocator::kReadWrite;

  if (FLAG_wasm_write_protect_code_memory) {
#if V8_OS_WIN
    // On windows, we need to switch permissions per separate virtual memory
    // reservation. This is really just a problem when the NativeModule is
    // growable (meaning can_request_more_memory_). That's 32-bit in production,
    // or unittests.
    // For now, in that case, we commit at reserved memory granularity.
    // Technically, that may be a waste, because we may reserve more than we
    // use. On 32-bit though, the scarce resource is the address space -
    // committed or not.
    if (can_request_more_memory_) {
      for (auto& vmem : owned_code_space_) {
        if (!SetPermissions(vmem.address(), vmem.size(), permission)) {
          return false;
        }
        TRACE_HEAP("Set %p:%p to executable:%d\n", vmem.address(), vmem.end(),
                   executable);
      }
      is_executable_ = executable;
      return true;
    }
#endif
    for (auto& range : allocated_code_space_.ranges()) {
      // allocated_code_space_ is fine-grained, so we need to
      // page-align it.
      size_t range_size = RoundUp(range.size(), AllocatePageSize());
      if (!SetPermissions(range.start, range_size, permission)) {
        return false;
      }
      TRACE_HEAP("Set %p:%p to executable:%d\n",
                 reinterpret_cast<void*>(range.start),
                 reinterpret_cast<void*>(range.end), executable);
    }
  }
  is_executable_ = executable;
  return true;
}

void WasmCodeManager::FreeNativeModule(NativeModule* native_module) {
  DCHECK_GE(active_, 1);
  --active_;
  TRACE_HEAP("Freeing %zu\n", native_module->instance_id);
  for (auto& vmem : native_module->owned_code_space_) {
    lookup_map_.erase(vmem.address());
    Free(&vmem);
    DCHECK(!vmem.IsReserved());
  }
  native_module->owned_code_space_.clear();

  size_t code_size = native_module->committed_code_space_;
  DCHECK(IsAligned(code_size, AllocatePageSize()));

  if (module_code_size_mb_) {
    module_code_size_mb_->AddSample(static_cast<int>(code_size / MB));
  }

  remaining_uncommitted_code_space_.fetch_add(code_size);
}

// TODO(wasm): We can make this more efficient if needed. For
// example, we can preface the first instruction with a pointer to
// the WasmCode. In the meantime, we have a separate API so we can
// easily identify those places where we know we have the first
// instruction PC.
WasmCode* WasmCodeManager::GetCodeFromStartAddress(Address pc) const {
  WasmCode* code = LookupCode(pc);
  // This method can only be called for valid instruction start addresses.
  DCHECK_NOT_NULL(code);
  DCHECK_EQ(pc, code->instruction_start());
  return code;
}

WasmCode* WasmCodeManager::LookupCode(Address pc) const {
  if (lookup_map_.empty()) return nullptr;

  auto iter = lookup_map_.upper_bound(pc);
  if (iter == lookup_map_.begin()) return nullptr;
  --iter;
  Address range_start = iter->first;
  Address range_end = iter->second.first;
  NativeModule* candidate = iter->second.second;

  DCHECK_NOT_NULL(candidate);
  if (range_start <= pc && pc < range_end) return candidate->Lookup(pc);
  return nullptr;
}

void WasmCodeManager::Free(VirtualMemory* mem) {
  DCHECK(mem->IsReserved());
  void* start = reinterpret_cast<void*>(mem->address());
  void* end = reinterpret_cast<void*>(mem->end());
  size_t size = mem->size();
  mem->Free();
  TRACE_HEAP("VMem Release: %p:%p (%zu)\n", start, end, size);
}

size_t WasmCodeManager::remaining_uncommitted_code_space() const {
  return remaining_uncommitted_code_space_.load();
}

NativeModuleModificationScope::NativeModuleModificationScope(
    NativeModule* native_module)
    : native_module_(native_module) {
  if (native_module_ && (native_module_->modification_scope_depth_++) == 0) {
    bool success = native_module_->SetExecutable(false);
    CHECK(success);
  }
}

NativeModuleModificationScope::~NativeModuleModificationScope() {
  if (native_module_ && (native_module_->modification_scope_depth_--) == 1) {
    bool success = native_module_->SetExecutable(true);
    CHECK(success);
  }
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
#undef TRACE_HEAP
