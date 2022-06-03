// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_WASM_GC_OPERATOR_REDUCER_H_
#define V8_COMPILER_WASM_GC_OPERATOR_REDUCER_H_

#include "src/compiler/graph-reducer.h"
#include "src/compiler/wasm-graph-assembler.h"

namespace v8 {
namespace internal {
namespace compiler {

class MachineGraph;

class WasmGCOperatorReducer final : public AdvancedReducer {
 public:
  WasmGCOperatorReducer(Editor* editor, MachineGraph* mcgraph,
                        const wasm::WasmModule* module);

  const char* reducer_name() const override { return "WasmGCOperatorReducer"; }

  Reduction Reduce(Node* node) final;

 private:
  Reduction ReduceAssertNotNull(Node* node);
  Reduction ReduceIsNull(Node* node);
  Reduction ReduceWasmTypeCheck(Node* node);
  Reduction ReduceWasmTypeCast(Node* node);

  Node* SetType(Node* node, wasm::ValueType type);

  Graph* graph() { return mcgraph_->graph(); }
  CommonOperatorBuilder* common() { return mcgraph_->common(); }

  MachineGraph* mcgraph_;
  WasmGraphAssembler gasm_;
  const wasm::WasmModule* module_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_WASM_GC_OPERATOR_REDUCER_H_
