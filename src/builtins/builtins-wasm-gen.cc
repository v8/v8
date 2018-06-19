// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/code-stub-assembler.h"
#include "src/objects-inl.h"
#include "src/wasm/wasm-opcodes.h"

namespace v8 {
namespace internal {

class WasmBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit WasmBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  TNode<Object> UncheckedParameter(int index) {
    return UncheckedCast<Object>(Parameter(index));
  }

  TNode<Code> LoadBuiltinFromFrame(Builtins::Name id) {
    TNode<Object> instance = UncheckedCast<Object>(
        LoadFromParentFrame(WasmCompiledFrameConstants::kWasmInstanceOffset));
    TNode<IntPtrT> roots = UncheckedCast<IntPtrT>(
        Load(MachineType::Pointer(), instance,
             IntPtrConstant(WasmInstanceObject::kRootsArrayAddressOffset -
                            kHeapObjectTag)));
    TNode<Code> target = UncheckedCast<Code>(Load(
        MachineType::TaggedPointer(), roots,
        IntPtrConstant(Heap::roots_to_builtins_offset() + id * kPointerSize)));
    return target;
  }
};

TF_BUILTIN(WasmAllocateHeapNumber, WasmBuiltinsAssembler) {
  TNode<Object> context = UncheckedParameter(Descriptor::kContext);
  TNode<Code> target = LoadBuiltinFromFrame(Builtins::kAllocateHeapNumber);
  TailCallStub(AllocateHeapNumberDescriptor(), target, context);
}

TF_BUILTIN(WasmArgumentsAdaptor, WasmBuiltinsAssembler) {
  TNode<Object> context = UncheckedParameter(Descriptor::kContext);
  TNode<Object> function = UncheckedParameter(Descriptor::kFunction);
  TNode<Object> new_target = UncheckedParameter(Descriptor::kNewTarget);
  TNode<Object> argc1 = UncheckedParameter(Descriptor::kActualArgumentsCount);
  TNode<Object> argc2 = UncheckedParameter(Descriptor::kExpectedArgumentsCount);
  TNode<Code> target =
      LoadBuiltinFromFrame(Builtins::kArgumentsAdaptorTrampoline);
  TailCallStub(ArgumentAdaptorDescriptor{}, target, context, function,
               new_target, argc1, argc2);
}

TF_BUILTIN(WasmCallJavaScript, WasmBuiltinsAssembler) {
  TNode<Object> context = UncheckedParameter(Descriptor::kContext);
  TNode<Object> function = UncheckedParameter(Descriptor::kFunction);
  TNode<Object> argc = UncheckedParameter(Descriptor::kActualArgumentsCount);
  TNode<Code> target = LoadBuiltinFromFrame(Builtins::kCall_ReceiverIsAny);
  TailCallStub(CallTrampolineDescriptor{}, target, context, function, argc);
}

TF_BUILTIN(WasmToNumber, WasmBuiltinsAssembler) {
  TNode<Object> context = UncheckedParameter(Descriptor::kContext);
  TNode<Object> argument = UncheckedParameter(Descriptor::kArgument);
  TNode<Code> target = LoadBuiltinFromFrame(Builtins::kToNumber);
  TailCallStub(TypeConversionDescriptor(), target, context, argument);
}

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
