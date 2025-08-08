// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_TRUNCATION_H_
#define V8_MAGLEV_MAGLEV_TRUNCATION_H_

#include <type_traits>

#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

template <typename T>
concept IsValueNodeT = std::is_base_of_v<ValueNode, T>;

// This pass propagates updates for the `CanTruncateToInt32` flag.
// At the end of the pass, if a node has `CanTruncateToInt32` then all its uses
// can handle the node's output being truncated to an int32. IMPORTANT: This is
// a necessary, but not sufficient, condition. The actual truncation will only
// occur if all of the node's inputs can be truncated.
class PropagateTruncationProcessor {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessBasicBlock(BasicBlock* block) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }

  template <IsValueNodeT NodeT>
  ProcessResult Process(NodeT* node) {
    if constexpr (NodeT::kProperties.can_eager_deopt()) {
      node->eager_deopt_info()->ForEachInput([&](ValueNode* node) {
        UnsetCanTruncateToInt32ForDeoptFrameInput(node);
      });
    }
    if constexpr (NodeT::kProperties.can_lazy_deopt()) {
      node->lazy_deopt_info()->ForEachInput([&](ValueNode* node) {
        UnsetCanTruncateToInt32ForDeoptFrameInput(node);
      });
    }
    // If the output is not a Float64, then it cannot (or doesn't need)
    // to be truncated. Just propagate that all inputs should not be
    // truncated.
    if constexpr (NodeT::kProperties.value_representation() !=
                  ValueRepresentation::kFloat64) {
      UnsetCanTruncateToInt32Inputs(node);
      return ProcessResult::kContinue;
    }
    // If the output node is a Float64 and cannot be truncated, then
    // its inputs cannot be truncated.
    if (!node->can_truncate_to_int32()) {
      UnsetCanTruncateToInt32Inputs(node);
    }
    // Otherwise don't unset truncation...
    return ProcessResult::kContinue;
  }

  // Non-value nodes.
  template <typename NodeT>
  ProcessResult Process(NodeT* node) {
    // Non value nodes does not need to be truncated, but we should
    // propagate that we do not want to truncate its inputs.
    if constexpr (NodeT::kProperties.can_eager_deopt()) {
      node->eager_deopt_info()->ForEachInput([&](ValueNode* node) {
        UnsetCanTruncateToInt32ForDeoptFrameInput(node);
      });
    }
    if constexpr (NodeT::kProperties.can_lazy_deopt()) {
      node->lazy_deopt_info()->ForEachInput([&](ValueNode* node) {
        UnsetCanTruncateToInt32ForDeoptFrameInput(node);
      });
    }
    UnsetCanTruncateToInt32Inputs(node);
    return ProcessResult::kContinue;
  }

  ProcessResult Process(Identity* node) { return ProcessResult::kContinue; }
  ProcessResult Process(Dead* node) { return ProcessResult::kContinue; }

  ProcessResult Process(CheckedTruncateFloat64ToInt32* node) {
    // We can always truncate the input of this node.
    return ProcessResult::kContinue;
  }
  ProcessResult Process(TruncateFloat64ToInt32* node) {
    // We can always truncate the input of this node.
    return ProcessResult::kContinue;
  }
  ProcessResult Process(UnsafeTruncateFloat64ToInt32* node) {
    // We can always truncate the input of this node.
    return ProcessResult::kContinue;
  }

  void PostProcessGraph(Graph* graph) {}

 private:
  template <typename NodeT, int I>
  void UnsetCanTruncateToInt32ForFixedInputNodes(NodeT* node) {
    if constexpr (I < static_cast<int>(NodeT::kInputCount)) {
      if constexpr (NodeT::kInputTypes[I] == ValueRepresentation::kFloat64 ||
                    NodeT::kInputTypes[I] ==
                        ValueRepresentation::kHoleyFloat64) {
        node->NodeBase::input(I).node()->set_can_truncate_to_int32(false);
      }
      UnsetCanTruncateToInt32ForFixedInputNodes<NodeT, I + 1>(node);
    }
  }

  template <typename NodeT>
  void UnsetCanTruncateToInt32Inputs(NodeT* node) {
    if constexpr (IsFixedInputNode<NodeT>()) {
      return UnsetCanTruncateToInt32ForFixedInputNodes<NodeT, 0>(node);
    }
#ifdef DEBUG
    // Non-fixed input nodes don't expect float64 as inputs, except
    // ReturnedValue.
    if constexpr (std::is_same_v<NodeT, ReturnedValue>) {
      return;
    }
    for (Input input : node->inputs()) {
      DCHECK_NE(input.node()->value_representation(),
                ValueRepresentation::kFloat64);
    }
#endif  // DEBUG
  }

  void UnsetCanTruncateToInt32ForDeoptFrameInput(ValueNode* node) {
    // TODO(victorgomes): Technically if node is in the int32 range, this use
    // would still allow truncation.
    if (node->is_float64_or_holey_float64()) {
      node->set_can_truncate_to_int32(false);
    }
  }
};

// This pass performs the truncation optimization by replacing floating-point
// operations with their more efficient integer-based equivalents.
//
// A node is truncated if, and only if, both of these conditions are met:
//  1. It is marked with the `CanTruncateToInt32` flag.
//  2. All of its inputs have already been converted/truncated to int32.
class TruncationProcessor {
 public:
  explicit TruncationProcessor(Graph* graph) : graph_(graph) {}

  void PreProcessGraph(Graph* graph) {}
  void PostProcessBasicBlock(BasicBlock* block) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}
  void PostProcessGraph(Graph* graph) {}

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    return ProcessResult::kContinue;
  }

#define PROCESS_BINOP(Op)                                                  \
  ProcessResult Process(Float64##Op* node, const ProcessingState& state) { \
    ProcessFloat64BinaryOp<Int32##Op>(node);                               \
    return ProcessResult::kContinue;                                       \
  }
  PROCESS_BINOP(Add)
  PROCESS_BINOP(Subtract)
  PROCESS_BINOP(Multiply)
  PROCESS_BINOP(Divide)
#undef PROCESS_BINOP

  ProcessResult Process(CheckedTruncateFloat64ToInt32* node,
                        const ProcessingState& state);
  ProcessResult Process(TruncateFloat64ToInt32* node,
                        const ProcessingState& state);
  ProcessResult Process(UnsafeTruncateFloat64ToInt32* node,
                        const ProcessingState& state);

 private:
  Graph* graph_;

  bool AllInputsAreValid(ValueNode* node);
  ValueNode* GetUnwrappedInput(ValueNode* node, int index);
  void UnwrapInputs(ValueNode* node);

  template <typename NodeT>
  void ProcessFloat64BinaryOp(ValueNode* node) {
    if (!node->can_truncate_to_int32() || !AllInputsAreValid(node)) return;
    UnwrapInputs(node);
    node->OverwriteWith<NodeT>();
  }

  ValueNode* GetTruncatedInt32Constant(double constant);

  bool is_tracing_enabled() { return graph_->is_tracing_enabled(); }
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_TRUNCATION_H_
