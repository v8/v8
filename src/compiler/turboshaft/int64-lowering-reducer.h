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
        case WordBinopOp::Kind::kSub:
          return ReducePairBinOp(left, right, Word32PairBinopOp::Kind::kSub);
        case WordBinopOp::Kind::kMul:
          return ReducePairBinOp(left, right, Word32PairBinopOp::Kind::kMul);
        case WordBinopOp::Kind::kBitwiseAnd:
          return ReduceBitwiseAnd(left, right);
        case WordBinopOp::Kind::kBitwiseOr:
          return ReduceBitwiseOr(left, right);
        case WordBinopOp::Kind::kBitwiseXor:
          return ReduceBitwiseXor(left, right);
        default:
          break;
      }
    }
    return Next::ReduceWordBinop(left, right, kind, rep);
  }

  OpIndex REDUCE(Shift)(OpIndex left, OpIndex right, ShiftOp::Kind kind,
                        WordRepresentation rep) {
    if (rep == WordRepresentation::Word64()) {
      switch (kind) {
        case ShiftOp::Kind::kShiftLeft:
          return ReducePairBinOp(left, right,
                                 Word32PairBinopOp::Kind::kShiftLeft);
        case ShiftOp::Kind::kShiftRightArithmetic:
          return ReducePairBinOp(
              left, right, Word32PairBinopOp::Kind::kShiftRightArithmetic);
        case ShiftOp::Kind::kShiftRightLogical:
          return ReducePairBinOp(left, right,
                                 Word32PairBinopOp::Kind::kShiftRightLogical);
        case ShiftOp::Kind::kRotateRight:
          return ReduceRotateRight(left, right);
        default:
          break;
      }
    }
    return Next::ReduceShift(left, right, kind, rep);
  }

  OpIndex REDUCE(Equal)(OpIndex left, OpIndex right,
                        RegisterRepresentation rep) {
    if (rep != WordRepresentation::Word64()) {
      return Next::ReduceEqual(left, right, rep);
    }

    auto [left_low, left_high] = Unpack(left);
    auto [right_low, right_high] = Unpack(right);
    // TODO(wasm): Use explicit comparisons and && here?
    return __ Word32Equal(
        __ Word32BitwiseOr(__ Word32BitwiseXor(left_low, right_low),
                           __ Word32BitwiseXor(left_high, right_high)),
        __ Word32Constant(0));
  }

  OpIndex REDUCE(Comparison)(OpIndex left, OpIndex right,
                             ComparisonOp::Kind kind,
                             RegisterRepresentation rep) {
    if (rep != WordRepresentation::Word64()) {
      return Next::ReduceComparison(left, right, kind, rep);
    }

    auto [left_low, left_high] = Unpack(left);
    auto [right_low, right_high] = Unpack(right);
    OpIndex high_comparison;
    OpIndex low_comparison;
    switch (kind) {
      case ComparisonOp::Kind::kSignedLessThan:
        high_comparison = __ Int32LessThan(left_high, right_high);
        low_comparison = __ Uint32LessThan(left_low, right_low);
        break;
      case ComparisonOp::Kind::kSignedLessThanOrEqual:
        high_comparison = __ Int32LessThan(left_high, right_high);
        low_comparison = __ Uint32LessThanOrEqual(left_low, right_low);
        break;
      case ComparisonOp::Kind::kUnsignedLessThan:
        high_comparison = __ Uint32LessThan(left_high, right_high);
        low_comparison = __ Uint32LessThan(left_low, right_low);
        break;
      case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
        high_comparison = __ Uint32LessThan(left_high, right_high);
        low_comparison = __ Uint32LessThanOrEqual(left_low, right_low);
        break;
    }

    return __ Word32BitwiseOr(
        high_comparison,
        __ Word32BitwiseAnd(__ Word32Equal(left_high, right_high),
                            low_comparison));
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
    if (const TupleOp* tuple = Asm().template TryCast<TupleOp>(input)) {
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

  OpIndex ReduceBitwiseAnd(OpIndex left, OpIndex right) {
    auto [left_low, left_high] = Unpack(left);
    auto [right_low, right_high] = Unpack(right);
    OpIndex low_result = __ Word32BitwiseAnd(left_low, right_low);
    OpIndex high_result = __ Word32BitwiseAnd(left_high, right_high);
    return __ Tuple(low_result, high_result);
  }

  OpIndex ReduceBitwiseOr(OpIndex left, OpIndex right) {
    auto [left_low, left_high] = Unpack(left);
    auto [right_low, right_high] = Unpack(right);
    OpIndex low_result = __ Word32BitwiseOr(left_low, right_low);
    OpIndex high_result = __ Word32BitwiseOr(left_high, right_high);
    return __ Tuple(low_result, high_result);
  }

  OpIndex ReduceBitwiseXor(OpIndex left, OpIndex right) {
    auto [left_low, left_high] = Unpack(left);
    auto [right_low, right_high] = Unpack(right);
    OpIndex low_result = __ Word32BitwiseXor(left_low, right_low);
    OpIndex high_result = __ Word32BitwiseXor(left_high, right_high);
    return __ Tuple(low_result, high_result);
  }

  OpIndex ReduceRotateRight(OpIndex left, OpIndex right) {
    // This reducer assumes that all rotates are mapped to rotate right.
    DCHECK(!SupportedOperations::word64_rol());
    auto [left_low, left_high] = Unpack(left);
    // We can safely ignore the high word of the shift (as it encodes a multiple
    // of 64).
    OpIndex shift = Unpack(right).first;
    uint32_t constant_shift = 0;

    if (Asm().MatchWord32Constant(shift, &constant_shift)) {
      // Precondition: 0 <= shift < 64.
      uint32_t shift_value = constant_shift & 0x3F;
      if (shift_value == 0) {
        // No-op, return original tuple.
        return left;
      }
      if (shift_value == 32) {
        // Swap low and high of left.
        return __ Tuple(left_high, left_low);
      }

      OpIndex low_input = left_high;
      OpIndex high_input = left_low;
      if (shift_value < 32) {
        low_input = left_low;
        high_input = left_high;
      }

      uint32_t masked_shift_value = shift_value & 0x1F;
      OpIndex masked_shift = __ Word32Constant(masked_shift_value);
      OpIndex inv_shift = __ Word32Constant(32 - masked_shift_value);

      OpIndex low_node = __ Word32BitwiseOr(
          __ Word32ShiftRightLogical(low_input, masked_shift),
          __ Word32ShiftLeft(high_input, inv_shift));
      OpIndex high_node = __ Word32BitwiseOr(
          __ Word32ShiftRightLogical(high_input, masked_shift),
          __ Word32ShiftLeft(low_input, inv_shift));
      return __ Tuple(low_node, high_node);
    }

    OpIndex safe_shift = shift;
    if (!SupportedOperations::word32_shift_is_safe()) {
      // safe_shift = shift % 32
      safe_shift = __ Word32BitwiseAnd(shift, __ Word32Constant(0x1F));
    }
    OpIndex all_bits_set = __ Word32Constant(-1);
    OpIndex inv_mask = __ Word32BitwiseXor(
        __ Word32ShiftRightLogical(all_bits_set, safe_shift), all_bits_set);
    OpIndex bit_mask = __ Word32BitwiseXor(inv_mask, all_bits_set);

    OpIndex less_than_32 = __ Int32LessThan(shift, __ Word32Constant(32));
    // The low word and the high word can be swapped either at the input or
    // at the output. We swap the inputs so that shift does not have to be
    // kept for so long in a register.
    ScopedVar<Word32> var_low(Asm(), left_high);
    ScopedVar<Word32> var_high(Asm(), left_low);
    IF (less_than_32) {
      var_low = left_low;
      var_high = left_high;
    }
    END_IF

    OpIndex rotate_low = __ Word32RotateRight(*var_low, safe_shift);
    OpIndex rotate_high = __ Word32RotateRight(*var_high, safe_shift);

    OpIndex low_node =
        __ Word32BitwiseOr(__ Word32BitwiseAnd(rotate_low, bit_mask),
                           __ Word32BitwiseAnd(rotate_high, inv_mask));
    OpIndex high_node =
        __ Word32BitwiseOr(__ Word32BitwiseAnd(rotate_high, bit_mask),
                           __ Word32BitwiseAnd(rotate_low, inv_mask));
    return __ Tuple(low_node, high_node);
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
