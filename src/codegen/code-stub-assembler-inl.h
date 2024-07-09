// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_CODE_STUB_ASSEMBLER_INL_H_
#define V8_CODEGEN_CODE_STUB_ASSEMBLER_INL_H_

#include <functional>

#include "src/builtins/builtins-constructor-gen.h"
#include "src/builtins/builtins-inl.h"
#include "src/codegen/code-stub-assembler.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

template <typename TCallable, class... TArgs>
TNode<Object> CodeStubAssembler::Call(TNode<Context> context,
                                      TNode<TCallable> callable,
                                      ConvertReceiverMode mode,
                                      TNode<Object> receiver, TArgs... args) {
  static_assert(std::is_same<Object, TCallable>::value ||
                std::is_base_of<HeapObject, TCallable>::value);
  static_assert(!std::is_base_of<JSFunction, TCallable>::value,
                "Use CallFunction() when the callable is a JSFunction.");

  if (IsUndefinedConstant(receiver) || IsNullConstant(receiver)) {
    DCHECK_NE(mode, ConvertReceiverMode::kNotNullOrUndefined);
    return CallJS(Builtins::Call(ConvertReceiverMode::kNullOrUndefined),
                  context, callable, /* new_target */ {}, receiver, args...);
  }
  DCheckReceiver(mode, receiver);
  return CallJS(Builtins::Call(mode), context, callable,
                /* new_target */ {}, receiver, args...);
}

template <typename TCallable, class... TArgs>
TNode<Object> CodeStubAssembler::Call(TNode<Context> context,
                                      TNode<TCallable> callable,
                                      TNode<JSReceiver> receiver,
                                      TArgs... args) {
  return Call(context, callable, ConvertReceiverMode::kNotNullOrUndefined,
              receiver, args...);
}

template <typename TCallable, class... TArgs>
TNode<Object> CodeStubAssembler::Call(TNode<Context> context,
                                      TNode<TCallable> callable,
                                      TNode<Object> receiver, TArgs... args) {
  return Call(context, callable, ConvertReceiverMode::kAny, receiver, args...);
}

template <class... TArgs>
TNode<Object> CodeStubAssembler::CallFunction(TNode<Context> context,
                                              TNode<JSFunction> callable,
                                              ConvertReceiverMode mode,
                                              TNode<Object> receiver,
                                              TArgs... args) {
  if (IsUndefinedConstant(receiver) || IsNullConstant(receiver)) {
    DCHECK_NE(mode, ConvertReceiverMode::kNotNullOrUndefined);
    return CallJS(Builtins::CallFunction(ConvertReceiverMode::kNullOrUndefined),
                  context, callable, /* new_target */ {}, receiver, args...);
  }
  DCheckReceiver(mode, receiver);
  return CallJS(Builtins::CallFunction(mode), context, callable,
                /* new_target */ {}, receiver, args...);
}

template <class... TArgs>
TNode<Object> CodeStubAssembler::CallFunction(TNode<Context> context,
                                              TNode<JSFunction> callable,
                                              TNode<JSReceiver> receiver,
                                              TArgs... args) {
  return CallFunction(context, callable,
                      ConvertReceiverMode::kNotNullOrUndefined, receiver,
                      args...);
}

template <class... TArgs>
TNode<Object> CodeStubAssembler::CallFunction(TNode<Context> context,
                                              TNode<JSFunction> callable,
                                              TNode<Object> receiver,
                                              TArgs... args) {
  return CallFunction(context, callable, ConvertReceiverMode::kAny, receiver,
                      args...);
}

