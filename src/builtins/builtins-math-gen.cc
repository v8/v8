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

TNode<Number> MathBuiltinsAssembler::MathPow(TNode<Context> context,
                                             TNode<Object> base,
                                             TNode<Object> exponent) {
  TNode<Float64T> base_value = TruncateTaggedToFloat64(context, base);
  TNode<Float64T> exponent_value = TruncateTaggedToFloat64(context, exponent);
  TNode<Float64T> value = Float64Pow(base_value, exponent_value);
  return ChangeFloat64ToTagged(value);
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

}  // namespace internal
}  // namespace v8
