// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ic/unary-op-assembler.h"

#include "src/common/globals.h"

namespace v8 {
namespace internal {

// Unary op helper classes.
namespace {

class UnaryNumericOpAssembler : public CodeStubAssembler {
 public:
  explicit UnaryNumericOpAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  virtual TNode<Number> SmiOp(TNode<Smi> smi_value,
                              TVariable<Smi>* var_feedback, Label* do_float_op,
                              TVariable<Float64T>* var_float) = 0;
  virtual TNode<Float64T> FloatOp(TNode<Float64T> float_value) = 0;
  virtual TNode<HeapObject> BigIntOp(TNode<Context> context,
                                     TNode<HeapObject> bigint_value) = 0;

  TNode<Object> UnaryOpWithFeedback(TNode<Context> context, TNode<Object> value,
                                    TNode<UintPtrT> slot,
                                    TNode<HeapObject> maybe_feedback_vector) {
    TVARIABLE(Object, var_value, value);
    TVARIABLE(Object, var_result);
    TVARIABLE(Float64T, var_float_value);
    TVARIABLE(Smi, var_feedback, SmiConstant(BinaryOperationFeedback::kNone));
    Label start(this, {&var_value, &var_feedback}), end(this);
    Label do_float_op(this, &var_float_value);
    Goto(&start);
    // We might have to try again after ToNumeric conversion.
    BIND(&start);
    {
      Label if_smi(this), if_heapnumber(this), if_oddball(this);
      Label if_bigint(this, Label::kDeferred);
      Label if_other(this, Label::kDeferred);
      TNode<Object> value = var_value.value();
      GotoIf(TaggedIsSmi(value), &if_smi);

      TNode<HeapObject> value_heap_object = CAST(value);
      TNode<Map> map = LoadMap(value_heap_object);
      GotoIf(IsHeapNumberMap(map), &if_heapnumber);
      TNode<Uint16T> instance_type = LoadMapInstanceType(map);
      GotoIf(IsBigIntInstanceType(instance_type), &if_bigint);
      Branch(InstanceTypeEqual(instance_type, ODDBALL_TYPE), &if_oddball,
             &if_other);

      BIND(&if_smi);
      {
        var_result =
            SmiOp(CAST(value), &var_feedback, &do_float_op, &var_float_value);
        Goto(&end);
      }

      BIND(&if_heapnumber);
      {
        var_float_value = LoadHeapNumberValue(value_heap_object);
        Goto(&do_float_op);
      }

      BIND(&if_bigint);
      {
        var_result = BigIntOp(context, value_heap_object);
        CombineFeedback(&var_feedback, BinaryOperationFeedback::kBigInt);
        Goto(&end);
      }

      BIND(&if_oddball);
      {
        // We do not require an Or with earlier feedback here because once we
        // convert the value to a number, we cannot reach this path. We can
        // only reach this path on the first pass when the feedback is kNone.
        CSA_ASSERT(this, SmiEqual(var_feedback.value(),
                                  SmiConstant(BinaryOperationFeedback::kNone)));
        OverwriteFeedback(&var_feedback,
                          BinaryOperationFeedback::kNumberOrOddball);
        var_value =
            LoadObjectField(value_heap_object, Oddball::kToNumberOffset);
        Goto(&start);
      }

      BIND(&if_other);
      {
        // We do not require an Or with earlier feedback here because once we
        // convert the value to a number, we cannot reach this path. We can
        // only reach this path on the first pass when the feedback is kNone.
        CSA_ASSERT(this, SmiEqual(var_feedback.value(),
                                  SmiConstant(BinaryOperationFeedback::kNone)));
        OverwriteFeedback(&var_feedback, BinaryOperationFeedback::kAny);
        var_value = CallBuiltin(Builtins::kNonNumberToNumeric, context,
                                value_heap_object);
        Goto(&start);
      }
    }

    BIND(&do_float_op);
    {
      CombineFeedback(&var_feedback, BinaryOperationFeedback::kNumber);
      var_result =
          AllocateHeapNumberWithValue(FloatOp(var_float_value.value()));
      Goto(&end);
    }

    BIND(&end);
    UpdateFeedback(var_feedback.value(), maybe_feedback_vector, slot);
    return var_result.value();
  }
};

class NegateAssembler : public UnaryNumericOpAssembler {
 public:
  explicit NegateAssembler(compiler::CodeAssemblerState* state)
      : UnaryNumericOpAssembler(state) {}

  using UnaryNumericOpAssembler::UnaryOpWithFeedback;

 private:
  TNode<Number> SmiOp(TNode<Smi> smi_value, TVariable<Smi>* var_feedback,
                      Label* do_float_op,
                      TVariable<Float64T>* var_float) override {
    TVARIABLE(Number, var_result);
    Label if_zero(this), if_min_smi(this), end(this);
    // Return -0 if operand is 0.
    GotoIf(SmiEqual(smi_value, SmiConstant(0)), &if_zero);

    // Special-case the minimum Smi to avoid overflow.
    GotoIf(SmiEqual(smi_value, SmiConstant(Smi::kMinValue)), &if_min_smi);

    // Else simply subtract operand from 0.
    CombineFeedback(var_feedback, BinaryOperationFeedback::kSignedSmall);
    var_result = SmiSub(SmiConstant(0), smi_value);
    Goto(&end);

    BIND(&if_zero);
    CombineFeedback(var_feedback, BinaryOperationFeedback::kNumber);
    var_result = MinusZeroConstant();
    Goto(&end);

    BIND(&if_min_smi);
    *var_float = SmiToFloat64(smi_value);
    Goto(do_float_op);

    BIND(&end);
    return var_result.value();
  }

