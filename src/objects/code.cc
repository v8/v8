// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/code.h"

#include <iomanip>

#include "src/codegen/assembler-inl.h"
#include "src/codegen/flush-instruction-cache.h"
#include "src/codegen/reloc-info-inl.h"
#include "src/codegen/source-position-table.h"
#include "src/codegen/source-position.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/objects/code-inl.h"

#ifdef ENABLE_DISASSEMBLER
#include "src/diagnostics/disassembler.h"
#include "src/diagnostics/eh-frame.h"
#endif

namespace v8 {
namespace internal {

Tagged<Object> Code::raw_deoptimization_data_or_interpreter_data() const {
  return RawProtectedPointerField(kDeoptimizationDataOrInterpreterDataOffset)
      .load();
}

Tagged<Object> Code::raw_position_table() const {
  return RawProtectedPointerField(kPositionTableOffset).load();
}

void Code::ClearEmbeddedObjectsAndJSDispatchHandles(Heap* heap) {
  DisallowGarbageCollection no_gc;
  Tagged<HeapObject> undefined = ReadOnlyRoots(heap).undefined_value();
  Tagged<InstructionStream> istream = unchecked_instruction_stream();
  int mode_mask = RelocInfo::EmbeddedObjectModeMask();
#ifdef V8_ENABLE_LEAPTIERING
  mode_mask |= RelocInfo::JSDispatchHandleModeMask();
#endif
  {
    WritableJitAllocation jit_allocation = ThreadIsolation::LookupJitAllocation(
        istream->address(), istream->Size(),
        ThreadIsolation::JitAllocationType::kInstructionStream, true);
    for (WritableRelocIterator it(jit_allocation, istream, constant_pool(),
                                  mode_mask);
         !it.done(); it.next()) {
      const auto mode = it.rinfo()->rmode();
      if (RelocInfo::IsEmbeddedObjectMode(mode)) {
        it.rinfo()->set_target_object(istream, undefined, SKIP_WRITE_BARRIER);
#ifdef V8_ENABLE_LEAPTIERING
      } else {
        it.rinfo()->set_js_dispatch_handle(istream, kNullJSDispatchHandle,
                                           SKIP_WRITE_BARRIER);
#endif  // V8_ENABLE_LEAPTIERING
      }
    }
  }
  set_embedded_objects_cleared(true);
}

void Code::FlushICache() const {
  FlushInstructionCache(instruction_start(), instruction_size());
}

int Code::SourcePosition(int offset) const {
  CHECK_NE(kind(), CodeKind::BASELINE);

  // Subtract one because the current PC is one instruction after the call site.
  offset--;

  int position = 0;
  if (!has_source_position_table()) return position;
  for (SourcePositionTableIterator it(
           source_position_table(),
           SourcePositionTableIterator::kJavaScriptOnly,
           SourcePositionTableIterator::kDontSkipFunctionEntry);
       !it.done() && it.code_offset() <= offset; it.Advance()) {
    position = it.source_position().ScriptOffset();
  }
  return position;
}

int Code::SourceStatementPosition(int offset) const {
  CHECK_NE(kind(), CodeKind::BASELINE);

  // Subtract one because the current PC is one instruction after the call site.
  offset--;

  int position = 0;
  if (!has_source_position_table()) return position;
  for (SourcePositionTableIterator it(source_position_table());
       !it.done() && it.code_offset() <= offset; it.Advance()) {
    if (it.is_statement()) {
      position = it.source_position().ScriptOffset();
    }
  }
  return position;
}

SafepointEntry Code::GetSafepointEntry(Isolate* isolate, Address pc) {
  DCHECK(!is_maglevved());
  SafepointTable table(isolate, pc, *this);
  return table.FindEntry(pc);
}

MaglevSafepointEntry Code::GetMaglevSafepointEntry(Isolate* isolate,
                                                   Address pc) {
  DCHECK(is_maglevved());
  MaglevSafepointTable table(isolate, pc, *this);
  return table.FindEntry(pc);
}

bool Code::IsIsolateIndependent(Isolate* isolate) {
  static constexpr int kModeMask =
      RelocInfo::AllRealModesMask() &
      ~RelocInfo::ModeMask(RelocInfo::CONST_POOL) &
      ~RelocInfo::ModeMask(RelocInfo::OFF_HEAP_TARGET) &
      ~RelocInfo::ModeMask(RelocInfo::VENEER_POOL) &
      ~RelocInfo::ModeMask(RelocInfo::WASM_CANONICAL_SIG_ID) &
      ~RelocInfo::ModeMask(RelocInfo::WASM_CODE_POINTER_TABLE_ENTRY);
  static_assert(kModeMask ==
                (RelocInfo::ModeMask(RelocInfo::CODE_TARGET) |
                 RelocInfo::ModeMask(RelocInfo::RELATIVE_CODE_TARGET) |
                 RelocInfo::ModeMask(RelocInfo::COMPRESSED_EMBEDDED_OBJECT) |
                 RelocInfo::ModeMask(RelocInfo::FULL_EMBEDDED_OBJECT) |
                 RelocInfo::ModeMask(RelocInfo::EXTERNAL_REFERENCE) |
                 RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE) |
                 RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE_ENCODED) |
                 RelocInfo::ModeMask(RelocInfo::JS_DISPATCH_HANDLE) |
                 RelocInfo::ModeMask(RelocInfo::NEAR_BUILTIN_ENTRY) |
                 RelocInfo::ModeMask(RelocInfo::WASM_CALL) |
                 RelocInfo::ModeMask(RelocInfo::WASM_STUB_CALL)));

#if defined(V8_TARGET_ARCH_PPC64) || defined(V8_TARGET_ARCH_MIPS64)
  return RelocIterator(*this, kModeMask).done();
#elif defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_ARM64) ||  \
    defined(V8_TARGET_ARCH_ARM) || defined(V8_TARGET_ARCH_S390X) ||    \
    defined(V8_TARGET_ARCH_IA32) || defined(V8_TARGET_ARCH_RISCV64) || \
    defined(V8_TARGET_ARCH_LOONG64) || defined(V8_TARGET_ARCH_RISCV32)
  for (RelocIterator it(*this, kModeMask); !it.done(); it.next()) {
    // On these platforms we emit relative builtin-to-builtin
    // jumps for isolate independent builtins in the snapshot. They are later
    // rewritten as pc-relative jumps to the off-heap instruction stream and are
    // thus process-independent. See also: FinalizeEmbeddedCodeTargets.
    if (RelocInfo::IsCodeTargetMode(it.rinfo()->rmode())) {
      Address target_address = it.rinfo()->target_address();
      if (OffHeapInstructionStream::PcIsOffHeap(isolate, target_address))
        continue;

      Tagged<Code> target = Code::FromTargetAddress(target_address);
      if (Builtins::IsIsolateIndependentBuiltin(target)) {
        continue;
      }
    }
    return false;
  }
  return true;
#else
#error Unsupported architecture.
#endif
}

