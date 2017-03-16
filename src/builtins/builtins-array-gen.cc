// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

class ArrayBuiltinCodeStubAssembler : public CodeStubAssembler {
 public:
  explicit ArrayBuiltinCodeStubAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  typedef std::function<Node*(Node* o, Node* len)> BuiltinResultGenerator;
  typedef std::function<void(Node* a, Node* pK, Node* value)>
      CallResultProcessor;

  void GenerateIteratingArrayBuiltinBody(
      const char* name, const BuiltinResultGenerator& generator,
      const CallResultProcessor& processor,
      const Callable& slow_case_continuation) {
    Node* receiver = Parameter(IteratingArrayBuiltinDescriptor::kReceiver);
    Node* callbackfn = Parameter(IteratingArrayBuiltinDescriptor::kCallback);
    Node* this_arg = Parameter(IteratingArrayBuiltinDescriptor::kThisArg);
    Node* context = Parameter(IteratingArrayBuiltinDescriptor::kContext);
    Node* new_target = Parameter(IteratingArrayBuiltinDescriptor::kNewTarget);

    Variable k(this, MachineRepresentation::kTagged, SmiConstant(0));
    Label non_array(this), slow(this, &k), array_changes(this, &k);

    // TODO(danno): Seriously? Do we really need to throw the exact error
    // message on null and undefined so that the webkit tests pass?
    Label throw_null_undefined_exception(this, Label::kDeferred);
    GotoIf(WordEqual(receiver, NullConstant()),
           &throw_null_undefined_exception);
    GotoIf(WordEqual(receiver, UndefinedConstant()),
           &throw_null_undefined_exception);

    // By the book: taken directly from the ECMAScript 2015 specification

    // 1. Let O be ToObject(this value).
    // 2. ReturnIfAbrupt(O)
    Node* o = CallStub(CodeFactory::ToObject(isolate()), context, receiver);

    // 3. Let len be ToLength(Get(O, "length")).
    // 4. ReturnIfAbrupt(len).
    Variable merged_length(this, MachineRepresentation::kTagged);
    Label has_length(this, &merged_length), not_js_array(this);
    GotoIf(DoesntHaveInstanceType(o, JS_ARRAY_TYPE), &not_js_array);
    merged_length.Bind(LoadJSArrayLength(o));
    Goto(&has_length);
    Bind(&not_js_array);
    Node* len_property =
        GetProperty(context, o, isolate()->factory()->length_string());
    merged_length.Bind(
        CallStub(CodeFactory::ToLength(isolate()), context, len_property));
    Goto(&has_length);
    Bind(&has_length);
    Node* len = merged_length.value();

    // 5. If IsCallable(callbackfn) is false, throw a TypeError exception.
    Label type_exception(this, Label::kDeferred);
    Label done(this);
    GotoIf(TaggedIsSmi(callbackfn), &type_exception);
    Branch(IsCallableMap(LoadMap(callbackfn)), &done, &type_exception);

    Bind(&throw_null_undefined_exception);
    {
      CallRuntime(
          Runtime::kThrowTypeError, context,
          SmiConstant(MessageTemplate::kCalledOnNullOrUndefined),
          HeapConstant(isolate()->factory()->NewStringFromAsciiChecked(name)));
      Unreachable();
    }

    Bind(&type_exception);
    {
      CallRuntime(Runtime::kThrowTypeError, context,
                  SmiConstant(MessageTemplate::kCalledNonCallable), callbackfn);
      Unreachable();
    }

    Bind(&done);

    Node* a = generator(o, len);

    // 6. If thisArg was supplied, let T be thisArg; else let T be undefined.
    // [Already done by the arguments adapter]

    HandleFastElements(context, this_arg, o, len, callbackfn, processor, a, k,
                       &slow);

    // 7. Let k be 0.
    // Already done above in initialization of the Variable k

    Bind(&slow);

    Node* target = LoadFromFrame(StandardFrameConstants::kFunctionOffset,
                                 MachineType::TaggedPointer());
    TailCallStub(
        slow_case_continuation, context, target, new_target,
        Int32Constant(IteratingArrayBuiltinLoopContinuationDescriptor::kArity),
        receiver, callbackfn, this_arg, a, o, k.value(), len);
  }

  void GenerateIteratingArrayBuiltinLoopContinuation(
      const CallResultProcessor& processor) {
    Node* callbackfn =
        Parameter(IteratingArrayBuiltinLoopContinuationDescriptor::kCallback);
    Node* this_arg =
        Parameter(IteratingArrayBuiltinLoopContinuationDescriptor::kThisArg);
    Node* a =
        Parameter(IteratingArrayBuiltinLoopContinuationDescriptor::kArray);
    Node* o =
        Parameter(IteratingArrayBuiltinLoopContinuationDescriptor::kObject);
    Node* initial_k =
        Parameter(IteratingArrayBuiltinLoopContinuationDescriptor::kInitialK);
    Node* len =
        Parameter(IteratingArrayBuiltinLoopContinuationDescriptor::kLength);
    Node* context =
        Parameter(IteratingArrayBuiltinLoopContinuationDescriptor::kContext);

    // 8. Repeat, while k < len
    Variable k(this, MachineRepresentation::kTagged, initial_k);
    Label loop(this, &k);
    Label after_loop(this);
    Goto(&loop);
    Bind(&loop);
    {
      GotoUnlessNumberLessThan(k.value(), len, &after_loop);

      Label done_element(this);
      // a. Let Pk be ToString(k).
      Node* p_k = ToString(context, k.value());

      // b. Let kPresent be HasProperty(O, Pk).
      // c. ReturnIfAbrupt(kPresent).
      Node* k_present = HasProperty(o, p_k, context);

      // d. If kPresent is true, then
      GotoIf(WordNotEqual(k_present, TrueConstant()), &done_element);

      // i. Let kValue be Get(O, Pk).
      // ii. ReturnIfAbrupt(kValue).
      Node* k_value = GetProperty(context, o, k.value());

      // iii. Let funcResult be Call(callbackfn, T, «kValue, k, O»).
      // iv. ReturnIfAbrupt(funcResult).
      Node* result = CallJS(CodeFactory::Call(isolate()), context, callbackfn,
                            this_arg, k_value, k.value(), o);

      processor(a, p_k, result);
      Goto(&done_element);
      Bind(&done_element);

      // e. Increase k by 1.
      k.Bind(NumberInc(k.value()));
      Goto(&loop);
    }
    Bind(&after_loop);
    Return(a);
  }

  void ForEachProcessor(Node* a, Node* p_k, Node* value) {}

  void SomeProcessor(Node* a, Node* p_k, Node* value) {
    Label false_continue(this), return_true(this);
    BranchIfToBooleanIsTrue(value, &return_true, &false_continue);
    Bind(&return_true);
    Return(TrueConstant());
    Bind(&false_continue);
  }

  void EveryProcessor(Node* a, Node* p_k, Node* value) {
    Label true_continue(this), return_false(this);
    BranchIfToBooleanIsTrue(value, &true_continue, &return_false);
    Bind(&return_false);
    Return(FalseConstant());
    Bind(&true_continue);
  }

