// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_TURBOSHAFT_WASM_SHUFFLE_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_WASM_SHUFFLE_REDUCER_H_

#include <optional>

#include "src/base/template-utils.h"
#include "src/builtins/builtins.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/utils.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// The aim of this reducer is to reduce the size of shuffles, by looking at
// what elements are required and we do this by looking at their users:
// - Simd128UnaryOp ConvertLow ops
// - Simd128BinaryOp ExtMulLow ops
// - Simd128ShuffleOps
// If a shuffle is only used by an operation which only reads the low half of
// shuffle input, then we can reduce the shuffle to one which shuffles fewer
// bytes. When multiple ConvertLow and/or ExtMulLow are chained, then the
// required width of the shuffle can be further reduced.
// If a shuffle is only used by a shuffle which only uses half of a shuffle
// input, that input shuffle can also reduced.

// Used by the analysis to search back from uses to their defs, looking for
// shuffles that could be reduced.
class DemandedElementAnalysis {
 public:
  static constexpr uint16_t k8x16 = 0xFFFF;
  static constexpr uint16_t k8x8Low = 0xFF;
  static constexpr uint16_t k8x4Low = 0xF;
  static constexpr uint16_t k8x2Low = 0x3;

  using LaneBitSet = std::bitset<16>;
  using DemandedElementMap =
      ZoneVector<std::pair<const Operation*, LaneBitSet>>;

  DemandedElementAnalysis(Zone* phase_zone, const Graph& input_graph)
      : phase_zone_(phase_zone), input_graph_(input_graph) {}

  void AddUnaryOp(const Simd128UnaryOp& unop, LaneBitSet lanes);
  void AddBinaryOp(const Simd128BinopOp& binop, LaneBitSet lanes);
  void RecordOp(const Operation* op, LaneBitSet lanes);

  const DemandedElementMap& demanded_elements() const {
    return demanded_elements_;
  }

  const Graph& input_graph() const { return input_graph_; }

  bool Visited(const Operation* op) const { return visited_.count(op); }

 private:
  Zone* phase_zone_;
  const Graph& input_graph_;
  DemandedElementMap demanded_elements_{phase_zone_};
  ZoneUnorderedSet<const Operation*> visited_{phase_zone_};
};

class WasmShuffleAnalyzer {
 public:
  WasmShuffleAnalyzer(Zone* phase_zone, const Graph& input_graph)
      : phase_zone_(phase_zone), input_graph_(input_graph) {
    Run();
  }

  V8_EXPORT_PRIVATE void Run();

  void Process(const Operation& op);
  void ProcessUnary(const Simd128UnaryOp& unop);
  void ProcessBinary(const Simd128BinopOp& binop);
  void ProcessShuffle(const Simd128ShuffleOp& shuffle_op);
  void ProcessShuffleOfShuffle(const Simd128ShuffleOp& shuffle_op,
                               const Simd128ShuffleOp& shuffle,
                               uint8_t lower_limit, uint8_t upper_limit);
  bool ShouldReduce() const {
    return !demanded_element_analysis.demanded_elements().empty();
  }

  const DemandedElementAnalysis::DemandedElementMap& ops_to_reduce() const {
    return demanded_element_analysis.demanded_elements();
  }

  std::optional<DemandedElementAnalysis::LaneBitSet> DemandedByteLanes(
      const Operation* op) const {
    for (auto const& [narrow_op, lanes] : ops_to_reduce()) {
      if (op == narrow_op) {
        return lanes;
      }
    }
    return {};
  }

  // Is only the top half (lanes 8...15) of the result of shuffle required?
  // If so shuffle will need to be modified so that it writes the designed data
  // into the low half lanes instead.
  bool ShouldRewriteShuffleToLow(const Simd128ShuffleOp* shuffle) const {
    for (auto shift_shuffle : shift_shuffles_) {
      if (shift_shuffle == shuffle) {
        return true;
      }
    }
    return false;
  }

#ifdef DEBUG
  bool ShouldRewriteShuffleToLow(OpIndex op) const {
    return ShouldRewriteShuffleToLow(
        &input_graph().Get(op).Cast<Simd128ShuffleOp>());
  }
#endif

