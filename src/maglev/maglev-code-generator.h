// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_CODE_GENERATOR_H_
#define V8_MAGLEV_MAGLEV_CODE_GENERATOR_H_

#include "src/codegen/maglev-safepoint-table.h"
#include "src/common/globals.h"
#include "src/maglev/maglev-assembler.h"
#include "src/maglev/maglev-code-gen-state.h"

namespace v8 {
namespace internal {
namespace maglev {

class Graph;
class MaglevCompilationInfo;

class MaglevCodeGenerator final {
 public:
  MaglevCodeGenerator(Isolate* isolate, MaglevCompilationInfo* compilation_info,
                      Graph* graph);

  void Assemble();

  MaybeHandle<Code> Generate();

 private:
  void EmitCode();
  void EmitDeferredCode();
  void EmitDeopts();
  void EmitExceptionHandlerTrampolines();
  void EmitMetadata();

  MaybeHandle<Code> BuildCodeObject();

  Handle<DeoptimizationData> GenerateDeoptimizationData();

  int stack_slot_count() const { return code_gen_state_.stack_slots(); }
  int stack_slot_count_with_fixed_frame() const {
    return stack_slot_count() + StandardFrameConstants::kFixedSlotCount;
  }

  Isolate* isolate() const { return isolate_; }
  MaglevAssembler* masm() { return &masm_; }
  MaglevSafepointTableBuilder* safepoint_table_builder() {
    return &safepoint_table_builder_;
  }

  Isolate* isolate_;
  MaglevSafepointTableBuilder safepoint_table_builder_;
  MaglevCodeGenState code_gen_state_;
  MaglevAssembler masm_;
  Graph* const graph_;

  int deopt_exit_start_offset_ = -1;
  int handler_table_offset_ = 0;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_CODE_GENERATOR_H_