bool Code::Inlines(Tagged<SharedFunctionInfo> sfi) {
  // We can only check for inlining for optimized code.
  DCHECK(is_optimized_code());
  DisallowGarbageCollection no_gc;
  Tagged<DeoptimizationData> const data =
      Cast<DeoptimizationData>(deoptimization_data());
  if (data->length() == 0) return false;
  if (data->GetSharedFunctionInfo() == sfi) return true;
  Tagged<DeoptimizationLiteralArray> const literals = data->LiteralArray();
  int const inlined_count = data->InlinedFunctionCount().value();
  for (int i = 0; i < inlined_count; ++i) {
    if (Cast<SharedFunctionInfo>(literals->get(i)) == sfi) return true;
  }
  return false;
}

void Code::SetMarkedForDeoptimization(Isolate* isolate,
                                      LazyDeoptimizeReason reason) {
  set_marked_for_deoptimization(true);
  // Eager deopts are already logged by the deoptimizer.
  if (reason != LazyDeoptimizeReason::kEagerDeopt &&
      V8_UNLIKELY(v8_flags.trace_deopt || v8_flags.log_deopt)) {
    TraceMarkForDeoptimization(isolate, reason);
  }
#ifdef V8_ENABLE_LEAPTIERING
  JSDispatchHandle handle = js_dispatch_handle();
  if (handle != kNullJSDispatchHandle) {
    JSDispatchTable* jdt = IsolateGroup::current()->js_dispatch_table();
    Tagged<Code> cur = jdt->GetCode(handle);
    if (SafeEquals(cur)) {
      if (v8_flags.reopt_after_lazy_deopts &&
          isolate->concurrent_recompilation_enabled()) {
        jdt->SetCodeNoWriteBarrier(
            handle, *BUILTIN_CODE(isolate, InterpreterEntryTrampoline));
        // Somewhat arbitrary list of lazy deopt reasons which we expect to be
        // stable enough to warrant either immediate re-optimization, or
        // re-optimization after one invocation (to detect potential follow-up
        // IC changes).
        // TODO(olivf): We should also work on reducing the number of
        // dependencies we create in the compilers to require less of these
        // quick re-compilations.
        switch (reason) {
          case LazyDeoptimizeReason::kAllocationSiteTenuringChange:
          case LazyDeoptimizeReason::kAllocationSiteTransitionChange:
          case LazyDeoptimizeReason::kEmptyContextExtensionChange:
          case LazyDeoptimizeReason::kFrameValueMaterialized:
          case LazyDeoptimizeReason::kPropertyCellChange:
          case LazyDeoptimizeReason::kContextCellChange:
          case LazyDeoptimizeReason::kPrototypeChange:
          case LazyDeoptimizeReason::kExceptionCaught:
          case LazyDeoptimizeReason::kFieldTypeConstChange:
          case LazyDeoptimizeReason::kFieldRepresentationChange:
          case LazyDeoptimizeReason::kFieldTypeChange:
          case LazyDeoptimizeReason::kInitialMapChange:
          case LazyDeoptimizeReason::kMapDeprecated:
            jdt->SetTieringRequest(
                handle, TieringBuiltin::kMarkReoptimizeLazyDeoptimized,
                isolate);
            break;
          default:
            // TODO(olivf): This trampoline is just used to reset the budget. If
            // we knew the feedback cell and the bytecode size here, we could
            // directly reset the budget.
            jdt->SetTieringRequest(handle, TieringBuiltin::kMarkLazyDeoptimized,
                                   isolate);
            break;
        }
      } else {
        jdt->SetCodeNoWriteBarrier(handle, *BUILTIN_CODE(isolate, CompileLazy));
      }
    }
    // Ensure we don't try to patch the entry multiple times.
    set_js_dispatch_handle(kNullJSDispatchHandle);
  }
#endif
  Tagged<ProtectedFixedArray> tmp = deoptimization_data();
  // TODO(422951610): Zapping code discovered a bug in
  // --maglev-inline-api-calls. Remove the flag check here once the bug is
  // fixed.
  if (tmp->length() > 0 && !v8_flags.maglev_inline_api_calls) {
    Address start = instruction_start();
    Address end = start + Cast<DeoptimizationData>(deoptimization_data())
                              ->DeoptExitStart()
                              .value();
    RelocIterator it(instruction_stream(), RelocIterator::kAllModesMask);
    Deoptimizer::ZapCode(start, end, it);
  }
}

