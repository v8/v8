// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_OPERATION_MATCHING_H_
#define V8_COMPILER_TURBOSHAFT_OPERATION_MATCHING_H_

#include "src/compiler/node-matchers.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"

namespace v8::internal::compiler::turboshaft {

template <typename T>
struct MatchOrBind {
  MatchOrBind(const T& value) : value_(value) {}  // NOLINT(runtime/explicit)
  MatchOrBind(std::function<bool(const Graph*, const T&)>
                  predicate)  // NOLINT(runtime/explicit)
      : predicate_(predicate) {}
  MatchOrBind(T* bind_result = nullptr)  // NOLINT(runtime/explicit)
      : bind_result_(bind_result) {}

  bool resolve(const Graph* graph, const T& v) const {
    if (value_.has_value()) {
      return *value_ == v;
    } else if (predicate_) {
      return predicate_(graph, v);
    } else if (bind_result_) {
      *bind_result_ = v;
      return true;
    } else {
      // Wildcard.
      return true;
    }
  }

  bool MatchesWith(const Graph* graph, const T& value) const {
    return resolve(graph, value);
  }

 private:
  base::Optional<T> value_;
  std::function<bool(const Graph*, const T&)> predicate_;
  T* bind_result_ = nullptr;
};

template <class Assembler>
class OperationMatching {
 public:
  template <class Op>
  bool Is(OpIndex op_idx) {
    return assembler().output_graph().Get(op_idx).template Is<Op>();
  }

  template <class Op>
  const Op* TryCast(OpIndex op_idx) {
    return assembler().output_graph().Get(op_idx).template TryCast<Op>();
  }

  template <class Op>
  const Op& Cast(OpIndex op_idx) {
    return assembler().output_graph().Get(op_idx).template Cast<Op>();
  }

  const Operation& Get(OpIndex op_idx) {
    return assembler().output_graph().Get(op_idx);
  }

  bool MatchZero(OpIndex matched) {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    switch (op->kind) {
      case ConstantOp::Kind::kWord32:
      case ConstantOp::Kind::kWord64:
        return op->integral() == 0;
      case ConstantOp::Kind::kFloat32:
        return op->float32() == 0;
      case ConstantOp::Kind::kFloat64:
        return op->float64() == 0;
      default:
        return false;
    }
  }

  bool MatchFloat32Constant(OpIndex matched, float* constant) {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind != ConstantOp::Kind::kFloat32) return false;
    *constant = op->float32();
    return true;
  }