  // Is the low half (lanes 0...7) result of shuffle coming exclusively from
  // the high half of one of its operands.
  bool DoesShuffleIntoLowHalf(const Simd128ShuffleOp* shuffle) const {
    for (auto half_shuffle : low_half_shuffles_) {
      if (half_shuffle == shuffle) {
        return true;
      }
    }
    return false;
  }

  // Is the high half (lanes: 8...15) result of shuffle coming exclusively from
  // the high half of its operands.
  bool DoesShuffleIntoHighHalf(const Simd128ShuffleOp* shuffle) const {
    for (auto half_shuffle : high_half_shuffles_) {
      if (half_shuffle == shuffle) {
        return true;
      }
    }
    return false;
  }

  const Graph& input_graph() const { return input_graph_; }

 private:
  Zone* phase_zone_;
  const Graph& input_graph_;
  DemandedElementAnalysis demanded_element_analysis{phase_zone_, input_graph_};
  SmallZoneVector<const Simd128ShuffleOp*, 8> shift_shuffles_{phase_zone_};
  SmallZoneVector<const Simd128ShuffleOp*, 8> low_half_shuffles_{phase_zone_};
  SmallZoneVector<const Simd128ShuffleOp*, 8> high_half_shuffles_{phase_zone_};
};

template <class Next>
class WasmShuffleReducer : public Next {
 private:
  std::optional<WasmShuffleAnalyzer> analyzer_;

 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(WasmShuffleReducer)

  void Analyze() {
    analyzer_.emplace(__ phase_zone(), __ input_graph());
    analyzer_->Run();
    Next::Analyze();
  }

  OpIndex REDUCE_INPUT_GRAPH(Simd128Shuffle)(OpIndex ig_index,
                                             const Simd128ShuffleOp& shuffle) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphSimd128Shuffle(ig_index, shuffle);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    if (shuffle.kind != Simd128ShuffleOp::Kind::kI8x16) goto no_change;

    auto og_left = __ MapToNewGraph(shuffle.left());
    auto og_right = __ MapToNewGraph(shuffle.right());
    std::array<uint8_t, kSimd128Size> shuffle_bytes = {0};
    std::copy(shuffle.shuffle, shuffle.shuffle + kSimd128Size,
              shuffle_bytes.begin());
    constexpr size_t half_lanes = kSimd128Size / 2;

    bool does_shuffle_into_low_half =
        analyzer_->DoesShuffleIntoLowHalf(&shuffle);
    bool does_shuffle_into_high_half =
        analyzer_->DoesShuffleIntoHighHalf(&shuffle);

