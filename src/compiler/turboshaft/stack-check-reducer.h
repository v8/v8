// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_STACK_CHECK_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_STACK_CHECK_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/uniform-reducer-adapter.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class Next>
class StackCheckReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()

  OpIndex REDUCE(StackCheck)(StackCheckOp::CheckOrigin origin,
                             StackCheckOp::CheckKind kind) {
    V<WordPtr> limit = __ Load(
        __ LoadRootRegister(), LoadOp::Kind::RawAligned(),
        MemoryRepresentation::PointerSized(), IsolateData::jslimit_offset());
    compiler::StackCheckKind check_kind =
        origin == StackCheckOp::CheckOrigin::kFromJS
            ? compiler::StackCheckKind::kJSFunctionEntry
            : compiler::StackCheckKind::kWasm;
    V<Word32> check = __ StackPointerGreaterThan(limit, check_kind);
    IF_NOT (LIKELY(check)) {
      if (origin == StackCheckOp::CheckOrigin::kFromJS) {
        if (kind == StackCheckOp::CheckKind::kLoopCheck) {
          UNIMPLEMENTED();
        }
        DCHECK_EQ(kind, StackCheckOp::CheckKind::kFunctionHeaderCheck);

        if (!isolate_) isolate_ = PipelineData::Get().isolate();
        __ CallRuntime_StackGuardWithGap(isolate_, __ NoContextConstant(),
                                         __ StackCheckOffset());
      }
#ifdef V8_ENABLE_WEBASSEMBLY
      else if (origin == StackCheckOp::CheckOrigin::kFromWasm) {
        // TODO(14108): Cache descriptor.
        V<WordPtr> builtin =
            __ RelocatableWasmBuiltinCallTarget(Builtin::kWasmStackGuard);
        const CallDescriptor* call_descriptor =
            compiler::Linkage::GetStubCallDescriptor(
                __ graph_zone(),                      // zone
                NoContextDescriptor{},                // descriptor
                0,                                    // stack parameter count
                CallDescriptor::kNoFlags,             // flags
                Operator::kNoProperties,              // properties
                StubCallMode::kCallWasmRuntimeStub);  // stub call mode
        const TSCallDescriptor* ts_call_descriptor = TSCallDescriptor::Create(
            call_descriptor, compiler::CanThrow::kNo, __ graph_zone());
        __ Call(builtin, {}, ts_call_descriptor);
      }
#endif  // V8_ENABLE_WEBASSEMBLY
    }
    END_IF
    return OpIndex::Invalid();
  }

 private:
  Isolate* isolate_ = nullptr;
  // We cache the instance because we need it to load the limit_address used to
  // lower stack checks.
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft
#endif  // V8_COMPILER_TURBOSHAFT_STACK_CHECK_REDUCER_H_
