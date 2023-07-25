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
#include "src/compiler/wasm-compiler.h"
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
          return ReducePairShiftOp(left, right,
                                   Word32PairBinopOp::Kind::kShiftLeft);
        case ShiftOp::Kind::kShiftRightArithmetic:
          return ReducePairShiftOp(
              left, right, Word32PairBinopOp::Kind::kShiftRightArithmetic);
        case ShiftOp::Kind::kShiftRightLogical:
          return ReducePairShiftOp(left, right,
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
        0);
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

  OpIndex REDUCE(Call)(OpIndex callee, OpIndex frame_state,
                       base::Vector<const OpIndex> arguments,
                       const TSCallDescriptor* descriptor, OpEffects effects) {
    const bool is_tail_call = false;
    return ReduceCall(callee, frame_state, arguments, descriptor, effects,
                      is_tail_call);
  }

  OpIndex REDUCE(TailCall)(OpIndex callee,
                           base::Vector<const OpIndex> arguments,
                           const TSCallDescriptor* descriptor) {
    const bool is_tail_call = true;
    OpIndex frame_state = OpIndex::Invalid();
    return ReduceCall(callee, frame_state, arguments, descriptor,
                      OpEffects().CanCallAnything(), is_tail_call);
  }

  OpIndex REDUCE(Projection)(OpIndex input, uint16_t idx,
                             RegisterRepresentation rep) {
    // Update projections of call results to the updated indices for any call
    // returning at least 2 values and at least one i64.
    auto calls_entry = lowered_calls_.find(input);
    if (calls_entry != lowered_calls_.end()) {
      idx = calls_entry->second[idx];
      if (rep == RegisterRepresentation::Word64()) {
        RegisterRepresentation word32 = RegisterRepresentation::Word32();
        return __ Tuple(Next::ReduceProjection(input, idx, word32),
                        Next::ReduceProjection(input, idx + 1, word32));
      }
    }
    return Next::ReduceProjection(input, idx, rep);
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
    DCHECK_LE(parameter_index, sig_->parameter_count());
    int32_t new_index = param_index_map_[parameter_index];
    if (rep == RegisterRepresentation::Word64()) {
      rep = RegisterRepresentation::Word32();
      return __ Tuple(Next::ReduceParameter(new_index, rep),
                      Next::ReduceParameter(new_index + 1, rep));
    }
    return Next::ReduceParameter(new_index, rep, debug_name);
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

  OpIndex REDUCE(WordUnary)(OpIndex input, WordUnaryOp::Kind kind,
                            WordRepresentation rep) {
    if (rep == RegisterRepresentation::Word64()) {
      switch (kind) {
        case WordUnaryOp::Kind::kCountLeadingZeros:
          return ReduceClz(input);
        case WordUnaryOp::Kind::kCountTrailingZeros:
          return ReduceCtz(input);
        case WordUnaryOp::Kind::kPopCount:
          return ReducePopCount(input);
        default:
          UNIMPLEMENTED();
      }
    }
    return Next::ReduceWordUnary(input, kind, rep);
  }

  OpIndex REDUCE(Change)(OpIndex input, ChangeOp::Kind kind,
                         ChangeOp::Assumption assumption,
                         RegisterRepresentation from,
                         RegisterRepresentation to) {
    auto word32 = RegisterRepresentation::Word32();
    auto word64 = RegisterRepresentation::Word64();
    auto float64 = RegisterRepresentation::Float64();
    using Kind = ChangeOp::Kind;
    // TODO(mliedtke): Also support other conversions.
    if (from == word32 && to == word64) {
      if (kind == Kind::kZeroExtend) {
        return __ Tuple(input, __ Word32Constant(0));
      }
      if (kind == Kind::kSignExtend) {
        // We use SAR to preserve the sign in the high word.
        return __ Tuple(input, __ Word32ShiftRightArithmetic(input, 31));
      }
      UNIMPLEMENTED();
    }
    if (from == float64 && to == word64) {
      if (kind == Kind::kBitcast) {
        return __ Tuple(__ Float64ExtractLowWord32(input),
                        __ Float64ExtractHighWord32(input));
      }
      UNIMPLEMENTED();
    }
    if (from == word64 && to == word32 && kind == Kind::kTruncate) {
      return __ Projection(input, 0, word32);
    }
    return Next::ReduceChange(input, kind, assumption, from, to);
  }

  OpIndex REDUCE(Load)(OpIndex base_idx, OpIndex index, LoadOp::Kind kind,
                       MemoryRepresentation loaded_rep,
                       RegisterRepresentation result_rep, int32_t offset,
                       uint8_t element_scale) {
    if (loaded_rep == MemoryRepresentation::Int64()) {
      return __ Tuple(
          Next::ReduceLoad(base_idx, index, kind, MemoryRepresentation::Int32(),
                           RegisterRepresentation::Word32(), offset,
                           element_scale),
          Next::ReduceLoad(base_idx, index, kind, MemoryRepresentation::Int32(),
                           RegisterRepresentation::Word32(),
                           offset + sizeof(int32_t), element_scale));
    }
    return Next::ReduceLoad(base_idx, index, kind, loaded_rep, result_rep,
                            offset, element_scale);
  }

  OpIndex REDUCE(Store)(OpIndex base, OpIndex index, OpIndex value,
                        StoreOp::Kind kind, MemoryRepresentation stored_rep,
                        WriteBarrierKind write_barrier, int32_t offset,
                        uint8_t element_size_log2,
                        bool maybe_initializing_or_transitioning) {
    if (stored_rep == MemoryRepresentation::Int64()) {
      auto [low, high] = Unpack(value);
      return __ Tuple(
          Next::ReduceStore(base, index, low, kind,
                            MemoryRepresentation::Int32(), write_barrier,
                            offset, element_size_log2,
                            maybe_initializing_or_transitioning),
          Next::ReduceStore(base, index, high, kind,
                            MemoryRepresentation::Int32(), write_barrier,
                            offset + sizeof(int32_t), element_size_log2,
                            maybe_initializing_or_transitioning));
    }
    return Next::ReduceStore(base, index, value, kind, stored_rep,
                             write_barrier, offset, element_size_log2,
                             maybe_initializing_or_transitioning);
  }

  OpIndex REDUCE(Phi)(base::Vector<const OpIndex> inputs,
                      RegisterRepresentation rep) {
    if (rep == RegisterRepresentation::Word64()) {
      base::SmallVector<OpIndex, 8> inputs_low;
      base::SmallVector<OpIndex, 8> inputs_high;
      auto word32 = RegisterRepresentation::Word32();
      inputs_low.reserve_no_init(inputs.size());
      inputs_high.reserve_no_init(inputs.size());
      for (OpIndex input : inputs) {
        inputs_low.push_back(__ Projection(input, 0, word32));
        inputs_high.push_back(__ Projection(input, 1, word32));
      }
      return __ Tuple(Next::ReducePhi(base::VectorOf(inputs_low), word32),
                      Next::ReducePhi(base::VectorOf(inputs_high), word32));
    }
    return Next::ReducePhi(inputs, rep);
  }

 private:
  bool CheckPairOrPairOp(OpIndex input) {
#ifdef DEBUG
    if (const TupleOp* tuple = Asm().template TryCast<TupleOp>(input)) {
      DCHECK_EQ(2, tuple->input_count);
    } else if (const DidntThrowOp* didnt_throw =
                   Asm().template TryCast<DidntThrowOp>(input)) {
      // If it's a call, it must be a call that returns exactly one i64.
      // (Note that the CallDescriptor has already been lowered to [i32, i32].)
      const CallOp& call =
          Asm().Get(didnt_throw->throwing_operation()).template Cast<CallOp>();
      DCHECK_EQ(call.descriptor->descriptor->ReturnCount(), 2);
      DCHECK_EQ(call.descriptor->descriptor->GetReturnType(0),
                MachineType::Int32());
      DCHECK_EQ(call.descriptor->descriptor->GetReturnType(1),
                MachineType::Int32());
    } else {
      DCHECK(Asm().template Is<Word32PairBinopOp>(input));
    }
#endif
    return true;
  }

  std::pair<OpIndex, OpIndex> Unpack(OpIndex input) {
    DCHECK(CheckPairOrPairOp(input));
    return {__ Projection(input, 0, RegisterRepresentation::Word32()),
            __ Projection(input, 1, RegisterRepresentation::Word32())};
  }

  OpIndex ReduceClz(OpIndex input) {
    auto [low, high] = Unpack(input);
    ScopedVar<Word32> result(Asm());
    IF (__ Word32Equal(high, 0)) {
      result = __ Word32Add(32, __ Word32CountLeadingZeros(low));
    }
    ELSE {
      result = __ Word32CountLeadingZeros(high);
    }
    END_IF
    return __ Tuple(*result, __ Word32Constant(0));
  }

  OpIndex ReduceCtz(OpIndex input) {
    DCHECK(SupportedOperations::word32_ctz());
    auto [low, high] = Unpack(input);
    ScopedVar<Word32> result(Asm());
    IF (__ Word32Equal(low, 0)) {
      result = __ Word32Add(32, __ Word32CountTrailingZeros(high));
    }
    ELSE {
      result = __ Word32CountTrailingZeros(low);
    }
    END_IF
    return __ Tuple(*result, __ Word32Constant(0));
  }

  OpIndex ReducePopCount(OpIndex input) {
    DCHECK(SupportedOperations::word32_popcnt());
    auto [low, high] = Unpack(input);
    return __ Tuple(
        __ Word32Add(__ Word32PopCount(low), __ Word32PopCount(high)),
        __ Word32Constant(0));
  }

  OpIndex ReducePairBinOp(OpIndex left, OpIndex right,
                          Word32PairBinopOp::Kind kind) {
    auto [left_low, left_high] = Unpack(left);
    auto [right_low, right_high] = Unpack(right);
    return __ Word32PairBinop(left_low, left_high, right_low, right_high, kind);
  }

  OpIndex ReducePairShiftOp(OpIndex left, OpIndex right,
                            Word32PairBinopOp::Kind kind) {
    auto [left_low, left_high] = Unpack(left);
    // Note: The rhs of a 64 bit shift is a 32 bit value in turboshaft.
    OpIndex right_high = __ Word32Constant(0);
    return __ Word32PairBinop(left_low, left_high, right, right_high, kind);
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
    OpIndex shift = right;
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
      safe_shift = __ Word32BitwiseAnd(shift, 0x1F);
    }
    OpIndex all_bits_set = __ Word32Constant(-1);
    OpIndex inv_mask = __ Word32BitwiseXor(
        __ Word32ShiftRightLogical(all_bits_set, safe_shift), all_bits_set);
    OpIndex bit_mask = __ Word32BitwiseXor(inv_mask, all_bits_set);

    OpIndex less_than_32 = __ Int32LessThan(shift, 32);
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

  OpIndex ReduceCall(OpIndex callee, OpIndex frame_state,
                     base::Vector<const OpIndex> arguments,
                     const TSCallDescriptor* descriptor, OpEffects effects,
                     bool is_tail_call) {
    // Iterate over the call descriptor to skip lowering if the signature does
    // not contain an i64.
    const CallDescriptor* call_descriptor = descriptor->descriptor;
    size_t param_count = call_descriptor->ParameterCount();
    size_t i64_params = 0;
    for (size_t i = 0; i < param_count; ++i) {
      i64_params += call_descriptor->GetParameterType(i).representation() ==
                    MachineRepresentation::kWord64;
    }
    size_t return_count = call_descriptor->ReturnCount();
    size_t i64_returns = 0;
    for (size_t i = 0; i < return_count; ++i) {
      i64_returns += call_descriptor->GetReturnType(i).representation() ==
                     MachineRepresentation::kWord64;
    }
    if (i64_params + i64_returns == 0) {
      // No lowering required.
      return is_tail_call ? Next::ReduceTailCall(callee, arguments, descriptor)
                          : Next::ReduceCall(callee, frame_state, arguments,
                                             descriptor, effects);
    }

    // Create descriptor with 2 i32s for every i64.
    const CallDescriptor* lowered_descriptor =
        GetI32WasmCallDescriptor(Asm().graph_zone(), call_descriptor);

    // Map the arguments by unpacking i64 arguments (which have already been
    // lowered to Tuple(i32, i32).)
    base::SmallVector<OpIndex, 16> lowered_args;
    lowered_args.reserve_no_init(param_count + i64_params);

    DCHECK_EQ(param_count, arguments.size());
    for (size_t i = 0; i < param_count; ++i) {
      if (call_descriptor->GetParameterType(i).representation() ==
          MachineRepresentation::kWord64) {
        auto [low, high] = Unpack(arguments[i]);
        lowered_args.push_back(low);
        lowered_args.push_back(high);
      } else {
        lowered_args.push_back(arguments[i]);
      }
    }

    auto lowered_ts_descriptor = TSCallDescriptor::Create(
        lowered_descriptor, descriptor->can_throw, __ graph_zone());
    OpIndex call =
        is_tail_call
            ? Next::ReduceTailCall(callee, base::VectorOf(lowered_args),
                                   lowered_ts_descriptor)
            : Next::ReduceCall(callee, frame_state,
                               base::VectorOf(lowered_args),
                               lowered_ts_descriptor, effects);
    // If it only returns one value, there isn't any projection for the
    // different returns, so we don't need to update them. Similarly we don't
    // need to update projections if there isn't any i64 in the result types.
    if (return_count <= 1 || i64_returns == 0) {
      return call;
    }

    // Create a map from the old projection index to the new projection index,
    // so this information doesn't have to be recreated for each projection on
    // the result.
    int* result_map =
        __ phase_zone()->template AllocateArray<int>(return_count);
    int lowered_index = 0;
    for (size_t i = 0; i < return_count; ++i) {
      result_map[i] = lowered_index;
      bool is_i64 = call_descriptor->GetReturnType(i).representation() ==
                    MachineRepresentation::kWord64;
      lowered_index += is_i64 ? 2 : 1;
    }
    lowered_calls_[call] = result_map;
    DCHECK_EQ(lowered_index, return_count + i64_returns);
    return call;
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

  // Map for all call nodes which require lowering of the result projections.
  // The value is an array mapping the original projection index to the new one.
  ZoneUnorderedMap<OpIndex, int*> lowered_calls_{__ phase_zone()};
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_INT64_LOWERING_REDUCER_H_
