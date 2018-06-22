// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-code-specialization.h"

#include "src/assembler-inl.h"
#include "src/base/optional.h"
#include "src/objects-inl.h"
#include "src/source-position-table.h"
#include "src/wasm/decoder.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"

namespace v8 {
namespace internal {
namespace wasm {

uint32_t ExtractDirectCallIndex(wasm::Decoder& decoder, const byte* pc) {
  DCHECK_EQ(static_cast<int>(kExprCallFunction), static_cast<int>(*pc));
  decoder.Reset(pc + 1, pc + 6);
  uint32_t call_idx = decoder.consume_u32v("call index");
  DCHECK(decoder.ok());
  DCHECK_GE(kMaxInt, call_idx);
  return call_idx;
}

namespace {

int AdvanceSourcePositionTableIterator(SourcePositionTableIterator& iterator,
                                       size_t offset_l) {
  DCHECK_GE(kMaxInt, offset_l);
  int offset = static_cast<int>(offset_l);
  DCHECK(!iterator.done());
  int byte_pos;
  do {
    byte_pos = iterator.source_position().ScriptOffset();
    iterator.Advance();
  } while (!iterator.done() && iterator.code_offset() <= offset);
  return byte_pos;
}

class PatchDirectCallsHelper {
 public:
  PatchDirectCallsHelper(NativeModule* native_module, const WasmCode* code)
      : source_pos_it(code->source_positions()), decoder(nullptr, nullptr) {
    uint32_t func_index = code->index();
    const WasmModule* module = native_module->module();
    func_bytes = native_module->wire_bytes().start() +
                 module->functions[func_index].code.offset();
  }

  SourcePositionTableIterator source_pos_it;
  Decoder decoder;
  const byte* func_bytes;
};

}  // namespace

CodeSpecialization::CodeSpecialization() {}

CodeSpecialization::~CodeSpecialization() {}

void CodeSpecialization::RelocateDirectCalls(NativeModule* native_module) {
  DCHECK_NULL(relocate_direct_calls_module_);
  DCHECK_NOT_NULL(native_module);
  relocate_direct_calls_module_ = native_module;
}

bool CodeSpecialization::ApplyToWholeModule(NativeModule* native_module,
                                            ICacheFlushMode icache_flush_mode) {
  DisallowHeapAllocation no_gc;

  bool changed = false;

  // Patch all wasm functions.
  for (WasmCode* wasm_code : native_module->code_table()) {
    if (wasm_code == nullptr) continue;
    if (wasm_code->kind() != WasmCode::kFunction) continue;
    changed |= ApplyToWasmCode(wasm_code, icache_flush_mode);
  }

  return changed;
}

bool CodeSpecialization::ApplyToWasmCode(wasm::WasmCode* code,
                                         ICacheFlushMode icache_flush_mode) {
  DisallowHeapAllocation no_gc;
  DCHECK_EQ(wasm::WasmCode::kFunction, code->kind());

  bool reloc_direct_calls = relocate_direct_calls_module_ != nullptr;

  int reloc_mode = 0;
  if (reloc_direct_calls) {
    reloc_mode |= RelocInfo::ModeMask(RelocInfo::WASM_CALL);
  }
  if (!reloc_mode) return false;

  base::Optional<PatchDirectCallsHelper> patch_direct_calls_helper;
  bool changed = false;

  NativeModule* native_module = code->native_module();

  RelocIterator it(code->instructions(), code->reloc_info(),
                   code->constant_pool(), reloc_mode);
  for (; !it.done(); it.next()) {
    RelocInfo::Mode mode = it.rinfo()->rmode();
    switch (mode) {
      case RelocInfo::WASM_CALL: {
        DCHECK(reloc_direct_calls);
        // Iterate simultaneously over the relocation information and the source
        // position table. For each call in the reloc info, move the source
        // position iterator forward to that position to find the byte offset of
        // the respective call. Then extract the call index from the module wire
        // bytes to find the new compiled function.
        size_t offset = it.rinfo()->pc() - code->instruction_start();
        if (!patch_direct_calls_helper) {
          patch_direct_calls_helper.emplace(relocate_direct_calls_module_,
                                            code);
        }
        int byte_pos = AdvanceSourcePositionTableIterator(
            patch_direct_calls_helper->source_pos_it, offset);
        uint32_t called_func_index = ExtractDirectCallIndex(
            patch_direct_calls_helper->decoder,
            patch_direct_calls_helper->func_bytes + byte_pos);
        Address new_target =
            native_module->GetCallTargetForFunction(called_func_index);
        it.rinfo()->set_wasm_call_address(new_target, icache_flush_mode);
        changed = true;
      } break;

      default:
        UNREACHABLE();
    }
  }

  return changed;
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
