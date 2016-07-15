// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_SIMD_LOWERING_H_
#define V8_COMPILER_SIMD_LOWERING_H_

#include "src/compiler/graph-reducer.h"
#include "src/compiler/wasm-compiler.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {
namespace compiler {

typedef Signature<Conversion> ConversionSignature;

class SimdLowering final : public Reducer {
 public:
  SimdLowering(Zone* zone, WasmGraphBuilder* builder)
      : builder_(builder), zone_(zone) {
    InitializeSignatures();
  }
  virtual ~SimdLowering();
  Reduction Reduce(Node* node) override;

 private:
  WasmGraphBuilder* builder_;
  Zone* zone_;

  void InitializeSignatures();
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_SIMD_LOWERING_H_