 private:
  Node* VisitAllFastElementsOneKind(Node* context, ElementsKind kind,
                                    Node* this_arg, Node* o, Node* len,
                                    Node* callbackfn,
                                    const CallResultProcessor& processor,
                                    Node* a, Label* array_changed,
                                    ParameterMode mode) {
    Comment("begin VisitAllFastElementsOneKind");
    Variable original_map(this, MachineRepresentation::kTagged);
    original_map.Bind(LoadMap(o));
    VariableList list({&original_map}, zone());
    Node* last_index = nullptr;
    BuildFastLoop(
        list, IntPtrOrSmiConstant(0, mode), TaggedToParameter(len, mode),
        [=, &original_map, &last_index](Node* index) {
          last_index = index;
          Label one_element_done(this), hole_element(this);

          // Check if o's map has changed during the callback. If so, we have to
          // fall back to the slower spec implementation for the rest of the
          // iteration.
          Node* o_map = LoadMap(o);
          GotoIf(WordNotEqual(o_map, original_map.value()), array_changed);

          // Check if o's length has changed during the callback and if the
          // index is now out of range of the new length.
          Node* tagged_index = ParameterToTagged(index, mode);
          GotoIf(SmiGreaterThanOrEqual(tagged_index, LoadJSArrayLength(o)),
                 array_changed);

          // Re-load the elements array. If may have been resized.
          Node* elements = LoadElements(o);

          // Fast case: load the element directly from the elements FixedArray
          // and call the callback if the element is not the hole.
          DCHECK(kind == FAST_ELEMENTS || kind == FAST_DOUBLE_ELEMENTS);
          int base_size = kind == FAST_ELEMENTS
                              ? FixedArray::kHeaderSize
                              : (FixedArray::kHeaderSize - kHeapObjectTag);
          Node* offset = ElementOffsetFromIndex(index, kind, mode, base_size);
          Node* value = nullptr;
          if (kind == FAST_ELEMENTS) {
            value = LoadObjectField(elements, offset);
            GotoIf(WordEqual(value, TheHoleConstant()), &hole_element);
          } else {
            Node* double_value =
                LoadDoubleWithHoleCheck(elements, offset, &hole_element);
            value = AllocateHeapNumberWithValue(double_value);
          }
          Node* result = CallJS(CodeFactory::Call(isolate()), context,
                                callbackfn, this_arg, value, tagged_index, o);
          processor(a, tagged_index, result);
          Goto(&one_element_done);

          Bind(&hole_element);
          // Check if o's prototype change unexpectedly has elements after the
          // callback in the case of a hole.
          BranchIfPrototypesHaveNoElements(o_map, &one_element_done,
                                           array_changed);

          Bind(&one_element_done);
        },
        1, mode, IndexAdvanceMode::kPost);
    Comment("end VisitAllFastElementsOneKind");
    return last_index;
  }

  void HandleFastElements(Node* context, Node* this_arg, Node* o, Node* len,
                          Node* callbackfn, CallResultProcessor processor,
                          Node* a, Variable& k, Label* slow) {
    Label switch_on_elements_kind(this), fast_elements(this),
        maybe_double_elements(this), fast_double_elements(this);

    Comment("begin HandleFastElements");
    // Non-smi lengths must use the slow path.
    GotoIf(TaggedIsNotSmi(len), slow);

    BranchIfFastJSArray(o, context,
                        CodeStubAssembler::FastJSArrayAccessMode::INBOUNDS_READ,
                        &switch_on_elements_kind, slow);

    Bind(&switch_on_elements_kind);
    // Select by ElementsKind
    Node* o_map = LoadMap(o);
    Node* bit_field2 = LoadMapBitField2(o_map);
    Node* kind = DecodeWord32<Map::ElementsKindBits>(bit_field2);
    Branch(Int32GreaterThan(kind, Int32Constant(FAST_HOLEY_ELEMENTS)),
           &maybe_double_elements, &fast_elements);

    ParameterMode mode = OptimalParameterMode();
    Bind(&fast_elements);
    {
      Label array_changed(this, Label::kDeferred);
      Node* last_index = VisitAllFastElementsOneKind(
          context, FAST_ELEMENTS, this_arg, o, len, callbackfn, processor, a,
          &array_changed, mode);

      // No exception, return success
      Return(a);

      Bind(&array_changed);
      k.Bind(ParameterToTagged(last_index, mode));
      Goto(slow);
    }

    Bind(&maybe_double_elements);
    Branch(Int32GreaterThan(kind, Int32Constant(FAST_HOLEY_DOUBLE_ELEMENTS)),
           slow, &fast_double_elements);

    Bind(&fast_double_elements);
    {
      Label array_changed(this, Label::kDeferred);
      Node* last_index = VisitAllFastElementsOneKind(
          context, FAST_DOUBLE_ELEMENTS, this_arg, o, len, callbackfn,
          processor, a, &array_changed, mode);

      // No exception, return success
      Return(a);

      Bind(&array_changed);
      k.Bind(ParameterToTagged(last_index, mode));
      Goto(slow);
    }
  }
};

TF_BUILTIN(FastArrayPush, CodeStubAssembler) {
  Variable arg_index(this, MachineType::PointerRepresentation());
  Label default_label(this, &arg_index);
  Label smi_transition(this);
  Label object_push_pre(this);
  Label object_push(this, &arg_index);
  Label double_push(this, &arg_index);
  Label double_transition(this);
  Label runtime(this, Label::kDeferred);

  Node* argc = Parameter(BuiltinDescriptor::kArgumentsCount);
  Node* context = Parameter(BuiltinDescriptor::kContext);
  Node* new_target = Parameter(BuiltinDescriptor::kNewTarget);

  CodeStubArguments args(this, ChangeInt32ToIntPtr(argc));
  Node* receiver = args.GetReceiver();
  Node* kind = nullptr;

  Label fast(this);
  BranchIfFastJSArray(receiver, context, FastJSArrayAccessMode::ANY_ACCESS,
                      &fast, &runtime);

  Bind(&fast);
  {
    // Disallow pushing onto prototypes. It might be the JSArray prototype.
    // Disallow pushing onto non-extensible objects.
    Comment("Disallow pushing onto prototypes");
    Node* map = LoadMap(receiver);
    Node* bit_field2 = LoadMapBitField2(map);
    int mask = static_cast<int>(Map::IsPrototypeMapBits::kMask) |
               (1 << Map::kIsExtensible);
    Node* test = Word32And(bit_field2, Int32Constant(mask));
    GotoIf(Word32NotEqual(test, Int32Constant(1 << Map::kIsExtensible)),
           &runtime);

    // Disallow pushing onto arrays in dictionary named property mode. We need
    // to figure out whether the length property is still writable.
    Comment("Disallow pushing onto arrays in dictionary named property mode");
    GotoIf(IsDictionaryMap(map), &runtime);

    // Check whether the length property is writable. The length property is the
    // only default named property on arrays. It's nonconfigurable, hence is
    // guaranteed to stay the first property.
    Node* descriptors = LoadMapDescriptors(map);
    Node* details =
        LoadFixedArrayElement(descriptors, DescriptorArray::ToDetailsIndex(0));
    GotoIf(IsSetSmi(details, PropertyDetails::kAttributesReadOnlyMask),
           &runtime);

    arg_index.Bind(IntPtrConstant(0));
    kind = DecodeWord32<Map::ElementsKindBits>(bit_field2);

    GotoIf(Int32GreaterThan(kind, Int32Constant(FAST_HOLEY_SMI_ELEMENTS)),
           &object_push_pre);

    Node* new_length = BuildAppendJSArray(FAST_SMI_ELEMENTS, context, receiver,
                                          args, arg_index, &smi_transition);
    args.PopAndReturn(new_length);
  }

  // If the argument is not a smi, then use a heavyweight SetProperty to
  // transition the array for only the single next element. If the argument is
  // a smi, the failure is due to some other reason and we should fall back on
  // the most generic implementation for the rest of the array.
  Bind(&smi_transition);
  {
    Node* arg = args.AtIndex(arg_index.value());
    GotoIf(TaggedIsSmi(arg), &default_label);
    Node* length = LoadJSArrayLength(receiver);
    // TODO(danno): Use the KeyedStoreGeneric stub here when possible,
    // calling into the runtime to do the elements transition is overkill.
    CallRuntime(Runtime::kSetProperty, context, receiver, length, arg,
                SmiConstant(STRICT));
    Increment(arg_index);
    // The runtime SetProperty call could have converted the array to dictionary
    // mode, which must be detected to abort the fast-path.
    Node* map = LoadMap(receiver);
    Node* bit_field2 = LoadMapBitField2(map);
    Node* kind = DecodeWord32<Map::ElementsKindBits>(bit_field2);
    GotoIf(Word32Equal(kind, Int32Constant(DICTIONARY_ELEMENTS)),
           &default_label);

    GotoIfNotNumber(arg, &object_push);
    Goto(&double_push);
  }

  Bind(&object_push_pre);
  {
    Branch(Int32GreaterThan(kind, Int32Constant(FAST_HOLEY_ELEMENTS)),
           &double_push, &object_push);
  }

  Bind(&object_push);
  {
    Node* new_length = BuildAppendJSArray(FAST_ELEMENTS, context, receiver,
                                          args, arg_index, &default_label);
    args.PopAndReturn(new_length);
  }

  Bind(&double_push);
  {
    Node* new_length =
        BuildAppendJSArray(FAST_DOUBLE_ELEMENTS, context, receiver, args,
                           arg_index, &double_transition);
    args.PopAndReturn(new_length);
  }

  // If the argument is not a double, then use a heavyweight SetProperty to
  // transition the array for only the single next element. If the argument is
  // a double, the failure is due to some other reason and we should fall back
  // on the most generic implementation for the rest of the array.
  Bind(&double_transition);
  {
    Node* arg = args.AtIndex(arg_index.value());
    GotoIfNumber(arg, &default_label);
    Node* length = LoadJSArrayLength(receiver);
    // TODO(danno): Use the KeyedStoreGeneric stub here when possible,
    // calling into the runtime to do the elements transition is overkill.
    CallRuntime(Runtime::kSetProperty, context, receiver, length, arg,
                SmiConstant(STRICT));
    Increment(arg_index);
    // The runtime SetProperty call could have converted the array to dictionary
    // mode, which must be detected to abort the fast-path.
    Node* map = LoadMap(receiver);
    Node* bit_field2 = LoadMapBitField2(map);
    Node* kind = DecodeWord32<Map::ElementsKindBits>(bit_field2);
    GotoIf(Word32Equal(kind, Int32Constant(DICTIONARY_ELEMENTS)),
           &default_label);
    Goto(&object_push);
  }

  // Fallback that stores un-processed arguments using the full, heavyweight
  // SetProperty machinery.
  Bind(&default_label);
  {
    args.ForEach(
        [this, receiver, context](Node* arg) {
          Node* length = LoadJSArrayLength(receiver);
          CallRuntime(Runtime::kSetProperty, context, receiver, length, arg,
                      SmiConstant(STRICT));
        },
        arg_index.value());
    args.PopAndReturn(LoadJSArrayLength(receiver));
  }

  Bind(&runtime);
  {
    Node* target = LoadFromFrame(StandardFrameConstants::kFunctionOffset,
                                 MachineType::TaggedPointer());
    TailCallStub(CodeFactory::ArrayPush(isolate()), context, target, new_target,
                 argc);
  }
}

