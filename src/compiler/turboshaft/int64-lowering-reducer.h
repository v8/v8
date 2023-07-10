// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_TURBOSHAFT_INT64_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_INT64_LOWERING_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/wasm-graph-assembler.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// This reducer is run on 32 bit platforms to lower unsupported 64 bit integer
// operations to supported 32 bit operations.
template <class Next>
class Int64LoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()

  Int64LoweringReducer() { InitializeIndexMaps(); }

  OpIndex REDUCE(WordBinop)(OpIndex left, OpIndex right, WordBinopOp::Kind kind,
                            WordRepresentation rep) {
    if (rep == WordRepresentation::Word64()) {
      switch (kind) {
        case WordBinopOp::Kind::kAdd:
          return ReducePairBinOp(left, right, Word32PairBinopOp::Kind::kAdd);
        case WordBinopOp::Kind::kMul:
          return ReducePairBinOp(left, right, Word32PairBinopOp::Kind::kMul);
        default:
          break;
      }
    }
    return Next::ReduceWordBinop(left, right, kind, rep);
  }

  OpIndex REDUCE(Constant)(ConstantOp::Kind kind, ConstantOp::Storage value) {
    if (kind == ConstantOp::Kind::kWord64) {
      uint32_t high = value.integral >> 32;
      uint32_t low = value.integral & std::numeric_limits<uint32_t>::max();
      return __ Tuple(__ Word32Constant(low), __ Word32Constant(high));
    }
    return Next::ReduceConstant(kind, value);
  }

  OpIndex REDUCE(Parameter)(int32_t parameter_index, RegisterRepresentation rep,
                            const char* debug_name = "") {
    if (rep == RegisterRepresentation::Word64()) {
      rep = RegisterRepresentation::Word32();
      int32_t low_index = param_index_map_[parameter_index];
      return __ Tuple(__ Parameter(low_index, rep),
                      __ Parameter(low_index + 1, rep));
    }
    return Next::ReduceParameter(parameter_index, rep, debug_name);
  }

  OpIndex REDUCE(Return)(OpIndex pop_count,
                         base::Vector<const OpIndex> return_values) {
    if (!returns_i64_) {
      return Next::ReduceReturn(pop_count, return_values);
    }
    base::SmallVector<OpIndex, 8> lowered_values;
    for (size_t i = 0; i < sig_->return_count(); ++i) {
      if (sig_->GetReturn(i) == wasm::kWasmI64) {
        auto [low, high] = Unpack(return_values[i]);
        lowered_values.push_back(low);
        lowered_values.push_back(high);
      } else {
        lowered_values.push_back(return_values[i]);
      }
    }
    return Next::ReduceReturn(pop_count, base::VectorOf(lowered_values));
  }

 private:
  bool CheckPairOrPairOp(OpIndex input) {
    const TupleOp* tuple = Asm().template TryCast<TupleOp>(input);
    if (tuple) {
      DCHECK_EQ(2, tuple->input_count);
    } else {
      DCHECK(Asm().template Is<Word32PairBinopOp>(input));
    }
    return true;
  }

  std::pair<OpIndex, OpIndex> Unpack(OpIndex input) {
    DCHECK(CheckPairOrPairOp(input));
    return {__ Projection(input, 0, RegisterRepresentation::Word32()),
            __ Projection(input, 1, RegisterRepresentation::Word32())};
  }

  OpIndex ReducePairBinOp(OpIndex left, OpIndex right,
                          Word32PairBinopOp::Kind kind) {
    auto [left_low, left_high] = Unpack(left);
    auto [right_low, right_high] = Unpack(right);
    return __ Word32PairBinop(left_low, left_high, right_low, right_high, kind);
  }

  void InitializeIndexMaps() {
    // Add one implicit parameter in front.
    param_index_map_.push_back(0);
    int32_t new_index = 0;
    for (size_t i = 0; i < sig_->parameter_count(); ++i) {
      param_index_map_.push_back(++new_index);
      if (sig_->GetParam(i) == wasm::kWasmI64) {
        // i64 becomes [i32 low, i32 high], so the next parameter index is
        // shifted by one.
        ++new_index;
      }
    }

    // TODO(mliedtke): Use sig_.contains(wasm::kWasmI64), once it's merged.
    returns_i64_ = std::any_of(
        sig_->returns().begin(), sig_->returns().end(),
        [](const wasm::ValueType& v) { return v == wasm::kWasmI64; });
  }

  const wasm::FunctionSig* sig_ = PipelineData::Get().wasm_sig();
  Zone* zone_ = PipelineData::Get().graph_zone();
  ZoneVector<int32_t> param_index_map_{__ phase_zone()};
  bool returns_i64_ = false;  // Returns at least one i64.
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_INT64_LOWERING_REDUCER_H_
