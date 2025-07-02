// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-truncation.h"

#include "src/execution/local-isolate-inl.h"
#include "src/maglev/maglev-ir-inl.h"
#include "src/numbers/conversions-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball-inl.h"

#define TRACE(...)                                               \
  do {                                                           \
    if (V8_UNLIKELY(v8_flags.trace_maglev_truncation)) {         \
      std::cout << "[truncation]: " << __VA_ARGS__ << std::endl; \
    }                                                            \
  } while (false);
#define PRINT(node) PrintNodeLabel(node) << ": " << node->opcode()

namespace v8::internal::maglev {

void MaglevTruncationProcessor::TruncateInput(ValueNode* node, int index) {
  ValueNode* input = node->input(index).node();
  if (CanTruncate(input)) {
    node->change_input(index, Truncate(input));
  }
}

void MaglevTruncationProcessor::UnsafeTruncateInput(ValueNode* node,
                                                    int index) {
  ValueNode* input = node->input(index).node();
  node->change_input(index, Truncate(input));
}

// TODO(victorgomes): CanTruncate could be calculated during graph building.
bool MaglevTruncationProcessor::CanTruncate(ValueNode* node) {
  switch (node->opcode()) {
    // Constants
    case Opcode::kConstant:
      return node->Cast<Constant>()->object().IsHeapNumber();
    case Opcode::kRootConstant:
      return IsOddball(GetRootConstant(node));
    case Opcode::kFloat64Constant:
      return true;
    // Conversion nodes
    case Opcode::kChangeInt32ToFloat64:
      return true;
    case Opcode::kCheckedTruncateFloat64ToInt32:
    case Opcode::kTruncateFloat64ToInt32:
    case Opcode::kCheckedTruncateNumberOrOddballToInt32:
    case Opcode::kTruncateNumberOrOddballToInt32:
    case Opcode::kCheckedNumberToInt32:
      if (node->use_count() != 1) {
        TRACE("conversion node " << PRINT(node) << " has many uses");
        return false;
      }
      return CanTruncate(node->input(0).node());
    // Arithmetic pure operations
    case Opcode::kFloat64Add:
    case Opcode::kFloat64Subtract:
      return IsIntN(node, kMaxSafeIntegerLog2);
    // TODO(victorgomes): We need to guarantee that the multiplication
    // produces a safe integer. case Opcode::kFloat64Multiply:
    case Opcode::kFloat64Divide:
      if (node->use_count() != 1) {
        TRACE(PRINT(node) << " has many uses");
        return false;
      }
      // The operation can be truncated if the numerator is a safe integer.
      // The denominator can be any integer.
      return IsIntN(node->input(0).node(), kMaxSafeIntegerLog2) &&
             IsIntN(node->input(1).node(), kMaxInteger64Log2);
    default:
      return false;
  }
}

bool MaglevTruncationProcessor::IsIntN(ValueNode* node, int nbits) {
  if (nbits <= 0) return false;
  switch (node->opcode()) {
    // Constants
    case Opcode::kConstant:
      return node->Cast<Constant>()->object().IsHeapNumber() &&
             IsIntN(node->Cast<Constant>()->object().AsHeapNumber().value(),
                    nbits);
    case Opcode::kRootConstant:
      return IsOddball(GetRootConstant(node)) &&
             IsIntN(Cast<Oddball>(GetRootConstant(node))->to_number_raw(),
                    nbits);
    case Opcode::kFloat64Constant:
      return IsIntN(node->Cast<Float64Constant>()->value().get_scalar(), nbits);
    // Conversion nodes
    case Opcode::kChangeInt32ToFloat64:
      return nbits >= 32;
    case Opcode::kCheckedTruncateFloat64ToInt32:
    case Opcode::kTruncateFloat64ToInt32:
    case Opcode::kCheckedTruncateNumberOrOddballToInt32:
    case Opcode::kTruncateNumberOrOddballToInt32:
    case Opcode::kCheckedNumberToInt32:
      if (node->use_count() != 1) {
        TRACE("conversion node " << PRINT(node) << " has many uses");
        return false;
      }
      return IsIntN(node->input(0).node(), nbits);
    // Arithmetic pure operations
    case Opcode::kFloat64Add:
    case Opcode::kFloat64Subtract:
      if (node->use_count() != 1) {
        TRACE(PRINT(node) << " has many uses");
        return false;
      }
      // Integer addition/subtraction can be represented with one more bit
      // than its inputs.
      return IsIntN(node->input(0).node(), nbits - 1) &&
             IsIntN(node->input(1).node(), nbits - 1);
    default:
      return false;
  }
}

bool MaglevTruncationProcessor::IsIntN(double value, int nbits) {
  DCHECK_LE(nbits, 64);
  if (nbits == 64) return true;
  if (nbits <= 0) return false;
  double limit = 1LL << (nbits - 1);
  return -limit <= value && value < limit && std::trunc(value) == value;
}

ValueNode* MaglevTruncationProcessor::Truncate(ValueNode* node) {
  switch (node->opcode()) {
    // Constants
    case Opcode::kConstant:
      DCHECK(node->Cast<Constant>()->object().IsHeapNumber());
      return GetTruncatedInt32Constant(
          node->Cast<Constant>()->object().AsHeapNumber().value());
    case Opcode::kRootConstant:
      DCHECK(IsOddball(GetRootConstant(node)));
      return GetTruncatedInt32Constant(
          Cast<Oddball>(GetRootConstant(node))->to_number_raw());
    case Opcode::kFloat64Constant:
      return GetTruncatedInt32Constant(
          node->Cast<Float64Constant>()->value().get_scalar());
    // Conversion nodes
    case Opcode::kChangeInt32ToFloat64:
      TRACE("bypassing conversion node " << PRINT(node));
      return node->input(0).node();
    case Opcode::kCheckedTruncateFloat64ToInt32:
    case Opcode::kTruncateFloat64ToInt32: {
      TRACE("bypassing conversion node " << PRINT(node));
      ValueNode* input = node->input(0).node();
#ifdef DEBUG
      // This conversion node is now dead, since we recursively truncate its
      // input and return that instead. The recursive truncation can change
      // the representation of the input node, which would cause a type
      // mismatch for this (dead) node's input and crash the graph verifier.
      // We set the input to a constant zero to ensure the dead node remains
      // valid for the verifier.
      node->change_input(0, graph_->GetFloat64Constant(0));
#endif  // DEBUG
      return Truncate(input);
    }
    case Opcode::kCheckedTruncateNumberOrOddballToInt32:
    case Opcode::kTruncateNumberOrOddballToInt32:
    case Opcode::kCheckedNumberToInt32: {
      TRACE("bypassing conversion node " << PRINT(node));
      ValueNode* input = node->input(0).node();
#ifdef DEBUG
      // This conversion node is now dead, since we recursively truncate its
      // input and return that instead. The recursive truncation can change
      // the representation of the input node, which would cause a type
      // mismatch for this (dead) node's input and crash the graph verifier.
      // We set the input to a constant zero to ensure the dead node remains
      // valid for the verifier.
      node->change_input(0, graph_->GetSmiConstant(0));
#endif  // DEBUG
      return Truncate(input);
    }
    // Arithmetic pure operations
    case Opcode::kFloat64Add:
      return OverwriteWith<Int32Add>(node);
    case Opcode::kFloat64Subtract:
      return OverwriteWith<Int32Subtract>(node);
    // case Opcode::kFloat64Multiply:
    //   return OverwriteWith<Int32Multiply>(node);
    case Opcode::kFloat64Divide:
      return OverwriteWith<Int32Divide>(node);
    default:
      UNREACHABLE();
  }
}

template <typename NodeT>
ValueNode* MaglevTruncationProcessor::OverwriteWith(ValueNode* node) {
  TRACE("overwriting " << PRINT(node));
  UnsafeTruncateInput(node, 0);
  UnsafeTruncateInput(node, 1);
  node->OverwriteWith<NodeT>();
  // TODO(victorgomes): I don't think we should initialize register data in
  // the value node constructor, maybe choose a less prone place for it,
  // before register allocation.
  node->InitializeRegisterData();
  TRACE("   with " << PRINT(node));
  return node;
}

ValueNode* MaglevTruncationProcessor::GetTruncatedInt32Constant(
    double constant) {
  return graph_->GetInt32Constant(DoubleToInt32(constant));
}

Tagged<Object> MaglevTruncationProcessor::GetRootConstant(ValueNode* node) {
  return graph_->broker()->local_isolate()->root(
      node->Cast<RootConstant>()->index());
}

}  // namespace v8::internal::maglev