  TNode<Float64T> FloatOp(TNode<Float64T> float_value) override {
    return Float64Neg(float_value);
  }

  TNode<HeapObject> BigIntOp(TNode<Context> context,
                             TNode<HeapObject> bigint_value) override {
    return CAST(CallRuntime(Runtime::kBigIntUnaryOp, context, bigint_value,
                            SmiConstant(Operation::kNegate)));
  }
};

template <int kAddValue, Operation kOperation>
class IncDecAssembler : public UnaryNumericOpAssembler {
 public:
  explicit IncDecAssembler(compiler::CodeAssemblerState* state)
      : UnaryNumericOpAssembler(state) {}

  using UnaryNumericOpAssembler::UnaryOpWithFeedback;

 private:
  TNode<Number> SmiOp(TNode<Smi> value, TVariable<Smi>* var_feedback,
                      Label* do_float_op,
                      TVariable<Float64T>* var_float) override {
    Label if_overflow(this), out(this);
    TNode<Smi> result = TrySmiAdd(value, SmiConstant(kAddValue), &if_overflow);
    CombineFeedback(var_feedback, BinaryOperationFeedback::kSignedSmall);
    Goto(&out);

    BIND(&if_overflow);
    *var_float = SmiToFloat64(value);
    Goto(do_float_op);

    BIND(&out);
    return result;
  }
  TNode<Float64T> FloatOp(TNode<Float64T> float_value) override {
    return Float64Add(float_value, Float64Constant(kAddValue));
  }
  TNode<HeapObject> BigIntOp(TNode<Context> context,
                             TNode<HeapObject> bigint_value) override {
    return CAST(CallRuntime(Runtime::kBigIntUnaryOp, context, bigint_value,
                            SmiConstant(kOperation)));
  }
};

using IncAssembler = IncDecAssembler<1, Operation::kIncrement>;
using DecAssembler = IncDecAssembler<-1, Operation::kDecrement>;

class BitwiseNotAssembler : public CodeStubAssembler {
 public:
  explicit BitwiseNotAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  TNode<Object> BitwiseNotWithFeedback(
      TNode<Context> context, TNode<Object> value, TNode<UintPtrT> slot,
      TNode<HeapObject> maybe_feedback_vector) {
    TVARIABLE(Word32T, var_word32);
    TVARIABLE(Smi, var_feedback);
    TVARIABLE(BigInt, var_bigint);
    TVARIABLE(Object, var_result);
    Label if_number(this), if_bigint(this, Label::kDeferred), out(this);
    TaggedToWord32OrBigIntWithFeedback(context, value, &if_number, &var_word32,
                                       &if_bigint, &var_bigint, &var_feedback);

    // Number case.
    BIND(&if_number);
    var_result =
        ChangeInt32ToTagged(Signed(Word32BitwiseNot(var_word32.value())));
    TNode<Smi> result_type = SelectSmiConstant(
        TaggedIsSmi(var_result.value()), BinaryOperationFeedback::kSignedSmall,
        BinaryOperationFeedback::kNumber);
    UpdateFeedback(SmiOr(result_type, var_feedback.value()),
                   maybe_feedback_vector, slot);
    Goto(&out);

    // BigInt case.
    BIND(&if_bigint);
    UpdateFeedback(SmiConstant(BinaryOperationFeedback::kBigInt),
                   maybe_feedback_vector, slot);
    var_result =
        CallRuntime(Runtime::kBigIntUnaryOp, context, var_bigint.value(),
                    SmiConstant(Operation::kBitwiseNot));
    Goto(&out);

    BIND(&out);
    return var_result.value();
  }
};

}  // namespace

TNode<Object> UnaryOpAssembler::Generate_BitwiseNotWithFeedback(
    TNode<Context> context, TNode<Object> value, TNode<UintPtrT> slot,
    TNode<HeapObject> maybe_feedback_vector) {
  // TODO(jgruber): Make this implementation more consistent with other unary
  // ops (i.e. have them all use UnaryOpWithFeedback or some other mechanism).
  BitwiseNotAssembler a(state_);
  return a.BitwiseNotWithFeedback(context, value, slot, maybe_feedback_vector);
}

TNode<Object> UnaryOpAssembler::Generate_DecrementWithFeedback(
    TNode<Context> context, TNode<Object> value, TNode<UintPtrT> slot,
    TNode<HeapObject> maybe_feedback_vector) {
  DecAssembler a(state_);
  return a.UnaryOpWithFeedback(context, value, slot, maybe_feedback_vector);
}

TNode<Object> UnaryOpAssembler::Generate_IncrementWithFeedback(
    TNode<Context> context, TNode<Object> value, TNode<UintPtrT> slot,
    TNode<HeapObject> maybe_feedback_vector) {
  IncAssembler a(state_);
  return a.UnaryOpWithFeedback(context, value, slot, maybe_feedback_vector);
}

TNode<Object> UnaryOpAssembler::Generate_NegateWithFeedback(
    TNode<Context> context, TNode<Object> value, TNode<UintPtrT> slot,
    TNode<HeapObject> maybe_feedback_vector) {
  NegateAssembler a(state_);
  return a.UnaryOpWithFeedback(context, value, slot, maybe_feedback_vector);
}

}  // namespace internal
}  // namespace v8