    // Shuffles to adjust because one, or both, of their inputs have been
    // narrowed.
    if (does_shuffle_into_low_half && does_shuffle_into_high_half) {
      DCHECK(analyzer_->ShouldRewriteShuffleToLow(shuffle.left()));
      DCHECK(analyzer_->ShouldRewriteShuffleToLow(shuffle.right()));
      // We have a shuffle where both inputs have been reduced and shifted, so
      // something like this:
      // |--------|--------|---a1---|---b3---|  shf0 = (a, b)
      //
      // |--------|--------|---c2---|---d4---|  shf1 = (c, d)
      //
      // |---a1---|---b3---|---c2---|---d4---|  shf2 = (shf0, shf1)
      //
      // Is being changed into this:
      // |---a1---|---b3---|--------|--------|  shf0 = (a, b)
      //
      // |---c2---|---d4---|--------|--------|  shf1 = (c, d)
      //
      // |---a1---|---b3---|---c2---|---d4---|  shf2 = (shf0, shf1)
      std::transform(shuffle_bytes.begin(), shuffle_bytes.end(),
                     shuffle_bytes.begin(),
                     [](uint8_t lane) { return lane - half_lanes; });
    } else if (does_shuffle_into_low_half) {
      DCHECK(analyzer_->ShouldRewriteShuffleToLow(shuffle.left()) ||
             analyzer_->ShouldRewriteShuffleToLow(shuffle.right()));
      DCHECK_NE(analyzer_->ShouldRewriteShuffleToLow(shuffle.left()),
                analyzer_->ShouldRewriteShuffleToLow(shuffle.right()));
      // We have a shuffle where both inputs have been reduced and one has
      // been shifted, so something like this:
      // |--------|--------|---a1---|---b3---|  shf0 = (a, b)
      //
      // |---c2---|---d4---|--------|--------|  shf1 = (c, d)
      //
      // |---a1---|---b3---|---c2---|---d4---|  shf2 = (shf0, shf1)
      //
      // Is being changed into this:
      // |---a1---|---b3---|--------|--------|  shf0 = (a, b)
      //
      // |---c2---|---d4---|--------|--------|  shf1 = (c, d)
      //
      // |---a1---|---b3---|---c2---|---d4---|  shf2 = (shf0, shf1)
      //
      // Original shf2 lane-wise shuffle: [2, 3, 4, 5]
      // Needs to be converted to: [0, 1, 4, 5]
      std::transform(shuffle_bytes.begin(), shuffle_bytes.begin() + half_lanes,
                     shuffle_bytes.begin(),
                     [](uint8_t lane) { return lane - half_lanes; });
    } else if (does_shuffle_into_high_half) {
      DCHECK(analyzer_->ShouldRewriteShuffleToLow(shuffle.left()) ||
             analyzer_->ShouldRewriteShuffleToLow(shuffle.right()));
      DCHECK_NE(analyzer_->ShouldRewriteShuffleToLow(shuffle.left()),
                analyzer_->ShouldRewriteShuffleToLow(shuffle.right()));
      // We have a shuffle where both inputs have been reduced and one has
      // been shifted, so something like this:
      // |---a1---|---b3---|--------|--------|  shf0 = (a, b)
      //
      // |--------|--------|---c2---|---d4---|  shf1 = (c, d)
      //
      // |---a1---|---b3---|---c2---|---d4---|  shf2 = (shf0, shf1)
      //
      // Is being changed into this:
      // |---a1---|---b3---|--------|--------|  shf0 = (a, b)
      //
      // |---c2---|---d4---|--------|--------|  shf1 = (c, d)
      //
      // |---a1---|---b3---|---c2---|---d4---|  shf2 = (shf0, shf1)
      std::transform(shuffle_bytes.begin() + half_lanes, shuffle_bytes.end(),
                     shuffle_bytes.begin() + half_lanes,
                     [](uint8_t lane) { return lane - half_lanes; });
    }

    if (does_shuffle_into_low_half || does_shuffle_into_high_half) {
      return __ Simd128Shuffle(og_left, og_right,
                               Simd128ShuffleOp::Kind::kI8x16,
                               shuffle_bytes.data());
    }

    // Shuffles to narrow.
    if (auto maybe_lanes = analyzer_->DemandedByteLanes(&shuffle);
        maybe_lanes.has_value()) {
      auto lanes = maybe_lanes.value();
      if (analyzer_->ShouldRewriteShuffleToLow(&shuffle)) {
        DCHECK_EQ(lanes, DemandedElementAnalysis::k8x8Low);
        // Take the top half of the shuffle bytes and these will now write
        // those values into the low half of the result instead.
        std::copy(shuffle.shuffle + half_lanes, shuffle.shuffle + kSimd128Size,
                  shuffle_bytes.begin());
      } else {
        // Just truncate the lower half.
        std::copy(shuffle.shuffle, shuffle.shuffle + half_lanes,
                  shuffle_bytes.begin());
      }

      if (lanes == DemandedElementAnalysis::k8x2Low) {
        return __ Simd128Shuffle(og_left, og_right,
                                 Simd128ShuffleOp::Kind::kI8x2,
                                 shuffle_bytes.data());
      } else if (lanes == DemandedElementAnalysis::k8x4Low) {
        return __ Simd128Shuffle(og_left, og_right,
                                 Simd128ShuffleOp::Kind::kI8x4,
                                 shuffle_bytes.data());
      } else if (lanes == DemandedElementAnalysis::k8x8Low) {
        return __ Simd128Shuffle(og_left, og_right,
                                 Simd128ShuffleOp::Kind::kI8x8,
                                 shuffle_bytes.data());
      }
    }
    goto no_change;
  }
};

}  // namespace v8::internal::compiler::turboshaft

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

#endif  // V8_COMPILER_TURBOSHAFT_WASM_SHUFFLE_REDUCER_H_
