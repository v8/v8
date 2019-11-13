// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-math-gen.h"

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/code-stub-assembler.h"
#include "src/objects/fixed-array.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// ES6 section 20.2.2 Function Properties of the Math Object

void MathBuiltinsAssembler::MathRoundingOperation(
    TNode<Context> context, TNode<Object> x,
    TNode<Float64T> (CodeStubAssembler::*float64op)(SloppyTNode<Float64T>)) {
  // We might need to loop once for ToNumber conversion.
  TVARIABLE(Object, var_x, x);
  Label loop(this, &var_x);
  Goto(&loop);
  BIND(&loop);
  {
    // Load the current {x} value.
    TNode<Object> x = var_x.value();

    // Check if {x} is a Smi or a HeapObject.
    Label if_xissmi(this), if_xisnotsmi(this);
    Branch(TaggedIsSmi(x), &if_xissmi, &if_xisnotsmi);

    BIND(&if_xissmi);
    {
      // Nothing to do when {x} is a Smi.
      Return(x);
    }

    BIND(&if_xisnotsmi);
    {
      // Check if {x} is a HeapNumber.
      Label if_xisheapnumber(this), if_xisnotheapnumber(this, Label::kDeferred);
      TNode<HeapObject> x_heap_object = CAST(x);
      Branch(IsHeapNumber(x_heap_object), &if_xisheapnumber,
             &if_xisnotheapnumber);

      BIND(&if_xisheapnumber);
      {
        TNode<Float64T> x_value = LoadHeapNumberValue(x_heap_object);
        TNode<Float64T> value = (this->*float64op)(x_value);
        TNode<Number> result = ChangeFloat64ToTagged(value);
        Return(result);
      }

      BIND(&if_xisnotheapnumber);
      {
        // Need to convert {x} to a Number first.
        var_x = CallBuiltin(Builtins::kNonNumberToNumber, context, x);
        Goto(&loop);
      }
    }
  }
}

void MathBuiltinsAssembler::MathMaxMin(
    TNode<Context> context, TNode<Int32T> argc,
    TNode<Float64T> (CodeStubAssembler::*float64op)(SloppyTNode<Float64T>,
                                                    SloppyTNode<Float64T>),
    double default_val) {
  CodeStubArguments arguments(this, argc);

  TVARIABLE(Float64T, result, Float64Constant(default_val));

  CodeStubAssembler::VariableList vars({&result}, zone());
  arguments.ForEach(vars, [&](TNode<Object> arg) {
    TNode<Float64T> float_value = TruncateTaggedToFloat64(context, arg);
    result = (this->*float64op)(result.value(), float_value);
  });

  arguments.PopAndReturn(ChangeFloat64ToTagged(result.value()));
}

// ES6 #sec-math.ceil
TF_BUILTIN(MathCeil, MathBuiltinsAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  TNode<Object> x = CAST(Parameter(Descriptor::kX));
  MathRoundingOperation(context, x, &CodeStubAssembler::Float64Ceil);
}

// ES6 #sec-math.floor
TF_BUILTIN(MathFloor, MathBuiltinsAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  TNode<Object> x = CAST(Parameter(Descriptor::kX));
  MathRoundingOperation(context, x, &CodeStubAssembler::Float64Floor);
}

TNode<Number> MathBuiltinsAssembler::MathPow(TNode<Context> context,
                                             TNode<Object> base,
                                             TNode<Object> exponent) {
  TNode<Float64T> base_value = TruncateTaggedToFloat64(context, base);
  TNode<Float64T> exponent_value = TruncateTaggedToFloat64(context, exponent);
  TNode<Float64T> value = Float64Pow(base_value, exponent_value);
  return ChangeFloat64ToTagged(value);
}

// ES6 #sec-math.pow
TF_BUILTIN(MathPow, MathBuiltinsAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  TNode<Object> base = CAST(Parameter(Descriptor::kBase));
  TNode<Object> exponent = CAST(Parameter(Descriptor::kExponent));
  Return(MathPow(context, base, exponent));
}

// ES6 #sec-math.random
TF_BUILTIN(MathRandom, CodeStubAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  TNode<NativeContext> native_context = LoadNativeContext(context);

  // Load cache index.
  TVARIABLE(Smi, smi_index);
  smi_index = CAST(
      LoadContextElement(native_context, Context::MATH_RANDOM_INDEX_INDEX));

  // Cached random numbers are exhausted if index is 0. Go to slow path.
  Label if_cached(this);
  GotoIf(SmiAbove(smi_index.value(), SmiConstant(0)), &if_cached);

  // Cache exhausted, populate the cache. Return value is the new index.
  const TNode<ExternalReference> refill_math_random =
      ExternalConstant(ExternalReference::refill_math_random());
  const TNode<ExternalReference> isolate_ptr =
      ExternalConstant(ExternalReference::isolate_address(isolate()));
  MachineType type_tagged = MachineType::AnyTagged();
  MachineType type_ptr = MachineType::Pointer();

  smi_index = CAST(CallCFunction(refill_math_random, type_tagged,
                                 std::make_pair(type_ptr, isolate_ptr),
                                 std::make_pair(type_tagged, native_context)));
  Goto(&if_cached);

  // Compute next index by decrement.
  BIND(&if_cached);
  TNode<Smi> new_smi_index = SmiSub(smi_index.value(), SmiConstant(1));
  StoreContextElement(native_context, Context::MATH_RANDOM_INDEX_INDEX,
                      new_smi_index);

  // Load and return next cached random number.
  TNode<FixedDoubleArray> array = CAST(
      LoadContextElement(native_context, Context::MATH_RANDOM_CACHE_INDEX));
  TNode<Float64T> random = LoadFixedDoubleArrayElement(
      array, new_smi_index, MachineType::Float64(), 0, SMI_PARAMETERS);
  Return(AllocateHeapNumberWithValue(random));
}

// ES6 #sec-math.round
TF_BUILTIN(MathRound, MathBuiltinsAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  TNode<Object> x = CAST(Parameter(Descriptor::kX));
  MathRoundingOperation(context, x, &CodeStubAssembler::Float64Round);
}

// ES6 #sec-math.trunc
TF_BUILTIN(MathTrunc, MathBuiltinsAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  TNode<Object> x = CAST(Parameter(Descriptor::kX));
  MathRoundingOperation(context, x, &CodeStubAssembler::Float64Trunc);
}

// ES6 #sec-math.max
TF_BUILTIN(MathMax, MathBuiltinsAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  TNode<Int32T> argc =
      UncheckedCast<Int32T>(Parameter(Descriptor::kJSActualArgumentsCount));
  MathMaxMin(context, argc, &CodeStubAssembler::Float64Max, -1.0 * V8_INFINITY);
}

// ES6 #sec-math.min
TF_BUILTIN(MathMin, MathBuiltinsAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  TNode<Int32T> argc =
      UncheckedCast<Int32T>(Parameter(Descriptor::kJSActualArgumentsCount));
  MathMaxMin(context, argc, &CodeStubAssembler::Float64Min, V8_INFINITY);
}

}  // namespace internal
}  // namespace v8
