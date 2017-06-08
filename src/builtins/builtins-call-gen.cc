// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-call-gen.h"

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/globals.h"
#include "src/isolate.h"
#include "src/macro-assembler.h"

namespace v8 {
namespace internal {

void Builtins::Generate_CallFunction_ReceiverIsNullOrUndefined(
    MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kNullOrUndefined,
                        TailCallMode::kDisallow);
}

void Builtins::Generate_CallFunction_ReceiverIsNotNullOrUndefined(
    MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kNotNullOrUndefined,
                        TailCallMode::kDisallow);
}

void Builtins::Generate_CallFunction_ReceiverIsAny(MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kAny,
                        TailCallMode::kDisallow);
}

void Builtins::Generate_TailCallFunction_ReceiverIsNullOrUndefined(
    MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kNullOrUndefined,
                        TailCallMode::kAllow);
}

void Builtins::Generate_TailCallFunction_ReceiverIsNotNullOrUndefined(
    MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kNotNullOrUndefined,
                        TailCallMode::kAllow);
}

void Builtins::Generate_TailCallFunction_ReceiverIsAny(MacroAssembler* masm) {
  Generate_CallFunction(masm, ConvertReceiverMode::kAny, TailCallMode::kAllow);
}

void Builtins::Generate_CallBoundFunction(MacroAssembler* masm) {
  Generate_CallBoundFunctionImpl(masm, TailCallMode::kDisallow);
}

void Builtins::Generate_TailCallBoundFunction(MacroAssembler* masm) {
  Generate_CallBoundFunctionImpl(masm, TailCallMode::kAllow);
}

void Builtins::Generate_Call_ReceiverIsNullOrUndefined(MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kNullOrUndefined,
                TailCallMode::kDisallow);
}

void Builtins::Generate_Call_ReceiverIsNotNullOrUndefined(
    MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kNotNullOrUndefined,
                TailCallMode::kDisallow);
}

void Builtins::Generate_Call_ReceiverIsAny(MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kAny, TailCallMode::kDisallow);
}

void Builtins::Generate_TailCall_ReceiverIsNullOrUndefined(
    MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kNullOrUndefined,
                TailCallMode::kAllow);
}

void Builtins::Generate_TailCall_ReceiverIsNotNullOrUndefined(
    MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kNotNullOrUndefined,
                TailCallMode::kAllow);
}

void Builtins::Generate_TailCall_ReceiverIsAny(MacroAssembler* masm) {
  Generate_Call(masm, ConvertReceiverMode::kAny, TailCallMode::kAllow);
}

void Builtins::Generate_CallVarargs(MacroAssembler* masm) {
  Generate_CallOrConstructVarargs(masm, masm->isolate()->builtins()->Call());
}

void Builtins::Generate_CallForwardVarargs(MacroAssembler* masm) {
  Generate_CallOrConstructForwardVarargs(masm,
                                         masm->isolate()->builtins()->Call());
}

void Builtins::Generate_CallFunctionForwardVarargs(MacroAssembler* masm) {
  Generate_CallOrConstructForwardVarargs(
      masm, masm->isolate()->builtins()->CallFunction());
}