#ifdef ENABLE_DISASSEMBLER

namespace {

void DisassembleCodeRange(Isolate* isolate, std::ostream& os, Tagged<Code> code,
                          Address begin, size_t size, Address current_pc,
                          size_t range_limit = 0) {
  Address end = begin + size;
  AllowHandleAllocation allow_handles;
  DisallowGarbageCollection no_gc;
  HandleScope handle_scope(isolate);
  Disassembler::Decode(isolate, os, reinterpret_cast<uint8_t*>(begin),
                       reinterpret_cast<uint8_t*>(end),
                       CodeReference(handle(code, isolate)), current_pc,
                       range_limit);
}

void DisassembleOnlyCode(const char* name, std::ostream& os, Isolate* isolate,
                         Tagged<Code> code, Address current_pc,
                         size_t range_limit) {
  int code_size = code->instruction_size();
  DisassembleCodeRange(isolate, os, code, code->instruction_start(), code_size,
                       current_pc, range_limit);
}

void Disassemble(const char* name, std::ostream& os, Isolate* isolate,
                 Tagged<Code> code, Address current_pc) {
  CodeKind kind = code->kind();
  os << "kind = " << CodeKindToString(kind) << "\n";
  if (name == nullptr && code->is_builtin()) {
    name = Builtins::name(code->builtin_id());
  }
  if ((name != nullptr) && (name[0] != '\0')) {
    os << "name = " << name << "\n";
  }
  os << "compiler = "
     << (code->is_turbofanned()       ? "turbofan"
         : code->is_maglevved()       ? "maglev"
         : kind == CodeKind::BASELINE ? "baseline"
                                      : "unknown")
     << "\n";
  os << "address = " << reinterpret_cast<void*>(code.ptr()) << "\n\n";

  {
    int code_size = code->instruction_size();
    os << "Instructions (size = " << code_size << ")\n";
    DisassembleCodeRange(isolate, os, code, code->instruction_start(),
                         code_size, current_pc);

    if (int pool_size = code->constant_pool_size()) {
      DCHECK_EQ(pool_size & kPointerAlignmentMask, 0);
      os << "\nConstant Pool (size = " << pool_size << ")\n";
      base::Vector<char> buf = base::Vector<char>::New(50);
      intptr_t* ptr = reinterpret_cast<intptr_t*>(code->constant_pool());
      for (int i = 0; i < pool_size; i += kSystemPointerSize, ptr++) {
        SNPrintF(buf, "%4d %08" V8PRIxPTR, i, *ptr);
        os << static_cast<const void*>(ptr) << "  " << buf.begin() << "\n";
      }
    }
  }
  os << "\n";

  // TODO(cbruni): add support for baseline code.
  if (code->has_source_position_table()) {
    {
      SourcePositionTableIterator it(
          code->source_position_table(),
          SourcePositionTableIterator::kJavaScriptOnly);
      if (!it.done()) {
        os << "Source positions:\n pc offset  position\n";
        for (; !it.done(); it.Advance()) {
          os << std::setw(10) << std::hex << it.code_offset() << std::dec
             << std::setw(10) << it.source_position().ScriptOffset()
             << (it.is_statement() ? "  statement" : "") << "\n";
        }
        os << "\n";
      }
    }

    {
      SourcePositionTableIterator it(
          code->source_position_table(),
          SourcePositionTableIterator::kExternalOnly);
      if (!it.done()) {
        os << "External Source positions:\n pc offset  fileid  line\n";
        for (; !it.done(); it.Advance()) {
          DCHECK(it.source_position().IsExternal());
          os << std::setw(10) << std::hex << it.code_offset() << std::dec
             << std::setw(10) << it.source_position().ExternalFileId()
             << std::setw(10) << it.source_position().ExternalLine() << "\n";
        }
        os << "\n";
      }
    }
  }

  if (code->uses_deoptimization_data()) {
    Tagged<DeoptimizationData> data =
        Cast<DeoptimizationData>(code->deoptimization_data());
    data->PrintDeoptimizationData(os);
  }
  os << "\n";

  if (code->uses_safepoint_table()) {
    if (code->is_maglevved()) {
      MaglevSafepointTable table(isolate, current_pc, code);
      table.Print(os);
    } else {
      SafepointTable table(isolate, current_pc, code);
      table.Print(os);
    }
    os << "\n";
  }

  if (code->has_handler_table()) {
    HandlerTable table(code);
    os << "Handler Table (size = " << table.NumberOfReturnEntries() << ")\n";
    table.HandlerTableReturnPrint(os);
    os << "\n";
  }

  os << "RelocInfo (size = " << code->relocation_size() << ")\n";
  if (code->has_instruction_stream()) {
    for (RelocIterator it(code); !it.done(); it.next()) {
      it.rinfo()->Print(isolate, os);
    }
  }
  os << "\n";

  if (code->has_unwinding_info()) {
    os << "UnwindingInfo (size = " << code->unwinding_info_size() << ")\n";
    EhFrameDisassembler eh_frame_disassembler(
        reinterpret_cast<uint8_t*>(code->unwinding_info_start()),
        reinterpret_cast<uint8_t*>(code->unwinding_info_end()));
    eh_frame_disassembler.DisassembleToStream(os);
    os << "\n";
  }
}

}  // namespace

