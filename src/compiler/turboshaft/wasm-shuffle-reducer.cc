// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/wasm-shuffle-reducer.h"

namespace v8::internal::compiler::turboshaft {

void DemandedElementAnalysis::AddUnaryOp(const Simd128UnaryOp& unop,
                                         LaneBitSet lanes) {
  if (Visited(&unop)) return;
  visited_.insert(&unop);

  const Operation& input = input_graph().Get(unop.input());
  if (!input.saturated_use_count.IsOne()) {
    return;
  }
  // TODO(sparker): Add floating-point conversions:
  // - PromoteLow
  // - ConvertLow
  static constexpr std::array low_half_ops = {
      Simd128UnaryOp::Kind::kI16x8SConvertI8x16Low,
      Simd128UnaryOp::Kind::kI16x8UConvertI8x16Low,
      Simd128UnaryOp::Kind::kI32x4SConvertI16x8Low,
      Simd128UnaryOp::Kind::kI32x4UConvertI16x8Low,
      Simd128UnaryOp::Kind::kI64x2SConvertI32x4Low,
      Simd128UnaryOp::Kind::kI64x2UConvertI32x4Low,
  };

  for (auto const kind : low_half_ops) {
    if (kind == unop.kind) {
      DCHECK(lanes == k8x16 || lanes == k8x8Low || lanes == k8x4Low);
      lanes >>= lanes.count() / 2;
      RecordOp(&input, lanes);
      return;
    }
  }
}

void DemandedElementAnalysis::AddBinaryOp(const Simd128BinopOp& binop,
                                          LaneBitSet lanes) {
  if (Visited(&binop)) return;
  visited_.insert(&binop);

  static constexpr std::array low_half_ops = {
      Simd128BinopOp::Kind::kI16x8ExtMulLowI8x16S,
      Simd128BinopOp::Kind::kI16x8ExtMulLowI8x16U,
      Simd128BinopOp::Kind::kI32x4ExtMulLowI16x8S,
      Simd128BinopOp::Kind::kI32x4ExtMulLowI16x8U,
      Simd128BinopOp::Kind::kI64x2ExtMulLowI32x4S,
      Simd128BinopOp::Kind::kI64x2ExtMulLowI32x4U,
  };
  const Operation& left = input_graph().Get(binop.left());
  const Operation& right = input_graph().Get(binop.right());
  for (auto const& kind : low_half_ops) {
    if (kind == binop.kind) {
      DCHECK(lanes == k8x16 || lanes == k8x8Low);
      lanes >>= lanes.count() / 2;
      if (left.saturated_use_count.IsOne()) {
        RecordOp(&left, lanes);
      }
      if (right.saturated_use_count.IsOne()) {
        RecordOp(&right, lanes);
      }
    }
  }
}

void DemandedElementAnalysis::RecordOp(const Operation* op, LaneBitSet lanes) {
  if (auto* unop = op->TryCast<Simd128UnaryOp>()) {
    AddUnaryOp(*unop, lanes);
  } else if (auto* binop = op->TryCast<Simd128BinopOp>()) {
    AddBinaryOp(*binop, lanes);
  } else if (auto* shuffle = op->TryCast<Simd128ShuffleOp>()) {
    demanded_elements_.emplace_back(shuffle, lanes);
  }
}

void WasmShuffleAnalyzer::Run() {
  for (uint32_t processed = input_graph().block_count(); processed > 0;
       --processed) {
    BlockIndex block_index = static_cast<BlockIndex>(processed - 1);
    const Block& block = input_graph().Get(block_index);
    auto idx_range = input_graph().OperationIndices(block);
    for (auto it = idx_range.rbegin(); it != idx_range.rend(); ++it) {
      const Operation& op = input_graph().Get(*it);
      Process(op);
    }
  }
}

void WasmShuffleAnalyzer::Process(const Operation& op) {
  if (ShouldSkipOperation(op)) {
    return;
  }

  if (auto* unop = op.TryCast<Simd128UnaryOp>()) {
    ProcessUnary(*unop);
    return;
  }

  if (auto* binop = op.TryCast<Simd128BinopOp>()) {
    ProcessBinary(*binop);
    return;
  }

  if (auto* shuffle_op = op.TryCast<Simd128ShuffleOp>()) {
    ProcessShuffle(*shuffle_op);
    return;
  }
}

void WasmShuffleAnalyzer::ProcessUnary(const Simd128UnaryOp& unop) {
  demanded_element_analysis.AddUnaryOp(unop, DemandedElementAnalysis::k8x16);
}

void WasmShuffleAnalyzer::ProcessBinary(const Simd128BinopOp& binop) {
  demanded_element_analysis.AddBinaryOp(binop, DemandedElementAnalysis::k8x16);
}

void WasmShuffleAnalyzer::ProcessShuffleOfShuffle(
    const Simd128ShuffleOp& shuffle_op, const Simd128ShuffleOp& shuffle,
    uint8_t lower_limit, uint8_t upper_limit) {
  // Suppose we have two 16-byte shuffles:
  // |---a1---|---b3---|--------|--------|  shuffle_op = (a, b)
  //
  // |---a1---|---b3---|---c?---|---c?---|  shuffle = (shf0, c)
  //
  // As only half of the shf0 is used, it means that half the work of shf0 is
  // wasted so, here, we try to reduce shf0 to a more narrow kind. In the case
  // above we can simply truncate shf0.shuffle but there are other situations
  // which involve more work:
  //
  // In the following case, shf0.shuffle needs to be shifted left so that it
  // writes the required lanes to the low half of the result. This then means
  // that shf1.shuffle needs to be updated to read from the low half.
  //
  // |--------|--------|---a1---|---b3---|  shuffle_op = (a, b)
  //
  // |---a1---|---b3---|---c?---|---c?---|  shuffle = (shf0, c)
  //

  struct ShuffleHelper {
    explicit ShuffleHelper(const uint8_t* shuffle) : shuffle(shuffle) {}

    const uint8_t* begin() const { return shuffle; }

    const uint8_t* midpoint() const {
      constexpr size_t half_lanes = kSimd128Size / 2;
      return shuffle + half_lanes;
    }

    const uint8_t* end() const { return shuffle + kSimd128Size; }

    const uint8_t* shuffle;
  };

  ShuffleHelper view(shuffle.shuffle);

  // Test whether the low half of the shuffle is within the inclusive range.
  auto all_low_half = [&view](uint8_t lower_limit, uint8_t upper_limit) {
    return std::all_of(view.begin(), view.midpoint(),
                       [lower_limit, upper_limit](uint8_t i) {
                         return i >= lower_limit && i <= upper_limit;
                       });
  };
  // Test whether the high half of the shuffle is within the inclusive range.
  auto all_high_half = [&view](uint8_t lower_limit, uint8_t upper_limit) {
    return std::all_of(view.midpoint(), view.end(),
                       [lower_limit, upper_limit](uint8_t i) {
                         return i >= lower_limit && i <= upper_limit;
                       });
  };
  // Test whether none of the low half of the shuffle contains lanes within the
  // inclusive range.
  auto none_low_half = [&view](uint8_t lower_limit, uint8_t upper_limit) {
    return std::none_of(view.begin(), view.midpoint(),
                        [lower_limit, upper_limit](uint8_t i) {
                          return i >= lower_limit && i <= upper_limit;
                        });
  };
  // Test whether none of the high half of the shuffle contains lanes within the
  // inclusive range.
  auto none_high_half = [&view](uint8_t lower_limit, uint8_t upper_limit) {
    return std::none_of(view.midpoint(), view.end(),
                        [lower_limit, upper_limit](uint8_t i) {
                          return i >= lower_limit && i <= upper_limit;
                        });
  };

  // lower_ and upper_limit and set from the caller depending on whether we're
  // examining the left or right operand of shuffle. So, here we check if
  // shuffle_op is being exclusively shuffled into the low or high half using
  // either the lower and upper limits of {0,15} or {16,31}.
  bool shf_into_low_half = all_low_half(lower_limit, upper_limit) &&
                           none_high_half(lower_limit, upper_limit);
  bool shf_into_high_half = all_high_half(lower_limit, upper_limit) &&
                            none_low_half(lower_limit, upper_limit);
  DCHECK(!(shf_into_low_half && shf_into_high_half));

  constexpr size_t quarter_lanes = kSimd128Size / 4;
  if (shf_into_low_half) {
    if (all_low_half(lower_limit + quarter_lanes, upper_limit)) {
      // Low half of shuffle is sourced from the high half of shuffle_op.
      demanded_element_analysis.RecordOp(&shuffle_op,
                                         DemandedElementAnalysis::k8x8Low);
      shift_shuffles_.push_back(&shuffle_op);
      low_half_shuffles_.push_back(&shuffle);
    } else if (all_low_half(lower_limit, upper_limit - quarter_lanes)) {
      // Low half of shuffle is sourced from the low half of shuffle_op.
      demanded_element_analysis.RecordOp(&shuffle_op,
                                         DemandedElementAnalysis::k8x8Low);
    }
  } else if (shf_into_high_half) {
    if (all_high_half(lower_limit + quarter_lanes, upper_limit)) {
      // High half of shuffle is sourced from the high half of shuffle_op.
      demanded_element_analysis.RecordOp(&shuffle_op,
                                         DemandedElementAnalysis::k8x8Low);
      shift_shuffles_.push_back(&shuffle_op);
      high_half_shuffles_.push_back(&shuffle);
    } else if (all_high_half(lower_limit, upper_limit - quarter_lanes)) {
      // High half of shuffle is sourced from the low half of shuffle_op.
      demanded_element_analysis.RecordOp(&shuffle_op,
                                         DemandedElementAnalysis::k8x8Low);
    }
  }
}

void WasmShuffleAnalyzer::ProcessShuffle(const Simd128ShuffleOp& shuffle) {
  if (shuffle.kind != Simd128ShuffleOp::Kind::kI8x16) {
    return;
  }
  const Operation& left = input_graph().Get(shuffle.left());
  const Operation& right = input_graph().Get(shuffle.right());

  auto* shuffle_left = left.TryCast<Simd128ShuffleOp>();
  auto* shuffle_right = right.TryCast<Simd128ShuffleOp>();
  if (!shuffle_left && !shuffle_right) {
    return;
  }
  constexpr uint8_t left_lower = 0;
  constexpr uint8_t left_upper = 15;
  constexpr uint8_t right_lower = 16;
  constexpr uint8_t right_upper = 31;
  if (shuffle_left && shuffle_left->kind == Simd128ShuffleOp::Kind::kI8x16 &&
      shuffle_left->saturated_use_count.IsOne()) {
    ProcessShuffleOfShuffle(*shuffle_left, shuffle, left_lower, left_upper);
  }
  if (shuffle_right && shuffle_right->kind == Simd128ShuffleOp::Kind::kI8x16 &&
      shuffle_right->saturated_use_count.IsOne()) {
    ProcessShuffleOfShuffle(*shuffle_right, shuffle, right_lower, right_upper);
  }
}

}  // namespace v8::internal::compiler::turboshaft
