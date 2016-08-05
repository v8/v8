// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"
#include "src/builtins/builtins-utils.h"
#include "src/interface-descriptors.h"
#include "src/macro-assembler.h"

namespace v8 {
namespace internal {

BUILTIN(Illegal) {
  UNREACHABLE();
  return isolate->heap()->undefined_value();  // Make compiler happy.
}

BUILTIN(EmptyFunction) { return isolate->heap()->undefined_value(); }

BUILTIN(UnsupportedThrower) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(isolate,
                                 NewError(MessageTemplate::kUnsupported));
}

// -----------------------------------------------------------------------------
// Throwers for restricted function properties and strict arguments object
// properties

BUILTIN(RestrictedFunctionPropertiesThrower) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kRestrictedFunctionProperties));
}

BUILTIN(RestrictedStrictArgumentsPropertiesThrower) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kStrictPoisonPill));
}

// -----------------------------------------------------------------------------
// Interrupt and stack checks.

void Builtins::Generate_InterruptCheck(MacroAssembler* masm) {
  masm->TailCallRuntime(Runtime::kInterrupt);
}

void Builtins::Generate_StackCheck(MacroAssembler* masm) {
  masm->TailCallRuntime(Runtime::kStackGuard);
}

// -----------------------------------------------------------------------------
// FixedArray helpers.

void Builtins::Generate_CopyFixedArray(CodeStubAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;
  typedef CopyFixedArrayDescriptor Descriptor;

  Node* source = assembler->Parameter(Descriptor::kSource);

  // Load the {source} length.
  Node* source_length_tagged =
      assembler->LoadObjectField(source, FixedArray::kLengthOffset);
  Node* source_length = assembler->SmiToWord(source_length_tagged);

  // Compute the size of {source} in bytes.
  Node* source_size = assembler->IntPtrAdd(
      assembler->WordShl(source_length,
                         assembler->IntPtrConstant(kPointerSizeLog2)),
      assembler->IntPtrConstant(FixedArray::kHeaderSize));

  // Check if we can allocate in new space.
  Label if_newspace(assembler), if_oldspace(assembler);
  assembler->Branch(assembler->UintPtrLessThan(
                        source_size, assembler->IntPtrConstant(
                                         Page::kMaxRegularHeapObjectSize)),
                    &if_newspace, &if_oldspace);

  assembler->Bind(&if_newspace);
  {
    // Allocate the targeting FixedArray in new space.
    Node* target = assembler->Allocate(source_size);
    assembler->StoreMapNoWriteBarrier(
        target, assembler->LoadRoot(Heap::kFixedArrayMapRootIndex));
    assembler->StoreObjectFieldNoWriteBarrier(target, FixedArray::kLengthOffset,
                                              source_length_tagged);

    // Compute the limit.
    Node* limit = assembler->IntPtrSub(
        source_size, assembler->IntPtrConstant(kHeapObjectTag));

    // Copy the {source} to the {target}.
    Variable var_offset(assembler, MachineType::PointerRepresentation());
    Label loop(assembler, &var_offset), done_loop(assembler);
    var_offset.Bind(
        assembler->IntPtrConstant(FixedArray::kHeaderSize - kHeapObjectTag));
    assembler->Goto(&loop);
    assembler->Bind(&loop);
    {
      // Determine the current {offset}.
      Node* offset = var_offset.value();

      // Check if we are done.
      assembler->GotoUnless(assembler->UintPtrLessThan(offset, limit),
                            &done_loop);

      // Load the value from {source}.
      Node* value = assembler->Load(MachineType::AnyTagged(), source, offset);

      // Store the {value} to the {target} without a write barrier, since we
      // know that the {target} is allocated in new space.
      assembler->StoreNoWriteBarrier(MachineRepresentation::kTagged, target,
                                     offset, value);

      // Increment {offset} and continue.
      var_offset.Bind(assembler->IntPtrAdd(
          offset, assembler->IntPtrConstant(kPointerSize)));
      assembler->Goto(&loop);
    }

    assembler->Bind(&done_loop);
    assembler->Return(target);
  }

  assembler->Bind(&if_oldspace);
  {
    // Allocate the targeting FixedArray in old space
    // (maybe even in large object space).
    Node* flags = assembler->SmiConstant(
        Smi::FromInt(AllocateDoubleAlignFlag::encode(false) |
                     AllocateTargetSpace::encode(AllocationSpace::OLD_SPACE)));
    Node* source_size_tagged = assembler->SmiFromWord(source_size);
    Node* target = assembler->CallRuntime(Runtime::kAllocateInTargetSpace,
                                          assembler->NoContextConstant(),
                                          source_size_tagged, flags);
    assembler->StoreMapNoWriteBarrier(
        target, assembler->LoadRoot(Heap::kFixedArrayMapRootIndex));
    assembler->StoreObjectFieldNoWriteBarrier(target, FixedArray::kLengthOffset,
                                              source_length_tagged);

    // Compute the limit.
    Node* limit = assembler->IntPtrSub(
        source_size, assembler->IntPtrConstant(kHeapObjectTag));

    // Copy the {source} to the {target}.
    Variable var_offset(assembler, MachineType::PointerRepresentation());
    Label loop(assembler, &var_offset), done_loop(assembler);
    var_offset.Bind(
        assembler->IntPtrConstant(FixedArray::kHeaderSize - kHeapObjectTag));
    assembler->Goto(&loop);
    assembler->Bind(&loop);
    {
      // Determine the current {offset}.
      Node* offset = var_offset.value();

      // Check if we are done.
      assembler->GotoUnless(assembler->UintPtrLessThan(offset, limit),
                            &done_loop);

      // Load the value from {source}.
      Node* value = assembler->Load(MachineType::AnyTagged(), source, offset);

      // Store the {value} to the {target} with a proper write barrier.
      assembler->Store(MachineRepresentation::kTagged, target, offset, value);

      // Increment {offset} and continue.
      var_offset.Bind(assembler->IntPtrAdd(
          offset, assembler->IntPtrConstant(kPointerSize)));
      assembler->Goto(&loop);
    }

    assembler->Bind(&done_loop);
    assembler->Return(target);
  }
}

}  // namespace internal
}  // namespace v8
