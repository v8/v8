// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LIFTOFF_COMPILER_H_
#define V8_WASM_BASELINE_LIFTOFF_COMPILER_H_

#include "src/source-position-table.h"
#include "src/trap-handler/trap-handler.h"
#include "src/wasm/baseline/liftoff-assembler.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/function-compiler.h"

namespace v8 {
namespace internal {
namespace wasm {

class LiftoffCompilationUnit final {
 public:
  explicit LiftoffCompilationUnit(WasmCompilationUnit* wasm_unit)
      : wasm_unit_(wasm_unit) {}

  bool ExecuteCompilation();
  wasm::WasmCode* FinishCompilation(wasm::ErrorThrower*);

 private:
  WasmCompilationUnit* const wasm_unit_;
  // Must stay alive until the code is added to the {NativeModule}, because it
  // contains the instruction buffer.
  LiftoffAssembler asm_;

  // Result of compilation:
  CodeDesc desc_;
  OwnedVector<byte> source_positions_;
  OwnedVector<trap_handler::ProtectedInstructionData> protected_instructions_;
  uint32_t frame_slot_count_;
  int safepoint_table_offset_;

  DISALLOW_COPY_AND_ASSIGN(LiftoffCompilationUnit);
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_BASELINE_LIFTOFF_COMPILER_H_
