// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-constructor.h"
#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/code-factory.h"
#include "src/code-stub-assembler.h"
#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

typedef compiler::Node Node;

Node* ConstructorBuiltinsAssembler::EmitFastNewClosure(Node* shared_info,
                                                       Node* context) {
  typedef compiler::CodeAssembler::Label Label;
  typedef compiler::CodeAssembler::Variable Variable;

  Isolate* isolate = this->isolate();
  Factory* factory = isolate->factory();
  IncrementCounter(isolate->counters()->fast_new_closure_total(), 1);

  // Create a new closure from the given function info in new space
  Node* result = Allocate(JSFunction::kSize);

  // Calculate the index of the map we should install on the function based on
  // the FunctionKind and LanguageMode of the function.
  // Note: Must be kept in sync with Context::FunctionMapIndex
  Node* compiler_hints =
      LoadObjectField(shared_info, SharedFunctionInfo::kCompilerHintsOffset,
                      MachineType::Uint32());
  Node* is_strict = Word32And(
      compiler_hints, Int32Constant(1 << SharedFunctionInfo::kStrictModeBit));

  Label if_normal(this), if_generator(this), if_async(this),
      if_class_constructor(this), if_function_without_prototype(this),
      load_map(this);
  Variable map_index(this, MachineType::PointerRepresentation());

  STATIC_ASSERT(FunctionKind::kNormalFunction == 0);
  Node* is_not_normal =
      Word32And(compiler_hints,
                Int32Constant(SharedFunctionInfo::kAllFunctionKindBitsMask));
  GotoUnless(is_not_normal, &if_normal);

  Node* is_generator = Word32And(
      compiler_hints, Int32Constant(FunctionKind::kGeneratorFunction
                                    << SharedFunctionInfo::kFunctionKindShift));
  GotoIf(is_generator, &if_generator);

  Node* is_async = Word32And(
      compiler_hints, Int32Constant(FunctionKind::kAsyncFunction
                                    << SharedFunctionInfo::kFunctionKindShift));
  GotoIf(is_async, &if_async);

  Node* is_class_constructor = Word32And(
      compiler_hints, Int32Constant(FunctionKind::kClassConstructor
                                    << SharedFunctionInfo::kFunctionKindShift));
  GotoIf(is_class_constructor, &if_class_constructor);

  if (FLAG_debug_code) {
    // Function must be a function without a prototype.
    CSA_ASSERT(
        this,
        Word32And(compiler_hints,
                  Int32Constant((FunctionKind::kAccessorFunction |
                                 FunctionKind::kArrowFunction |
                                 FunctionKind::kConciseMethod)
                                << SharedFunctionInfo::kFunctionKindShift)));
  }
  Goto(&if_function_without_prototype);

  Bind(&if_normal);
  {
    map_index.Bind(SelectIntPtrConstant(is_strict,
                                        Context::STRICT_FUNCTION_MAP_INDEX,
                                        Context::SLOPPY_FUNCTION_MAP_INDEX));
    Goto(&load_map);
  }

  Bind(&if_generator);
  {
    map_index.Bind(SelectIntPtrConstant(
        is_strict, Context::STRICT_GENERATOR_FUNCTION_MAP_INDEX,
        Context::SLOPPY_GENERATOR_FUNCTION_MAP_INDEX));
    Goto(&load_map);
  }

  Bind(&if_async);
  {
    map_index.Bind(SelectIntPtrConstant(
        is_strict, Context::STRICT_ASYNC_FUNCTION_MAP_INDEX,
        Context::SLOPPY_ASYNC_FUNCTION_MAP_INDEX));
    Goto(&load_map);
  }

  Bind(&if_class_constructor);
  {
    map_index.Bind(IntPtrConstant(Context::CLASS_FUNCTION_MAP_INDEX));
    Goto(&load_map);
  }

  Bind(&if_function_without_prototype);
  {
    map_index.Bind(
        IntPtrConstant(Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX));
    Goto(&load_map);
  }

  Bind(&load_map);

  // Get the function map in the current native context and set that
  // as the map of the allocated object.
  Node* native_context = LoadNativeContext(context);
  Node* map_slot_value =
      LoadFixedArrayElement(native_context, map_index.value());
  StoreMapNoWriteBarrier(result, map_slot_value);

  // Initialize the rest of the function.
  Node* empty_fixed_array = HeapConstant(factory->empty_fixed_array());
  Node* empty_literals_array = HeapConstant(factory->empty_literals_array());
  StoreObjectFieldNoWriteBarrier(result, JSObject::kPropertiesOffset,
                                 empty_fixed_array);
  StoreObjectFieldNoWriteBarrier(result, JSObject::kElementsOffset,
                                 empty_fixed_array);
  StoreObjectFieldNoWriteBarrier(result, JSFunction::kLiteralsOffset,
                                 empty_literals_array);
  StoreObjectFieldNoWriteBarrier(
      result, JSFunction::kPrototypeOrInitialMapOffset, TheHoleConstant());
  StoreObjectFieldNoWriteBarrier(result, JSFunction::kSharedFunctionInfoOffset,
                                 shared_info);
  StoreObjectFieldNoWriteBarrier(result, JSFunction::kContextOffset, context);
  Handle<Code> lazy_builtin_handle(
      isolate->builtins()->builtin(Builtins::kCompileLazy));
  Node* lazy_builtin = HeapConstant(lazy_builtin_handle);
  Node* lazy_builtin_entry =
      IntPtrAdd(BitcastTaggedToWord(lazy_builtin),
                IntPtrConstant(Code::kHeaderSize - kHeapObjectTag));
  StoreObjectFieldNoWriteBarrier(result, JSFunction::kCodeEntryOffset,
                                 lazy_builtin_entry,
                                 MachineType::PointerRepresentation());
  StoreObjectFieldNoWriteBarrier(result, JSFunction::kNextFunctionLinkOffset,
                                 UndefinedConstant());

  return result;
}

