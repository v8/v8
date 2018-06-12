// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/code-stub-assembler.h"
#include "src/objects-inl.h"
#include "src/wasm/wasm-opcodes.h"

namespace v8 {
namespace internal {

typedef compiler::Node Node;

TF_BUILTIN(WasmStackGuard, CodeStubAssembler) {
  TNode<Object> instance = UncheckedCast<Object>(
      LoadFromParentFrame(WasmCompiledFrameConstants::kWasmInstanceOffset));
  TNode<Code> centry = UncheckedCast<Code>(Load(
      MachineType::AnyTagged(), instance,
      IntPtrConstant(WasmInstanceObject::kCEntryStubOffset - kHeapObjectTag)));
  TailCallRuntimeWithCEntry(Runtime::kWasmStackGuard, centry,
                            NoContextConstant());
}

#define DECLARE_ENUM(name)                                                     \
  TF_BUILTIN(ThrowWasm##name, CodeStubAssembler) {                             \
    TNode<Object> instance = UncheckedCast<Object>(                            \
        LoadFromParentFrame(WasmCompiledFrameConstants::kWasmInstanceOffset)); \
    TNode<Code> centry = UncheckedCast<Code>(                                  \
        Load(MachineType::AnyTagged(), instance,                               \
             IntPtrConstant(WasmInstanceObject::kCEntryStubOffset -            \
                            kHeapObjectTag)));                                 \
    int message_id = wasm::WasmOpcodes::TrapReasonToMessageId(wasm::k##name);  \
    TailCallRuntimeWithCEntry(Runtime::kThrowWasmError, centry,                \
                              NoContextConstant(), SmiConstant(message_id));   \
  }
FOREACH_WASM_TRAPREASON(DECLARE_ENUM)
#undef DECLARE_ENUM

}  // namespace internal
}  // namespace v8
