// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_TURBOSHAFT_WASM_JS_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_WASM_JS_LOWERING_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/wasm-graph-assembler.h"
#include "src/wasm/wasm-code-manager.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// This reducer is part of the JavaScript pipeline and contains lowering of
// wasm nodes (from inlined wasm functions).
//
// The reducer replaces all TrapIf nodes with a conditional goto to deferred
// code containing a call to the trap builtin.
template <class Next>
class WasmJSLoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()

  OpIndex REDUCE(TrapIf)(OpIndex condition, OpIndex frame_state, bool negated,
                         TrapId trap_id) {
    // All TrapIf nodes in JS need to have a FrameState.
    DCHECK(frame_state.valid());
    Builtin trap = wasm::RuntimeStubIdToBuiltinName(
        static_cast<wasm::WasmCode::RuntimeStubId>(trap_id));
    // The call is not marked as Operator::kNoDeopt. While it cannot actually
    // deopt, deopt info based on the provided FrameState is required for stack
    // trace creation of the wasm trap.
    const bool needs_frame_state = true;
    const CallDescriptor* tf_descriptor = GetBuiltinCallDescriptor(
        trap, Asm().graph_zone(), StubCallMode::kCallBuiltinPointer,
        needs_frame_state, Operator::kNoProperties);
    const TSCallDescriptor* ts_descriptor =
        TSCallDescriptor::Create(tf_descriptor, Asm().graph_zone());

    OpIndex should_trap = negated ? __ Word32Equal(condition, 0) : condition;
    IF (UNLIKELY(should_trap)) {
      OpIndex call_target = __ NumberConstant(static_cast<int>(trap));
      __ Call(call_target, frame_state, {}, ts_descriptor);
      __ Unreachable();  // The trap builtin never returns.
    }
    END_IF
    return OpIndex::Invalid();
  }

 private:
  Isolate* isolate_ = PipelineData::Get().isolate();
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_WASM_JS_LOWERING_REDUCER_H_
