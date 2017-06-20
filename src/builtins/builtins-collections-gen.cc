// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-constructor-gen.h"
#include "src/builtins/builtins-iterator-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/code-stub-assembler.h"
#include "src/objects/hash-table.h"

namespace v8 {
namespace internal {

using compiler::Node;

class CollectionsBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit CollectionsBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  Node* AllocateJSMap(Node* js_map_function);

  template <typename CollectionType>
  Node* AllocateOrderedHashTable();
  Node* AllocateJSCollection(Node* js_map_function);

  Node* CallGetRaw(Node* const table, Node* const key);
  template <typename CollectionType, int entrysize>
  Node* CallHasRaw(Node* const table, Node* const key);
};

template <typename CollectionType>
Node* CollectionsBuiltinsAssembler::AllocateOrderedHashTable() {
  static const int kCapacity = CollectionType::kMinCapacity;
  static const int kBucketCount = kCapacity / CollectionType::kLoadFactor;
  static const int kDataTableLength = kCapacity * CollectionType::kEntrySize;
  static const int kFixedArrayLength =
      CollectionType::kHashTableStartIndex + kBucketCount + kDataTableLength;
  static const int kDataTableStartIndex =
      CollectionType::kHashTableStartIndex + kBucketCount;

  STATIC_ASSERT(base::bits::IsPowerOfTwo32(kCapacity));
  STATIC_ASSERT(kCapacity <= CollectionType::kMaxCapacity);

  // Allocate the table and add the proper map.
  const ElementsKind elements_kind = FAST_HOLEY_ELEMENTS;
  Node* const length_intptr = IntPtrConstant(kFixedArrayLength);
  Node* const table = AllocateFixedArray(elements_kind, length_intptr);
  CSA_ASSERT(this,
             IntPtrLessThanOrEqual(
                 length_intptr, IntPtrConstant(FixedArray::kMaxRegularLength)));
  Heap::RootListIndex map_index = Heap::kOrderedHashTableMapRootIndex;
  // TODO(gsathya): Directly store correct in AllocateFixedArray,
  // instead of overwriting here.
  StoreMapNoWriteBarrier(table, map_index);

  // Initialize the OrderedHashTable fields.
  const WriteBarrierMode barrier_mode = SKIP_WRITE_BARRIER;
  StoreFixedArrayElement(table, CollectionType::kNumberOfElementsIndex,
                         SmiConstant(0), barrier_mode);
  StoreFixedArrayElement(table, CollectionType::kNumberOfDeletedElementsIndex,
                         SmiConstant(0), barrier_mode);
  StoreFixedArrayElement(table, CollectionType::kNumberOfBucketsIndex,
                         SmiConstant(kBucketCount), barrier_mode);

  // Fill the buckets with kNotFound.
  Node* const not_found = SmiConstant(CollectionType::kNotFound);
  STATIC_ASSERT(CollectionType::kHashTableStartIndex ==
                CollectionType::kNumberOfBucketsIndex + 1);
  STATIC_ASSERT((CollectionType::kHashTableStartIndex + kBucketCount) ==
                kDataTableStartIndex);
  for (int i = 0; i < kBucketCount; i++) {
    StoreFixedArrayElement(table, CollectionType::kHashTableStartIndex + i,
                           not_found, barrier_mode);
  }

  // Fill the data table with undefined.
  STATIC_ASSERT(kDataTableStartIndex + kDataTableLength == kFixedArrayLength);
  for (int i = 0; i < kDataTableLength; i++) {
    StoreFixedArrayElement(table, kDataTableStartIndex + i, UndefinedConstant(),
                           barrier_mode);
  }

  return table;
}

Node* CollectionsBuiltinsAssembler::AllocateJSCollection(
    Node* js_map_function) {
  CSA_ASSERT(this, IsConstructorMap(LoadMap(js_map_function)));
  Node* const initial_map = LoadObjectField(
      js_map_function, JSFunction::kPrototypeOrInitialMapOffset);
  Node* const instance = AllocateJSObjectFromMap(initial_map);

  StoreObjectFieldRoot(instance, JSMap::kTableOffset,
                       Heap::kUndefinedValueRootIndex);

  return instance;
}

TF_BUILTIN(MapConstructor, CollectionsBuiltinsAssembler) {
  const int kIterableArg = 0;

  Node* argc =
      ChangeInt32ToIntPtr(Parameter(BuiltinDescriptor::kArgumentsCount));
  CodeStubArguments args(this, argc);

  Node* const iterable = args.GetOptionalArgumentValue(kIterableArg);
  Node* const new_target = Parameter(BuiltinDescriptor::kNewTarget);
  Node* const context = Parameter(BuiltinDescriptor::kContext);

  Label if_target_is_undefined(this, Label::kDeferred);
  GotoIf(IsUndefined(new_target), &if_target_is_undefined);

  Node* const native_context = LoadNativeContext(context);
  Node* const js_map_fun =
      LoadContextElement(native_context, Context::JS_MAP_FUN_INDEX);

  VARIABLE(var_result, MachineRepresentation::kTagged);

  Label init(this), exit(this), if_targetisnotmodified(this),
      if_targetismodified(this);
  Branch(WordEqual(js_map_fun, new_target), &if_targetisnotmodified,
         &if_targetismodified);

  BIND(&if_targetisnotmodified);
  {
    Node* const instance = AllocateJSCollection(js_map_fun);
    var_result.Bind(instance);
    Goto(&init);
  }

  BIND(&if_targetismodified);
  {
    ConstructorBuiltinsAssembler constructor_assembler(this->state());
    Node* const instance = constructor_assembler.EmitFastNewObject(
        context, js_map_fun, new_target);
    var_result.Bind(instance);
    Goto(&init);
  }

  BIND(&init);
  Node* table = AllocateOrderedHashTable<OrderedHashMap>();
  StoreObjectField(var_result.value(), JSMap::kTableOffset, table);

  GotoIf(Word32Or(IsUndefined(iterable), IsNull(iterable)), &exit);

  Label if_notcallable(this);
  // TODO(gsathya): Add fast path for unmodified maps.
  Node* const adder = GetProperty(context, var_result.value(),
                                  isolate()->factory()->set_string());
  GotoIf(TaggedIsSmi(adder), &if_notcallable);
  GotoIfNot(IsCallable(adder), &if_notcallable);

  IteratorBuiltinsAssembler iterator_assembler(this->state());
  Node* const iterator = iterator_assembler.GetIterator(context, iterable);
  GotoIf(IsUndefined(iterator), &exit);

  Node* const fast_iterator_result_map =
      LoadContextElement(native_context, Context::ITERATOR_RESULT_MAP_INDEX);

  VARIABLE(var_exception, MachineRepresentation::kTagged, TheHoleConstant());

  Label loop(this), if_notobject(this), if_exception(this);
  Goto(&loop);

  BIND(&loop);
  {
    Node* const next = iterator_assembler.IteratorStep(
        context, iterator, &exit, fast_iterator_result_map);

    Node* const next_value = iterator_assembler.IteratorValue(
        context, next, fast_iterator_result_map);

    GotoIf(TaggedIsSmi(next_value), &if_notobject);
    GotoIfNot(IsJSReceiver(next_value), &if_notobject);

    Node* const k =
        GetProperty(context, next_value, isolate()->factory()->zero_string());
    GotoIfException(k, &if_exception, &var_exception);

    Node* const v =
        GetProperty(context, next_value, isolate()->factory()->one_string());
    GotoIfException(v, &if_exception, &var_exception);

    Node* add_call = CallJS(CodeFactory::Call(isolate()), context, adder,
                            var_result.value(), k, v);
    GotoIfException(add_call, &if_exception, &var_exception);
    Goto(&loop);

    BIND(&if_notobject);
    {
      Node* const exception = MakeTypeError(
          MessageTemplate::kIteratorValueNotAnObject, context, next_value);
      var_exception.Bind(exception);
      Goto(&if_exception);
    }
  }

  BIND(&if_exception);
  {
    iterator_assembler.IteratorCloseOnException(context, iterator,
                                                &var_exception);
  }

  BIND(&if_notcallable);
  {
    Node* const message_id = SmiConstant(MessageTemplate::kPropertyNotFunction);
    Node* const receiver_str = HeapConstant(isolate()->factory()->add_string());
    CallRuntime(Runtime::kThrowTypeError, context, message_id, adder,
                receiver_str, var_result.value());
    Unreachable();
  }

  BIND(&if_target_is_undefined);
  {
    Node* const message_id =
        SmiConstant(MessageTemplate::kConstructorNotFunction);
    CallRuntime(Runtime::kThrowTypeError, context, message_id,
                HeapConstant(isolate()->factory()->Map_string()));
    Unreachable();
  }

  BIND(&exit);
  args.PopAndReturn(var_result.value());
}

TF_BUILTIN(SetConstructor, CollectionsBuiltinsAssembler) {
  const int kIterableArg = 0;

  Node* argc =
      ChangeInt32ToIntPtr(Parameter(BuiltinDescriptor::kArgumentsCount));
  CodeStubArguments args(this, argc);

  Node* const iterable = args.GetOptionalArgumentValue(kIterableArg);
  Node* const new_target = Parameter(BuiltinDescriptor::kNewTarget);
  Node* const context = Parameter(BuiltinDescriptor::kContext);

  Label if_target_is_undefined(this, Label::kDeferred);
  GotoIf(IsUndefined(new_target), &if_target_is_undefined);

  Node* const native_context = LoadNativeContext(context);
  Node* const js_set_fun =
      LoadContextElement(native_context, Context::JS_SET_FUN_INDEX);

  VARIABLE(var_result, MachineRepresentation::kTagged);

  Label init(this), exit(this), if_targetisnotmodified(this),
      if_targetismodified(this);
  Branch(WordEqual(js_set_fun, new_target), &if_targetisnotmodified,
         &if_targetismodified);

  BIND(&if_targetisnotmodified);
  {
    Node* const instance = AllocateJSCollection(js_set_fun);
    var_result.Bind(instance);
    Goto(&init);
  }

  BIND(&if_targetismodified);
  {
    ConstructorBuiltinsAssembler constructor_assembler(this->state());
    Node* const instance = constructor_assembler.EmitFastNewObject(
        context, js_set_fun, new_target);
    var_result.Bind(instance);
    Goto(&init);
  }

  BIND(&init);
  Node* table = AllocateOrderedHashTable<OrderedHashSet>();
  StoreObjectField(var_result.value(), JSSet::kTableOffset, table);

  GotoIf(Word32Or(IsUndefined(iterable), IsNull(iterable)), &exit);

  Label if_notcallable(this);
  // TODO(gsathya): Add fast path for unmodified maps.
  Node* const adder = GetProperty(context, var_result.value(),
                                  isolate()->factory()->add_string());
  GotoIf(TaggedIsSmi(adder), &if_notcallable);
  GotoIfNot(IsCallable(adder), &if_notcallable);

  IteratorBuiltinsAssembler iterator_assembler(this->state());
  Node* const iterator = iterator_assembler.GetIterator(context, iterable);
  GotoIf(IsUndefined(iterator), &exit);

  Node* const fast_iterator_result_map =
      LoadContextElement(native_context, Context::ITERATOR_RESULT_MAP_INDEX);

  VARIABLE(var_exception, MachineRepresentation::kTagged, TheHoleConstant());

  Label loop(this), if_notobject(this), if_exception(this);
  Goto(&loop);

  BIND(&loop);
  {
    Node* const next = iterator_assembler.IteratorStep(
        context, iterator, &exit, fast_iterator_result_map);

    Node* const next_value = iterator_assembler.IteratorValue(
        context, next, fast_iterator_result_map);

    Node* add_call = CallJS(CodeFactory::Call(isolate()), context, adder,
                            var_result.value(), next_value);

    GotoIfException(add_call, &if_exception, &var_exception);
    Goto(&loop);
  }

  BIND(&if_exception);
  {
    iterator_assembler.IteratorCloseOnException(context, iterator,
                                                &var_exception);
  }

  BIND(&if_notcallable);
  {
    Node* const message_id = SmiConstant(MessageTemplate::kPropertyNotFunction);
    Node* const receiver_str = HeapConstant(isolate()->factory()->add_string());
    CallRuntime(Runtime::kThrowTypeError, context, message_id, adder,
                receiver_str, var_result.value());
    Unreachable();
  }

  BIND(&if_target_is_undefined);
  {
    Node* const message_id =
        SmiConstant(MessageTemplate::kConstructorNotFunction);
    CallRuntime(Runtime::kThrowTypeError, context, message_id,
                HeapConstant(isolate()->factory()->Set_string()));
    Unreachable();
  }

  BIND(&exit);
  args.PopAndReturn(var_result.value());
}

Node* CollectionsBuiltinsAssembler::CallGetRaw(Node* const table,
                                               Node* const key) {
  Node* const function_addr =
      ExternalConstant(ExternalReference::orderedhashmap_get_raw(isolate()));
  Node* const isolate_ptr =
      ExternalConstant(ExternalReference::isolate_address(isolate()));

  MachineType type_ptr = MachineType::Pointer();
  MachineType type_tagged = MachineType::AnyTagged();

  Node* const result =
      CallCFunction3(type_tagged, type_ptr, type_tagged, type_tagged,
                     function_addr, isolate_ptr, table, key);

  return result;
}

template <typename CollectionType, int entrysize>
Node* CollectionsBuiltinsAssembler::CallHasRaw(Node* const table,
                                               Node* const key) {
  Node* const function_addr = ExternalConstant(
      ExternalReference::orderedhashtable_has_raw<CollectionType, entrysize>(
          isolate()));
  Node* const isolate_ptr =
      ExternalConstant(ExternalReference::isolate_address(isolate()));

  MachineType type_uint8 = MachineType::Uint8();
  MachineType type_ptr = MachineType::Pointer();
  MachineType type_tagged = MachineType::AnyTagged();

  Node* const result =
      CallCFunction3(type_uint8, type_ptr, type_tagged, type_tagged,
                     function_addr, isolate_ptr, table, key);

  return SelectBooleanConstant(
      Word32NotEqual(Word32And(result, Int32Constant(0xFF)), Int32Constant(0)));
}

TF_BUILTIN(MapGet, CollectionsBuiltinsAssembler) {
  Node* const receiver = Parameter(Descriptor::kReceiver);
  Node* const key = Parameter(Descriptor::kKey);
  Node* const context = Parameter(Descriptor::kContext);

  ThrowIfNotInstanceType(context, receiver, JS_MAP_TYPE, "Map.prototype.get");

  Node* const table = LoadObjectField(receiver, JSMap::kTableOffset);
  Return(CallGetRaw(table, key));
}

TF_BUILTIN(MapHas, CollectionsBuiltinsAssembler) {
  Node* const receiver = Parameter(Descriptor::kReceiver);
  Node* const key = Parameter(Descriptor::kKey);
  Node* const context = Parameter(Descriptor::kContext);

  ThrowIfNotInstanceType(context, receiver, JS_MAP_TYPE, "Map.prototype.has");

  Node* const table = LoadObjectField(receiver, JSMap::kTableOffset);
  Return(CallHasRaw<OrderedHashMap, 2>(table, key));
}

TF_BUILTIN(SetHas, CollectionsBuiltinsAssembler) {
  Node* const receiver = Parameter(Descriptor::kReceiver);
  Node* const key = Parameter(Descriptor::kKey);
  Node* const context = Parameter(Descriptor::kContext);

  ThrowIfNotInstanceType(context, receiver, JS_SET_TYPE, "Set.prototype.has");

  Node* const table = LoadObjectField(receiver, JSMap::kTableOffset);
  Return(CallHasRaw<OrderedHashSet, 1>(table, key));
}

}  // namespace internal
}  // namespace v8
