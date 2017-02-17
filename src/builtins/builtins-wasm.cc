// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

typedef compiler::Node Node;

TF_BUILTIN(WasmStackGuard, CodeStubAssembler) {
  Node* context = SmiConstant(Smi::kZero);
  TailCallRuntime(Runtime::kWasmStackGuard, context);
}

}  // namespace internal
}  // namespace v8