void Code::Disassemble(const char* name, std::ostream& os, Isolate* isolate,
                       Address current_pc) {
  i::Disassemble(name, os, isolate, *this, current_pc);
}

void Code::DisassembleOnlyCode(const char* name, std::ostream& os,
                               Isolate* isolate, Address current_pc,
                               size_t range_limit) {
  i::DisassembleOnlyCode(name, os, isolate, *this, current_pc, range_limit);
}

#endif  // ENABLE_DISASSEMBLER

void Code::TraceMarkForDeoptimization(Isolate* isolate,
                                      LazyDeoptimizeReason reason) {
  Deoptimizer::TraceMarkForDeoptimization(isolate, *this, reason);
}

#if V8_ENABLE_GEARBOX
void Code::CopyFieldsWithGearboxForSerialization(Tagged<Code> dst,
                                                 Tagged<Code> src,
                                                 Isolate* isolate) {
  Builtin src_id = src->builtin_id();
  DCHECK(dst->is_gearbox_placeholder_builtin());
  DCHECK(Builtins::IsISXVariant(src_id) || Builtins::IsGenericVariant(src_id) ||
         src_id == Builtin::kIllegal);
  dst->set_builtin_id(src_id);
  dst->set_instruction_size(src->instruction_size());
  dst->set_metadata_size(src->metadata_size());
  dst->set_handler_table_offset(src->handler_table_offset());
  dst->set_jump_table_info_offset(src->jump_table_info_offset());
  dst->set_unwinding_info_offset(src->unwinding_info_offset());
  dst->set_parameter_count(src->parameter_count());
  dst->set_code_comments_offset(src->code_comments_offset());
  dst->set_constant_pool_offset(src->constant_pool_offset());
}

void Code::CopyFieldsWithGearboxForDeserialization(Tagged<Code> dst,
                                                   Tagged<Code> src,
                                                   Isolate* isolate) {
  CopyFieldsWithGearboxForSerialization(dst, src, isolate);
  // We only set instruction_start field when we're doing deserialization,
  // because in the serialization it was already be cleaned.
  dst->SetInstructionStartForOffHeapBuiltin(isolate, src->instruction_start());
}
#endif  // V8_ENABLE_GEARBOX

}  // namespace internal
}  // namespace v8
