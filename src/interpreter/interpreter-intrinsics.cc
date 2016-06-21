// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/interpreter-intrinsics.h"

namespace v8 {
namespace internal {
namespace interpreter {

using compiler::Node;

#define __ assembler_->

IntrinsicsHelper::IntrinsicsHelper(InterpreterAssembler* assembler)
    : assembler_(assembler) {}

// static
bool IntrinsicsHelper::IsSupported(Runtime::FunctionId function_id) {
  switch (function_id) {
#define SUPPORTED(name, lower_case, count) case Runtime::kInline##name:
    INTRINSICS_LIST(SUPPORTED)
    return true;
#undef SUPPORTED
    default:
      return false;
  }
}

// static
IntrinsicsHelper::IntrinsicId IntrinsicsHelper::FromRuntimeId(
    Runtime::FunctionId function_id) {
  switch (function_id) {
#define TO_RUNTIME_ID(name, lower_case, count) \
  case Runtime::kInline##name:                 \
    return IntrinsicId::k##name;
    INTRINSICS_LIST(TO_RUNTIME_ID)
#undef TO_RUNTIME_ID
    default:
      UNREACHABLE();
      return static_cast<IntrinsicsHelper::IntrinsicId>(-1);
  }
}

// static
Runtime::FunctionId IntrinsicsHelper::ToRuntimeId(
    IntrinsicsHelper::IntrinsicId intrinsic_id) {
  switch (intrinsic_id) {
#define TO_INTRINSIC_ID(name, lower_case, count) \
  case IntrinsicId::k##name:                     \
    return Runtime::kInline##name;
    INTRINSICS_LIST(TO_INTRINSIC_ID)
#undef TO_INTRINSIC_ID
    default:
      UNREACHABLE();
      return static_cast<Runtime::FunctionId>(-1);
  }
}

Node* IntrinsicsHelper::InvokeIntrinsic(Node* function_id, Node* context,
                                        Node* first_arg_reg, Node* arg_count) {
  InterpreterAssembler::Label abort(assembler_), end(assembler_);
  InterpreterAssembler::Variable result(assembler_,
                                        MachineRepresentation::kTagged);

#define MAKE_LABEL(name, lower_case, count) \
  InterpreterAssembler::Label lower_case(assembler_);
  INTRINSICS_LIST(MAKE_LABEL)
#undef MAKE_LABEL

#define LABEL_POINTER(name, lower_case, count) &lower_case,
  InterpreterAssembler::Label* labels[] = {INTRINSICS_LIST(LABEL_POINTER)};
#undef LABEL_POINTER

#define CASE(name, lower_case, count) \
  static_cast<int32_t>(IntrinsicId::k##name),
  int32_t cases[] = {INTRINSICS_LIST(CASE)};
#undef CASE

  __ Switch(function_id, &abort, cases, labels, arraysize(cases));
#define HANDLE_CASE(name, lower_case, expected_arg_count)   \
  __ Bind(&lower_case);                                     \
  if (FLAG_debug_code && expected_arg_count >= 0) {         \
    AbortIfArgCountMismatch(expected_arg_count, arg_count); \
  }                                                         \
  result.Bind(name(first_arg_reg, arg_count, context));     \
  __ Goto(&end);
  INTRINSICS_LIST(HANDLE_CASE)
#undef HANDLE_CASE

  __ Bind(&abort);
  {
    __ Abort(BailoutReason::kUnexpectedFunctionIDForInvokeIntrinsic);
    result.Bind(__ UndefinedConstant());
    __ Goto(&end);
  }

  __ Bind(&end);
  return result.value();
}

Node* IntrinsicsHelper::CompareInstanceType(Node* map, int type,
                                            InstanceTypeCompareMode mode) {
  InterpreterAssembler::Variable return_value(assembler_,
                                              MachineRepresentation::kTagged);
  Node* instance_type = __ LoadInstanceType(map);

  InterpreterAssembler::Label if_true(assembler_), if_false(assembler_),
      end(assembler_);
  Node* condition;
  if (mode == kInstanceTypeEqual) {
    condition = __ Word32Equal(instance_type, __ Int32Constant(type));
  } else {
    DCHECK(mode == kInstanceTypeGreaterThanOrEqual);
    condition =
        __ Int32GreaterThanOrEqual(instance_type, __ Int32Constant(type));
  }
  __ Branch(condition, &if_true, &if_false);

  __ Bind(&if_true);
  {
    return_value.Bind(__ BooleanConstant(true));
    __ Goto(&end);
  }

  __ Bind(&if_false);
  {
    return_value.Bind(__ BooleanConstant(false));
    __ Goto(&end);
  }

  __ Bind(&end);
  return return_value.value();
}

Node* IntrinsicsHelper::IsInstanceType(Node* input, int type) {
  InterpreterAssembler::Variable return_value(assembler_,
                                              MachineRepresentation::kTagged);
  InterpreterAssembler::Label if_smi(assembler_), if_not_smi(assembler_),
      end(assembler_);
  Node* arg = __ LoadRegister(input);
  __ Branch(__ WordIsSmi(arg), &if_smi, &if_not_smi);

  __ Bind(&if_smi);
  {
    return_value.Bind(__ BooleanConstant(false));
    __ Goto(&end);
  }

  __ Bind(&if_not_smi);
  {
    return_value.Bind(CompareInstanceType(arg, type, kInstanceTypeEqual));
    __ Goto(&end);
  }

  __ Bind(&end);
  return return_value.value();
}

Node* IntrinsicsHelper::IsJSReceiver(Node* input, Node* arg_count,
                                     Node* context) {
  InterpreterAssembler::Variable return_value(assembler_,
                                              MachineRepresentation::kTagged);
  InterpreterAssembler::Label if_smi(assembler_), if_not_smi(assembler_),
      end(assembler_);

  Node* arg = __ LoadRegister(input);
  __ Branch(__ WordIsSmi(arg), &if_smi, &if_not_smi);

  __ Bind(&if_smi);
  {
    return_value.Bind(__ BooleanConstant(false));
    __ Goto(&end);
  }

  __ Bind(&if_not_smi);
  {
    STATIC_ASSERT(LAST_TYPE == LAST_JS_RECEIVER_TYPE);
    return_value.Bind(CompareInstanceType(arg, FIRST_JS_RECEIVER_TYPE,
                                          kInstanceTypeGreaterThanOrEqual));
    __ Goto(&end);
  }

  __ Bind(&end);
  return return_value.value();
}

Node* IntrinsicsHelper::IsArray(Node* input, Node* arg_count, Node* context) {
  return IsInstanceType(input, JS_ARRAY_TYPE);
}

Node* IntrinsicsHelper::IsJSProxy(Node* input, Node* arg_count, Node* context) {
  return IsInstanceType(input, JS_PROXY_TYPE);
}

Node* IntrinsicsHelper::IsRegExp(Node* input, Node* arg_count, Node* context) {
  return IsInstanceType(input, JS_REGEXP_TYPE);
}

Node* IntrinsicsHelper::IsTypedArray(Node* input, Node* arg_count,
                                     Node* context) {
  return IsInstanceType(input, JS_TYPED_ARRAY_TYPE);
}

Node* IntrinsicsHelper::IsSmi(Node* input, Node* arg_count, Node* context) {
  InterpreterAssembler::Variable return_value(assembler_,
                                              MachineRepresentation::kTagged);
  InterpreterAssembler::Label if_smi(assembler_), if_not_smi(assembler_),
      end(assembler_);

  Node* arg = __ LoadRegister(input);

  __ Branch(__ WordIsSmi(arg), &if_smi, &if_not_smi);
  __ Bind(&if_smi);
  {
    return_value.Bind(__ BooleanConstant(true));
    __ Goto(&end);
  }

  __ Bind(&if_not_smi);
  {
    return_value.Bind(__ BooleanConstant(false));
    __ Goto(&end);
  }

  __ Bind(&end);
  return return_value.value();
}

Node* IntrinsicsHelper::Call(Node* args_reg, Node* arg_count, Node* context) {
  // First argument register contains the function target.
  Node* function = __ LoadRegister(args_reg);

  // Receiver is the second runtime call argument.
  Node* receiver_reg = __ NextRegister(args_reg);
  Node* receiver_arg = __ RegisterLocation(receiver_reg);

  // Subtract function and receiver from arg count.
  Node* function_and_receiver_count = __ Int32Constant(2);
  Node* target_args_count = __ Int32Sub(arg_count, function_and_receiver_count);

  if (FLAG_debug_code) {
    InterpreterAssembler::Label arg_count_positive(assembler_);
    Node* comparison = __ Int32LessThan(target_args_count, __ Int32Constant(0));
    __ GotoUnless(comparison, &arg_count_positive);
    __ Abort(kWrongArgumentCountForInvokeIntrinsic);
    __ Goto(&arg_count_positive);
    __ Bind(&arg_count_positive);
  }

  Node* result = __ CallJS(function, context, receiver_arg, target_args_count,
                           TailCallMode::kDisallow);
  return result;
}

void IntrinsicsHelper::AbortIfArgCountMismatch(int expected, Node* actual) {
  InterpreterAssembler::Label match(assembler_);
  Node* comparison = __ Word32Equal(actual, __ Int32Constant(expected));
  __ GotoIf(comparison, &match);
  __ Abort(kWrongArgumentCountForInvokeIntrinsic);
  __ Goto(&match);
  __ Bind(&match);
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
