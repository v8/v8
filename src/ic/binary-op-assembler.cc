// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ic/binary-op-assembler.h"

#include "src/globals.h"

namespace v8 {
namespace internal {

using compiler::Node;

Node* BinaryOpAssembler::Generate_AddWithFeedback(Node* context, Node* lhs,
                                                  Node* rhs, Node* slot_id,
                                                  Node* feedback_vector) {
  // Shared entry for floating point addition.
  Label do_fadd(this), if_lhsisnotnumber(this, Label::kDeferred),
      check_rhsisoddball(this, Label::kDeferred),
      call_with_oddball_feedback(this), call_with_any_feedback(this),
      call_add_stub(this), end(this);
  Variable var_fadd_lhs(this, MachineRepresentation::kFloat64),
      var_fadd_rhs(this, MachineRepresentation::kFloat64),
      var_type_feedback(this, MachineRepresentation::kTaggedSigned),
      var_result(this, MachineRepresentation::kTagged);

  // Check if the {lhs} is a Smi or a HeapObject.
  Label if_lhsissmi(this), if_lhsisnotsmi(this);
  Branch(TaggedIsSmi(lhs), &if_lhsissmi, &if_lhsisnotsmi);

  Bind(&if_lhsissmi);
  {
    // Check if the {rhs} is also a Smi.
    Label if_rhsissmi(this), if_rhsisnotsmi(this);
    Branch(TaggedIsSmi(rhs), &if_rhsissmi, &if_rhsisnotsmi);

    Bind(&if_rhsissmi);
    {
      // Try fast Smi addition first.
      Node* pair = IntPtrAddWithOverflow(BitcastTaggedToWord(lhs),
                                         BitcastTaggedToWord(rhs));
      Node* overflow = Projection(1, pair);

      // Check if the Smi additon overflowed.
      Label if_overflow(this), if_notoverflow(this);
      Branch(overflow, &if_overflow, &if_notoverflow);

      Bind(&if_overflow);
      {
        var_fadd_lhs.Bind(SmiToFloat64(lhs));
        var_fadd_rhs.Bind(SmiToFloat64(rhs));
        Goto(&do_fadd);
      }

      Bind(&if_notoverflow);
      {
        var_type_feedback.Bind(
            SmiConstant(BinaryOperationFeedback::kSignedSmall));
        var_result.Bind(BitcastWordToTaggedSigned(Projection(0, pair)));
        Goto(&end);
      }
    }

    Bind(&if_rhsisnotsmi);
    {
      // Load the map of {rhs}.
      Node* rhs_map = LoadMap(rhs);

      // Check if the {rhs} is a HeapNumber.
      GotoIfNot(IsHeapNumberMap(rhs_map), &check_rhsisoddball);

      var_fadd_lhs.Bind(SmiToFloat64(lhs));
      var_fadd_rhs.Bind(LoadHeapNumberValue(rhs));
      Goto(&do_fadd);
    }
  }

  Bind(&if_lhsisnotsmi);
  {
    // Load the map of {lhs}.
    Node* lhs_map = LoadMap(lhs);

    // Check if {lhs} is a HeapNumber.
    GotoIfNot(IsHeapNumberMap(lhs_map), &if_lhsisnotnumber);

    // Check if the {rhs} is Smi.
    Label if_rhsissmi(this), if_rhsisnotsmi(this);
    Branch(TaggedIsSmi(rhs), &if_rhsissmi, &if_rhsisnotsmi);

    Bind(&if_rhsissmi);
    {
      var_fadd_lhs.Bind(LoadHeapNumberValue(lhs));
      var_fadd_rhs.Bind(SmiToFloat64(rhs));
      Goto(&do_fadd);
    }

    Bind(&if_rhsisnotsmi);
    {
      // Load the map of {rhs}.
      Node* rhs_map = LoadMap(rhs);

      // Check if the {rhs} is a HeapNumber.
      GotoIfNot(IsHeapNumberMap(rhs_map), &check_rhsisoddball);

      var_fadd_lhs.Bind(LoadHeapNumberValue(lhs));
      var_fadd_rhs.Bind(LoadHeapNumberValue(rhs));
      Goto(&do_fadd);
    }
  }

  Bind(&do_fadd);
  {
    var_type_feedback.Bind(SmiConstant(BinaryOperationFeedback::kNumber));
    Node* value = Float64Add(var_fadd_lhs.value(), var_fadd_rhs.value());
    Node* result = AllocateHeapNumberWithValue(value);
    var_result.Bind(result);
    Goto(&end);
  }

  Bind(&if_lhsisnotnumber);
  {
    // No checks on rhs are done yet. We just know lhs is not a number or Smi.
    Label if_lhsisoddball(this), if_lhsisnotoddball(this);
    Node* lhs_instance_type = LoadInstanceType(lhs);
    Node* lhs_is_oddball =
        Word32Equal(lhs_instance_type, Int32Constant(ODDBALL_TYPE));
    Branch(lhs_is_oddball, &if_lhsisoddball, &if_lhsisnotoddball);

    Bind(&if_lhsisoddball);
    {
      GotoIf(TaggedIsSmi(rhs), &call_with_oddball_feedback);

      // Load the map of the {rhs}.
      Node* rhs_map = LoadMap(rhs);

      // Check if {rhs} is a HeapNumber.
      Branch(IsHeapNumberMap(rhs_map), &call_with_oddball_feedback,
             &check_rhsisoddball);
    }

    Bind(&if_lhsisnotoddball);
    {
      // Exit unless {lhs} is a string
      GotoIfNot(IsStringInstanceType(lhs_instance_type),
                &call_with_any_feedback);

      // Check if the {rhs} is a smi, and exit the string check early if it is.
      GotoIf(TaggedIsSmi(rhs), &call_with_any_feedback);

      Node* rhs_instance_type = LoadInstanceType(rhs);

      // Exit unless {rhs} is a string. Since {lhs} is a string we no longer
      // need an Oddball check.
      GotoIfNot(IsStringInstanceType(rhs_instance_type),
                &call_with_any_feedback);

      var_type_feedback.Bind(SmiConstant(BinaryOperationFeedback::kString));
      Callable callable =
          CodeFactory::StringAdd(isolate(), STRING_ADD_CHECK_NONE, NOT_TENURED);
      var_result.Bind(CallStub(callable, context, lhs, rhs));

      Goto(&end);
    }
  }

  Bind(&check_rhsisoddball);
  {
    // Check if rhs is an oddball. At this point we know lhs is either a
    // Smi or number or oddball and rhs is not a number or Smi.
    Node* rhs_instance_type = LoadInstanceType(rhs);
    Node* rhs_is_oddball =
        Word32Equal(rhs_instance_type, Int32Constant(ODDBALL_TYPE));
    Branch(rhs_is_oddball, &call_with_oddball_feedback,
           &call_with_any_feedback);
  }

  Bind(&call_with_oddball_feedback);
  {
    var_type_feedback.Bind(
        SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
    Goto(&call_add_stub);
  }

  Bind(&call_with_any_feedback);
  {
    var_type_feedback.Bind(SmiConstant(BinaryOperationFeedback::kAny));
    Goto(&call_add_stub);
  }

  Bind(&call_add_stub);
  {
    Callable callable = CodeFactory::Add(isolate());
    var_result.Bind(CallStub(callable, context, lhs, rhs));
    Goto(&end);
  }

  Bind(&end);
  UpdateFeedback(var_type_feedback.value(), feedback_vector, slot_id);
  return var_result.value();
}

Node* BinaryOpAssembler::Generate_SubtractWithFeedback(Node* context, Node* lhs,
                                                       Node* rhs, Node* slot_id,
                                                       Node* feedback_vector) {
  // Shared entry for floating point subtraction.
  Label do_fsub(this), end(this), call_subtract_stub(this),
      if_lhsisnotnumber(this), check_rhsisoddball(this),
      call_with_any_feedback(this);
  Variable var_fsub_lhs(this, MachineRepresentation::kFloat64),
      var_fsub_rhs(this, MachineRepresentation::kFloat64),
      var_type_feedback(this, MachineRepresentation::kTaggedSigned),
      var_result(this, MachineRepresentation::kTagged);

  // Check if the {lhs} is a Smi or a HeapObject.
  Label if_lhsissmi(this), if_lhsisnotsmi(this);
  Branch(TaggedIsSmi(lhs), &if_lhsissmi, &if_lhsisnotsmi);

  Bind(&if_lhsissmi);
  {
    // Check if the {rhs} is also a Smi.
    Label if_rhsissmi(this), if_rhsisnotsmi(this);
    Branch(TaggedIsSmi(rhs), &if_rhsissmi, &if_rhsisnotsmi);

    Bind(&if_rhsissmi);
    {
      // Try a fast Smi subtraction first.
      Node* pair = IntPtrSubWithOverflow(BitcastTaggedToWord(lhs),
                                         BitcastTaggedToWord(rhs));
      Node* overflow = Projection(1, pair);

      // Check if the Smi subtraction overflowed.
      Label if_overflow(this), if_notoverflow(this);
      Branch(overflow, &if_overflow, &if_notoverflow);

      Bind(&if_overflow);
      {
        // lhs, rhs - smi and result - number. combined - number.
        // The result doesn't fit into Smi range.
        var_fsub_lhs.Bind(SmiToFloat64(lhs));
        var_fsub_rhs.Bind(SmiToFloat64(rhs));
        Goto(&do_fsub);
      }

      Bind(&if_notoverflow);
      // lhs, rhs, result smi. combined - smi.
      var_type_feedback.Bind(
          SmiConstant(BinaryOperationFeedback::kSignedSmall));
      var_result.Bind(BitcastWordToTaggedSigned(Projection(0, pair)));
      Goto(&end);
    }

    Bind(&if_rhsisnotsmi);
    {
      // Load the map of the {rhs}.
      Node* rhs_map = LoadMap(rhs);

      // Check if {rhs} is a HeapNumber.
      GotoIfNot(IsHeapNumberMap(rhs_map), &check_rhsisoddball);

      // Perform a floating point subtraction.
      var_fsub_lhs.Bind(SmiToFloat64(lhs));
      var_fsub_rhs.Bind(LoadHeapNumberValue(rhs));
      Goto(&do_fsub);
    }
  }

  Bind(&if_lhsisnotsmi);
  {
    // Load the map of the {lhs}.
    Node* lhs_map = LoadMap(lhs);

    // Check if the {lhs} is a HeapNumber.
    GotoIfNot(IsHeapNumberMap(lhs_map), &if_lhsisnotnumber);

    // Check if the {rhs} is a Smi.
    Label if_rhsissmi(this), if_rhsisnotsmi(this);
    Branch(TaggedIsSmi(rhs), &if_rhsissmi, &if_rhsisnotsmi);

    Bind(&if_rhsissmi);
    {
      // Perform a floating point subtraction.
      var_fsub_lhs.Bind(LoadHeapNumberValue(lhs));
      var_fsub_rhs.Bind(SmiToFloat64(rhs));
      Goto(&do_fsub);
    }

    Bind(&if_rhsisnotsmi);
    {
      // Load the map of the {rhs}.
      Node* rhs_map = LoadMap(rhs);

      // Check if the {rhs} is a HeapNumber.
      GotoIfNot(IsHeapNumberMap(rhs_map), &check_rhsisoddball);

      // Perform a floating point subtraction.
      var_fsub_lhs.Bind(LoadHeapNumberValue(lhs));
      var_fsub_rhs.Bind(LoadHeapNumberValue(rhs));
      Goto(&do_fsub);
    }
  }

  Bind(&do_fsub);
  {
    var_type_feedback.Bind(SmiConstant(BinaryOperationFeedback::kNumber));
    Node* lhs_value = var_fsub_lhs.value();
    Node* rhs_value = var_fsub_rhs.value();
    Node* value = Float64Sub(lhs_value, rhs_value);
    var_result.Bind(AllocateHeapNumberWithValue(value));
    Goto(&end);
  }

  Bind(&if_lhsisnotnumber);
  {
    // No checks on rhs are done yet. We just know lhs is not a number or Smi.
    // Check if lhs is an oddball.
    Node* lhs_instance_type = LoadInstanceType(lhs);
    Node* lhs_is_oddball =
        Word32Equal(lhs_instance_type, Int32Constant(ODDBALL_TYPE));
    GotoIfNot(lhs_is_oddball, &call_with_any_feedback);

    Label if_rhsissmi(this), if_rhsisnotsmi(this);
    Branch(TaggedIsSmi(rhs), &if_rhsissmi, &if_rhsisnotsmi);

    Bind(&if_rhsissmi);
    {
      var_type_feedback.Bind(
          SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
      Goto(&call_subtract_stub);
    }

    Bind(&if_rhsisnotsmi);
    {
      // Load the map of the {rhs}.
      Node* rhs_map = LoadMap(rhs);

      // Check if {rhs} is a HeapNumber.
      GotoIfNot(IsHeapNumberMap(rhs_map), &check_rhsisoddball);

      var_type_feedback.Bind(
          SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
      Goto(&call_subtract_stub);
    }
  }

  Bind(&check_rhsisoddball);
  {
    // Check if rhs is an oddball. At this point we know lhs is either a
    // Smi or number or oddball and rhs is not a number or Smi.
    Node* rhs_instance_type = LoadInstanceType(rhs);
    Node* rhs_is_oddball =
        Word32Equal(rhs_instance_type, Int32Constant(ODDBALL_TYPE));
    GotoIfNot(rhs_is_oddball, &call_with_any_feedback);

    var_type_feedback.Bind(
        SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
    Goto(&call_subtract_stub);
  }

  Bind(&call_with_any_feedback);
  {
    var_type_feedback.Bind(SmiConstant(BinaryOperationFeedback::kAny));
    Goto(&call_subtract_stub);
  }

  Bind(&call_subtract_stub);
  {
    Callable callable = CodeFactory::Subtract(isolate());
    var_result.Bind(CallStub(callable, context, lhs, rhs));
    Goto(&end);
  }

  Bind(&end);
  UpdateFeedback(var_type_feedback.value(), feedback_vector, slot_id);
  return var_result.value();
}

Node* BinaryOpAssembler::Generate_MultiplyWithFeedback(Node* context, Node* lhs,
                                                       Node* rhs, Node* slot_id,
                                                       Node* feedback_vector) {
  // Shared entry point for floating point multiplication.
  Label do_fmul(this), if_lhsisnotnumber(this, Label::kDeferred),
      check_rhsisoddball(this, Label::kDeferred),
      call_with_oddball_feedback(this), call_with_any_feedback(this),
      call_multiply_stub(this), end(this);
  Variable var_lhs_float64(this, MachineRepresentation::kFloat64),
      var_rhs_float64(this, MachineRepresentation::kFloat64),
      var_result(this, MachineRepresentation::kTagged),
      var_type_feedback(this, MachineRepresentation::kTaggedSigned);

  Label lhs_is_smi(this), lhs_is_not_smi(this);
  Branch(TaggedIsSmi(lhs), &lhs_is_smi, &lhs_is_not_smi);

  Bind(&lhs_is_smi);
  {
    Label rhs_is_smi(this), rhs_is_not_smi(this);
    Branch(TaggedIsSmi(rhs), &rhs_is_smi, &rhs_is_not_smi);

    Bind(&rhs_is_smi);
    {
      // Both {lhs} and {rhs} are Smis. The result is not necessarily a smi,
      // in case of overflow.
      var_result.Bind(SmiMul(lhs, rhs));
      var_type_feedback.Bind(
          SelectSmiConstant(TaggedIsSmi(var_result.value()),
                            BinaryOperationFeedback::kSignedSmall,
                            BinaryOperationFeedback::kNumber));
      Goto(&end);
    }

    Bind(&rhs_is_not_smi);
    {
      Node* rhs_map = LoadMap(rhs);

      // Check if {rhs} is a HeapNumber.
      GotoIfNot(IsHeapNumberMap(rhs_map), &check_rhsisoddball);

      // Convert {lhs} to a double and multiply it with the value of {rhs}.
      var_lhs_float64.Bind(SmiToFloat64(lhs));
      var_rhs_float64.Bind(LoadHeapNumberValue(rhs));
      Goto(&do_fmul);
    }
  }

  Bind(&lhs_is_not_smi);
  {
    Node* lhs_map = LoadMap(lhs);

    // Check if {lhs} is a HeapNumber.
    GotoIfNot(IsHeapNumberMap(lhs_map), &if_lhsisnotnumber);

    // Check if {rhs} is a Smi.
    Label rhs_is_smi(this), rhs_is_not_smi(this);
    Branch(TaggedIsSmi(rhs), &rhs_is_smi, &rhs_is_not_smi);

    Bind(&rhs_is_smi);
    {
      // Convert {rhs} to a double and multiply it with the value of {lhs}.
      var_lhs_float64.Bind(LoadHeapNumberValue(lhs));
      var_rhs_float64.Bind(SmiToFloat64(rhs));
      Goto(&do_fmul);
    }

    Bind(&rhs_is_not_smi);
    {
      Node* rhs_map = LoadMap(rhs);

      // Check if {rhs} is a HeapNumber.
      GotoIfNot(IsHeapNumberMap(rhs_map), &check_rhsisoddball);

      // Both {lhs} and {rhs} are HeapNumbers. Load their values and
      // multiply them.
      var_lhs_float64.Bind(LoadHeapNumberValue(lhs));
      var_rhs_float64.Bind(LoadHeapNumberValue(rhs));
      Goto(&do_fmul);
    }
  }

  Bind(&do_fmul);
  {
    var_type_feedback.Bind(SmiConstant(BinaryOperationFeedback::kNumber));
    Node* value = Float64Mul(var_lhs_float64.value(), var_rhs_float64.value());
    Node* result = AllocateHeapNumberWithValue(value);
    var_result.Bind(result);
    Goto(&end);
  }

  Bind(&if_lhsisnotnumber);
  {
    // No checks on rhs are done yet. We just know lhs is not a number or Smi.
    // Check if lhs is an oddball.
    Node* lhs_instance_type = LoadInstanceType(lhs);
    Node* lhs_is_oddball =
        Word32Equal(lhs_instance_type, Int32Constant(ODDBALL_TYPE));
    GotoIfNot(lhs_is_oddball, &call_with_any_feedback);

    GotoIf(TaggedIsSmi(rhs), &call_with_oddball_feedback);

    // Load the map of the {rhs}.
    Node* rhs_map = LoadMap(rhs);

    // Check if {rhs} is a HeapNumber.
    Branch(IsHeapNumberMap(rhs_map), &call_with_oddball_feedback,
           &check_rhsisoddball);
  }

  Bind(&check_rhsisoddball);
  {
    // Check if rhs is an oddball. At this point we know lhs is either a
    // Smi or number or oddball and rhs is not a number or Smi.
    Node* rhs_instance_type = LoadInstanceType(rhs);
    Node* rhs_is_oddball =
        Word32Equal(rhs_instance_type, Int32Constant(ODDBALL_TYPE));
    Branch(rhs_is_oddball, &call_with_oddball_feedback,
           &call_with_any_feedback);
  }

  Bind(&call_with_oddball_feedback);
  {
    var_type_feedback.Bind(
        SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
    Goto(&call_multiply_stub);
  }

  Bind(&call_with_any_feedback);
  {
    var_type_feedback.Bind(SmiConstant(BinaryOperationFeedback::kAny));
    Goto(&call_multiply_stub);
  }

  Bind(&call_multiply_stub);
  {
    Callable callable = CodeFactory::Multiply(isolate());
    var_result.Bind(CallStub(callable, context, lhs, rhs));
    Goto(&end);
  }

  Bind(&end);
  UpdateFeedback(var_type_feedback.value(), feedback_vector, slot_id);
  return var_result.value();
}

Node* BinaryOpAssembler::Generate_DivideWithFeedback(Node* context,
                                                     Node* dividend,
                                                     Node* divisor,
                                                     Node* slot_id,
                                                     Node* feedback_vector) {
  // Shared entry point for floating point division.
  Label do_fdiv(this), dividend_is_not_number(this, Label::kDeferred),
      check_divisor_for_oddball(this, Label::kDeferred),
      call_with_oddball_feedback(this), call_with_any_feedback(this),
      call_divide_stub(this), end(this);
  Variable var_dividend_float64(this, MachineRepresentation::kFloat64),
      var_divisor_float64(this, MachineRepresentation::kFloat64),
      var_result(this, MachineRepresentation::kTagged),
      var_type_feedback(this, MachineRepresentation::kTaggedSigned);

  Label dividend_is_smi(this), dividend_is_not_smi(this);
  Branch(TaggedIsSmi(dividend), &dividend_is_smi, &dividend_is_not_smi);

  Bind(&dividend_is_smi);
  {
    Label divisor_is_smi(this), divisor_is_not_smi(this);
    Branch(TaggedIsSmi(divisor), &divisor_is_smi, &divisor_is_not_smi);

    Bind(&divisor_is_smi);
    {
      Label bailout(this);

      // Do floating point division if {divisor} is zero.
      GotoIf(WordEqual(divisor, SmiConstant(0)), &bailout);

      // Do floating point division {dividend} is zero and {divisor} is
      // negative.
      Label dividend_is_zero(this), dividend_is_not_zero(this);
      Branch(WordEqual(dividend, SmiConstant(0)), &dividend_is_zero,
             &dividend_is_not_zero);

      Bind(&dividend_is_zero);
      {
        GotoIf(SmiLessThan(divisor, SmiConstant(0)), &bailout);
        Goto(&dividend_is_not_zero);
      }
      Bind(&dividend_is_not_zero);

      Node* untagged_divisor = SmiToWord32(divisor);
      Node* untagged_dividend = SmiToWord32(dividend);

      // Do floating point division if {dividend} is kMinInt (or kMinInt - 1
      // if the Smi size is 31) and {divisor} is -1.
      Label divisor_is_minus_one(this), divisor_is_not_minus_one(this);
      Branch(Word32Equal(untagged_divisor, Int32Constant(-1)),
             &divisor_is_minus_one, &divisor_is_not_minus_one);

      Bind(&divisor_is_minus_one);
      {
        GotoIf(Word32Equal(untagged_dividend,
                           Int32Constant(kSmiValueSize == 32 ? kMinInt
                                                             : (kMinInt >> 1))),
               &bailout);
        Goto(&divisor_is_not_minus_one);
      }
      Bind(&divisor_is_not_minus_one);

      Node* untagged_result = Int32Div(untagged_dividend, untagged_divisor);
      Node* truncated = Int32Mul(untagged_result, untagged_divisor);
      // Do floating point division if the remainder is not 0.
      GotoIf(Word32NotEqual(untagged_dividend, truncated), &bailout);
      var_type_feedback.Bind(
          SmiConstant(BinaryOperationFeedback::kSignedSmall));
      var_result.Bind(SmiFromWord32(untagged_result));
      Goto(&end);

      // Bailout: convert {dividend} and {divisor} to double and do double
      // division.
      Bind(&bailout);
      {
        var_dividend_float64.Bind(SmiToFloat64(dividend));
        var_divisor_float64.Bind(SmiToFloat64(divisor));
        Goto(&do_fdiv);
      }
    }

    Bind(&divisor_is_not_smi);
    {
      Node* divisor_map = LoadMap(divisor);

      // Check if {divisor} is a HeapNumber.
      GotoIfNot(IsHeapNumberMap(divisor_map), &check_divisor_for_oddball);

      // Convert {dividend} to a double and divide it with the value of
      // {divisor}.
      var_dividend_float64.Bind(SmiToFloat64(dividend));
      var_divisor_float64.Bind(LoadHeapNumberValue(divisor));
      Goto(&do_fdiv);
    }

    Bind(&dividend_is_not_smi);
    {
      Node* dividend_map = LoadMap(dividend);

      // Check if {dividend} is a HeapNumber.
      GotoIfNot(IsHeapNumberMap(dividend_map), &dividend_is_not_number);

      // Check if {divisor} is a Smi.
      Label divisor_is_smi(this), divisor_is_not_smi(this);
      Branch(TaggedIsSmi(divisor), &divisor_is_smi, &divisor_is_not_smi);

      Bind(&divisor_is_smi);
      {
        // Convert {divisor} to a double and use it for a floating point
        // division.
        var_dividend_float64.Bind(LoadHeapNumberValue(dividend));
        var_divisor_float64.Bind(SmiToFloat64(divisor));
        Goto(&do_fdiv);
      }

      Bind(&divisor_is_not_smi);
      {
        Node* divisor_map = LoadMap(divisor);

        // Check if {divisor} is a HeapNumber.
        GotoIfNot(IsHeapNumberMap(divisor_map), &check_divisor_for_oddball);

        // Both {dividend} and {divisor} are HeapNumbers. Load their values
        // and divide them.
        var_dividend_float64.Bind(LoadHeapNumberValue(dividend));
        var_divisor_float64.Bind(LoadHeapNumberValue(divisor));
        Goto(&do_fdiv);
      }
    }
  }

  Bind(&do_fdiv);
  {
    var_type_feedback.Bind(SmiConstant(BinaryOperationFeedback::kNumber));
    Node* value =
        Float64Div(var_dividend_float64.value(), var_divisor_float64.value());
    var_result.Bind(AllocateHeapNumberWithValue(value));
    Goto(&end);
  }

  Bind(&dividend_is_not_number);
  {
    // We just know dividend is not a number or Smi. No checks on divisor yet.
    // Check if dividend is an oddball.
    Node* dividend_instance_type = LoadInstanceType(dividend);
    Node* dividend_is_oddball =
        Word32Equal(dividend_instance_type, Int32Constant(ODDBALL_TYPE));
    GotoIfNot(dividend_is_oddball, &call_with_any_feedback);

    GotoIf(TaggedIsSmi(divisor), &call_with_oddball_feedback);

    // Load the map of the {divisor}.
    Node* divisor_map = LoadMap(divisor);

    // Check if {divisor} is a HeapNumber.
    Branch(IsHeapNumberMap(divisor_map), &call_with_oddball_feedback,
           &check_divisor_for_oddball);
  }

  Bind(&check_divisor_for_oddball);
  {
    // Check if divisor is an oddball. At this point we know dividend is either
    // a Smi or number or oddball and divisor is not a number or Smi.
    Node* divisor_instance_type = LoadInstanceType(divisor);
    Node* divisor_is_oddball =
        Word32Equal(divisor_instance_type, Int32Constant(ODDBALL_TYPE));
    Branch(divisor_is_oddball, &call_with_oddball_feedback,
           &call_with_any_feedback);
  }

  Bind(&call_with_oddball_feedback);
  {
    var_type_feedback.Bind(
        SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
    Goto(&call_divide_stub);
  }

  Bind(&call_with_any_feedback);
  {
    var_type_feedback.Bind(SmiConstant(BinaryOperationFeedback::kAny));
    Goto(&call_divide_stub);
  }

  Bind(&call_divide_stub);
  {
    Callable callable = CodeFactory::Divide(isolate());
    var_result.Bind(CallStub(callable, context, dividend, divisor));
    Goto(&end);
  }

  Bind(&end);
  UpdateFeedback(var_type_feedback.value(), feedback_vector, slot_id);
  return var_result.value();
}

Node* BinaryOpAssembler::Generate_ModulusWithFeedback(Node* context,
                                                      Node* dividend,
                                                      Node* divisor,
                                                      Node* slot_id,
                                                      Node* feedback_vector) {
  // Shared entry point for floating point division.
  Label do_fmod(this), dividend_is_not_number(this, Label::kDeferred),
      check_divisor_for_oddball(this, Label::kDeferred),
      call_with_oddball_feedback(this), call_with_any_feedback(this),
      call_modulus_stub(this), end(this);
  Variable var_dividend_float64(this, MachineRepresentation::kFloat64),
      var_divisor_float64(this, MachineRepresentation::kFloat64),
      var_result(this, MachineRepresentation::kTagged),
      var_type_feedback(this, MachineRepresentation::kTaggedSigned);

  Label dividend_is_smi(this), dividend_is_not_smi(this);
  Branch(TaggedIsSmi(dividend), &dividend_is_smi, &dividend_is_not_smi);

  Bind(&dividend_is_smi);
  {
    Label divisor_is_smi(this), divisor_is_not_smi(this);
    Branch(TaggedIsSmi(divisor), &divisor_is_smi, &divisor_is_not_smi);

    Bind(&divisor_is_smi);
    {
      var_result.Bind(SmiMod(dividend, divisor));
      var_type_feedback.Bind(
          SelectSmiConstant(TaggedIsSmi(var_result.value()),
                            BinaryOperationFeedback::kSignedSmall,
                            BinaryOperationFeedback::kNumber));
      Goto(&end);
    }

    Bind(&divisor_is_not_smi);
    {
      Node* divisor_map = LoadMap(divisor);

      // Check if {divisor} is a HeapNumber.
      GotoIfNot(IsHeapNumberMap(divisor_map), &check_divisor_for_oddball);

      // Convert {dividend} to a double and divide it with the value of
      // {divisor}.
      var_dividend_float64.Bind(SmiToFloat64(dividend));
      var_divisor_float64.Bind(LoadHeapNumberValue(divisor));
      Goto(&do_fmod);
    }
  }

  Bind(&dividend_is_not_smi);
  {
    Node* dividend_map = LoadMap(dividend);

    // Check if {dividend} is a HeapNumber.
    GotoIfNot(IsHeapNumberMap(dividend_map), &dividend_is_not_number);

    // Check if {divisor} is a Smi.
    Label divisor_is_smi(this), divisor_is_not_smi(this);
    Branch(TaggedIsSmi(divisor), &divisor_is_smi, &divisor_is_not_smi);

    Bind(&divisor_is_smi);
    {
      // Convert {divisor} to a double and use it for a floating point
      // division.
      var_dividend_float64.Bind(LoadHeapNumberValue(dividend));
      var_divisor_float64.Bind(SmiToFloat64(divisor));
      Goto(&do_fmod);
    }

    Bind(&divisor_is_not_smi);
    {
      Node* divisor_map = LoadMap(divisor);

      // Check if {divisor} is a HeapNumber.
      GotoIfNot(IsHeapNumberMap(divisor_map), &check_divisor_for_oddball);

      // Both {dividend} and {divisor} are HeapNumbers. Load their values
      // and divide them.
      var_dividend_float64.Bind(LoadHeapNumberValue(dividend));
      var_divisor_float64.Bind(LoadHeapNumberValue(divisor));
      Goto(&do_fmod);
    }
  }

  Bind(&do_fmod);
  {
    var_type_feedback.Bind(SmiConstant(BinaryOperationFeedback::kNumber));
    Node* value =
        Float64Mod(var_dividend_float64.value(), var_divisor_float64.value());
    var_result.Bind(AllocateHeapNumberWithValue(value));
    Goto(&end);
  }

  Bind(&dividend_is_not_number);
  {
    // No checks on divisor yet. We just know dividend is not a number or Smi.
    // Check if dividend is an oddball.
    Node* dividend_instance_type = LoadInstanceType(dividend);
    Node* dividend_is_oddball =
        Word32Equal(dividend_instance_type, Int32Constant(ODDBALL_TYPE));
    GotoIfNot(dividend_is_oddball, &call_with_any_feedback);

    GotoIf(TaggedIsSmi(divisor), &call_with_oddball_feedback);

    // Load the map of the {divisor}.
    Node* divisor_map = LoadMap(divisor);

    // Check if {divisor} is a HeapNumber.
    Branch(IsHeapNumberMap(divisor_map), &call_with_oddball_feedback,
           &check_divisor_for_oddball);
  }

  Bind(&check_divisor_for_oddball);
  {
    // Check if divisor is an oddball. At this point we know dividend is either
    // a Smi or number or oddball and divisor is not a number or Smi.
    Node* divisor_instance_type = LoadInstanceType(divisor);
    Node* divisor_is_oddball =
        Word32Equal(divisor_instance_type, Int32Constant(ODDBALL_TYPE));
    Branch(divisor_is_oddball, &call_with_oddball_feedback,
           &call_with_any_feedback);
  }

  Bind(&call_with_oddball_feedback);
  {
    var_type_feedback.Bind(
        SmiConstant(BinaryOperationFeedback::kNumberOrOddball));
    Goto(&call_modulus_stub);
  }

  Bind(&call_with_any_feedback);
  {
    var_type_feedback.Bind(SmiConstant(BinaryOperationFeedback::kAny));
    Goto(&call_modulus_stub);
  }

  Bind(&call_modulus_stub);
  {
    Callable callable = CodeFactory::Modulus(isolate());
    var_result.Bind(CallStub(callable, context, dividend, divisor));
    Goto(&end);
  }

  Bind(&end);
  UpdateFeedback(var_type_feedback.value(), feedback_vector, slot_id);
  return var_result.value();
}

}  // namespace internal
}  // namespace v8