TF_BUILTIN(ArrayForEachLoopContinuation, ArrayBuiltinCodeStubAssembler) {
  GenerateIteratingArrayBuiltinLoopContinuation(
      [this](Node* a, Node* p_k, Node* value) {
        ForEachProcessor(a, p_k, value);
      });
}

TF_BUILTIN(ArrayForEach, ArrayBuiltinCodeStubAssembler) {
  GenerateIteratingArrayBuiltinBody(
      "Array.prototype.forEach",
      [=](Node*, Node*) { return UndefinedConstant(); },
      [this](Node* a, Node* p_k, Node* value) {
        ForEachProcessor(a, p_k, value);
      },
      CodeFactory::ArrayForEachLoopContinuation(isolate()));
}

TF_BUILTIN(ArraySomeLoopContinuation, ArrayBuiltinCodeStubAssembler) {
  GenerateIteratingArrayBuiltinLoopContinuation(
      [this](Node* a, Node* p_k, Node* value) {
        SomeProcessor(a, p_k, value);
      });
}

TF_BUILTIN(ArraySome, ArrayBuiltinCodeStubAssembler) {
  GenerateIteratingArrayBuiltinBody(
      "Array.prototype.some", [=](Node*, Node*) { return FalseConstant(); },
      [this](Node* a, Node* p_k, Node* value) { SomeProcessor(a, p_k, value); },
      CodeFactory::ArraySomeLoopContinuation(isolate()));
}

TF_BUILTIN(ArrayEveryLoopContinuation, ArrayBuiltinCodeStubAssembler) {
  GenerateIteratingArrayBuiltinLoopContinuation(
      [this](Node* a, Node* p_k, Node* value) {
        EveryProcessor(a, p_k, value);
      });
}

TF_BUILTIN(ArrayEvery, ArrayBuiltinCodeStubAssembler) {
  GenerateIteratingArrayBuiltinBody(
      "Array.prototype.every", [=](Node*, Node*) { return TrueConstant(); },
      [this](Node* a, Node* p_k, Node* value) {
        EveryProcessor(a, p_k, value);
      },
      CodeFactory::ArrayEveryLoopContinuation(isolate()));
}

TF_BUILTIN(ArrayIsArray, CodeStubAssembler) {
  Node* object = Parameter(1);
  Node* context = Parameter(4);

  Label call_runtime(this), return_true(this), return_false(this);

  GotoIf(TaggedIsSmi(object), &return_false);
  Node* instance_type = LoadInstanceType(object);

  GotoIf(Word32Equal(instance_type, Int32Constant(JS_ARRAY_TYPE)),
         &return_true);

  // TODO(verwaest): Handle proxies in-place.
  Branch(Word32Equal(instance_type, Int32Constant(JS_PROXY_TYPE)),
         &call_runtime, &return_false);

  Bind(&return_true);
  Return(BooleanConstant(true));

  Bind(&return_false);
  Return(BooleanConstant(false));

  Bind(&call_runtime);
  Return(CallRuntime(Runtime::kArrayIsArray, context, object));
}

