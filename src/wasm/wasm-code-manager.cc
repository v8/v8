// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-code-manager.h"

#include <iomanip>

#include "src/assembler-inl.h"
#include "src/base/adapters.h"
#include "src/base/macros.h"
#include "src/base/platform/platform.h"
#include "src/disassembler.h"
#include "src/globals.h"
#include "src/log.h"
#include "src/macro-assembler-inl.h"
#include "src/macro-assembler.h"
#include "src/objects-inl.h"
#include "src/ostreams.h"
#include "src/snapshot/embedded-data.h"
#include "src/vector.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/jump-table-assembler.h"
#include "src/wasm/wasm-import-wrapper-cache.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-objects.h"

#define TRACE_HEAP(...)                                   \
  do {                                                    \
    if (FLAG_trace_wasm_native_heap) PrintF(__VA_ARGS__); \
  } while (false)

namespace v8 {
namespace internal {
namespace wasm {

using trap_handler::ProtectedInstructionData;

void DisjointAllocationPool::Merge(base::AddressRegion region) {
  auto dest_it = regions_.begin();
  auto dest_end = regions_.end();

  // Skip over dest regions strictly before {region}.
  while (dest_it != dest_end && dest_it->end() < region.begin()) ++dest_it;

  // After last dest region: insert and done.
  if (dest_it == dest_end) {
    regions_.push_back(region);
    return;
  }

  // Adjacent (from below) to dest: merge and done.
  if (dest_it->begin() == region.end()) {
    base::AddressRegion merged_region{region.begin(),
                                      region.size() + dest_it->size()};
    DCHECK_EQ(merged_region.end(), dest_it->end());
    *dest_it = merged_region;
    return;
  }

  // Before dest: insert and done.
  if (dest_it->begin() > region.end()) {
    regions_.insert(dest_it, region);
    return;
  }

  // Src is adjacent from above. Merge and check whether the merged region is
  // now adjacent to the next region.
  DCHECK_EQ(dest_it->end(), region.begin());
  dest_it->set_size(dest_it->size() + region.size());
  DCHECK_EQ(dest_it->end(), region.end());
  auto next_dest = dest_it;
  ++next_dest;
  if (next_dest != dest_end && dest_it->end() == next_dest->begin()) {
    dest_it->set_size(dest_it->size() + next_dest->size());
    DCHECK_EQ(dest_it->end(), next_dest->end());
    regions_.erase(next_dest);
  }
}

base::AddressRegion DisjointAllocationPool::Allocate(size_t size) {
  for (auto it = regions_.begin(), end = regions_.end(); it != end; ++it) {
    if (size > it->size()) continue;
    base::AddressRegion ret{it->begin(), size};
    if (size == it->size()) {
      regions_.erase(it);
    } else {
      *it = base::AddressRegion{it->begin() + size, it->size() - size};
    }
    return ret;
  }
  return {};
}

Address WasmCode::constant_pool() const {
  if (FLAG_enable_embedded_constant_pool) {
    if (constant_pool_offset_ < code_comments_offset_) {
      return instruction_start() + constant_pool_offset_;
    }
  }
  return kNullAddress;
}

Address WasmCode::code_comments() const {
  if (code_comments_offset_ < unpadded_binary_size_) {
    return instruction_start() + code_comments_offset_;
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
  DCHECK(!HasTrapHandlerIndex());
  if (kind() != WasmCode::kFunction) return;
  if (protected_instructions_.is_empty()) return;

  Address base = instruction_start();

  size_t size = instructions().size();
  const int index =
      RegisterHandlerData(base, size, protected_instructions().size(),
                          protected_instructions().start());

  // TODO(eholk): if index is negative, fail.
  CHECK_LE(0, index);
  set_trap_handler_index(static_cast<size_t>(index));
}

bool WasmCode::HasTrapHandlerIndex() const { return trap_handler_index_ >= 0; }

bool WasmCode::ShouldBeLogged(Isolate* isolate) {
  // The return value is cached in {WasmEngine::IsolateData::log_codes}. Ensure
  // to call {WasmEngine::EnableCodeLogging} if this return value would change
  // for any isolate. Otherwise we might lose code events.
  return isolate->logger()->is_listening_to_code_events() ||
         isolate->is_profiling();
}

void WasmCode::LogCode(Isolate* isolate) const {
  DCHECK(ShouldBeLogged(isolate));
  if (IsAnonymous()) return;

  ModuleWireBytes wire_bytes(native_module()->wire_bytes());
  // TODO(herhut): Allow to log code without on-heap round-trip of the name.
  WireBytesRef name_ref =
      native_module()->module()->LookupFunctionName(wire_bytes, index());
  WasmName name_vec = wire_bytes.GetNameOrNull(name_ref);
  if (!name_vec.is_empty()) {
    HandleScope scope(isolate);
    MaybeHandle<String> maybe_name = isolate->factory()->NewStringFromUtf8(
        Vector<const char>::cast(name_vec));
    Handle<String> name;
    if (!maybe_name.ToHandle(&name)) {
      name = isolate->factory()->NewStringFromAsciiChecked("<name too long>");
    }
    int name_length;
    auto cname =
        name->ToCString(AllowNullsFlag::DISALLOW_NULLS,
                        RobustnessFlag::ROBUST_STRING_TRAVERSAL, &name_length);
    PROFILE(isolate,
            CodeCreateEvent(CodeEventListener::FUNCTION_TAG, this,
                            {cname.get(), static_cast<size_t>(name_length)}));
  } else {
    EmbeddedVector<char, 32> generated_name;
    int length = SNPrintF(generated_name, "wasm-function[%d]", index());
    generated_name.Truncate(length);
    PROFILE(isolate, CodeCreateEvent(CodeEventListener::FUNCTION_TAG, this,
                                     generated_name));
  }

  if (!source_positions().is_empty()) {
    LOG_CODE_EVENT(isolate, CodeLinePosInfoRecordEvent(instruction_start(),
                                                       source_positions()));
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
      case RelocInfo::WASM_CALL: {
        Address target = it.rinfo()->wasm_call_address();
        WasmCode* code = native_module_->Lookup(target);
        CHECK_NOT_NULL(code);
        CHECK_EQ(WasmCode::kJumpTable, code->kind());
        CHECK_EQ(native_module()->jump_table_, code);
        CHECK(code->contains(target));
        break;
      }
      case RelocInfo::WASM_STUB_CALL: {
        Address target = it.rinfo()->wasm_stub_call_address();
        WasmCode* code = native_module_->Lookup(target);
        CHECK_NOT_NULL(code);
#ifdef V8_EMBEDDED_BUILTINS
        CHECK_EQ(WasmCode::kJumpTable, code->kind());
        CHECK_EQ(native_module()->runtime_stub_table_, code);
        CHECK(code->contains(target));
#else
        CHECK_EQ(WasmCode::kRuntimeStub, code->kind());
        CHECK_EQ(target, code->instruction_start());
#endif
        break;
      }
      case RelocInfo::INTERNAL_REFERENCE:
      case RelocInfo::INTERNAL_REFERENCE_ENCODED: {
        Address target = it.rinfo()->target_internal_reference();
        CHECK(contains(target));
        break;
      }
      case RelocInfo::EXTERNAL_REFERENCE:
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

void WasmCode::MaybePrint(const char* name) const {
  // Determines whether flags want this code to be printed.
  if ((FLAG_print_wasm_code && kind() == kFunction) ||
      (FLAG_print_wasm_stub_code && kind() != kFunction) || FLAG_print_code) {
    Print(name);
  }
}

void WasmCode::Print(const char* name) const {
  StdoutStream os;
  os << "--- WebAssembly code ---\n";
  Disassemble(name, os);
  os << "--- End code ---\n";
}

void WasmCode::Disassemble(const char* name, std::ostream& os,
                           Address current_pc) const {
  if (name) os << "name: " << name << "\n";
  if (!IsAnonymous()) os << "index: " << index() << "\n";
  os << "kind: " << GetWasmCodeKindAsString(kind_) << "\n";
  os << "compiler: " << (is_liftoff() ? "Liftoff" : "TurboFan") << "\n";
  size_t padding = instructions().size() - unpadded_binary_size_;
  os << "Body (size = " << instructions().size() << " = "
     << unpadded_binary_size_ << " + " << padding << " padding)\n";

#ifdef ENABLE_DISASSEMBLER
  size_t instruction_size = unpadded_binary_size_;
  if (constant_pool_offset_ < instruction_size) {
    instruction_size = constant_pool_offset_;
  }
  if (safepoint_table_offset_ && safepoint_table_offset_ < instruction_size) {
    instruction_size = safepoint_table_offset_;
  }
  if (handler_table_offset_ && handler_table_offset_ < instruction_size) {
    instruction_size = handler_table_offset_;
  }
  DCHECK_LT(0, instruction_size);
  os << "Instructions (size = " << instruction_size << ")\n";
  Disassembler::Decode(nullptr, &os, instructions().start(),
                       instructions().start() + instruction_size,
                       CodeReference(this), current_pc);
  os << "\n";

  if (handler_table_offset_ > 0) {
    HandlerTable table(instruction_start(), handler_table_offset_);
    os << "Exception Handler Table (size = " << table.NumberOfReturnEntries()
       << "):\n";
    table.HandlerTableReturnPrint(os);
    os << "\n";
  }

  if (!protected_instructions_.is_empty()) {
    os << "Protected instructions:\n pc offset  land pad\n";
    for (auto& data : protected_instructions()) {
      os << std::setw(10) << std::hex << data.instr_offset << std::setw(10)
         << std::hex << data.landing_offset << "\n";
    }
    os << "\n";
  }

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

  if (safepoint_table_offset_ > 0) {
    SafepointTable table(instruction_start(), safepoint_table_offset_,
                         stack_slots_);
    os << "Safepoints (size = " << table.size() << ")\n";
    for (uint32_t i = 0; i < table.length(); i++) {
      uintptr_t pc_offset = table.GetPcOffset(i);
      os << reinterpret_cast<const void*>(instruction_start() + pc_offset);
      os << std::setw(6) << std::hex << pc_offset << "  " << std::dec;
      table.PrintEntry(i, os);
      os << " (sp -> fp)";
      SafepointEntry entry = table.GetEntry(i);
      if (entry.trampoline_pc() != -1) {
        os << " trampoline: " << std::hex << entry.trampoline_pc() << std::dec;
      }
      if (entry.has_deoptimization_index()) {
        os << " deopt: " << std::setw(6) << entry.deoptimization_index();
      }
      os << "\n";
    }
    os << "\n";
  }

  os << "RelocInfo (size = " << reloc_info_.size() << ")\n";
  for (RelocIterator it(instructions(), reloc_info(), constant_pool());
       !it.done(); it.next()) {
    it.rinfo()->Print(nullptr, os);
  }
  os << "\n";

  if (code_comments_offset() < unpadded_binary_size_) {
    Address code_comments = reinterpret_cast<Address>(instructions().start() +
                                                      code_comments_offset());
    PrintCodeCommentsSection(os, code_comments);
  }
#endif  // ENABLE_DISASSEMBLER
}

const char* GetWasmCodeKindAsString(WasmCode::Kind kind) {
  switch (kind) {
    case WasmCode::kFunction:
      return "wasm function";
    case WasmCode::kWasmToJsWrapper:
      return "wasm-to-js";
    case WasmCode::kRuntimeStub:
      return "runtime-stub";
    case WasmCode::kInterpreterEntry:
      return "interpreter entry";
    case WasmCode::kJumpTable:
      return "jump table";
  }
  return "unknown kind";
}

WasmCode::~WasmCode() {
  if (HasTrapHandlerIndex()) {
    CHECK_LT(trap_handler_index(),
             static_cast<size_t>(std::numeric_limits<int>::max()));
    trap_handler::ReleaseHandlerData(static_cast<int>(trap_handler_index()));
  }
}

NativeModule::NativeModule(WasmEngine* engine, const WasmFeatures& enabled,
                           bool can_request_more, VirtualMemory code_space,
                           std::shared_ptr<const WasmModule> module,
                           std::shared_ptr<Counters> async_counters,
                           std::shared_ptr<NativeModule>* shared_this)
    : enabled_features_(enabled),
      module_(std::move(module)),
      import_wrapper_cache_(std::unique_ptr<WasmImportWrapperCache>(
          new WasmImportWrapperCache(this))),
      free_code_space_(code_space.region()),
      engine_(engine),
      can_request_more_memory_(can_request_more),
      use_trap_handler_(trap_handler::IsTrapHandlerEnabled() ? kUseTrapHandler
                                                             : kNoTrapHandler) {
  // We receive a pointer to an empty {std::shared_ptr}, and install ourselve
  // there.
  DCHECK_NOT_NULL(shared_this);
  DCHECK_NULL(*shared_this);
  shared_this->reset(this);
  compilation_state_ =
      CompilationState::New(*shared_this, std::move(async_counters));
  DCHECK_NOT_NULL(module_);
  owned_code_space_.emplace_back(std::move(code_space));
  owned_code_.reserve(num_functions());

  uint32_t num_wasm_functions = module_->num_declared_functions;
  if (num_wasm_functions > 0) {
    code_table_.reset(new WasmCode* [num_wasm_functions] {});

    jump_table_ = CreateEmptyJumpTable(
        JumpTableAssembler::SizeForNumberOfSlots(num_wasm_functions));
  }
}

void NativeModule::ReserveCodeTableForTesting(uint32_t max_functions) {
  DCHECK_LE(num_functions(), max_functions);
  WasmCode** new_table = new WasmCode* [max_functions] {};
  if (module_->num_declared_functions > 0) {
    memcpy(new_table, code_table_.get(),
           module_->num_declared_functions * sizeof(*new_table));
  }
  code_table_.reset(new_table);

  // Re-allocate jump table.
  jump_table_ = CreateEmptyJumpTable(
      JumpTableAssembler::SizeForNumberOfSlots(max_functions));
}

void NativeModule::LogWasmCodes(Isolate* isolate) {
  if (!WasmCode::ShouldBeLogged(isolate)) return;

  // TODO(titzer): we skip the logging of the import wrappers
  // here, but they should be included somehow.
  for (WasmCode* code : code_table()) {
    if (code != nullptr) code->LogCode(isolate);
  }
}

CompilationEnv NativeModule::CreateCompilationEnv() const {
  return {module(), use_trap_handler_, kRuntimeExceptionSupport,
          enabled_features_};
}

WasmCode* NativeModule::AddCodeForTesting(Handle<Code> code) {
  return AddAndPublishAnonymousCode(code, WasmCode::kFunction);
}

void NativeModule::UseLazyStubs() {
  uint32_t start = module_->num_imported_functions;
  uint32_t end = start + module_->num_declared_functions;
  for (uint32_t func_index = start; func_index < end; func_index++) {
    UseLazyStub(func_index);
  }
}

void NativeModule::UseLazyStub(uint32_t func_index) {
  DCHECK_LE(module_->num_imported_functions, func_index);
  DCHECK_LT(func_index,
            module_->num_imported_functions + module_->num_declared_functions);

  // Add jump table entry for jump to the lazy compile stub.
  uint32_t slot_index = func_index - module_->num_imported_functions;
  DCHECK_NE(runtime_stub_entry(WasmCode::kWasmCompileLazy), kNullAddress);
  JumpTableAssembler::EmitLazyCompileJumpSlot(
      jump_table_->instruction_start(), slot_index, func_index,
      runtime_stub_entry(WasmCode::kWasmCompileLazy), WasmCode::kFlushICache);
}

// TODO(mstarzinger): Remove {Isolate} parameter once {V8_EMBEDDED_BUILTINS}
// was removed and embedded builtins are no longer optional.
void NativeModule::SetRuntimeStubs(Isolate* isolate) {
  DCHECK_EQ(kNullAddress, runtime_stub_entries_[0]);  // Only called once.
#ifdef V8_EMBEDDED_BUILTINS
  WasmCode* jump_table =
      CreateEmptyJumpTable(JumpTableAssembler::SizeForNumberOfStubSlots(
          WasmCode::kRuntimeStubCount));
  Address base = jump_table->instruction_start();
  EmbeddedData embedded_data = EmbeddedData::FromBlob();
#define RUNTIME_STUB(Name) {Builtins::k##Name, WasmCode::k##Name},
#define RUNTIME_STUB_TRAP(Name) RUNTIME_STUB(ThrowWasm##Name)
  std::pair<Builtins::Name, WasmCode::RuntimeStubId> wasm_runtime_stubs[] = {
      WASM_RUNTIME_STUB_LIST(RUNTIME_STUB, RUNTIME_STUB_TRAP)};
#undef RUNTIME_STUB
#undef RUNTIME_STUB_TRAP
  for (auto pair : wasm_runtime_stubs) {
    CHECK(embedded_data.ContainsBuiltin(pair.first));
    Address builtin = embedded_data.InstructionStartOfBuiltin(pair.first);
    JumpTableAssembler::EmitRuntimeStubSlot(base, pair.second, builtin,
                                            WasmCode::kNoFlushICache);
    uint32_t slot_offset =
        JumpTableAssembler::StubSlotIndexToOffset(pair.second);
    runtime_stub_entries_[pair.second] = base + slot_offset;
  }
  FlushInstructionCache(jump_table->instructions().start(),
                        jump_table->instructions().size());
  DCHECK_NULL(runtime_stub_table_);
  runtime_stub_table_ = jump_table;
#else  // V8_EMBEDDED_BUILTINS
  HandleScope scope(isolate);
  USE(runtime_stub_table_);  // Actually unused, but avoids ifdef's in header.
#define COPY_BUILTIN(Name)                                        \
  runtime_stub_entries_[WasmCode::k##Name] =                      \
      AddAndPublishAnonymousCode(                                 \
          isolate->builtins()->builtin_handle(Builtins::k##Name), \
          WasmCode::kRuntimeStub, #Name)                          \
          ->instruction_start();
#define COPY_BUILTIN_TRAP(Name) COPY_BUILTIN(ThrowWasm##Name)
  WASM_RUNTIME_STUB_LIST(COPY_BUILTIN, COPY_BUILTIN_TRAP)
#undef COPY_BUILTIN_TRAP
#undef COPY_BUILTIN
#endif  // V8_EMBEDDED_BUILTINS
  DCHECK_NE(kNullAddress, runtime_stub_entries_[0]);
}

WasmCode* NativeModule::AddAndPublishAnonymousCode(Handle<Code> code,
                                                   WasmCode::Kind kind,
                                                   const char* name) {
  // For off-heap builtins, we create a copy of the off-heap instruction stream
  // instead of the on-heap code object containing the trampoline. Ensure that
  // we do not apply the on-heap reloc info to the off-heap instructions.
  const size_t relocation_size =
      code->is_off_heap_trampoline() ? 0 : code->relocation_size();
  OwnedVector<byte> reloc_info;
  if (relocation_size > 0) {
    reloc_info = OwnedVector<byte>::New(relocation_size);
    memcpy(reloc_info.start(), code->relocation_start(), relocation_size);
  }
  Handle<ByteArray> source_pos_table(code->SourcePositionTable(),
                                     code->GetIsolate());
  OwnedVector<byte> source_pos =
      OwnedVector<byte>::New(source_pos_table->length());
  if (source_pos_table->length() > 0) {
    source_pos_table->copy_out(0, source_pos.start(),
                               source_pos_table->length());
  }
  Vector<const byte> instructions(
      reinterpret_cast<byte*>(code->InstructionStart()),
      static_cast<size_t>(code->InstructionSize()));
  const uint32_t stack_slots = static_cast<uint32_t>(
      code->has_safepoint_info() ? code->stack_slots() : 0);

  // TODO(jgruber,v8:8758): Remove this translation. It exists only because
  // Code objects contains real offsets but WasmCode expects an offset of 0 to
  // mean 'empty'.
  const size_t safepoint_table_offset = static_cast<size_t>(
      code->has_safepoint_table() ? code->safepoint_table_offset() : 0);
  const size_t handler_table_offset = static_cast<size_t>(
      code->has_handler_table() ? code->handler_table_offset() : 0);
  const size_t constant_pool_offset =
      static_cast<size_t>(code->constant_pool_offset());
  const size_t code_comments_offset =
      static_cast<size_t>(code->code_comments_offset());

  Vector<uint8_t> dst_code_bytes = AllocateForCode(instructions.size());
  memcpy(dst_code_bytes.begin(), instructions.start(), instructions.size());

  // Apply the relocation delta by iterating over the RelocInfo.
  intptr_t delta = reinterpret_cast<Address>(dst_code_bytes.begin()) -
                   code->InstructionStart();
  int mode_mask = RelocInfo::kApplyMask |
                  RelocInfo::ModeMask(RelocInfo::WASM_STUB_CALL);
  Address constant_pool_start =
      reinterpret_cast<Address>(dst_code_bytes.begin()) + constant_pool_offset;
  RelocIterator orig_it(*code, mode_mask);
  for (RelocIterator it(dst_code_bytes, reloc_info.as_vector(),
                        constant_pool_start, mode_mask);
       !it.done(); it.next(), orig_it.next()) {
    RelocInfo::Mode mode = it.rinfo()->rmode();
    if (RelocInfo::IsWasmStubCall(mode)) {
      uint32_t stub_call_tag = orig_it.rinfo()->wasm_call_tag();
      DCHECK_LT(stub_call_tag, WasmCode::kRuntimeStubCount);
      Address entry = runtime_stub_entry(
          static_cast<WasmCode::RuntimeStubId>(stub_call_tag));
      it.rinfo()->set_wasm_stub_call_address(entry, SKIP_ICACHE_FLUSH);
    } else {
      it.rinfo()->apply(delta);
    }
  }

  // Flush the i-cache after relocation.
  FlushInstructionCache(dst_code_bytes.start(), dst_code_bytes.size());

  std::unique_ptr<WasmCode> new_code{new WasmCode{
      this,                                     // native_module
      WasmCode::kAnonymousFuncIndex,            // index
      dst_code_bytes,                           // instructions
      stack_slots,                              // stack_slots
      0,                                        // tagged_parameter_slots
      safepoint_table_offset,                   // safepoint_table_offset
      handler_table_offset,                     // handler_table_offset
      constant_pool_offset,                     // constant_pool_offset
      code_comments_offset,                     // code_comments_offset
      instructions.size(),                      // unpadded_binary_size
      OwnedVector<ProtectedInstructionData>{},  // protected_instructions
      std::move(reloc_info),                    // reloc_info
      std::move(source_pos),                    // source positions
      kind,                                     // kind
      WasmCode::kOther}};                       // tier
  new_code->MaybePrint(name);
  new_code->Validate();

  return PublishCode(std::move(new_code)).code;
}

std::unique_ptr<WasmCode> NativeModule::AddCode(
    uint32_t index, const CodeDesc& desc, uint32_t stack_slots,
    uint32_t tagged_parameter_slots,
    OwnedVector<trap_handler::ProtectedInstructionData> protected_instructions,
    OwnedVector<const byte> source_position_table, WasmCode::Kind kind,
    WasmCode::Tier tier) {
  return AddCodeWithCodeSpace(index, desc, stack_slots, tagged_parameter_slots,
                              std::move(protected_instructions),
                              std::move(source_position_table), kind, tier,
                              AllocateForCode(desc.instr_size));
}

std::unique_ptr<WasmCode> NativeModule::AddCodeWithCodeSpace(
    uint32_t index, const CodeDesc& desc, uint32_t stack_slots,
    uint32_t tagged_parameter_slots,
    OwnedVector<ProtectedInstructionData> protected_instructions,
    OwnedVector<const byte> source_position_table, WasmCode::Kind kind,
    WasmCode::Tier tier, Vector<uint8_t> dst_code_bytes) {
  OwnedVector<byte> reloc_info;
  if (desc.reloc_size > 0) {
    reloc_info = OwnedVector<byte>::New(desc.reloc_size);
    memcpy(reloc_info.start(), desc.buffer + desc.buffer_size - desc.reloc_size,
           desc.reloc_size);
  }

  // TODO(jgruber,v8:8758): Remove this translation. It exists only because
  // CodeDesc contains real offsets but WasmCode expects an offset of 0 to mean
  // 'empty'.
  const size_t safepoint_table_offset = static_cast<size_t>(
      desc.safepoint_table_size == 0 ? 0 : desc.safepoint_table_offset);
  const size_t handler_table_offset = static_cast<size_t>(
      desc.handler_table_size == 0 ? 0 : desc.handler_table_offset);
  const size_t constant_pool_offset =
      static_cast<size_t>(desc.constant_pool_offset);
  const size_t code_comments_offset =
      static_cast<size_t>(desc.code_comments_offset);
  const size_t instr_size = static_cast<size_t>(desc.instr_size);

  memcpy(dst_code_bytes.begin(), desc.buffer,
         static_cast<size_t>(desc.instr_size));

  // Apply the relocation delta by iterating over the RelocInfo.
  intptr_t delta = dst_code_bytes.begin() - desc.buffer;
  int mode_mask = RelocInfo::kApplyMask |
                  RelocInfo::ModeMask(RelocInfo::WASM_CALL) |
                  RelocInfo::ModeMask(RelocInfo::WASM_STUB_CALL);
  Address constant_pool_start =
      reinterpret_cast<Address>(dst_code_bytes.begin()) + constant_pool_offset;
  for (RelocIterator it(dst_code_bytes, reloc_info.as_vector(),
                        constant_pool_start, mode_mask);
       !it.done(); it.next()) {
    RelocInfo::Mode mode = it.rinfo()->rmode();
    if (RelocInfo::IsWasmCall(mode)) {
      uint32_t call_tag = it.rinfo()->wasm_call_tag();
      Address target = GetCallTargetForFunction(call_tag);
      it.rinfo()->set_wasm_call_address(target, SKIP_ICACHE_FLUSH);
    } else if (RelocInfo::IsWasmStubCall(mode)) {
      uint32_t stub_call_tag = it.rinfo()->wasm_call_tag();
      DCHECK_LT(stub_call_tag, WasmCode::kRuntimeStubCount);
      Address entry = runtime_stub_entry(
          static_cast<WasmCode::RuntimeStubId>(stub_call_tag));
      it.rinfo()->set_wasm_stub_call_address(entry, SKIP_ICACHE_FLUSH);
    } else {
      it.rinfo()->apply(delta);
    }
  }

  // Flush the i-cache after relocation.
  FlushInstructionCache(dst_code_bytes.start(), dst_code_bytes.size());

  std::unique_ptr<WasmCode> code{new WasmCode{
      this, index, dst_code_bytes, stack_slots, tagged_parameter_slots,
      safepoint_table_offset, handler_table_offset, constant_pool_offset,
      code_comments_offset, instr_size, std::move(protected_instructions),
      std::move(reloc_info), std::move(source_position_table), kind, tier}};
  code->MaybePrint();
  code->Validate();

  code->RegisterTrapHandlerData();

  return code;
}

WasmCodeUpdate NativeModule::PublishCode(std::unique_ptr<WasmCode> code) {
  base::MutexGuard lock(&allocation_mutex_);
  return PublishCodeLocked(std::move(code));
}

namespace {
WasmCode::Tier GetCodeTierForExecutionTier(ExecutionTier tier) {
  switch (tier) {
    case ExecutionTier::kInterpreter:
      return WasmCode::Tier::kOther;
    case ExecutionTier::kBaseline:
      return WasmCode::Tier::kLiftoff;
    case ExecutionTier::kOptimized:
      return WasmCode::Tier::kTurbofan;
  }
}

WasmCode::Kind GetCodeKindForExecutionTier(ExecutionTier tier) {
  switch (tier) {
    case ExecutionTier::kInterpreter:
      return WasmCode::Kind::kInterpreterEntry;
    case ExecutionTier::kBaseline:
    case ExecutionTier::kOptimized:
      return WasmCode::Kind::kFunction;
  }
}

// TODO(frgossen): We should merge ExecutionTier with WasmCode::Tier.
base::Optional<ExecutionTier> GetExecutionTier(WasmCode* code) {
  if (code == nullptr) return {};
  switch (code->tier()) {
    case WasmCode::Tier::kLiftoff:
      DCHECK_EQ(code->kind(), WasmCode::Kind::kFunction);
      return ExecutionTier::kBaseline;
    case WasmCode::Tier::kTurbofan:
      DCHECK_EQ(code->kind(), WasmCode::Kind::kFunction);
      return ExecutionTier::kOptimized;
    case WasmCode::Tier::kOther:
      if (code->kind() == WasmCode::Kind::kInterpreterEntry)
        return ExecutionTier::kInterpreter;
      return {};
  }
  UNREACHABLE();
}
}  // namespace

WasmCodeUpdate NativeModule::PublishCodeLocked(std::unique_ptr<WasmCode> code) {
  // The caller must hold the {allocation_mutex_}, thus we fail to lock it here.
  DCHECK(!allocation_mutex_.TryLock());
  WasmCodeUpdate update;

  if (!code->IsAnonymous()) {
    DCHECK_LT(code->index(), num_functions());
    DCHECK_LE(module_->num_imported_functions, code->index());

    // Assume an order of execution tiers that represents the quality of their
    // generated code.
    static_assert(ExecutionTier::kInterpreter < ExecutionTier::kBaseline &&
                      ExecutionTier::kBaseline < ExecutionTier::kOptimized,
                  "Assume an order on execution tiers");

    // Update code table but avoid to fall back to less optimized code. We use
    // the new code if it was compiled with a higher tier and also if we cannot
    // determine the tier.
    uint32_t slot_idx = code->index() - module_->num_imported_functions;
    WasmCode* prior_code = code_table_[slot_idx];
    update.prior_tier = GetExecutionTier(prior_code);
    update.tier = GetExecutionTier(code.get());
    bool update_code_table = !update.prior_tier.has_value() ||
                             !update.tier.has_value() ||
                             update.prior_tier.value() < update.tier.value();
    if (update_code_table) {
      code_table_[slot_idx] = code.get();
    }

    // Populate optimized code to the jump table unless there is an active
    // redirection to the interpreter that should be preserved.
    bool update_jump_table =
        update_code_table && !has_interpreter_redirection(code->index());

    // Ensure that interpreter entries always populate to the jump table.
    if (code->kind_ == WasmCode::Kind::kInterpreterEntry) {
      SetInterpreterRedirection(code->index());
      update_jump_table = true;
    }

    if (update_jump_table) {
      JumpTableAssembler::PatchJumpTableSlot(
          jump_table_->instruction_start(), slot_idx, code->instruction_start(),
          WasmCode::kFlushICache);
    }
  }
  update.code = code.get();
  owned_code_.emplace_back(std::move(code));
  return update;
}

WasmCode* NativeModule::AddDeserializedCode(
    uint32_t index, Vector<const byte> instructions, uint32_t stack_slots,
    uint32_t tagged_parameter_slots, size_t safepoint_table_offset,
    size_t handler_table_offset, size_t constant_pool_offset,
    size_t code_comments_offset, size_t unpadded_binary_size,
    OwnedVector<ProtectedInstructionData> protected_instructions,
    OwnedVector<const byte> reloc_info,
    OwnedVector<const byte> source_position_table, WasmCode::Kind kind,
    WasmCode::Tier tier) {
  Vector<uint8_t> dst_code_bytes = AllocateForCode(instructions.size());
  memcpy(dst_code_bytes.begin(), instructions.start(), instructions.size());

  std::unique_ptr<WasmCode> code{new WasmCode{
      this, index, dst_code_bytes, stack_slots, tagged_parameter_slots,
      safepoint_table_offset, handler_table_offset, constant_pool_offset,
      code_comments_offset, unpadded_binary_size,
      std::move(protected_instructions), std::move(reloc_info),
      std::move(source_position_table), kind, tier}};

  code->RegisterTrapHandlerData();

  // Note: we do not flush the i-cache here, since the code needs to be
  // relocated anyway. The caller is responsible for flushing the i-cache later.

  return PublishCode(std::move(code)).code;
}

std::vector<WasmCode*> NativeModule::SnapshotCodeTable() const {
  base::MutexGuard lock(&allocation_mutex_);
  std::vector<WasmCode*> result;
  result.reserve(code_table().size());
  for (WasmCode* code : code_table()) result.push_back(code);
  return result;
}

WasmCode* NativeModule::CreateEmptyJumpTable(uint32_t jump_table_size) {
  // Only call this if we really need a jump table.
  DCHECK_LT(0, jump_table_size);
  Vector<uint8_t> code_space = AllocateForCode(jump_table_size);
  ZapCode(reinterpret_cast<Address>(code_space.begin()), code_space.size());
  std::unique_ptr<WasmCode> code{new WasmCode{
      this,                                     // native_module
      WasmCode::kAnonymousFuncIndex,            // index
      code_space,                               // instructions
      0,                                        // stack_slots
      0,                                        // tagged_parameter_slots
      0,                                        // safepoint_table_offset
      0,                                        // handler_table_offset
      jump_table_size,                          // constant_pool_offset
      jump_table_size,                          // code_comments_offset
      jump_table_size,                          // unpadded_binary_size
      OwnedVector<ProtectedInstructionData>{},  // protected_instructions
      OwnedVector<const uint8_t>{},             // reloc_info
      OwnedVector<const uint8_t>{},             // source_pos
      WasmCode::kJumpTable,                     // kind
      WasmCode::kOther}};                       // tier
  return PublishCode(std::move(code)).code;
}

Vector<byte> NativeModule::AllocateForCode(size_t size) {
  base::MutexGuard lock(&allocation_mutex_);
  DCHECK_LT(0, size);
  v8::PageAllocator* page_allocator = GetPlatformPageAllocator();
  // This happens under a lock assumed by the caller.
  size = RoundUp<kCodeAlignment>(size);
  base::AddressRegion code_space = free_code_space_.Allocate(size);
  if (code_space.is_empty()) {
    if (!can_request_more_memory_) {
      V8::FatalProcessOutOfMemory(nullptr,
                                  "NativeModule::AllocateForCode reservation");
      UNREACHABLE();
    }

    Address hint = owned_code_space_.empty() ? kNullAddress
                                             : owned_code_space_.back().end();

    VirtualMemory new_mem = engine_->code_manager()->TryAllocate(
        size, reinterpret_cast<void*>(hint));
    if (!new_mem.IsReserved()) {
      V8::FatalProcessOutOfMemory(nullptr,
                                  "NativeModule::AllocateForCode reservation");
      UNREACHABLE();
    }
    engine_->code_manager()->AssignRanges(new_mem.address(), new_mem.end(),
                                          this);

    free_code_space_.Merge(new_mem.region());
    owned_code_space_.emplace_back(std::move(new_mem));
    code_space = free_code_space_.Allocate(size);
    DCHECK(!code_space.is_empty());
  }
  const Address page_size = page_allocator->AllocatePageSize();
  Address commit_start = RoundUp(code_space.begin(), page_size);
  Address commit_end = RoundUp(code_space.end(), page_size);
  // {commit_start} will be either code_space.start or the start of the next
  // page. {commit_end} will be the start of the page after the one in which
  // the allocation ends.
  // We start from an aligned start, and we know we allocated vmem in
  // page multiples.
  // We just need to commit what's not committed. The page in which we
  // start is already committed (or we start at the beginning of a page).
  // The end needs to be committed all through the end of the page.
  if (commit_start < commit_end) {
    committed_code_space_.fetch_add(commit_end - commit_start);
    // Committed code cannot grow bigger than maximum code space size.
    DCHECK_LE(committed_code_space_.load(), kMaxWasmCodeMemory);
#if V8_OS_WIN
    // On Windows, we cannot commit a region that straddles different
    // reservations of virtual memory. Because we bump-allocate, and because, if
    // we need more memory, we append that memory at the end of the
    // owned_code_space_ list, we traverse that list in reverse order to find
    // the reservation(s) that guide how to chunk the region to commit.
    for (auto& vmem : base::Reversed(owned_code_space_)) {
      if (commit_end <= vmem.address() || vmem.end() <= commit_start) continue;
      Address start = std::max(commit_start, vmem.address());
      Address end = std::min(commit_end, vmem.end());
      size_t commit_size = static_cast<size_t>(end - start);
      if (!engine_->code_manager()->Commit(start, commit_size)) {
        V8::FatalProcessOutOfMemory(nullptr,
                                    "NativeModule::AllocateForCode commit");
        UNREACHABLE();
      }
      // Opportunistically reduce the commit range. This might terminate the
      // loop early.
      if (commit_start == start) commit_start = end;
      if (commit_end == end) commit_end = start;
      if (commit_start >= commit_end) break;
    }
#else
    if (!engine_->code_manager()->Commit(commit_start,
                                         commit_end - commit_start)) {
      V8::FatalProcessOutOfMemory(nullptr,
                                  "NativeModule::AllocateForCode commit");
      UNREACHABLE();
    }
#endif
  }
  DCHECK(IsAligned(code_space.begin(), kCodeAlignment));
  allocated_code_space_.Merge(code_space);
  generated_code_size_.fetch_add(code_space.size(), std::memory_order_relaxed);

  TRACE_HEAP("Code alloc for %p: %" PRIxPTR ",+%zu\n", this, code_space.begin(),
             size);
  return {reinterpret_cast<byte*>(code_space.begin()), code_space.size()};
}

namespace {
class NativeModuleWireBytesStorage final : public WireBytesStorage {
 public:
  explicit NativeModuleWireBytesStorage(
      std::shared_ptr<OwnedVector<const uint8_t>> wire_bytes)
      : wire_bytes_(std::move(wire_bytes)) {}

  Vector<const uint8_t> GetCode(WireBytesRef ref) const final {
    return wire_bytes_->as_vector().SubVector(ref.offset(), ref.end_offset());
  }

 private:
  const std::shared_ptr<OwnedVector<const uint8_t>> wire_bytes_;
};
}  // namespace

void NativeModule::SetWireBytes(OwnedVector<const uint8_t> wire_bytes) {
  auto shared_wire_bytes =
      std::make_shared<OwnedVector<const uint8_t>>(std::move(wire_bytes));
  wire_bytes_ = shared_wire_bytes;
  if (!shared_wire_bytes->is_empty()) {
    compilation_state_->SetWireBytesStorage(
        std::make_shared<NativeModuleWireBytesStorage>(
            std::move(shared_wire_bytes)));
  }
}

WasmCode* NativeModule::Lookup(Address pc) const {
  base::MutexGuard lock(&allocation_mutex_);
  if (owned_code_.empty()) return nullptr;
  // First update the sorted portion counter.
  if (owned_code_sorted_portion_ == 0) ++owned_code_sorted_portion_;
  while (owned_code_sorted_portion_ < owned_code_.size() &&
         owned_code_[owned_code_sorted_portion_ - 1]->instruction_start() <=
             owned_code_[owned_code_sorted_portion_]->instruction_start()) {
    ++owned_code_sorted_portion_;
  }
  // Execute at most two rounds: First check whether the {pc} is within the
  // sorted portion of {owned_code_}. If it's not, then sort the whole vector
  // and retry.
  while (true) {
    auto iter =
        std::upper_bound(owned_code_.begin(), owned_code_.end(), pc,
                         [](Address pc, const std::unique_ptr<WasmCode>& code) {
                           DCHECK_NE(kNullAddress, pc);
                           DCHECK_NOT_NULL(code);
                           return pc < code->instruction_start();
                         });
    if (iter != owned_code_.begin()) {
      --iter;
      WasmCode* candidate = iter->get();
      DCHECK_NOT_NULL(candidate);
      if (candidate->contains(pc)) return candidate;
    }
    if (owned_code_sorted_portion_ == owned_code_.size()) return nullptr;
    std::sort(owned_code_.begin(), owned_code_.end(),
              [](const std::unique_ptr<WasmCode>& code1,
                 const std::unique_ptr<WasmCode>& code2) {
                return code1->instruction_start() < code2->instruction_start();
              });
    owned_code_sorted_portion_ = owned_code_.size();
  }
}

Address NativeModule::GetCallTargetForFunction(uint32_t func_index) const {
  // TODO(clemensh): Measure performance win of returning instruction start
  // directly if we have turbofan code. Downside: Redirecting functions (e.g.
  // for debugging) gets much harder.

  // Return the jump table slot for that function index.
  DCHECK_NOT_NULL(jump_table_);
  uint32_t slot_idx = func_index - module_->num_imported_functions;
  uint32_t slot_offset = JumpTableAssembler::SlotIndexToOffset(slot_idx);
  DCHECK_LT(slot_offset, jump_table_->instructions().size());
  return jump_table_->instruction_start() + slot_offset;
}

uint32_t NativeModule::GetFunctionIndexFromJumpTableSlot(
    Address slot_address) const {
  DCHECK(is_jump_table_slot(slot_address));
  uint32_t slot_offset =
      static_cast<uint32_t>(slot_address - jump_table_->instruction_start());
  uint32_t slot_idx = JumpTableAssembler::SlotOffsetToIndex(slot_offset);
  DCHECK_LT(slot_idx, module_->num_declared_functions);
  return module_->num_imported_functions + slot_idx;
}

void NativeModule::DisableTrapHandler() {
  // Switch {use_trap_handler_} from true to false.
  DCHECK(use_trap_handler_);
  use_trap_handler_ = kNoTrapHandler;

  // Clear the code table (just to increase the chances to hit an error if we
  // forget to re-add all code).
  uint32_t num_wasm_functions = module_->num_declared_functions;
  memset(code_table_.get(), 0, num_wasm_functions * sizeof(WasmCode*));

  // TODO(clemensh): Actually free the owned code, such that the memory can be
  // recycled.
}

const char* NativeModule::GetRuntimeStubName(Address runtime_stub_entry) const {
#define RETURN_NAME(Name)                                               \
  if (runtime_stub_entries_[WasmCode::k##Name] == runtime_stub_entry) { \
    return #Name;                                                       \
  }
#define RETURN_NAME_TRAP(Name) RETURN_NAME(ThrowWasm##Name)
  WASM_RUNTIME_STUB_LIST(RETURN_NAME, RETURN_NAME_TRAP)
#undef RETURN_NAME_TRAP
#undef RETURN_NAME
  return "<unknown>";
}

NativeModule::~NativeModule() {
  TRACE_HEAP("Deleting native module: %p\n", reinterpret_cast<void*>(this));
  // Cancel all background compilation before resetting any field of the
  // NativeModule or freeing anything.
  compilation_state_->AbortCompilation();
  engine_->FreeNativeModule(this);
}

WasmCodeManager::WasmCodeManager(WasmMemoryTracker* memory_tracker,
                                 size_t max_committed)
    : memory_tracker_(memory_tracker),
      remaining_uncommitted_code_space_(max_committed),
      critical_uncommitted_code_space_(max_committed / 2) {
  DCHECK_LE(max_committed, kMaxWasmCodeMemory);
}

bool WasmCodeManager::Commit(Address start, size_t size) {
  // TODO(v8:8462) Remove eager commit once perf supports remapping.
  if (FLAG_perf_prof) return true;
  DCHECK(IsAligned(start, AllocatePageSize()));
  DCHECK(IsAligned(size, AllocatePageSize()));
  // Reserve the size. Use CAS loop to avoid underflow on
  // {remaining_uncommitted_}. Temporary underflow would allow concurrent
  // threads to over-commit.
  size_t old_value = remaining_uncommitted_code_space_.load();
  while (true) {
    if (old_value < size) return false;
    if (remaining_uncommitted_code_space_.compare_exchange_weak(
            old_value, old_value - size)) {
      break;
    }
  }
  PageAllocator::Permission permission = FLAG_wasm_write_protect_code_memory
                                             ? PageAllocator::kReadWrite
                                             : PageAllocator::kReadWriteExecute;

  bool ret =
      SetPermissions(GetPlatformPageAllocator(), start, size, permission);
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
  base::MutexGuard lock(&native_modules_mutex_);
  lookup_map_.insert(std::make_pair(start, std::make_pair(end, native_module)));
}

VirtualMemory WasmCodeManager::TryAllocate(size_t size, void* hint) {
  v8::PageAllocator* page_allocator = GetPlatformPageAllocator();
  DCHECK_GT(size, 0);
  size = RoundUp(size, page_allocator->AllocatePageSize());
  if (!memory_tracker_->ReserveAddressSpace(size,
                                            WasmMemoryTracker::kHardLimit)) {
    return {};
  }
  if (hint == nullptr) hint = page_allocator->GetRandomMmapAddr();

  VirtualMemory mem(page_allocator, size, hint,
                    page_allocator->AllocatePageSize());
  if (!mem.IsReserved()) {
    memory_tracker_->ReleaseReservation(size);
    return {};
  }
  TRACE_HEAP("VMem alloc: %p:%p (%zu)\n",
             reinterpret_cast<void*>(mem.address()),
             reinterpret_cast<void*>(mem.end()), mem.size());

  // TODO(v8:8462) Remove eager commit once perf supports remapping.
  if (FLAG_perf_prof) {
    SetPermissions(GetPlatformPageAllocator(), mem.address(), mem.size(),
                   PageAllocator::kReadWriteExecute);
  }
  return mem;
}

void WasmCodeManager::SetMaxCommittedMemoryForTesting(size_t limit) {
  remaining_uncommitted_code_space_.store(limit);
  critical_uncommitted_code_space_.store(limit / 2);
}

// static
size_t WasmCodeManager::EstimateNativeModuleCodeSize(const WasmModule* module) {
  constexpr size_t kCodeSizeMultiplier = 4;
  constexpr size_t kCodeOverhead = 32;     // for prologue, stack check, ...
  constexpr size_t kStaticCodeSize = 512;  // runtime stubs, ...
  constexpr size_t kImportSize = 64 * kSystemPointerSize;

  size_t estimate = kStaticCodeSize;
  for (auto& function : module->functions) {
    estimate += kCodeOverhead + kCodeSizeMultiplier * function.code.length();
  }
  estimate +=
      JumpTableAssembler::SizeForNumberOfSlots(module->num_declared_functions);
  estimate += kImportSize * module->num_imported_functions;

  return estimate;
}

// static
size_t WasmCodeManager::EstimateNativeModuleNonCodeSize(
    const WasmModule* module) {
  size_t wasm_module_estimate = EstimateStoredSize(module);

  uint32_t num_wasm_functions = module->num_declared_functions;

  // TODO(wasm): Include wire bytes size.
  size_t native_module_estimate =
      sizeof(NativeModule) +                     /* NativeModule struct */
      (sizeof(WasmCode*) * num_wasm_functions) + /* code table size */
      (sizeof(WasmCode) * num_wasm_functions);   /* code object size */

  return wasm_module_estimate + native_module_estimate;
}

std::shared_ptr<NativeModule> WasmCodeManager::NewNativeModule(
    WasmEngine* engine, Isolate* isolate, const WasmFeatures& enabled,
    size_t code_size_estimate, bool can_request_more,
    std::shared_ptr<const WasmModule> module) {
  DCHECK_EQ(this, isolate->wasm_engine()->code_manager());
  if (remaining_uncommitted_code_space_.load() <
      critical_uncommitted_code_space_.load()) {
    (reinterpret_cast<v8::Isolate*>(isolate))
        ->MemoryPressureNotification(MemoryPressureLevel::kCritical);
    critical_uncommitted_code_space_.store(
        remaining_uncommitted_code_space_.load() / 2);
  }

  // If the code must be contiguous, reserve enough address space up front.
  size_t code_vmem_size =
      kRequiresCodeRange ? kMaxWasmCodeMemory : code_size_estimate;
  // Try up to two times; getting rid of dead JSArrayBuffer allocations might
  // require two GCs because the first GC maybe incremental and may have
  // floating garbage.
  static constexpr int kAllocationRetries = 2;
  VirtualMemory code_space;
  for (int retries = 0;; ++retries) {
    code_space = TryAllocate(code_vmem_size);
    if (code_space.IsReserved()) break;
    if (retries == kAllocationRetries) {
      V8::FatalProcessOutOfMemory(isolate, "WasmCodeManager::NewNativeModule");
      UNREACHABLE();
    }
    // Run one GC, then try the allocation again.
    isolate->heap()->MemoryPressureNotification(MemoryPressureLevel::kCritical,
                                                true);
  }

  Address start = code_space.address();
  size_t size = code_space.size();
  Address end = code_space.end();
  std::shared_ptr<NativeModule> ret;
  new NativeModule(engine, enabled, can_request_more, std::move(code_space),
                   std::move(module), isolate->async_counters(), &ret);
  // The constructor initialized the shared_ptr.
  DCHECK_NOT_NULL(ret);
  TRACE_HEAP("New NativeModule %p: Mem: %" PRIuPTR ",+%zu\n", ret.get(), start,
             size);
  base::MutexGuard lock(&native_modules_mutex_);
  lookup_map_.insert(std::make_pair(start, std::make_pair(end, ret.get())));
  return ret;
}

bool NativeModule::SetExecutable(bool executable) {
  if (is_executable_ == executable) return true;
  TRACE_HEAP("Setting module %p as executable: %d.\n", this, executable);

  v8::PageAllocator* page_allocator = GetPlatformPageAllocator();

  if (FLAG_wasm_write_protect_code_memory) {
    PageAllocator::Permission permission =
        executable ? PageAllocator::kReadExecute : PageAllocator::kReadWrite;
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
        if (!SetPermissions(page_allocator, vmem.address(), vmem.size(),
                            permission)) {
          return false;
        }
        TRACE_HEAP("Set %p:%p to executable:%d\n", vmem.address(), vmem.end(),
                   executable);
      }
      is_executable_ = executable;
      return true;
    }
#endif
    for (auto& region : allocated_code_space_.regions()) {
      // allocated_code_space_ is fine-grained, so we need to
      // page-align it.
      size_t region_size =
          RoundUp(region.size(), page_allocator->AllocatePageSize());
      if (!SetPermissions(page_allocator, region.begin(), region_size,
                          permission)) {
        return false;
      }
      TRACE_HEAP("Set %p:%p to executable:%d\n",
                 reinterpret_cast<void*>(region.begin()),
                 reinterpret_cast<void*>(region.end()), executable);
    }
  }
  is_executable_ = executable;
  return true;
}

void NativeModule::SampleCodeSize(
    Counters* counters, NativeModule::CodeSamplingTime sampling_time) const {
  size_t code_size = sampling_time == kSampling
                         ? committed_code_space()
                         : generated_code_size_.load(std::memory_order_relaxed);
  int code_size_mb = static_cast<int>(code_size / MB);
  Histogram* histogram = nullptr;
  switch (sampling_time) {
    case kAfterBaseline:
      histogram = counters->wasm_module_code_size_mb_after_baseline();
      break;
    case kAfterTopTier:
      histogram = counters->wasm_module_code_size_mb_after_top_tier();
      break;
    case kSampling:
      histogram = counters->wasm_module_code_size_mb();
      break;
  }
  histogram->AddSample(code_size_mb);
}

WasmCodeUpdate NativeModule::AddCompiledCode(WasmCompilationResult result) {
  return AddCompiledCode({&result, 1})[0];
}

std::vector<WasmCodeUpdate> NativeModule::AddCompiledCode(
    Vector<WasmCompilationResult> results) {
  DCHECK(!results.is_empty());
  // First, allocate code space for all the results.
  size_t total_code_space = 0;
  for (auto& result : results) {
    DCHECK(result.succeeded());
    total_code_space += RoundUp<kCodeAlignment>(result.code_desc.instr_size);
  }
  Vector<byte> code_space = AllocateForCode(total_code_space);

  std::vector<std::unique_ptr<WasmCode>> generated_code;
  generated_code.reserve(results.size());

  // Now copy the generated code into the code space and relocate it.
  for (auto& result : results) {
    DCHECK_EQ(result.code_desc.buffer, result.instr_buffer.get());
    size_t code_size = RoundUp<kCodeAlignment>(result.code_desc.instr_size);
    Vector<byte> this_code_space = code_space.SubVector(0, code_size);
    code_space += code_size;
    generated_code.emplace_back(AddCodeWithCodeSpace(
        result.func_index, result.code_desc, result.frame_slot_count,
        result.tagged_parameter_slots, std::move(result.protected_instructions),
        std::move(result.source_positions),
        GetCodeKindForExecutionTier(result.result_tier),
        GetCodeTierForExecutionTier(result.result_tier), this_code_space));
  }
  DCHECK_EQ(0, code_space.size());

  // Under the {allocation_mutex_}, publish the code.
  std::vector<WasmCodeUpdate> code_updates;
  code_updates.reserve(results.size());
  {
    base::MutexGuard lock(&allocation_mutex_);
    for (auto& result : generated_code)
      code_updates.push_back(PublishCodeLocked(std::move(result)));
  }

  return code_updates;
}

void NativeModule::FreeCode(Vector<WasmCode* const> codes) {
  // TODO(clemensh): Implement.
}

void WasmCodeManager::FreeNativeModule(NativeModule* native_module) {
  base::MutexGuard lock(&native_modules_mutex_);
  TRACE_HEAP("Freeing NativeModule %p\n", native_module);
  for (auto& code_space : native_module->owned_code_space_) {
    DCHECK(code_space.IsReserved());
    TRACE_HEAP("VMem Release: %" PRIxPTR ":%" PRIxPTR " (%zu)\n",
               code_space.address(), code_space.end(), code_space.size());
    lookup_map_.erase(code_space.address());
    memory_tracker_->ReleaseReservation(code_space.size());
    code_space.Free();
    DCHECK(!code_space.IsReserved());
  }
  native_module->owned_code_space_.clear();

  size_t code_size = native_module->committed_code_space_.load();
  DCHECK(IsAligned(code_size, AllocatePageSize()));
  remaining_uncommitted_code_space_.fetch_add(code_size);
  // Remaining code space cannot grow bigger than maximum code space size.
  DCHECK_LE(remaining_uncommitted_code_space_.load(), kMaxWasmCodeMemory);
}

NativeModule* WasmCodeManager::LookupNativeModule(Address pc) const {
  base::MutexGuard lock(&native_modules_mutex_);
  if (lookup_map_.empty()) return nullptr;

  auto iter = lookup_map_.upper_bound(pc);
  if (iter == lookup_map_.begin()) return nullptr;
  --iter;
  Address region_start = iter->first;
  Address region_end = iter->second.first;
  NativeModule* candidate = iter->second.second;

  DCHECK_NOT_NULL(candidate);
  return region_start <= pc && pc < region_end ? candidate : nullptr;
}

WasmCode* WasmCodeManager::LookupCode(Address pc) const {
  NativeModule* candidate = LookupNativeModule(pc);
  return candidate ? candidate->Lookup(pc) : nullptr;
}

size_t WasmCodeManager::remaining_uncommitted_code_space() const {
  return remaining_uncommitted_code_space_.load();
}

// TODO(v8:7424): Code protection scopes are not yet supported with shared code
// enabled and need to be revisited to work with --wasm-shared-code as well.
NativeModuleModificationScope::NativeModuleModificationScope(
    NativeModule* native_module)
    : native_module_(native_module) {
  if (FLAG_wasm_write_protect_code_memory && native_module_ &&
      (native_module_->modification_scope_depth_++) == 0) {
    bool success = native_module_->SetExecutable(false);
    CHECK(success);
  }
}

NativeModuleModificationScope::~NativeModuleModificationScope() {
  if (FLAG_wasm_write_protect_code_memory && native_module_ &&
      (native_module_->modification_scope_depth_--) == 1) {
    bool success = native_module_->SetExecutable(true);
    CHECK(success);
  }
}

namespace {
thread_local WasmCodeRefScope* current_code_refs_scope = nullptr;

// Receives a vector by value which is modified in this function.
void DecrementRefCount(std::vector<WasmCode*> code_vec) {
  // Decrement the ref counter of all given code objects. Keep the ones whose
  // ref count drops to zero.
  auto remaining_elements_it = code_vec.begin();
  for (auto it = code_vec.begin(), end = code_vec.end(); it != end; ++it) {
    if ((*it)->DecRef()) *remaining_elements_it++ = *it;
  }
  code_vec.resize(remaining_elements_it - code_vec.begin());

  // Sort the vector by NativeModule, then by instruction start.
  std::sort(code_vec.begin(), code_vec.end(),
            [](const WasmCode* a, const WasmCode* b) {
              return a->native_module() == b->native_module()
                         ? a->instruction_start() < b->instruction_start()
                         : a->native_module() < b->native_module();
            });
  // For each native module, free all its code objects at once.
  auto range_begin = code_vec.begin();
  while (range_begin != code_vec.end()) {
    NativeModule* native_module = (*range_begin)->native_module();
    auto range_end = range_begin + 1;
    while (range_end < code_vec.end() &&
           (*range_end)->native_module() == native_module) {
      ++range_end;
    }
    size_t range_size = static_cast<size_t>(range_end - range_begin);
    Vector<WasmCode*> code_vec{&*range_begin, range_size};
    native_module->FreeCode(code_vec);
    range_begin = range_end;
  }
}

}  // namespace

WasmCodeRefScope::WasmCodeRefScope()
    : previous_scope_(current_code_refs_scope) {
  current_code_refs_scope = this;
}

WasmCodeRefScope::~WasmCodeRefScope() {
  DCHECK_EQ(this, current_code_refs_scope);
  current_code_refs_scope = previous_scope_;
  DecrementRefCount({code_ptrs_.begin(), code_ptrs_.end()});
}

// static
void WasmCodeRefScope::AddRef(WasmCode* code) {
  WasmCodeRefScope* current_scope = current_code_refs_scope;
  // TODO(clemensh): Remove early return, activate DCHECK instead.
  // DCHECK_NOT_NULL(current_scope);
  if (!current_scope) return;
  auto entry = current_scope->code_ptrs_.insert(code);
  // If we added a new entry, increment the ref counter.
  if (entry.second) code->IncRef();
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
#undef TRACE_HEAP
