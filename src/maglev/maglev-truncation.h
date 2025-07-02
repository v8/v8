// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_TRUNCATION_H_
#define V8_MAGLEV_MAGLEV_TRUNCATION_H_

#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph.h"

namespace v8 {
namespace internal {
namespace maglev {

// The MaglevTruncationProcessor is an optimization pass that replaces
// floating-point operations with more efficient integer-based equivalents.
// It inspects the inputs of bitwise operations, which implicitly truncate
// their operands to 32-bit integers. If a floating-point input node (e.g.,
// Float64Add) can be proven to produce an integer-representable value, this
// pass replaces it with its integer counterpart (e.g., Int32Add), thus
// avoiding expensive floating-point arithmetic and conversions.
class MaglevTruncationProcessor {
 public:
  explicit MaglevTruncationProcessor(Graph* graph) : graph_(graph) {}

  constexpr static int kMaxInteger64Log2 = 64;
  constexpr static int kMaxSafeIntegerLog2 = 53;

  void PreProcessGraph(Graph* graph) {}
  void PostProcessBasicBlock(BasicBlock* block) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    return ProcessResult::kContinue;
  }

#define BITWISE_BINOP_PROCESS(Name)                                 \
  ProcessResult Process(Name* node, const ProcessingState& state) { \
    TruncateInput(node, 0);                                         \
    TruncateInput(node, 1);                                         \
    return ProcessResult::kContinue;                                \
  }
  BITWISE_BINOP_PROCESS(Int32BitwiseAnd)
  BITWISE_BINOP_PROCESS(Int32BitwiseOr)
  BITWISE_BINOP_PROCESS(Int32BitwiseXor)
  BITWISE_BINOP_PROCESS(Int32ShiftLeft)
  BITWISE_BINOP_PROCESS(Int32ShiftRight)
  BITWISE_BINOP_PROCESS(Int32ShiftRightLogical)
#undef BITWISE_BINOP_PROCESS

  ProcessResult Process(Int32BitwiseNot* node, const ProcessingState& state) {
    TruncateInput(node, 0);
    return ProcessResult::kContinue;
  }

  void PostProcessGraph(Graph* graph) {}

 private:
  Graph* graph_;

  void TruncateInput(ValueNode* node, int index);
  void UnsafeTruncateInput(ValueNode* node, int index);

  // TODO(victorgomes): CanTruncate could be calculated during graph building.
  bool CanTruncate(ValueNode* node);
  ValueNode* Truncate(ValueNode* node);

  bool IsIntN(ValueNode* node, int nbits);
  bool IsIntN(double value, int nbits);

  template <typename NodeT>
  ValueNode* OverwriteWith(ValueNode* node);

  ValueNode* GetTruncatedInt32Constant(double constant);
  Tagged<Object> GetRootConstant(ValueNode* node);
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_TRUNCATION_H_