TF_BUILTIN(ArrayIncludes, CodeStubAssembler) {
  Node* const array = Parameter(0);
  Node* const search_element = Parameter(1);
  Node* const start_from = Parameter(2);
  Node* const context = Parameter(3 + 2);

  Variable index_var(this, MachineType::PointerRepresentation());

  Label init_k(this), return_true(this), return_false(this), call_runtime(this);
  Label init_len(this), select_loop(this);

  index_var.Bind(IntPtrConstant(0));

  // Take slow path if not a JSArray, if retrieving elements requires
  // traversing prototype, or if access checks are required.
  BranchIfFastJSArray(array, context, FastJSArrayAccessMode::INBOUNDS_READ,
                      &init_len, &call_runtime);

  Bind(&init_len);
  // JSArray length is always an Smi for fast arrays.
  CSA_ASSERT(this, TaggedIsSmi(LoadObjectField(array, JSArray::kLengthOffset)));
  Node* const len = LoadAndUntagObjectField(array, JSArray::kLengthOffset);

  GotoIf(IsUndefined(start_from), &select_loop);

  // Bailout to slow path if startIndex is not an Smi.
  Branch(TaggedIsSmi(start_from), &init_k, &call_runtime);

  Bind(&init_k);
  CSA_ASSERT(this, TaggedIsSmi(start_from));
  Node* const untagged_start_from = SmiToWord(start_from);
  index_var.Bind(
      Select(IntPtrGreaterThanOrEqual(untagged_start_from, IntPtrConstant(0)),
             [=]() { return untagged_start_from; },
             [=]() {
               Node* const index = IntPtrAdd(len, untagged_start_from);
               return SelectConstant(IntPtrLessThan(index, IntPtrConstant(0)),
                                     IntPtrConstant(0), index,
                                     MachineType::PointerRepresentation());
             },
             MachineType::PointerRepresentation()));

  Goto(&select_loop);
  Bind(&select_loop);
  static int32_t kElementsKind[] = {
      FAST_SMI_ELEMENTS,   FAST_HOLEY_SMI_ELEMENTS, FAST_ELEMENTS,
      FAST_HOLEY_ELEMENTS, FAST_DOUBLE_ELEMENTS,    FAST_HOLEY_DOUBLE_ELEMENTS,
  };

  Label if_smiorobjects(this), if_packed_doubles(this), if_holey_doubles(this);
  Label* element_kind_handlers[] = {&if_smiorobjects,   &if_smiorobjects,
                                    &if_smiorobjects,   &if_smiorobjects,
                                    &if_packed_doubles, &if_holey_doubles};

  Node* map = LoadMap(array);
  Node* elements_kind = LoadMapElementsKind(map);
  Node* elements = LoadElements(array);
  Switch(elements_kind, &return_false, kElementsKind, element_kind_handlers,
         arraysize(kElementsKind));

  Bind(&if_smiorobjects);
  {
    Variable search_num(this, MachineRepresentation::kFloat64);
    Label ident_loop(this, &index_var), heap_num_loop(this, &search_num),
        string_loop(this, &index_var), undef_loop(this, &index_var),
        not_smi(this), not_heap_num(this);

    GotoIfNot(TaggedIsSmi(search_element), &not_smi);
    search_num.Bind(SmiToFloat64(search_element));
    Goto(&heap_num_loop);

    Bind(&not_smi);
    GotoIf(WordEqual(search_element, UndefinedConstant()), &undef_loop);
    Node* map = LoadMap(search_element);
    GotoIfNot(IsHeapNumberMap(map), &not_heap_num);
    search_num.Bind(LoadHeapNumberValue(search_element));
    Goto(&heap_num_loop);

    Bind(&not_heap_num);
    Node* search_type = LoadMapInstanceType(map);
    GotoIf(IsStringInstanceType(search_type), &string_loop);
    Goto(&ident_loop);

    Bind(&ident_loop);
    {
      GotoIfNot(UintPtrLessThan(index_var.value(), len), &return_false);
      Node* element_k = LoadFixedArrayElement(elements, index_var.value());
      GotoIf(WordEqual(element_k, search_element), &return_true);

      index_var.Bind(IntPtrAdd(index_var.value(), IntPtrConstant(1)));
      Goto(&ident_loop);
    }

    Bind(&undef_loop);
    {
      GotoIfNot(UintPtrLessThan(index_var.value(), len), &return_false);
      Node* element_k = LoadFixedArrayElement(elements, index_var.value());
      GotoIf(WordEqual(element_k, UndefinedConstant()), &return_true);
      GotoIf(WordEqual(element_k, TheHoleConstant()), &return_true);

      index_var.Bind(IntPtrAdd(index_var.value(), IntPtrConstant(1)));
      Goto(&undef_loop);
    }

    Bind(&heap_num_loop);
    {
      Label nan_loop(this, &index_var), not_nan_loop(this, &index_var);
      BranchIfFloat64IsNaN(search_num.value(), &nan_loop, &not_nan_loop);

      Bind(&not_nan_loop);
      {
        Label continue_loop(this), not_smi(this);
        GotoIfNot(UintPtrLessThan(index_var.value(), len), &return_false);
        Node* element_k = LoadFixedArrayElement(elements, index_var.value());
        GotoIfNot(TaggedIsSmi(element_k), &not_smi);
        Branch(Float64Equal(search_num.value(), SmiToFloat64(element_k)),
               &return_true, &continue_loop);

        Bind(&not_smi);
        GotoIfNot(IsHeapNumber(element_k), &continue_loop);
        Branch(Float64Equal(search_num.value(), LoadHeapNumberValue(element_k)),
               &return_true, &continue_loop);

        Bind(&continue_loop);
        index_var.Bind(IntPtrAdd(index_var.value(), IntPtrConstant(1)));
        Goto(&not_nan_loop);
      }

      Bind(&nan_loop);
      {
        Label continue_loop(this);
        GotoIfNot(UintPtrLessThan(index_var.value(), len), &return_false);
        Node* element_k = LoadFixedArrayElement(elements, index_var.value());
        GotoIf(TaggedIsSmi(element_k), &continue_loop);
        GotoIfNot(IsHeapNumber(element_k), &continue_loop);
        BranchIfFloat64IsNaN(LoadHeapNumberValue(element_k), &return_true,
                             &continue_loop);

        Bind(&continue_loop);
        index_var.Bind(IntPtrAdd(index_var.value(), IntPtrConstant(1)));
        Goto(&nan_loop);
      }
    }

    Bind(&string_loop);
    {
      Label continue_loop(this);
      GotoIfNot(UintPtrLessThan(index_var.value(), len), &return_false);
      Node* element_k = LoadFixedArrayElement(elements, index_var.value());
      GotoIf(TaggedIsSmi(element_k), &continue_loop);
      GotoIfNot(IsStringInstanceType(LoadInstanceType(element_k)),
                &continue_loop);

      // TODO(bmeurer): Consider inlining the StringEqual logic here.
      Node* result = CallStub(CodeFactory::StringEqual(isolate()), context,
                              search_element, element_k);
      Branch(WordEqual(BooleanConstant(true), result), &return_true,
             &continue_loop);

      Bind(&continue_loop);
      index_var.Bind(IntPtrAdd(index_var.value(), IntPtrConstant(1)));
      Goto(&string_loop);
    }
  }

  Bind(&if_packed_doubles);
  {
    Label nan_loop(this, &index_var), not_nan_loop(this, &index_var),
        hole_loop(this, &index_var), search_notnan(this);
    Variable search_num(this, MachineRepresentation::kFloat64);

    GotoIfNot(TaggedIsSmi(search_element), &search_notnan);
    search_num.Bind(SmiToFloat64(search_element));
    Goto(&not_nan_loop);

    Bind(&search_notnan);
    GotoIfNot(IsHeapNumber(search_element), &return_false);

    search_num.Bind(LoadHeapNumberValue(search_element));

    BranchIfFloat64IsNaN(search_num.value(), &nan_loop, &not_nan_loop);

    // Search for HeapNumber
    Bind(&not_nan_loop);
    {
      Label continue_loop(this);
      GotoIfNot(UintPtrLessThan(index_var.value(), len), &return_false);
      Node* element_k = LoadFixedDoubleArrayElement(elements, index_var.value(),
                                                    MachineType::Float64());
      Branch(Float64Equal(element_k, search_num.value()), &return_true,
             &continue_loop);
      Bind(&continue_loop);
      index_var.Bind(IntPtrAdd(index_var.value(), IntPtrConstant(1)));
      Goto(&not_nan_loop);
    }

    // Search for NaN
    Bind(&nan_loop);
    {
      Label continue_loop(this);
      GotoIfNot(UintPtrLessThan(index_var.value(), len), &return_false);
      Node* element_k = LoadFixedDoubleArrayElement(elements, index_var.value(),
                                                    MachineType::Float64());
      BranchIfFloat64IsNaN(element_k, &return_true, &continue_loop);
      Bind(&continue_loop);
      index_var.Bind(IntPtrAdd(index_var.value(), IntPtrConstant(1)));
      Goto(&nan_loop);
    }
  }

  Bind(&if_holey_doubles);
  {
    Label nan_loop(this, &index_var), not_nan_loop(this, &index_var),
        hole_loop(this, &index_var), search_notnan(this);
    Variable search_num(this, MachineRepresentation::kFloat64);

    GotoIfNot(TaggedIsSmi(search_element), &search_notnan);
    search_num.Bind(SmiToFloat64(search_element));
    Goto(&not_nan_loop);

    Bind(&search_notnan);
    GotoIf(WordEqual(search_element, UndefinedConstant()), &hole_loop);
    GotoIfNot(IsHeapNumber(search_element), &return_false);

    search_num.Bind(LoadHeapNumberValue(search_element));

    BranchIfFloat64IsNaN(search_num.value(), &nan_loop, &not_nan_loop);

    // Search for HeapNumber
    Bind(&not_nan_loop);
    {
      Label continue_loop(this);
      GotoIfNot(UintPtrLessThan(index_var.value(), len), &return_false);

      // Load double value or continue if it contains a double hole.
      Node* element_k = LoadFixedDoubleArrayElement(
          elements, index_var.value(), MachineType::Float64(), 0,
          INTPTR_PARAMETERS, &continue_loop);

      Branch(Float64Equal(element_k, search_num.value()), &return_true,
             &continue_loop);
      Bind(&continue_loop);
      index_var.Bind(IntPtrAdd(index_var.value(), IntPtrConstant(1)));
      Goto(&not_nan_loop);
    }

    // Search for NaN
    Bind(&nan_loop);
    {
      Label continue_loop(this);
      GotoIfNot(UintPtrLessThan(index_var.value(), len), &return_false);

      // Load double value or continue if it contains a double hole.
      Node* element_k = LoadFixedDoubleArrayElement(
          elements, index_var.value(), MachineType::Float64(), 0,
          INTPTR_PARAMETERS, &continue_loop);

      BranchIfFloat64IsNaN(element_k, &return_true, &continue_loop);
      Bind(&continue_loop);
      index_var.Bind(IntPtrAdd(index_var.value(), IntPtrConstant(1)));
      Goto(&nan_loop);
    }

    // Search for the Hole
    Bind(&hole_loop);
    {
      GotoIfNot(UintPtrLessThan(index_var.value(), len), &return_false);

      // Check if the element is a double hole, but don't load it.
      LoadFixedDoubleArrayElement(elements, index_var.value(),
                                  MachineType::None(), 0, INTPTR_PARAMETERS,
                                  &return_true);

      index_var.Bind(IntPtrAdd(index_var.value(), IntPtrConstant(1)));
      Goto(&hole_loop);
    }
  }

  Bind(&return_true);
  Return(TrueConstant());

  Bind(&return_false);
  Return(FalseConstant());

  Bind(&call_runtime);
  Return(CallRuntime(Runtime::kArrayIncludes_Slow, context, array,
                     search_element, start_from));
}

