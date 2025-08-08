// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-truncation.h"

#include "src/maglev/maglev-ir-inl.h"
#include "src/maglev/maglev-ir.h"

namespace v8::internal::maglev {

bool TruncationProcessor::AllInputsAreValid(ValueNode* node) {
  for (Input input : node->inputs()) {
    ValueNode* unwrapped = input.node()->UnwrapIdentities();
    if (!unwrapped->is_int32()) {
      if (unwrapped->Is<Float64Constant>() &&
          unwrapped->GetRange().IsSafeIntegerRange()) {
        // We can truncate Float64 constants if they're in the safe integer
        // range.
        continue;
      }
      if (unwrapped->Is<ChangeInt32ToFloat64>()) {
        // We can always truncate this safe conversion.
        continue;
      }
      return false;
    }
  }
  return true;
}

ValueNode* TruncationProcessor::GetUnwrappedInput(ValueNode* node, int index) {
  ValueNode* input = node->NodeBase::input(index).node()->UnwrapIdentities();
  if (input->Is<Float64Constant>()) {
    DCHECK(input->GetRange().IsSafeIntegerRange());
    input = GetTruncatedInt32Constant(
        input->Cast<Float64Constant>()->value().get_scalar());
  } else if (input->Is<ChangeInt32ToFloat64>()) {
    input = input->input(0).node()->UnwrapIdentities();
  }
  return input;
}

void TruncationProcessor::UnwrapInputs(ValueNode* node) {
  for (int i = 0; i < node->input_count(); i++) {
    node->change_input(i, GetUnwrappedInput(node, i));
  }
}

ProcessResult TruncationProcessor::Process(CheckedTruncateFloat64ToInt32* node,
                                           const ProcessingState& state) {
  if (AllInputsAreValid(node)) {
    node->OverwriteWithIdentityTo(GetUnwrappedInput(node, 0));
    return ProcessResult::kRemove;
  }
  return ProcessResult::kContinue;
}

ProcessResult TruncationProcessor::Process(TruncateFloat64ToInt32* node,
                                           const ProcessingState& state) {
  if (AllInputsAreValid(node)) {
    node->OverwriteWithIdentityTo(GetUnwrappedInput(node, 0));
    return ProcessResult::kRemove;
  }
  return ProcessResult::kContinue;
}

ProcessResult TruncationProcessor::Process(UnsafeTruncateFloat64ToInt32* node,
                                           const ProcessingState& state) {
  if (AllInputsAreValid(node)) {
    node->OverwriteWithIdentityTo(GetUnwrappedInput(node, 0));
    return ProcessResult::kRemove;
  }
  return ProcessResult::kContinue;
}

ValueNode* TruncationProcessor::GetTruncatedInt32Constant(double constant) {
  return graph_->GetInt32Constant(DoubleToInt32(constant));
}

}  // namespace v8::internal::maglev
