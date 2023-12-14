// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/code-elimination-and-simplification-phase.h"

#include "src/compiler/js-heap-broker.h"
#include "src/compiler/turboshaft/branch-condition-duplication-reducer.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/dead-code-elimination-reducer.h"
#include "src/compiler/turboshaft/load-store-simplification-reducer.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/stack-check-reducer.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/compiler/turboshaft/wasm-js-lowering-reducer.h"
#endif

namespace v8::internal::compiler::turboshaft {

void CodeEliminationAndSimplificationPhase::Run(Zone* temp_zone) {
  UnparkedScopeIfNeeded scope(PipelineData::Get().broker(), DEBUG_BOOL);

  CopyingPhase<DeadCodeEliminationReducer, StackCheckReducer,
#if V8_ENABLE_WEBASSEMBLY
               WasmJSLoweringReducer,
#endif
               BranchConditionDuplicationReducer
#if V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_RISCV64 || \
    V8_TARGET_ARCH_LOONG64 || V8_TARGET_ARCH_MIPS64 || V8_TARGET_ARCH_ARM
               ,
               LoadStoreSimplificationReducer, ValueNumberingReducer
#endif
               >::Run(temp_zone);
}

}  // namespace v8::internal::compiler::turboshaft