template <typename Function>
TNode<Object> CodeStubAssembler::FastCloneJSObject(
    TNode<HeapObject> object, TNode<IntPtrT> inobject_properties_start,
    TNode<IntPtrT> inobject_properties_size, bool target_has_same_offsets,
    TNode<Map> target_map, const Function& materialize_target) {
  Label done_copy_properties(this), done_copy_elements(this);

  // Next to the trivial case above the IC supports only JSObjects.
  // TODO(olivf): To support JSObjects other than JS_OBJECT_TYPE we need to
  // initialize the the in-object properties below in
  // `AllocateJSObjectFromMap`.
  CSA_DCHECK(this, InstanceTypeEqual(LoadInstanceType(object), JS_OBJECT_TYPE));
  CSA_DCHECK(this, IsStrong(TNode<MaybeObject>(target_map)));
  CSA_DCHECK(
      this, InstanceTypeEqual(LoadMapInstanceType(target_map), JS_OBJECT_TYPE));

  TVARIABLE(HeapObject, var_properties, EmptyFixedArrayConstant());
  TVARIABLE(FixedArray, var_elements, EmptyFixedArrayConstant());

  // Copy the PropertyArray backing store. The source PropertyArray
  // must be either an Smi, or a PropertyArray.
  TNode<Object> source_properties =
      LoadObjectField(object, JSObject::kPropertiesOrHashOffset);
  {
    GotoIf(TaggedIsSmi(source_properties), &done_copy_properties);
    GotoIf(IsEmptyFixedArray(source_properties), &done_copy_properties);

    // This fastcase requires that the source object has fast properties.
    TNode<PropertyArray> source_property_array = CAST(source_properties);

    TNode<IntPtrT> length = LoadPropertyArrayLength(source_property_array);
    GotoIf(IntPtrEqual(length, IntPtrConstant(0)), &done_copy_properties);

    TNode<PropertyArray> property_array = AllocatePropertyArray(length);
    FillPropertyArrayWithUndefined(property_array, IntPtrConstant(0), length);
    CopyPropertyArrayValues(source_property_array, property_array, length,
                            SKIP_WRITE_BARRIER, DestroySource::kNo);
    var_properties = property_array;
  }

  Goto(&done_copy_properties);
  BIND(&done_copy_properties);

  TNode<FixedArrayBase> source_elements = LoadElements(CAST(object));
  GotoIf(TaggedEqual(source_elements, EmptyFixedArrayConstant()),
         &done_copy_elements);
  var_elements = CAST(CloneFixedArray(
      source_elements, ExtractFixedArrayFlag::kAllFixedArraysDontCopyCOW));

  Goto(&done_copy_elements);
  BIND(&done_copy_elements);

  TNode<JSReceiver> target = materialize_target(
      target_map, var_properties.value(), var_elements.value());

  // Lastly, clone any in-object properties.
  TNode<IntPtrT> field_offset_difference;
  if (target_has_same_offsets) {
#ifdef DEBUG
    TNode<IntPtrT> target_inobject_properties_start =
        LoadMapInobjectPropertiesStartInWords(target_map);
    CSA_DCHECK(this, IntPtrEqual(inobject_properties_start,
                                 target_inobject_properties_start));
#endif
    field_offset_difference = IntPtrConstant(0);
  } else {
    TNode<IntPtrT> target_inobject_properties_start =
        LoadMapInobjectPropertiesStartInWords(target_map);
    field_offset_difference = TimesTaggedSize(
        IntPtrSub(target_inobject_properties_start, inobject_properties_start));
  }

  // Just copy the fields as raw data (pretending that there are no
  // mutable HeapNumbers). This doesn't need write barriers.
  BuildFastLoop<IntPtrT>(
      inobject_properties_start, inobject_properties_size,
      [=](TNode<IntPtrT> field_index) {
        TNode<IntPtrT> field_offset = TimesTaggedSize(field_index);
        TNode<TaggedT> field = LoadObjectField<TaggedT>(object, field_offset);
        TNode<IntPtrT> result_offset =
            target_has_same_offsets
                ? field_offset
                : IntPtrSub(field_offset, field_offset_difference);
        StoreObjectFieldNoWriteBarrier(target, result_offset, field);
      },
      1, LoopUnrollingMode::kYes, IndexAdvanceMode::kPost);

  TNode<IntPtrT> target_inobject_properties_size =
      LoadMapInstanceSizeInWords(target_map);
  CSA_DCHECK(this, IntPtrGreaterThanOrEqual(IntPtrSub(inobject_properties_size,
                                                      field_offset_difference),
                                            target_inobject_properties_size));

  // We need to go through the {object} again here and properly clone
  // them. We use a second loop here to ensure that the GC (and heap
  // verifier) always sees properly initialized objects, i.e. never
  // hits undefined values in double fields.
  TNode<IntPtrT> start_offset = IntPtrSub(
      TimesTaggedSize(inobject_properties_start), field_offset_difference);
  TNode<IntPtrT> end_offset = TimesTaggedSize(target_inobject_properties_size);
  ConstructorBuiltinsAssembler(state()).CopyMutableHeapNumbersInObject(
      target, start_offset, end_offset);

  return target;
}

}  // namespace internal
}  // namespace v8
#endif  // V8_CODEGEN_CODE_STUB_ASSEMBLER_INL_H_