TF_BUILTIN(ArrayIndexOf, CodeStubAssembler) {
  Node* array = Parameter(0);
  Node* search_element = Parameter(1);
  Node* start_from = Parameter(2);
  Node* context = Parameter(3 + 2);

  Node* intptr_zero = IntPtrConstant(0);
  Node* intptr_one = IntPtrConstant(1);

  Variable len_var(this, MachineType::PointerRepresentation()),
      index_var(this, MachineType::PointerRepresentation()),
      start_from_var(this, MachineType::PointerRepresentation());

  Label init_k(this), return_found(this), return_not_found(this),
      call_runtime(this);

  Label init_len(this);

  index_var.Bind(intptr_zero);
  len_var.Bind(intptr_zero);

  // Take slow path if not a JSArray, if retrieving elements requires
  // traversing prototype, or if access checks are required.
  BranchIfFastJSArray(array, context, FastJSArrayAccessMode::INBOUNDS_READ,
                      &init_len, &call_runtime);

  Bind(&init_len);
  {
    // JSArray length is always an Smi for fast arrays.
    CSA_ASSERT(this,
               TaggedIsSmi(LoadObjectField(array, JSArray::kLengthOffset)));
    Node* len = LoadAndUntagObjectField(array, JSArray::kLengthOffset);

    len_var.Bind(len);
    Branch(WordEqual(len_var.value(), intptr_zero), &return_not_found, &init_k);
  }

  Bind(&init_k);
  {
    // For now only deal with undefined and Smis here; we must be really careful
    // with side-effects from the ToInteger conversion as the side-effects might
    // render our assumptions about the receiver being a fast JSArray and the
    // length invalid.
    Label done(this), init_k_smi(this), init_k_other(this), init_k_zero(this),
        init_k_n(this);
    Branch(TaggedIsSmi(start_from), &init_k_smi, &init_k_other);

    Bind(&init_k_smi);
    {
      // The fromIndex is a Smi.
      start_from_var.Bind(SmiUntag(start_from));
      Goto(&init_k_n);
    }

    Bind(&init_k_other);
    {
      // The fromIndex must be undefined then, otherwise bailout and let the
      // runtime deal with the full ToInteger conversion.
      GotoIfNot(IsUndefined(start_from), &call_runtime);
      start_from_var.Bind(intptr_zero);
      Goto(&init_k_n);
    }

    Bind(&init_k_n);
    {
      Label if_positive(this), if_negative(this), done(this);
      Branch(IntPtrLessThan(start_from_var.value(), intptr_zero), &if_negative,
             &if_positive);

      Bind(&if_positive);
      {
        index_var.Bind(start_from_var.value());
        Goto(&done);
      }

      Bind(&if_negative);
      {
        index_var.Bind(IntPtrAdd(len_var.value(), start_from_var.value()));
        Branch(IntPtrLessThan(index_var.value(), intptr_zero), &init_k_zero,
               &done);
      }

      Bind(&init_k_zero);
      {
        index_var.Bind(intptr_zero);
        Goto(&done);
      }

      Bind(&done);
    }
  }

  static int32_t kElementsKind[] = {
      FAST_SMI_ELEMENTS,   FAST_HOLEY_SMI_ELEMENTS, FAST_ELEMENTS,
      FAST_HOLEY_ELEMENTS, FAST_DOUBLE_ELEMENTS,    FAST_HOLEY_DOUBLE_ELEMENTS,
  };

  Label if_smiorobjects(this), if_packed_doubles(this), if_holey_doubles(this);
  Label* element_kind_handlers[] = {&if_smiorobjects,   &if_smiorobjects,
                                    &if_smiorobjects,   &if_smiorobjects,
                                    &if_packed_doubles, &if_holey_doubles};

  Node* map = LoadMap(array);
  Node* elements_kind = LoadMapElementsKind(map);
  Node* elements = LoadElements(array);
  Switch(elements_kind, &return_not_found, kElementsKind, element_kind_handlers,
         arraysize(kElementsKind));

  Bind(&if_smiorobjects);
  {
    Variable search_num(this, MachineRepresentation::kFloat64);
    Label ident_loop(this, &index_var), heap_num_loop(this, &search_num),
        string_loop(this, &index_var), not_smi(this), not_heap_num(this);

    GotoIfNot(TaggedIsSmi(search_element), &not_smi);
    search_num.Bind(SmiToFloat64(search_element));
    Goto(&heap_num_loop);

    Bind(&not_smi);
    Node* map = LoadMap(search_element);
    GotoIfNot(IsHeapNumberMap(map), &not_heap_num);
    search_num.Bind(LoadHeapNumberValue(search_element));
    Goto(&heap_num_loop);

    Bind(&not_heap_num);
    Node* search_type = LoadMapInstanceType(map);
    GotoIf(IsStringInstanceType(search_type), &string_loop);
    Goto(&ident_loop);

    Bind(&ident_loop);
    {
      GotoIfNot(UintPtrLessThan(index_var.value(), len_var.value()),
                &return_not_found);
      Node* element_k = LoadFixedArrayElement(elements, index_var.value());
      GotoIf(WordEqual(element_k, search_element), &return_found);

      index_var.Bind(IntPtrAdd(index_var.value(), intptr_one));
      Goto(&ident_loop);
    }

    Bind(&heap_num_loop);
    {
      Label not_nan_loop(this, &index_var);
      BranchIfFloat64IsNaN(search_num.value(), &return_not_found,
                           &not_nan_loop);

      Bind(&not_nan_loop);
      {
        Label continue_loop(this), not_smi(this);
        GotoIfNot(UintPtrLessThan(index_var.value(), len_var.value()),
                  &return_not_found);
        Node* element_k = LoadFixedArrayElement(elements, index_var.value());
        GotoIfNot(TaggedIsSmi(element_k), &not_smi);
        Branch(Float64Equal(search_num.value(), SmiToFloat64(element_k)),
               &return_found, &continue_loop);

        Bind(&not_smi);
        GotoIfNot(IsHeapNumber(element_k), &continue_loop);
        Branch(Float64Equal(search_num.value(), LoadHeapNumberValue(element_k)),
               &return_found, &continue_loop);

        Bind(&continue_loop);
        index_var.Bind(IntPtrAdd(index_var.value(), intptr_one));
        Goto(&not_nan_loop);
      }
    }

    Bind(&string_loop);
    {
      Label continue_loop(this);
      GotoIfNot(UintPtrLessThan(index_var.value(), len_var.value()),
                &return_not_found);
      Node* element_k = LoadFixedArrayElement(elements, index_var.value());
      GotoIf(TaggedIsSmi(element_k), &continue_loop);
      GotoIfNot(IsString(element_k), &continue_loop);

      // TODO(bmeurer): Consider inlining the StringEqual logic here.
      Callable callable = CodeFactory::StringEqual(isolate());
      Node* result = CallStub(callable, context, search_element, element_k);
      Branch(WordEqual(BooleanConstant(true), result), &return_found,
             &continue_loop);

      Bind(&continue_loop);
      index_var.Bind(IntPtrAdd(index_var.value(), intptr_one));
      Goto(&string_loop);
    }
  }

  Bind(&if_packed_doubles);
  {
    Label not_nan_loop(this, &index_var), search_notnan(this);
    Variable search_num(this, MachineRepresentation::kFloat64);

    GotoIfNot(TaggedIsSmi(search_element), &search_notnan);
    search_num.Bind(SmiToFloat64(search_element));
    Goto(&not_nan_loop);

    Bind(&search_notnan);
    GotoIfNot(IsHeapNumber(search_element), &return_not_found);

    search_num.Bind(LoadHeapNumberValue(search_element));

    BranchIfFloat64IsNaN(search_num.value(), &return_not_found, &not_nan_loop);

    // Search for HeapNumber
    Bind(&not_nan_loop);
    {
      GotoIfNot(UintPtrLessThan(index_var.value(), len_var.value()),
                &return_not_found);
      Node* element_k = LoadFixedDoubleArrayElement(elements, index_var.value(),
                                                    MachineType::Float64());
      GotoIf(Float64Equal(element_k, search_num.value()), &return_found);

      index_var.Bind(IntPtrAdd(index_var.value(), intptr_one));
      Goto(&not_nan_loop);
    }
  }

  Bind(&if_holey_doubles);
  {
    Label not_nan_loop(this, &index_var), search_notnan(this);
    Variable search_num(this, MachineRepresentation::kFloat64);

    GotoIfNot(TaggedIsSmi(search_element), &search_notnan);
    search_num.Bind(SmiToFloat64(search_element));
    Goto(&not_nan_loop);

    Bind(&search_notnan);
    GotoIfNot(IsHeapNumber(search_element), &return_not_found);

    search_num.Bind(LoadHeapNumberValue(search_element));

    BranchIfFloat64IsNaN(search_num.value(), &return_not_found, &not_nan_loop);

    // Search for HeapNumber
    Bind(&not_nan_loop);
    {
      Label continue_loop(this);
      GotoIfNot(UintPtrLessThan(index_var.value(), len_var.value()),
                &return_not_found);

      // Load double value or continue if it contains a double hole.
      Node* element_k = LoadFixedDoubleArrayElement(
          elements, index_var.value(), MachineType::Float64(), 0,
          INTPTR_PARAMETERS, &continue_loop);

      Branch(Float64Equal(element_k, search_num.value()), &return_found,
             &continue_loop);
      Bind(&continue_loop);
      index_var.Bind(IntPtrAdd(index_var.value(), intptr_one));
      Goto(&not_nan_loop);
    }
  }

  Bind(&return_found);
  Return(SmiTag(index_var.value()));

  Bind(&return_not_found);
  Return(NumberConstant(-1));

  Bind(&call_runtime);
  Return(CallRuntime(Runtime::kArrayIndexOf, context, array, search_element,
                     start_from));
}

