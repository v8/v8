// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler.h"
#include "src/ic/binary-op-assembler.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// ES6 section 20.1 Number Objects

class AddStubAssembler : public CodeStubAssembler {
 public:
  explicit AddStubAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  TNode<Object> ConvertReceiver(TNode<JSReceiver> js_receiver,
                                TNode<Context> context) {
    // Call ToPrimitive explicitly without hint (whereas ToNumber
    // would pass a "number" hint).
    Callable callable = CodeFactory::NonPrimitiveToPrimitive(isolate());
    return CallStub(callable, context, js_receiver);
  }

  void ConvertNonReceiverAndLoop(TVariable<Object>* var_value, Label* loop,
                                 TNode<Context> context) {
    *var_value =
        CallBuiltin(Builtins::kNonNumberToNumeric, context, var_value->value());
    Goto(loop);
  }

  void ConvertAndLoop(TVariable<Object>* var_value,
                      TNode<Uint16T> instance_type, Label* loop,
                      TNode<Context> context) {
    Label is_not_receiver(this, Label::kDeferred);
    GotoIfNot(IsJSReceiverInstanceType(instance_type), &is_not_receiver);

    *var_value = ConvertReceiver(CAST(var_value->value()), context);
    Goto(loop);

    BIND(&is_not_receiver);
    ConvertNonReceiverAndLoop(var_value, loop, context);
  }
};

