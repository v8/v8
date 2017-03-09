// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"
#include "src/counters.h"
#include "src/interface-descriptors.h"
#include "src/macro-assembler.h"
#include "src/objects-inl.h"

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
// TurboFan support builtins.

TF_BUILTIN(CopyFastSmiOrObjectElements, CodeStubAssembler) {
  typedef CopyFastSmiOrObjectElementsDescriptor Descriptor;

  Node* object = Parameter(Descriptor::kObject);

  // Load the {object}s elements.
  Node* source = LoadObjectField(object, JSObject::kElementsOffset);

  ParameterMode mode = OptimalParameterMode();
  Node* length = TaggedToParameter(LoadFixedArrayBaseLength(source), mode);

  // Check if we can allocate in new space.
  ElementsKind kind = FAST_ELEMENTS;
  int max_elements = FixedArrayBase::GetMaxLengthForNewSpaceAllocation(kind);
  Label if_newspace(this), if_oldspace(this);
  Branch(UintPtrOrSmiLessThan(length, IntPtrOrSmiConstant(max_elements, mode),
                              mode),
         &if_newspace, &if_oldspace);

  Bind(&if_newspace);
  {
    Node* target = AllocateFixedArray(kind, length, mode);
    CopyFixedArrayElements(kind, source, target, length, SKIP_WRITE_BARRIER,
                           mode);
    StoreObjectField(object, JSObject::kElementsOffset, target);
    Return(target);
  }

  Bind(&if_oldspace);
  {
    Node* target = AllocateFixedArray(kind, length, mode, kPretenured);
    CopyFixedArrayElements(kind, source, target, length, UPDATE_WRITE_BARRIER,
                           mode);
    StoreObjectField(object, JSObject::kElementsOffset, target);
    Return(target);
  }
}

TF_BUILTIN(GrowFastDoubleElements, CodeStubAssembler) {
  typedef GrowArrayElementsDescriptor Descriptor;

  Node* object = Parameter(Descriptor::kObject);
  Node* key = Parameter(Descriptor::kKey);
  Node* context = Parameter(Descriptor::kContext);

  Label runtime(this, Label::kDeferred);
  Node* elements = LoadElements(object);
  elements = TryGrowElementsCapacity(object, elements, FAST_DOUBLE_ELEMENTS,
                                     key, &runtime);
  Return(elements);

  Bind(&runtime);
  TailCallRuntime(Runtime::kGrowArrayElements, context, object, key);
}

TF_BUILTIN(GrowFastSmiOrObjectElements, CodeStubAssembler) {
  typedef GrowArrayElementsDescriptor Descriptor;

  Node* object = Parameter(Descriptor::kObject);
  Node* key = Parameter(Descriptor::kKey);
  Node* context = Parameter(Descriptor::kContext);

  Label runtime(this, Label::kDeferred);
  Node* elements = LoadElements(object);
  elements =
      TryGrowElementsCapacity(object, elements, FAST_ELEMENTS, key, &runtime);
  Return(elements);

  Bind(&runtime);
  TailCallRuntime(Runtime::kGrowArrayElements, context, object, key);
}

TF_BUILTIN(NewUnmappedArgumentsElements, CodeStubAssembler) {
  typedef NewArgumentsElementsDescriptor Descriptor;

  Node* frame = Parameter(Descriptor::kFrame);
  Node* length = SmiToWord(Parameter(Descriptor::kLength));

  // Check if we can allocate in new space.
  ElementsKind kind = FAST_ELEMENTS;
  int max_elements = FixedArray::GetMaxLengthForNewSpaceAllocation(kind);
  Label if_newspace(this), if_oldspace(this, Label::kDeferred);
  Branch(IntPtrLessThan(length, IntPtrConstant(max_elements)), &if_newspace,
         &if_oldspace);

  Bind(&if_newspace);
  {
    // Prefer EmptyFixedArray in case of non-positive {length} (the {length}
    // can be negative here for rest parameters).
    Label if_empty(this), if_notempty(this);
    Branch(IntPtrLessThanOrEqual(length, IntPtrConstant(0)), &if_empty,
           &if_notempty);

    Bind(&if_empty);
    Return(EmptyFixedArrayConstant());

    Bind(&if_notempty);
    {
      // Allocate a FixedArray in new space.
      Node* result = AllocateFixedArray(kind, length);

      // Compute the effective {offset} into the {frame}.
      Node* offset = IntPtrAdd(length, IntPtrConstant(1));

      // Copy the parameters from {frame} (starting at {offset}) to {result}.
      Variable var_index(this, MachineType::PointerRepresentation());
      Label loop(this, &var_index), done_loop(this);
      var_index.Bind(IntPtrConstant(0));
      Goto(&loop);
      Bind(&loop);
      {
        // Load the current {index}.
        Node* index = var_index.value();

        // Check if we are done.
        GotoIf(WordEqual(index, length), &done_loop);

        // Load the parameter at the given {index}.
        Node* value = Load(MachineType::AnyTagged(), frame,
                           WordShl(IntPtrSub(offset, index),
                                   IntPtrConstant(kPointerSizeLog2)));

        // Store the {value} into the {result}.
        StoreFixedArrayElement(result, index, value, SKIP_WRITE_BARRIER);

        // Continue with next {index}.
        var_index.Bind(IntPtrAdd(index, IntPtrConstant(1)));
        Goto(&loop);
      }

      Bind(&done_loop);
      Return(result);
    }
  }

  Bind(&if_oldspace);
  {
    // Allocate in old space (or large object space).
    TailCallRuntime(Runtime::kNewArgumentsElements, NoContextConstant(),
                    BitcastWordToTagged(frame), SmiFromWord(length));
  }
}

TF_BUILTIN(ReturnReceiver, CodeStubAssembler) { Return(Parameter(0)); }

}  // namespace internal
}  // namespace v8