  bool MatchFloat64Constant(OpIndex matched, double* constant) {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind != ConstantOp::Kind::kFloat64) return false;
    *constant = op->float64();
    return true;
  }

  bool MatchFloat(OpIndex matched, double* value) {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind == ConstantOp::Kind::kFloat64) {
      *value = op->float64();
      return true;
    } else if (op->kind == ConstantOp::Kind::kFloat32) {
      *value = op->float32();
      return true;
    }
    return false;
  }

  bool MatchFloat(OpIndex matched, double value) {
    double k;
    if (!MatchFloat(matched, &k)) return false;
    return base::bit_cast<uint64_t>(value) == base::bit_cast<uint64_t>(k) ||
           (std::isnan(k) && std::isnan(value));
  }

  bool MatchNaN(OpIndex matched) {
    double k;
    return MatchFloat(matched, &k) && std::isnan(k);
  }

  bool MatchTaggedConstant(OpIndex matched, Handle<Object>* tagged) {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (!(op->kind == any_of(ConstantOp::Kind::kHeapObject,
                             ConstantOp::Kind::kCompressedHeapObject))) {
      return false;
    }
    *tagged = op->handle();
    return true;
  }

  bool MatchWordConstant(OpIndex matched, WordRepresentation rep,
                         uint64_t* unsigned_constant,
                         int64_t* signed_constant = nullptr) {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    switch (op->rep) {
      case RegisterRepresentation::Word32():
        if (rep != WordRepresentation::Word32()) return false;
        break;
      case RegisterRepresentation::Word64():
        if (!(rep == any_of(WordRepresentation::Word64(),
                            WordRepresentation::Word32()))) {
          return false;
        }
        break;
      default:
        return false;
    }
    if (unsigned_constant) {
      switch (rep.value()) {
        case WordRepresentation::Word32():
          *unsigned_constant = static_cast<uint32_t>(op->integral());
          break;
        case WordRepresentation::Word64():
          *unsigned_constant = op->integral();
          break;
      }
    }
    if (signed_constant) {
      switch (rep.value()) {
        case WordRepresentation::Word32():
          *signed_constant = static_cast<int32_t>(op->signed_integral());
          break;
        case WordRepresentation::Word64():
          *signed_constant = op->signed_integral();
          break;
      }
    }
    return true;
  }

  bool MatchWordConstant(OpIndex matched, WordRepresentation rep,
                         int64_t* signed_constant) {
    return MatchWordConstant(matched, rep, nullptr, signed_constant);
  }

  bool MatchWord64Constant(OpIndex matched, uint64_t* constant) {
    return MatchWordConstant(matched, WordRepresentation::Word64(), constant);
  }

  bool MatchWord32Constant(OpIndex matched, uint32_t* constant) {
    if (uint64_t value;
        MatchWordConstant(matched, WordRepresentation::Word32(), &value)) {
      *constant = static_cast<uint32_t>(value);
      return true;
    }
    return false;
  }

  bool MatchWord64Constant(OpIndex matched, int64_t* constant) {
    return MatchWordConstant(matched, WordRepresentation::Word64(), constant);
  }

  bool MatchWord32Constant(OpIndex matched, int32_t* constant) {
    if (int64_t value;
        MatchWordConstant(matched, WordRepresentation::Word32(), &value)) {
      *constant = static_cast<int32_t>(value);
      return true;
    }
    return false;
  }

  bool MatchChange(OpIndex matched, OpIndex* input, ChangeOp::Kind kind,
                   RegisterRepresentation from, RegisterRepresentation to) {
    const ChangeOp* op = TryCast<ChangeOp>(matched);
    if (!op || op->kind != kind || op->from != from || op->to != to) {
      return false;
    }
    *input = op->input();
    return true;
  }

  bool MatchWordBinop(OpIndex matched, OpIndex* left, OpIndex* right,
                      WordBinopOp::Kind* kind, WordRepresentation* rep) {
    const WordBinopOp* op = TryCast<WordBinopOp>(matched);
    if (!op) return false;
    *kind = op->kind;
    *rep = op->rep;
    *left = op->left();
    *right = op->right();
    return true;
  }

  bool MatchWordBinop(OpIndex matched, OpIndex* left, OpIndex* right,
                      WordBinopOp::Kind kind, WordRepresentation rep) {
    const WordBinopOp* op = TryCast<WordBinopOp>(matched);
    if (!op || kind != op->kind) {
      return false;
    }
    if (!(rep == op->rep ||
          (WordBinopOp::AllowsWord64ToWord32Truncation(kind) &&
           rep == WordRepresentation::Word32() &&
           op->rep == WordRepresentation::Word64()))) {
      return false;
    }
    *left = op->left();
    *right = op->right();
    return true;
  }

  bool MatchWordAdd(OpIndex matched, OpIndex* left, OpIndex* right,
                    WordRepresentation rep) {
    return MatchWordBinop(matched, left, right, WordBinopOp::Kind::kAdd, rep);
  }

  bool MatchWordSub(OpIndex matched, OpIndex* left, OpIndex* right,
                    WordRepresentation rep) {
    return MatchWordBinop(matched, left, right, WordBinopOp::Kind::kSub, rep);
  }

  bool MatchBitwiseAnd(OpIndex matched, OpIndex* left, OpIndex* right,
                       WordRepresentation rep) {
    return MatchWordBinop(matched, left, right, WordBinopOp::Kind::kBitwiseAnd,
                          rep);
  }

  bool MatchEqual(OpIndex matched, OpIndex* left, OpIndex* right,
                  WordRepresentation rep) {
    const EqualOp* op = TryCast<EqualOp>(matched);
    if (!op || rep != op->rep) return false;
    *left = op->left();
    *right = op->right();
    return true;
  }

  bool MatchComparison(OpIndex matched, OpIndex* left, OpIndex* right,
                       ComparisonOp::Kind* kind, RegisterRepresentation* rep) {
    const ComparisonOp* op = TryCast<ComparisonOp>(matched);
    if (!op) return false;
    *kind = op->kind;
    *rep = op->rep;
    *left = op->left();
    *right = op->right();
    return true;
  }

  bool MatchFloatUnary(OpIndex matched, OpIndex* input, FloatUnaryOp::Kind kind,
                       FloatRepresentation rep) {
    const FloatUnaryOp* op = TryCast<FloatUnaryOp>(matched);
    if (!op || op->kind != kind || op->rep != rep) return false;
    *input = op->input();
    return true;
  }

  bool MatchFloatRoundDown(OpIndex matched, OpIndex* input,
                           FloatRepresentation rep) {
    return MatchFloatUnary(matched, input, FloatUnaryOp::Kind::kRoundDown, rep);
  }

  bool MatchFloatBinary(OpIndex matched, OpIndex* left, OpIndex* right,
                        FloatBinopOp::Kind kind, FloatRepresentation rep) {
    const FloatBinopOp* op = TryCast<FloatBinopOp>(matched);
    if (!op || op->kind != kind || op->rep != rep) return false;
    *left = op->left();
    *right = op->right();
    return true;
  }

  bool MatchFloatSub(OpIndex matched, OpIndex* left, OpIndex* right,
                     FloatRepresentation rep) {
    return MatchFloatBinary(matched, left, right, FloatBinopOp::Kind::kSub,
                            rep);
  }

  bool MatchConstantShift(OpIndex matched, OpIndex* input, ShiftOp::Kind* kind,
                          WordRepresentation* rep, int* amount) {
    const ShiftOp* op = TryCast<ShiftOp>(matched);
    if (uint32_t rhs_constant;
        op && MatchWord32Constant(op->right(), &rhs_constant) &&
        rhs_constant < static_cast<uint64_t>(op->rep.bit_width())) {
      *input = op->left();
      *kind = op->kind;
      *rep = op->rep;
      *amount = static_cast<int>(rhs_constant);
      return true;
    }
    return false;
  }

  bool MatchConstantShift(OpIndex matched, OpIndex* input, ShiftOp::Kind kind,
                          WordRepresentation rep, int* amount) {
    const ShiftOp* op = TryCast<ShiftOp>(matched);
    if (uint32_t rhs_constant;
        op && op->kind == kind &&
        (op->rep == rep || (ShiftOp::AllowsWord64ToWord32Truncation(kind) &&
                            rep == WordRepresentation::Word32() &&
                            op->rep == WordRepresentation::Word64())) &&
        MatchWord32Constant(op->right(), &rhs_constant) &&
        rhs_constant < static_cast<uint64_t>(rep.bit_width())) {
      *input = op->left();
      *amount = static_cast<int>(rhs_constant);
      return true;
    }
    return false;
  }

  bool MatchConstantRightShift(OpIndex matched, OpIndex* input,
                               WordRepresentation rep, int* amount) {
    const ShiftOp* op = TryCast<ShiftOp>(matched);
    if (uint32_t rhs_constant;
        op && ShiftOp::IsRightShift(op->kind) && op->rep == rep &&
        MatchWord32Constant(op->right(), &rhs_constant) &&
        rhs_constant < static_cast<uint32_t>(rep.bit_width())) {
      *input = op->left();
      *amount = static_cast<int>(rhs_constant);
      return true;
    }
    return false;
  }

  bool MatchConstantShiftRightArithmeticShiftOutZeros(OpIndex matched,
                                                      OpIndex* input,
                                                      WordRepresentation rep,
                                                      uint16_t* amount) {
    const ShiftOp* op = TryCast<ShiftOp>(matched);
    if (uint32_t rhs_constant;
        op && op->kind == ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros &&
        op->rep == rep && MatchWord32Constant(op->right(), &rhs_constant) &&
        rhs_constant < static_cast<uint64_t>(rep.bit_width())) {
      *input = op->left();
      *amount = static_cast<uint16_t>(rhs_constant);
      return true;
    }
    return false;
  }

  class Pattern {
   public:
    static MatchOrBind<OpIndex> Constant(
        const MatchOrBind<ConstantOp::Kind>& kind,
        const MatchOrBind<ConstantOp::Storage>& storage) {
      return MatchOrBind<OpIndex>([=](const Graph* graph, const OpIndex& idx) {
        const ConstantOp* op = graph->Get(idx).TryCast<ConstantOp>();
        if (!op) return false;
        return kind.resolve(graph, op->kind) &&
               storage.resolve(graph, op->storage);
      });
    }

    static MatchOrBind<OpIndex> SignedIntegralConstant(
        const MatchOrBind<int64_t>& value) {
      return MatchOrBind<OpIndex>([=](const Graph* graph, const OpIndex& idx) {
        const ConstantOp* op = graph->Get(idx).TryCast<ConstantOp>();
        if (!op) return false;
        if (op->kind != ConstantOp::Kind::kWord32 &&
            op->kind != ConstantOp::Kind::kWord64) {
          return false;
        }
        return value.resolve(graph, op->signed_integral());
      });
    }

    static MatchOrBind<OpIndex> Load(
        const MatchOrBind<OpIndex>& base, const MatchOrBind<OpIndex>& index,
        const MatchOrBind<LoadOp::Kind>& kind,
        const MatchOrBind<MemoryRepresentation>& loaded_rep,
        const MatchOrBind<RegisterRepresentation>& result_rep,
        const MatchOrBind<uint8_t>& element_size_log2,
        const MatchOrBind<int32_t>& offset) {
      return MatchOrBind<OpIndex>([=](const Graph* graph, const OpIndex& idx) {
        const LoadOp* op = graph->Get(idx).TryCast<LoadOp>();
        if (!op) return false;
        return base.resolve(graph, op->base()) &&
               index.resolve(graph, op->index()) &&
               kind.resolve(graph, op->kind) &&
               loaded_rep.resolve(graph, op->loaded_rep) &&
               result_rep.resolve(graph, op->result_rep) &&
               element_size_log2.resolve(graph, op->element_size_log2) &&
               offset.resolve(graph, op->offset);
      });
    }

    static MatchOrBind<OpIndex> WordBinop(
        const MatchOrBind<OpIndex>& left, const MatchOrBind<OpIndex>& right,
        const MatchOrBind<WordBinopOp::Kind>& kind,
        const MatchOrBind<WordRepresentation>& rep) {
      return MatchOrBind<OpIndex>([=](const Graph* graph, const OpIndex& idx) {
        const WordBinopOp* op = graph->Get(idx).TryCast<WordBinopOp>();
        if (!op) return false;
        return left.resolve(graph, op->left()) &&
               right.resolve(graph, op->right()) &&
               kind.resolve(graph, op->kind) && rep.resolve(graph, op->rep);
      });
    }

    static bool OwnedByAddressingOperand(OpIndex node) {
      // Consider providing this. For now we just allow everything to be covered
      // regardless of other uses.
      return true;
    }

    static MatchOrBind<OpIndex> BaseWithScaledIndexAndDisplacement(
        const MatchOrBind<base::Optional<OpIndex>>& base,
        const MatchOrBind<base::Optional<OpIndex>>& index,
        const MatchOrBind<int>& scale, const MatchOrBind<int64_t>& displacement,
        const MatchOrBind<int>& displacement_mode) {
      // The BaseWithIndexAndDisplacementMatcher canonicalizes the order of
      // displacements and scale factors that are used as inputs, so instead of
      // enumerating all possible patterns by brute force, checking for node
      // clusters using the following templates in the following order suffices
      // to find all of the interesting cases (S = index * scale, B = base
      // input, D = displacement input):
      //
      // (S + (B + D))
      // (S + (B + B))
      // (S + D)
      // (S + B)
      // ((S + D) + B)
      // ((S + B) + D)
      // ((B + D) + B)
      // ((B + B) + D)
      // (B + D)
      // (B + B)

      // TODO(nicohartmann@): See if we need to support this.
      MatchOrBind<bool> power_of_two_plus_one_scale = false;

      return MatchOrBind<OpIndex>([=](const Graph* graph, const OpIndex idx) {
        OpIndex left, right;
        if (const LoadOp* load = graph->Get(idx).TryCast<LoadOp>()) {
          int32_t disp = load->offset;
          if (load->kind.tagged_base) disp -= kHeapObjectTag;
          return base.resolve(graph, load->base()) &&
                 index.resolve(graph, load->index()) &&
                 scale.resolve(graph, load->element_size_log2) &&
                 displacement.resolve(graph, disp) &&
                 displacement_mode.resolve(graph, kPositiveDisplacement);
        } else if (const StoreOp* store = graph->Get(idx).TryCast<StoreOp>()) {
          int32_t disp = store->offset;
          if (store->kind.tagged_base) disp -= kHeapObjectTag;
          return base.resolve(graph, store->base()) &&
                 index.resolve(graph, store->index()) &&
                 scale.resolve(graph, store->element_size_log2) &&
                 displacement.resolve(graph, disp) &&
                 displacement_mode.resolve(graph, kPositiveDisplacement);
        } else if (const WordBinopOp* binop =
                       graph->Get(idx).TryCast<WordBinopOp>();
                   binop && binop->kind == WordBinopOp::Kind::kAdd) {
          left = binop->left();
          right = binop->right();
        } else {
          return false;
        }

        // Check (S + ...)
        if (ScaledIndex(index, scale, power_of_two_plus_one_scale)
                .resolve(graph, left) &&
            OwnedByAddressingOperand(left)) {
          // Check (S + (... binop ...))
          if (const WordBinopOp* right_binop =
                  graph->Get(right).TryCast<WordBinopOp>()) {
            // Check (S + (B - D))
            if (right_binop->kind == WordBinopOp::Kind::kSub &&
                OwnedByAddressingOperand(right)) {
              return base.resolve(graph, right_binop->left()) &&
                     SignedIntegralConstant(displacement)
                         .resolve(graph, right_binop->right()) &&
                     displacement_mode.resolve(graph, kNegativeDisplacement);
            }
            // Check (S + (... + ...))
            if (right_binop->kind == WordBinopOp::Kind::kAdd &&
                OwnedByAddressingOperand(right)) {
              // Check (S + (B + D))
              if (SignedIntegralConstant(displacement)
                      .resolve(graph, right_binop->right()) &&
                  base.resolve(graph, right_binop->left()) &&
                  displacement_mode.resolve(graph, kPositiveDisplacement)) {
                return true;
              }
              // Check (S + (D + B))
              if (SignedIntegralConstant(displacement)
                      .resolve(graph, right_binop->left()) &&
                  base.resolve(graph, right_binop->right()) &&
                  displacement_mode.resolve(graph, kPositiveDisplacement)) {
                return true;
              }
              // Treat it as (S + B)
              return base.resolve(graph, right) &&
                     displacement.resolve(graph, 0) &&
                     displacement_mode.resolve(graph, kPositiveDisplacement);
            }
          }
          // Check (S + D)
          if (SignedIntegralConstant(displacement).resolve(graph, right) &&
              base.resolve(graph, base::nullopt) &&
              displacement_mode.resolve(graph, kPositiveDisplacement)) {
            return true;
          }
          // Treat it as (S + B)
          return base.resolve(graph, right) && displacement.resolve(graph, 0) &&
                 displacement_mode.resolve(graph, kPositiveDisplacement);
        }
        // All following cases have positive displacement mode.
        if (!displacement_mode.resolve(graph, kPositiveDisplacement)) {
          return false;
        }
        // Check ((... + ...) + ...)
        if (const WordBinopOp* left_add =
                graph->Get(left).TryCast<WordBinopOp>();
            left_add && left_add->kind == WordBinopOp::Kind::kAdd &&
            OwnedByAddressingOperand(left)) {
          // Check ((S + ...) + ...)
          if (ScaledIndex(index, scale, power_of_two_plus_one_scale)
                  .resolve(graph, left_add->left())) {
            // Check ((S + D) + B)
            if (SignedIntegralConstant(displacement)
                    .resolve(graph, left_add->right()) &&
                base.resolve(graph, right)) {
              return true;
            }
            // Check ((S + ...) + D)
            if (SignedIntegralConstant(displacement).resolve(graph, right)) {
              // Check ((S + B) + D)
              if (base.resolve(graph, left_add->right())) {
                return true;
              }
              // Treat it as (B + D)
              return index.resolve(graph, base::nullopt) &&
                     scale.resolve(graph, 0) &&
                     power_of_two_plus_one_scale.resolve(graph, false) &&
                     base.resolve(graph, left);
            }
          }
        }
        // Following cases have no scale.
        if (!scale.resolve(graph, 0) ||
            !power_of_two_plus_one_scale.resolve(graph, false)) {
          return false;
        }
        // Check (... + D)
        if (SignedIntegralConstant(displacement).resolve(graph, right)) {
          // Treat as (B + D)
          return index.resolve(graph, base::nullopt) &&
                 base.resolve(graph, left);
        }
        // Treat as (B + B) and use index as left B
        return index.resolve(graph, left) && base.resolve(graph, right);
      });
    }

    static MatchOrBind<OpIndex> ScaledIndex(
        const MatchOrBind<base::Optional<OpIndex>>& index,
        const MatchOrBind<int>& scale,
        const MatchOrBind<bool>& power_of_two_plus_one) {
      return ScaledIndex(
          MatchOrBind<OpIndex>([&](const Graph* graph, OpIndex inner_index) {
            return index.resolve(graph, inner_index);
          }),
          scale, power_of_two_plus_one);
    }

    static MatchOrBind<OpIndex> ScaledIndex(
        const MatchOrBind<OpIndex>& index, const MatchOrBind<int>& scale,
        const MatchOrBind<bool>& power_of_two_plus_one) {
      auto TryMatchScale = [=](const Operation& op, int& scale,
                               bool& power_of_two_plus_one) {
        if (!op.Is<ConstantOp>()) return false;
        const ConstantOp& constant = op.Cast<ConstantOp>();
        if (constant.kind != ConstantOp::Kind::kWord32 &&
            constant.kind != ConstantOp::Kind::kWord64) {
          return false;
        }
        uint64_t value = constant.integral();
        power_of_two_plus_one = false;
        if (value == 1) return (scale = 0), true;
        if (value == 2) return (scale = 1), true;
        if (value == 4) return (scale = 2), true;
        if (value == 8) return (scale = 3), true;
        power_of_two_plus_one = true;
        if (value == 3) return (scale = 1), true;
        if (value == 5) return (scale = 2), true;
        if (value == 9) return (scale = 3), true;
        return false;
      };

      return MatchOrBind<OpIndex>([=](const Graph* graph, const OpIndex& idx) {
        if (const WordBinopOp* binop = graph->Get(idx).TryCast<WordBinopOp>()) {
          if (binop->kind != WordBinopOp::Kind::kMul) return false;
          auto TryMatch = [&](OpIndex left, OpIndex right) {
            int scale_value;
            bool power_of_two_plus_one_value;
            if (TryMatchScale(graph->Get(right), scale_value,
                              power_of_two_plus_one_value)) {
              if (scale.resolve(graph, scale_value) &&
                  power_of_two_plus_one.resolve(graph,
                                                power_of_two_plus_one_value) &&
                  index.resolve(graph, left)) {
                return true;
              }
            }
            return false;
          };
          OpIndex left = binop->left();
          OpIndex right = binop->right();
          return TryMatch(left, right) || TryMatch(right, left);
        } else if (const ShiftOp* shift = graph->Get(idx).TryCast<ShiftOp>()) {
          if (shift->kind != ShiftOp::Kind::kShiftLeft) return false;
          const ConstantOp* constant =
              graph->Get(shift->right()).TryCast<ConstantOp>();
          if (constant == nullptr) return false;
          if (constant->kind != ConstantOp::Kind::kWord32 &&
              constant->kind != ConstantOp::Kind::kWord64) {
            return false;
          }
          uint64_t scale_value = constant->signed_integral();
          if (scale_value > 3) return false;
          return scale.resolve(graph, static_cast<int>(scale_value)) &&
                 power_of_two_plus_one.resolve(graph, false) &&
                 index.resolve(graph, shift->left());
        }
        return false;
      });
    }

    static MatchOrBind<OpIndex> Equal(
        const MatchOrBind<OpIndex>& left, const MatchOrBind<OpIndex>& right,
        const MatchOrBind<RegisterRepresentation>& rep) {
      return MatchOrBind<OpIndex>([=](const Graph* graph, const OpIndex& idx) {
        if (const EqualOp* equal = graph->Get(idx).TryCast<EqualOp>()) {
          return left.resolve(graph, equal->left()) &&
                 right.resolve(graph, equal->right()) &&
                 rep.resolve(graph, equal->rep);
        }
        return false;
      });
    }

    static MatchOrBind<OpIndex> FloatUnary(
        const MatchOrBind<OpIndex>& input,
        const MatchOrBind<FloatUnaryOp::Kind>& kind,
        const MatchOrBind<FloatRepresentation>& rep) {
      return MatchOrBind<OpIndex>([=](const Graph* graph, const OpIndex& idx) {
        if (const FloatUnaryOp* unary =
                graph->Get(idx).TryCast<FloatUnaryOp>()) {
          return input.resolve(graph, unary->input()) &&
                 kind.resolve(graph, unary->kind) &&
                 rep.resolve(graph, unary->rep);
        }
        return false;
      });
    }

    static MatchOrBind<OpIndex> Float64Abs(const MatchOrBind<OpIndex>& input) {
      return FloatUnary(input, FloatUnaryOp::Kind::kAbs,
                        FloatRepresentation::Float64());
    }

    static MatchOrBind<OpIndex> FloatBinop(
        const MatchOrBind<OpIndex>& left, const MatchOrBind<OpIndex>& right,
        const MatchOrBind<FloatBinopOp::Kind>& kind,
        const MatchOrBind<FloatRepresentation>& rep) {
      return MatchOrBind<OpIndex>([=](const Graph* graph, const OpIndex& idx) {
        if (const FloatBinopOp* binop =
                graph->Get(idx).TryCast<FloatBinopOp>()) {
          return left.resolve(graph, binop->left()) &&
                 right.resolve(graph, binop->right()) &&
                 kind.resolve(graph, binop->kind) &&
                 rep.resolve(graph, binop->rep);
        }
        return false;
      });
    }

    static MatchOrBind<OpIndex> Float64Constant(
        const MatchOrBind<double>& value) {
      return MatchOrBind<OpIndex>([=](const Graph* graph, const OpIndex& idx) {
        ConstantOp::Storage storage;
        if (Constant(ConstantOp::Kind::kFloat64, &storage)
                .MatchesWith(graph, idx)) {
          return value.resolve(graph, storage.float64);
        }
        return false;
      });
    }

    static MatchOrBind<OpIndex> WordConstant(
        const MatchOrBind<uint64_t>& value) {
      return MatchOrBind<OpIndex>([=](const Graph* graph, const OpIndex& idx) {
        ConstantOp::Kind kind;
        ConstantOp::Storage storage;
        if (Constant(&kind, &storage).MatchesWith(graph, idx)) {
          if (kind != ConstantOp::Kind::kWord32 &&
              kind != ConstantOp::Kind::kWord64) {
            return false;
          }
          return value.resolve(graph, storage.integral);
        }
        return false;
      });
    }

    static MatchOrBind<OpIndex> Comparison(
        const MatchOrBind<OpIndex>& left, const MatchOrBind<OpIndex>& right,
        const MatchOrBind<ComparisonOp::Kind>& kind,
        const MatchOrBind<RegisterRepresentation>& rep) {
      return MatchOrBind<OpIndex>([=](const Graph* graph, const OpIndex& idx) {
        if (const ComparisonOp* comparison =
                graph->Get(idx).TryCast<ComparisonOp>()) {
          return left.resolve(graph, comparison->left()) &&
                 right.resolve(graph, comparison->right()) &&
                 kind.resolve(graph, comparison->kind) &&
                 rep.resolve(graph, comparison->rep);
        }
        return false;
      });
    }

    static MatchOrBind<OpIndex> Float64LessThan(
        const MatchOrBind<OpIndex>& left, const MatchOrBind<OpIndex>& right) {
      return Comparison(left, right, ComparisonOp::Kind::kSignedLessThan,
                        RegisterRepresentation::Float64());
    }

    static bool MatchesWith(const Graph* graph, OpIndex index,
                            const MatchOrBind<OpIndex>& pattern) {
      return pattern.resolve(graph, index);
    }
  };

 private:
  Assembler& assembler() { return *static_cast<Assembler*>(this); }
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_OPERATION_MATCHING_H_