TF_BUILTIN(FastNewClosure, ConstructorBuiltinsAssembler) {
  Node* shared = Parameter(FastNewClosureDescriptor::kSharedFunctionInfo);
  Node* context = Parameter(FastNewClosureDescriptor::kContext);
  Return(EmitFastNewClosure(shared, context));
}

TF_BUILTIN(FastNewObject, ConstructorBuiltinsAssembler) {
  typedef FastNewObjectDescriptor Descriptor;
  Node* context = Parameter(Descriptor::kContext);
  Node* target = Parameter(Descriptor::kTarget);
  Node* new_target = Parameter(Descriptor::kNewTarget);

  CSA_ASSERT(this, HasInstanceType(target, JS_FUNCTION_TYPE));
  CSA_ASSERT(this, IsJSReceiver(new_target));

  // Verify that the new target is a JSFunction.
  Label runtime(this), fast(this);
  GotoIf(HasInstanceType(new_target, JS_FUNCTION_TYPE), &fast);
  Goto(&runtime);

  Bind(&runtime);
  TailCallRuntime(Runtime::kNewObject, context, target, new_target);

  Bind(&fast);

  // Load the initial map and verify that it's in fact a map.
  Node* initial_map =
      LoadObjectField(new_target, JSFunction::kPrototypeOrInitialMapOffset);
  GotoIf(TaggedIsSmi(initial_map), &runtime);
  GotoIf(DoesntHaveInstanceType(initial_map, MAP_TYPE), &runtime);

  // Fall back to runtime if the target differs from the new target's
  // initial map constructor.
  Node* new_target_constructor =
      LoadObjectField(initial_map, Map::kConstructorOrBackPointerOffset);
  GotoIf(WordNotEqual(target, new_target_constructor), &runtime);

  Node* instance_size_words = ChangeUint32ToWord(LoadObjectField(
      initial_map, Map::kInstanceSizeOffset, MachineType::Uint8()));
  Node* instance_size =
      WordShl(instance_size_words, IntPtrConstant(kPointerSizeLog2));

  Node* object = Allocate(instance_size);
  StoreMapNoWriteBarrier(object, initial_map);
  Node* empty_array = LoadRoot(Heap::kEmptyFixedArrayRootIndex);
  StoreObjectFieldNoWriteBarrier(object, JSObject::kPropertiesOffset,
                                 empty_array);
  StoreObjectFieldNoWriteBarrier(object, JSObject::kElementsOffset,
                                 empty_array);

  instance_size_words = ChangeUint32ToWord(LoadObjectField(
      initial_map, Map::kInstanceSizeOffset, MachineType::Uint8()));
  instance_size =
      WordShl(instance_size_words, IntPtrConstant(kPointerSizeLog2));

  // Perform in-object slack tracking if requested.
  Node* bit_field3 = LoadMapBitField3(initial_map);
  Label slack_tracking(this), finalize(this, Label::kDeferred), done(this);
  GotoIf(IsSetWord32<Map::ConstructionCounter>(bit_field3), &slack_tracking);

  // Initialize remaining fields.
  {
    Comment("no slack tracking");
    InitializeFieldsWithRoot(object, IntPtrConstant(JSObject::kHeaderSize),
                             instance_size, Heap::kUndefinedValueRootIndex);
    Return(object);
  }

  {
    Bind(&slack_tracking);

    // Decrease generous allocation count.
    STATIC_ASSERT(Map::ConstructionCounter::kNext == 32);
    Comment("update allocation count");
    Node* new_bit_field3 = Int32Sub(
        bit_field3, Int32Constant(1 << Map::ConstructionCounter::kShift));
    StoreObjectFieldNoWriteBarrier(initial_map, Map::kBitField3Offset,
                                   new_bit_field3,
                                   MachineRepresentation::kWord32);
    GotoIf(IsClearWord32<Map::ConstructionCounter>(new_bit_field3), &finalize);

    Node* unused_fields = LoadObjectField(
        initial_map, Map::kUnusedPropertyFieldsOffset, MachineType::Uint8());
    Node* used_size =
        IntPtrSub(instance_size, WordShl(ChangeUint32ToWord(unused_fields),
                                         IntPtrConstant(kPointerSizeLog2)));

    Comment("initialize filler fields (no finalize)");
    InitializeFieldsWithRoot(object, used_size, instance_size,
                             Heap::kOnePointerFillerMapRootIndex);

    Comment("initialize undefined fields (no finalize)");
    InitializeFieldsWithRoot(object, IntPtrConstant(JSObject::kHeaderSize),
                             used_size, Heap::kUndefinedValueRootIndex);
    Return(object);
  }

  {
    // Finalize the instance size.
    Bind(&finalize);

    Node* unused_fields = LoadObjectField(
        initial_map, Map::kUnusedPropertyFieldsOffset, MachineType::Uint8());
    Node* used_size =
        IntPtrSub(instance_size, WordShl(ChangeUint32ToWord(unused_fields),
                                         IntPtrConstant(kPointerSizeLog2)));

    Comment("initialize filler fields (finalize)");
    InitializeFieldsWithRoot(object, used_size, instance_size,
                             Heap::kOnePointerFillerMapRootIndex);

    Comment("initialize undefined fields (finalize)");
    InitializeFieldsWithRoot(object, IntPtrConstant(JSObject::kHeaderSize),
                             used_size, Heap::kUndefinedValueRootIndex);

    CallRuntime(Runtime::kFinalizeInstanceSize, context, initial_map);
    Return(object);
  }
}