void CallOrConstructBuiltinsAssembler::CallOrConstructWithArrayLike(
    Node* target, Node* new_target, Node* arguments_list, Node* context) {
  Variable var_elements(this, MachineRepresentation::kTagged);
  Variable var_length(this, MachineRepresentation::kWord32);
  Label if_done(this), if_arguments(this), if_array(this),
      if_holey_array(this, Label::kDeferred),
      if_runtime(this, Label::kDeferred);

  GotoIf(TaggedIsSmi(arguments_list), &if_runtime);
  Node* arguments_list_map = LoadMap(arguments_list);
  Node* native_context = LoadNativeContext(context);

  // Check if {arguments_list} is an (unmodified) arguments object.
  Node* sloppy_arguments_map =
      LoadContextElement(native_context, Context::SLOPPY_ARGUMENTS_MAP_INDEX);
  GotoIf(WordEqual(arguments_list_map, sloppy_arguments_map), &if_arguments);
  Node* strict_arguments_map =
      LoadContextElement(native_context, Context::STRICT_ARGUMENTS_MAP_INDEX);
  GotoIf(WordEqual(arguments_list_map, strict_arguments_map), &if_arguments);

  // Check if {arguments_list} is a fast JSArray.
  Branch(IsJSArrayMap(arguments_list_map), &if_array, &if_runtime);

  BIND(&if_array);
  {
    // Try to extract the elements from a JSArray object.
    var_elements.Bind(
        LoadObjectField(arguments_list, JSArray::kElementsOffset));
    var_length.Bind(LoadAndUntagToWord32ObjectField(arguments_list,
                                                    JSArray::kLengthOffset));

    // Holey arrays and double backing stores need special treatment.
    Node* kind = LoadMapElementsKind(arguments_list_map);
    STATIC_ASSERT(FAST_SMI_ELEMENTS == 0);
    STATIC_ASSERT(FAST_HOLEY_SMI_ELEMENTS == 1);
    STATIC_ASSERT(FAST_ELEMENTS == 2);
    STATIC_ASSERT(FAST_HOLEY_ELEMENTS == 3);
    GotoIf(Word32Equal(kind, Int32Constant(FAST_HOLEY_SMI_ELEMENTS)),
           &if_holey_array);
    GotoIf(Word32Equal(kind, Int32Constant(FAST_HOLEY_ELEMENTS)),
           &if_holey_array);
    Branch(Uint32LessThanOrEqual(kind, Int32Constant(FAST_HOLEY_ELEMENTS)),
           &if_done, &if_runtime);
  }

  BIND(&if_holey_array);
  {
    // For holey JSArrays we need to check that the array prototype chain
    // protector is intact and our prototype is the Array.prototype actually.
    Node* arguments_list_prototype = LoadMapPrototype(arguments_list_map);
    Node* initial_array_prototype = LoadContextElement(
        native_context, Context::INITIAL_ARRAY_PROTOTYPE_INDEX);
    GotoIfNot(WordEqual(arguments_list_prototype, initial_array_prototype),
              &if_runtime);
    Node* protector_cell = LoadRoot(Heap::kArrayProtectorRootIndex);
    DCHECK(isolate()->heap()->array_protector()->IsPropertyCell());
    Branch(
        WordEqual(LoadObjectField(protector_cell, PropertyCell::kValueOffset),
                  SmiConstant(Smi::FromInt(Isolate::kProtectorValid))),
        &if_done, &if_runtime);
  }

  BIND(&if_arguments);
  {
    // Try to extract the elements from an JSArgumentsObject.
    Node* length =
        LoadObjectField(arguments_list, JSArgumentsObject::kLengthOffset);
    Node* elements =
        LoadObjectField(arguments_list, JSArgumentsObject::kElementsOffset);
    Node* elements_length =
        LoadObjectField(elements, FixedArray::kLengthOffset);
    GotoIfNot(WordEqual(length, elements_length), &if_runtime);
    var_elements.Bind(elements);
    var_length.Bind(SmiToWord32(length));
    Goto(&if_done);
  }

  BIND(&if_runtime);
  {
    // Ask the runtime to create the list (actually a FixedArray).
    Node* elements =
        CallRuntime(Runtime::kCreateListFromArrayLike, context, arguments_list);
    var_elements.Bind(elements);
    var_length.Bind(
        LoadAndUntagToWord32ObjectField(elements, FixedArray::kLengthOffset));
    Goto(&if_done);
  }

  // Tail call to the appropriate builtin (depending on whether we have
  // a {new_target} passed).
  BIND(&if_done);
  {
    Node* elements = var_elements.value();
    Node* length = var_length.value();
    if (new_target == nullptr) {
      Callable callable = CodeFactory::CallVarargs(isolate());
      TailCallStub(callable, context, target, Int32Constant(0), elements,
                   length);
    } else {
      Callable callable = CodeFactory::ConstructVarargs(isolate());
      TailCallStub(callable, context, target, new_target, Int32Constant(0),
                   elements, length);
    }
  }
}

TF_BUILTIN(CallWithArrayLike, CallOrConstructBuiltinsAssembler) {
  Node* target = Parameter(CallWithArrayLikeDescriptor::kTarget);
  Node* new_target = nullptr;
  Node* arguments_list = Parameter(CallWithArrayLikeDescriptor::kArgumentsList);
  Node* context = Parameter(CallWithArrayLikeDescriptor::kContext);
  CallOrConstructWithArrayLike(target, new_target, arguments_list, context);
}

}  // namespace internal
}  // namespace v8