TF_BUILTIN(Add, AddStubAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  TVARIABLE(Object, var_left, CAST(Parameter(Descriptor::kLeft)));
  TVARIABLE(Object, var_right, CAST(Parameter(Descriptor::kRight)));

  // Shared entry for floating point addition.
  Label do_double_add(this);
  TVARIABLE(Float64T, var_left_double);
  TVARIABLE(Float64T, var_right_double);

  // We might need to loop several times due to ToPrimitive, ToString and/or
  // ToNumeric conversions.
  Label loop(this, {&var_left, &var_right}),
      string_add_convert_left(this, Label::kDeferred),
      string_add_convert_right(this, Label::kDeferred),
      do_bigint_add(this, Label::kDeferred);
  Goto(&loop);
  BIND(&loop);
  {
    TNode<Object> left = var_left.value();
    TNode<Object> right = var_right.value();

    Label if_left_smi(this), if_left_heapobject(this);
    Branch(TaggedIsSmi(left), &if_left_smi, &if_left_heapobject);

    BIND(&if_left_smi);
    {
      Label if_right_smi(this), if_right_heapobject(this);
      Branch(TaggedIsSmi(right), &if_right_smi, &if_right_heapobject);

      BIND(&if_right_smi);
      {
        Label if_overflow(this);
        TNode<Smi> left_smi = CAST(left);
        TNode<Smi> right_smi = CAST(right);
        TNode<Smi> result = TrySmiAdd(left_smi, right_smi, &if_overflow);
        Return(result);

        BIND(&if_overflow);
        {
          var_left_double = SmiToFloat64(left_smi);
          var_right_double = SmiToFloat64(right_smi);
          Goto(&do_double_add);
        }
      }  // if_right_smi

      BIND(&if_right_heapobject);
      {
        TNode<HeapObject> right_heap_object = CAST(right);
        TNode<Map> right_map = LoadMap(right_heap_object);

        Label if_right_not_number(this, Label::kDeferred);
        GotoIfNot(IsHeapNumberMap(right_map), &if_right_not_number);

        // {right} is a HeapNumber.
        var_left_double = SmiToFloat64(CAST(left));
        var_right_double = LoadHeapNumberValue(right_heap_object);
        Goto(&do_double_add);

        BIND(&if_right_not_number);
        {
          TNode<Uint16T> right_instance_type = LoadMapInstanceType(right_map);
          GotoIf(IsStringInstanceType(right_instance_type),
                 &string_add_convert_left);
          GotoIf(IsBigIntInstanceType(right_instance_type), &do_bigint_add);
          ConvertAndLoop(&var_right, right_instance_type, &loop, context);
        }
      }  // if_right_heapobject
    }    // if_left_smi

    BIND(&if_left_heapobject);
    {
      TNode<HeapObject> left_heap_object = CAST(left);
      TNode<Map> left_map = LoadMap(left_heap_object);
      Label if_right_smi(this), if_right_heapobject(this);
      Branch(TaggedIsSmi(right), &if_right_smi, &if_right_heapobject);

      BIND(&if_right_smi);
      {
        Label if_left_not_number(this, Label::kDeferred);
        GotoIfNot(IsHeapNumberMap(left_map), &if_left_not_number);

        // {left} is a HeapNumber, {right} is a Smi.
        var_left_double = LoadHeapNumberValue(left_heap_object);
        var_right_double = SmiToFloat64(CAST(right));
        Goto(&do_double_add);

        BIND(&if_left_not_number);
        {
          TNode<Uint16T> left_instance_type = LoadMapInstanceType(left_map);
          GotoIf(IsStringInstanceType(left_instance_type),
                 &string_add_convert_right);
          GotoIf(IsBigIntInstanceType(left_instance_type), &do_bigint_add);
          // {left} is neither a Numeric nor a String, and {right} is a Smi.
          ConvertAndLoop(&var_left, left_instance_type, &loop, context);
        }
      }  // if_right_smi

      BIND(&if_right_heapobject);
      {
        TNode<HeapObject> right_heap_object = CAST(right);
        TNode<Map> right_map = LoadMap(right_heap_object);

        Label if_left_number(this), if_left_not_number(this, Label::kDeferred);
        Branch(IsHeapNumberMap(left_map), &if_left_number, &if_left_not_number);

        BIND(&if_left_number);
        {
          Label if_right_not_number(this, Label::kDeferred);
          GotoIfNot(IsHeapNumberMap(right_map), &if_right_not_number);

          // Both {left} and {right} are HeapNumbers.
          var_left_double = LoadHeapNumberValue(CAST(left));
          var_right_double = LoadHeapNumberValue(right_heap_object);
          Goto(&do_double_add);

          BIND(&if_right_not_number);
          {
            TNode<Uint16T> right_instance_type = LoadMapInstanceType(right_map);
            GotoIf(IsStringInstanceType(right_instance_type),
                   &string_add_convert_left);
            GotoIf(IsBigIntInstanceType(right_instance_type), &do_bigint_add);
            // {left} is a HeapNumber, {right} is neither Number nor String.
            ConvertAndLoop(&var_right, right_instance_type, &loop, context);
          }
        }  // if_left_number

        BIND(&if_left_not_number);
        {
          Label if_left_bigint(this);
          TNode<Uint16T> left_instance_type = LoadMapInstanceType(left_map);
          GotoIf(IsStringInstanceType(left_instance_type),
                 &string_add_convert_right);
          TNode<Uint16T> right_instance_type = LoadMapInstanceType(right_map);
          GotoIf(IsStringInstanceType(right_instance_type),
                 &string_add_convert_left);
          GotoIf(IsBigIntInstanceType(left_instance_type), &if_left_bigint);
          Label if_left_not_receiver(this, Label::kDeferred);
          Label if_right_not_receiver(this, Label::kDeferred);
          GotoIfNot(IsJSReceiverInstanceType(left_instance_type),
                    &if_left_not_receiver);
          // {left} is a JSReceiver, convert it first.
          var_left = ConvertReceiver(CAST(var_left.value()), context);
          Goto(&loop);

          BIND(&if_left_bigint);
          {
            // {right} is a HeapObject, but not a String. Jump to
            // {do_bigint_add} if {right} is already a Numeric.
            GotoIf(IsBigIntInstanceType(right_instance_type), &do_bigint_add);
            GotoIf(IsHeapNumberMap(right_map), &do_bigint_add);
            ConvertAndLoop(&var_right, right_instance_type, &loop, context);
          }

          BIND(&if_left_not_receiver);
          GotoIfNot(IsJSReceiverInstanceType(right_instance_type),
                    &if_right_not_receiver);
          // {left} is a Primitive, but {right} is a JSReceiver, so convert
          // {right} with priority.
          var_right = ConvertReceiver(CAST(var_right.value()), context);
          Goto(&loop);

          BIND(&if_right_not_receiver);
          // Neither {left} nor {right} are JSReceivers.
          ConvertNonReceiverAndLoop(&var_left, &loop, context);
        }
      }  // if_right_heapobject
    }    // if_left_heapobject
  }
  BIND(&string_add_convert_left);
  {
    // Convert {left} to a String and concatenate it with the String {right}.
    TailCallBuiltin(Builtins::kStringAddConvertLeft, context, var_left.value(),
                    var_right.value());
  }

  BIND(&string_add_convert_right);
  {
    // Convert {right} to a String and concatenate it with the String {left}.
    TailCallBuiltin(Builtins::kStringAddConvertRight, context, var_left.value(),
                    var_right.value());
  }

  BIND(&do_bigint_add);
  {
    TailCallBuiltin(Builtins::kBigIntAdd, context, var_left.value(),
                    var_right.value());
  }

  BIND(&do_double_add);
  {
    TNode<Float64T> value =
        Float64Add(var_left_double.value(), var_right_double.value());
    Return(AllocateHeapNumberWithValue(value));
  }
}

}  // namespace internal
}  // namespace v8