Node* ConstructorBuiltinsAssembler::EmitFastNewFunctionContext(
    Node* function, Node* slots, Node* context, ScopeType scope_type) {
  slots = ChangeUint32ToWord(slots);

  // TODO(ishell): Use CSA::OptimalParameterMode() here.
  CodeStubAssembler::ParameterMode mode = CodeStubAssembler::INTPTR_PARAMETERS;
  Node* min_context_slots = IntPtrConstant(Context::MIN_CONTEXT_SLOTS);
  Node* length = IntPtrAdd(slots, min_context_slots);
  Node* size = GetFixedArrayAllocationSize(length, FAST_ELEMENTS, mode);

  // Create a new closure from the given function info in new space
  Node* function_context = Allocate(size);

  Heap::RootListIndex context_type;
  switch (scope_type) {
    case EVAL_SCOPE:
      context_type = Heap::kEvalContextMapRootIndex;
      break;
    case FUNCTION_SCOPE:
      context_type = Heap::kFunctionContextMapRootIndex;
      break;
    default:
      UNREACHABLE();
  }
  StoreMapNoWriteBarrier(function_context, context_type);
  StoreObjectFieldNoWriteBarrier(function_context, Context::kLengthOffset,
                                 SmiTag(length));

  // Set up the fixed slots.
  StoreFixedArrayElement(function_context, Context::CLOSURE_INDEX, function,
                         SKIP_WRITE_BARRIER);
  StoreFixedArrayElement(function_context, Context::PREVIOUS_INDEX, context,
                         SKIP_WRITE_BARRIER);
  StoreFixedArrayElement(function_context, Context::EXTENSION_INDEX,
                         TheHoleConstant(), SKIP_WRITE_BARRIER);

  // Copy the native context from the previous context.
  Node* native_context = LoadNativeContext(context);
  StoreFixedArrayElement(function_context, Context::NATIVE_CONTEXT_INDEX,
                         native_context, SKIP_WRITE_BARRIER);

  // Initialize the rest of the slots to undefined.
  Node* undefined = UndefinedConstant();
  BuildFastFixedArrayForEach(
      function_context, FAST_ELEMENTS, min_context_slots, length,
      [this, undefined](Node* context, Node* offset) {
        StoreNoWriteBarrier(MachineRepresentation::kTagged, context, offset,
                            undefined);
      },
      mode);

  return function_context;
}