class ArrayPrototypeIterationAssembler : public CodeStubAssembler {
 public:
  explicit ArrayPrototypeIterationAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  void Generate_ArrayPrototypeIterationMethod(IterationKind iteration_kind) {
    Node* receiver = Parameter(0);
    Node* context = Parameter(3);

    Variable var_array(this, MachineRepresentation::kTagged);
    Variable var_map(this, MachineRepresentation::kTagged);
    Variable var_type(this, MachineRepresentation::kWord32);

    Label if_isnotobject(this, Label::kDeferred);
    Label create_array_iterator(this);

    GotoIf(TaggedIsSmi(receiver), &if_isnotobject);
    var_array.Bind(receiver);
    var_map.Bind(LoadMap(receiver));
    var_type.Bind(LoadMapInstanceType(var_map.value()));
    Branch(IsJSReceiverInstanceType(var_type.value()), &create_array_iterator,
           &if_isnotobject);

    Bind(&if_isnotobject);
    {
      Callable callable = CodeFactory::ToObject(isolate());
      Node* result = CallStub(callable, context, receiver);
      var_array.Bind(result);
      var_map.Bind(LoadMap(result));
      var_type.Bind(LoadMapInstanceType(var_map.value()));
      Goto(&create_array_iterator);
    }

    Bind(&create_array_iterator);
    Return(CreateArrayIterator(var_array.value(), var_map.value(),
                               var_type.value(), context, iteration_kind));
  }
};

TF_BUILTIN(ArrayPrototypeValues, ArrayPrototypeIterationAssembler) {
  Generate_ArrayPrototypeIterationMethod(IterationKind::kValues);
}

TF_BUILTIN(ArrayPrototypeEntries, ArrayPrototypeIterationAssembler) {
  Generate_ArrayPrototypeIterationMethod(IterationKind::kEntries);
}

TF_BUILTIN(ArrayPrototypeKeys, ArrayPrototypeIterationAssembler) {
  Generate_ArrayPrototypeIterationMethod(IterationKind::kKeys);
}