// static
int ConstructorBuiltinsAssembler::MaximumFunctionContextSlots() {
  return FLAG_test_small_max_function_context_stub_size ? kSmallMaximumSlots
                                                        : kMaximumSlots;
}

TF_BUILTIN(FastNewFunctionContextEval, ConstructorBuiltinsAssembler) {
  Node* function = Parameter(FastNewFunctionContextDescriptor::kFunction);
  Node* slots = Parameter(FastNewFunctionContextDescriptor::kSlots);
  Node* context = Parameter(FastNewFunctionContextDescriptor::kContext);
  Return(EmitFastNewFunctionContext(function, slots, context,
                                    ScopeType::EVAL_SCOPE));
}

TF_BUILTIN(FastNewFunctionContextFunction, ConstructorBuiltinsAssembler) {
  Node* function = Parameter(FastNewFunctionContextDescriptor::kFunction);
  Node* slots = Parameter(FastNewFunctionContextDescriptor::kSlots);
  Node* context = Parameter(FastNewFunctionContextDescriptor::kContext);
  Return(EmitFastNewFunctionContext(function, slots, context,
                                    ScopeType::FUNCTION_SCOPE));
}

Handle<Code> Builtins::NewFunctionContext(ScopeType scope_type) {
  switch (scope_type) {
    case ScopeType::EVAL_SCOPE:
      return FastNewFunctionContextEval();
    case ScopeType::FUNCTION_SCOPE:
      return FastNewFunctionContextFunction();
    default:
      UNREACHABLE();
  }
  return Handle<Code>::null();
}

}  // namespace internal
}  // namespace v8