TF_BUILTIN(ArrayIteratorPrototypeNext, CodeStubAssembler) {
  Handle<String> operation = factory()->NewStringFromAsciiChecked(
      "Array Iterator.prototype.next", TENURED);

  Node* iterator = Parameter(0);
  Node* context = Parameter(3);

  Variable var_value(this, MachineRepresentation::kTagged);
  Variable var_done(this, MachineRepresentation::kTagged);

  // Required, or else `throw_bad_receiver` fails a DCHECK due to these
  // variables not being bound along all paths, despite not being used.
  var_done.Bind(TrueConstant());
  var_value.Bind(UndefinedConstant());

  Label throw_bad_receiver(this, Label::kDeferred);
  Label set_done(this);
  Label allocate_key_result(this);
  Label allocate_entry_if_needed(this);
  Label allocate_iterator_result(this);
  Label generic_values(this);

  // If O does not have all of the internal slots of an Array Iterator Instance
  // (22.1.5.3), throw a TypeError exception
  GotoIf(TaggedIsSmi(iterator), &throw_bad_receiver);
  Node* instance_type = LoadInstanceType(iterator);
  GotoIf(
      Uint32LessThan(
          Int32Constant(LAST_ARRAY_ITERATOR_TYPE - FIRST_ARRAY_ITERATOR_TYPE),
          Int32Sub(instance_type, Int32Constant(FIRST_ARRAY_ITERATOR_TYPE))),
      &throw_bad_receiver);

  // Let a be O.[[IteratedObject]].
  Node* array =
      LoadObjectField(iterator, JSArrayIterator::kIteratedObjectOffset);

  // Let index be O.[[ArrayIteratorNextIndex]].
  Node* index = LoadObjectField(iterator, JSArrayIterator::kNextIndexOffset);
  Node* orig_map =
      LoadObjectField(iterator, JSArrayIterator::kIteratedObjectMapOffset);
  Node* array_map = LoadMap(array);

  Label if_isfastarray(this), if_isnotfastarray(this),
      if_isdetached(this, Label::kDeferred);

  Branch(WordEqual(orig_map, array_map), &if_isfastarray, &if_isnotfastarray);

  Bind(&if_isfastarray);
  {
    CSA_ASSERT(this, Word32Equal(LoadMapInstanceType(array_map),
                                 Int32Constant(JS_ARRAY_TYPE)));

    Node* length = LoadObjectField(array, JSArray::kLengthOffset);

    CSA_ASSERT(this, TaggedIsSmi(length));
    CSA_ASSERT(this, TaggedIsSmi(index));

    GotoIfNot(SmiBelow(index, length), &set_done);

    Node* one = SmiConstant(Smi::FromInt(1));
    StoreObjectFieldNoWriteBarrier(iterator, JSArrayIterator::kNextIndexOffset,
                                   SmiAdd(index, one));

    var_done.Bind(FalseConstant());
    Node* elements = LoadElements(array);

    static int32_t kInstanceType[] = {
        JS_FAST_ARRAY_KEY_ITERATOR_TYPE,
        JS_FAST_SMI_ARRAY_KEY_VALUE_ITERATOR_TYPE,
        JS_FAST_HOLEY_SMI_ARRAY_KEY_VALUE_ITERATOR_TYPE,
        JS_FAST_ARRAY_KEY_VALUE_ITERATOR_TYPE,
        JS_FAST_HOLEY_ARRAY_KEY_VALUE_ITERATOR_TYPE,
        JS_FAST_DOUBLE_ARRAY_KEY_VALUE_ITERATOR_TYPE,
        JS_FAST_HOLEY_DOUBLE_ARRAY_KEY_VALUE_ITERATOR_TYPE,
        JS_FAST_SMI_ARRAY_VALUE_ITERATOR_TYPE,
        JS_FAST_HOLEY_SMI_ARRAY_VALUE_ITERATOR_TYPE,
        JS_FAST_ARRAY_VALUE_ITERATOR_TYPE,
        JS_FAST_HOLEY_ARRAY_VALUE_ITERATOR_TYPE,
        JS_FAST_DOUBLE_ARRAY_VALUE_ITERATOR_TYPE,
        JS_FAST_HOLEY_DOUBLE_ARRAY_VALUE_ITERATOR_TYPE,
    };

    Label packed_object_values(this), holey_object_values(this),
        packed_double_values(this), holey_double_values(this);
    Label* kInstanceTypeHandlers[] = {
        &allocate_key_result,  &packed_object_values, &holey_object_values,
        &packed_object_values, &holey_object_values,  &packed_double_values,
        &holey_double_values,  &packed_object_values, &holey_object_values,
        &packed_object_values, &holey_object_values,  &packed_double_values,
        &holey_double_values};

    Switch(instance_type, &throw_bad_receiver, kInstanceType,
           kInstanceTypeHandlers, arraysize(kInstanceType));

    Bind(&packed_object_values);
    {
      var_value.Bind(LoadFixedArrayElement(elements, index, 0, SMI_PARAMETERS));
      Goto(&allocate_entry_if_needed);
    }

    Bind(&packed_double_values);
    {
      Node* value = LoadFixedDoubleArrayElement(
          elements, index, MachineType::Float64(), 0, SMI_PARAMETERS);
      var_value.Bind(AllocateHeapNumberWithValue(value));
      Goto(&allocate_entry_if_needed);
    }

    Bind(&holey_object_values);
    {
      // Check the array_protector cell, and take the slow path if it's invalid.
      Node* invalid = SmiConstant(Smi::FromInt(Isolate::kProtectorInvalid));
      Node* cell = LoadRoot(Heap::kArrayProtectorRootIndex);
      Node* cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
      GotoIf(WordEqual(cell_value, invalid), &generic_values);

      var_value.Bind(UndefinedConstant());
      Node* value = LoadFixedArrayElement(elements, index, 0, SMI_PARAMETERS);
      GotoIf(WordEqual(value, TheHoleConstant()), &allocate_entry_if_needed);
      var_value.Bind(value);
      Goto(&allocate_entry_if_needed);
    }

    Bind(&holey_double_values);
    {
      // Check the array_protector cell, and take the slow path if it's invalid.
      Node* invalid = SmiConstant(Smi::FromInt(Isolate::kProtectorInvalid));
      Node* cell = LoadRoot(Heap::kArrayProtectorRootIndex);
      Node* cell_value = LoadObjectField(cell, PropertyCell::kValueOffset);
      GotoIf(WordEqual(cell_value, invalid), &generic_values);

      var_value.Bind(UndefinedConstant());
      Node* value = LoadFixedDoubleArrayElement(
          elements, index, MachineType::Float64(), 0, SMI_PARAMETERS,
          &allocate_entry_if_needed);
      var_value.Bind(AllocateHeapNumberWithValue(value));
      Goto(&allocate_entry_if_needed);
    }
  }

  Bind(&if_isnotfastarray);
  {
    Label if_istypedarray(this), if_isgeneric(this);

    // If a is undefined, return CreateIterResultObject(undefined, true)
    GotoIf(WordEqual(array, UndefinedConstant()), &allocate_iterator_result);

    Node* array_type = LoadInstanceType(array);
    Branch(Word32Equal(array_type, Int32Constant(JS_TYPED_ARRAY_TYPE)),
           &if_istypedarray, &if_isgeneric);

    Bind(&if_isgeneric);
    {
      Label if_wasfastarray(this);

      Node* length = nullptr;
      {
        Variable var_length(this, MachineRepresentation::kTagged);
        Label if_isarray(this), if_isnotarray(this), done(this);
        Branch(Word32Equal(array_type, Int32Constant(JS_ARRAY_TYPE)),
               &if_isarray, &if_isnotarray);

        Bind(&if_isarray);
        {
          var_length.Bind(LoadObjectField(array, JSArray::kLengthOffset));

          // Invalidate protector cell if needed
          Branch(WordNotEqual(orig_map, UndefinedConstant()), &if_wasfastarray,
                 &done);

          Bind(&if_wasfastarray);
          {
            Label if_invalid(this, Label::kDeferred);
            // A fast array iterator transitioned to a slow iterator during
            // iteration. Invalidate fast_array_iteration_prtoector cell to
            // prevent potential deopt loops.
            StoreObjectFieldNoWriteBarrier(
                iterator, JSArrayIterator::kIteratedObjectMapOffset,
                UndefinedConstant());
            GotoIf(Uint32LessThanOrEqual(
                       instance_type,
                       Int32Constant(JS_GENERIC_ARRAY_KEY_ITERATOR_TYPE)),
                   &done);

            Node* invalid =
                SmiConstant(Smi::FromInt(Isolate::kProtectorInvalid));
            Node* cell = LoadRoot(Heap::kFastArrayIterationProtectorRootIndex);
            StoreObjectFieldNoWriteBarrier(cell, Cell::kValueOffset, invalid);
            Goto(&done);
          }
        }

        Bind(&if_isnotarray);
        {
          Node* length =
              GetProperty(context, array, factory()->length_string());
          Callable to_length = CodeFactory::ToLength(isolate());
          var_length.Bind(CallStub(to_length, context, length));
          Goto(&done);
        }

        Bind(&done);
        length = var_length.value();
      }

      GotoUnlessNumberLessThan(index, length, &set_done);

      StoreObjectField(iterator, JSArrayIterator::kNextIndexOffset,
                       NumberInc(index));
      var_done.Bind(FalseConstant());

      Branch(
          Uint32LessThanOrEqual(
              instance_type, Int32Constant(JS_GENERIC_ARRAY_KEY_ITERATOR_TYPE)),
          &allocate_key_result, &generic_values);

      Bind(&generic_values);
      {
        var_value.Bind(GetProperty(context, array, index));
        Goto(&allocate_entry_if_needed);
      }
    }

    Bind(&if_istypedarray);
    {
      Node* buffer = LoadObjectField(array, JSTypedArray::kBufferOffset);
      GotoIf(IsDetachedBuffer(buffer), &if_isdetached);

      Node* length = LoadObjectField(array, JSTypedArray::kLengthOffset);

      CSA_ASSERT(this, TaggedIsSmi(length));
      CSA_ASSERT(this, TaggedIsSmi(index));

      GotoIfNot(SmiBelow(index, length), &set_done);

      Node* one = SmiConstant(1);
      StoreObjectFieldNoWriteBarrier(
          iterator, JSArrayIterator::kNextIndexOffset, SmiAdd(index, one));
      var_done.Bind(FalseConstant());

      Node* elements = LoadElements(array);
      Node* base_ptr =
          LoadObjectField(elements, FixedTypedArrayBase::kBasePointerOffset);
      Node* external_ptr =
          LoadObjectField(elements, FixedTypedArrayBase::kExternalPointerOffset,
                          MachineType::Pointer());
      Node* data_ptr = IntPtrAdd(BitcastTaggedToWord(base_ptr), external_ptr);

      static int32_t kInstanceType[] = {
          JS_TYPED_ARRAY_KEY_ITERATOR_TYPE,
          JS_UINT8_ARRAY_KEY_VALUE_ITERATOR_TYPE,
          JS_UINT8_CLAMPED_ARRAY_KEY_VALUE_ITERATOR_TYPE,
          JS_INT8_ARRAY_KEY_VALUE_ITERATOR_TYPE,
          JS_UINT16_ARRAY_KEY_VALUE_ITERATOR_TYPE,
          JS_INT16_ARRAY_KEY_VALUE_ITERATOR_TYPE,
          JS_UINT32_ARRAY_KEY_VALUE_ITERATOR_TYPE,
          JS_INT32_ARRAY_KEY_VALUE_ITERATOR_TYPE,
          JS_FLOAT32_ARRAY_KEY_VALUE_ITERATOR_TYPE,
          JS_FLOAT64_ARRAY_KEY_VALUE_ITERATOR_TYPE,
          JS_UINT8_ARRAY_VALUE_ITERATOR_TYPE,
          JS_UINT8_CLAMPED_ARRAY_VALUE_ITERATOR_TYPE,
          JS_INT8_ARRAY_VALUE_ITERATOR_TYPE,
          JS_UINT16_ARRAY_VALUE_ITERATOR_TYPE,
          JS_INT16_ARRAY_VALUE_ITERATOR_TYPE,
          JS_UINT32_ARRAY_VALUE_ITERATOR_TYPE,
          JS_INT32_ARRAY_VALUE_ITERATOR_TYPE,
          JS_FLOAT32_ARRAY_VALUE_ITERATOR_TYPE,
          JS_FLOAT64_ARRAY_VALUE_ITERATOR_TYPE,
      };

      Label uint8_values(this), int8_values(this), uint16_values(this),
          int16_values(this), uint32_values(this), int32_values(this),
          float32_values(this), float64_values(this);
      Label* kInstanceTypeHandlers[] = {
          &allocate_key_result, &uint8_values,  &uint8_values,
          &int8_values,         &uint16_values, &int16_values,
          &uint32_values,       &int32_values,  &float32_values,
          &float64_values,      &uint8_values,  &uint8_values,
          &int8_values,         &uint16_values, &int16_values,
          &uint32_values,       &int32_values,  &float32_values,
          &float64_values,
      };

      var_done.Bind(FalseConstant());
      Switch(instance_type, &throw_bad_receiver, kInstanceType,
             kInstanceTypeHandlers, arraysize(kInstanceType));

      Bind(&uint8_values);
      {
        Node* value_uint8 = LoadFixedTypedArrayElement(
            data_ptr, index, UINT8_ELEMENTS, SMI_PARAMETERS);
        var_value.Bind(SmiFromWord32(value_uint8));
        Goto(&allocate_entry_if_needed);
      }
      Bind(&int8_values);
      {
        Node* value_int8 = LoadFixedTypedArrayElement(
            data_ptr, index, INT8_ELEMENTS, SMI_PARAMETERS);
        var_value.Bind(SmiFromWord32(value_int8));
        Goto(&allocate_entry_if_needed);
      }
      Bind(&uint16_values);
      {
        Node* value_uint16 = LoadFixedTypedArrayElement(
            data_ptr, index, UINT16_ELEMENTS, SMI_PARAMETERS);
        var_value.Bind(SmiFromWord32(value_uint16));
        Goto(&allocate_entry_if_needed);
      }
      Bind(&int16_values);
      {
        Node* value_int16 = LoadFixedTypedArrayElement(
            data_ptr, index, INT16_ELEMENTS, SMI_PARAMETERS);
        var_value.Bind(SmiFromWord32(value_int16));
        Goto(&allocate_entry_if_needed);
      }
      Bind(&uint32_values);
      {
        Node* value_uint32 = LoadFixedTypedArrayElement(
            data_ptr, index, UINT32_ELEMENTS, SMI_PARAMETERS);
        var_value.Bind(ChangeUint32ToTagged(value_uint32));
        Goto(&allocate_entry_if_needed);
      }
      Bind(&int32_values);
      {
        Node* value_int32 = LoadFixedTypedArrayElement(
            data_ptr, index, INT32_ELEMENTS, SMI_PARAMETERS);
        var_value.Bind(ChangeInt32ToTagged(value_int32));
        Goto(&allocate_entry_if_needed);
      }
      Bind(&float32_values);
      {
        Node* value_float32 = LoadFixedTypedArrayElement(
            data_ptr, index, FLOAT32_ELEMENTS, SMI_PARAMETERS);
        var_value.Bind(
            AllocateHeapNumberWithValue(ChangeFloat32ToFloat64(value_float32)));
        Goto(&allocate_entry_if_needed);
      }
      Bind(&float64_values);
      {
        Node* value_float64 = LoadFixedTypedArrayElement(
            data_ptr, index, FLOAT64_ELEMENTS, SMI_PARAMETERS);
        var_value.Bind(AllocateHeapNumberWithValue(value_float64));
        Goto(&allocate_entry_if_needed);
      }
    }
  }

  Bind(&set_done);
  {
    StoreObjectFieldNoWriteBarrier(
        iterator, JSArrayIterator::kIteratedObjectOffset, UndefinedConstant());
    Goto(&allocate_iterator_result);
  }

  Bind(&allocate_key_result);
  {
    var_value.Bind(index);
    var_done.Bind(FalseConstant());
    Goto(&allocate_iterator_result);
  }

  Bind(&allocate_entry_if_needed);
  {
    GotoIf(Int32GreaterThan(instance_type,
                            Int32Constant(LAST_ARRAY_KEY_VALUE_ITERATOR_TYPE)),
           &allocate_iterator_result);

    Node* elements = AllocateFixedArray(FAST_ELEMENTS, IntPtrConstant(2));
    StoreFixedArrayElement(elements, 0, index, SKIP_WRITE_BARRIER);
    StoreFixedArrayElement(elements, 1, var_value.value(), SKIP_WRITE_BARRIER);

    Node* entry = Allocate(JSArray::kSize);
    Node* map = LoadContextElement(LoadNativeContext(context),
                                   Context::JS_ARRAY_FAST_ELEMENTS_MAP_INDEX);

    StoreMapNoWriteBarrier(entry, map);
    StoreObjectFieldRoot(entry, JSArray::kPropertiesOffset,
                         Heap::kEmptyFixedArrayRootIndex);
    StoreObjectFieldNoWriteBarrier(entry, JSArray::kElementsOffset, elements);
    StoreObjectFieldNoWriteBarrier(entry, JSArray::kLengthOffset,
                                   SmiConstant(Smi::FromInt(2)));

    var_value.Bind(entry);
    Goto(&allocate_iterator_result);
  }

  Bind(&allocate_iterator_result);
  {
    Node* result = Allocate(JSIteratorResult::kSize);
    Node* map = LoadContextElement(LoadNativeContext(context),
                                   Context::ITERATOR_RESULT_MAP_INDEX);
    StoreMapNoWriteBarrier(result, map);
    StoreObjectFieldRoot(result, JSIteratorResult::kPropertiesOffset,
                         Heap::kEmptyFixedArrayRootIndex);
    StoreObjectFieldRoot(result, JSIteratorResult::kElementsOffset,
                         Heap::kEmptyFixedArrayRootIndex);
    StoreObjectFieldNoWriteBarrier(result, JSIteratorResult::kValueOffset,
                                   var_value.value());
    StoreObjectFieldNoWriteBarrier(result, JSIteratorResult::kDoneOffset,
                                   var_done.value());
    Return(result);
  }

  Bind(&throw_bad_receiver);
  {
    // The {receiver} is not a valid JSArrayIterator.
    CallRuntime(Runtime::kThrowIncompatibleMethodReceiver, context,
                HeapConstant(operation), iterator);
    Unreachable();
  }

  Bind(&if_isdetached);
  {
    Node* message = SmiConstant(MessageTemplate::kDetachedOperation);
    CallRuntime(Runtime::kThrowTypeError, context, message,
                HeapConstant(operation));
    Unreachable();
  }
}

}  // namespace internal
}  // namespace v8
