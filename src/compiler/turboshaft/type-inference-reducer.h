// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_TYPE_INFERENCE_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_TYPE_INFERENCE_REDUCER_H_

#include <limits>

#include "src/base/logging.h"
#include "src/base/vector.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/compiler/turboshaft/types.h"

// #define TRACE_TYPING(...) PrintF(__VA_ARGS__)
#define TRACE_TYPING(...) ((void)0)

namespace v8::internal::compiler::turboshaft {

namespace {

// Returns the array's least element, ignoring NaN.
// There must be at least one non-NaN element.
// Any -0 is converted to 0.
template <typename T, size_t N>
T array_min(const std::array<T, N>& a) {
  DCHECK_NE(0, N);
  T x = +std::numeric_limits<T>::infinity();
  for (size_t i = 0; i < N; ++i) {
    if (!std::isnan(a[i])) {
      x = std::min(a[i], x);
    }
  }
  DCHECK(!std::isnan(x));
  return x == T{0} ? T{0} : x;  // -0 -> 0
}

// Returns the array's greatest element, ignoring NaN.
// There must be at least one non-NaN element.
// Any -0 is converted to 0.
template <typename T, size_t N>
T array_max(const std::array<T, N>& a) {
  DCHECK_NE(0, N);
  T x = -std::numeric_limits<T>::infinity();
  for (size_t i = 0; i < N; ++i) {
    if (!std::isnan(a[i])) {
      x = std::max(a[i], x);
    }
  }
  DCHECK(!std::isnan(x));
  return x == T{0} ? T{0} : x;  // -0 -> 0
}

}  // namespace

template <size_t Bits>
struct WordOperationTyper {
  static_assert(Bits == 32 || Bits == 64);
  using word_t = uint_type<Bits>;
  using type_t = WordType<Bits>;
  using ElementsVector = base::SmallVector<word_t, type_t::kMaxSetSize * 2>;

  static type_t FromElements(ElementsVector elements, Zone* zone) {
    base::sort(elements);
    auto it = std::unique(elements.begin(), elements.end());
    elements.pop_back(std::distance(it, elements.end()));
    DCHECK(!elements.empty());
    if (elements.size() <= type_t::kMaxSetSize) {
      return type_t::Set(elements, zone);
    }

    auto range =
        MakeRange(base::Vector<const word_t>{elements.data(), elements.size()});
    auto result = type_t::Range(range.first, range.second, zone);
    DCHECK(
        base::all_of(elements, [&](word_t e) { return result.Contains(e); }));
    return result;
  }

  static std::pair<word_t, word_t> MakeRange(const type_t& t) {
    if (t.is_range()) return t.range();
    DCHECK(t.is_set());
    return MakeRange(t.set_elements());
  }

  static std::pair<word_t, word_t> MakeRange(
      const base::Vector<const word_t>& elements) {
    DCHECK(!elements.empty());
    DCHECK(detail::is_unique_and_sorted(elements));
    if (elements[elements.size() - 1] - elements[0] <=
        std::numeric_limits<word_t>::max() / 2) {
      // Construct a non-wrapping range.
      return {elements[0], elements[elements.size() - 1]};
    }
    // Construct a wrapping range.
    size_t from_index = elements.size() - 1;
    size_t to_index = 0;
    while (to_index + 1 < from_index) {
      if ((elements[to_index + 1] - elements[to_index]) <
          (elements[from_index] - elements[from_index - 1])) {
        ++to_index;
      } else {
        ++from_index;
      }
    }
    return {elements[from_index], elements[to_index]};
  }

  static word_t distance(const std::pair<word_t, word_t>& range) {
    return is_wrapping(range) ? (std::numeric_limits<word_t>::max() -
                                 range.first + range.second)
                              : range.second - range.first;
  }

  static bool is_wrapping(const std::pair<word_t, word_t>& range) {
    return range.first > range.second;
  }

  static Type Add(const type_t& lhs, const type_t& rhs, Zone* zone) {
    if (lhs.is_any() || rhs.is_any()) return type_t::Any();

    // If both sides are decently small sets, we produce the product set.
    if (lhs.is_set() && rhs.is_set()) {
      ElementsVector result_elements;
      for (int i = 0; i < lhs.set_size(); ++i) {
        for (int j = 0; j < rhs.set_size(); ++j) {
          result_elements.push_back(lhs.set_element(i) + rhs.set_element(j));
        }
      }
      return FromElements(std::move(result_elements), zone);
    }

    // Otherwise just construct a range.
    std::pair<word_t, word_t> x = MakeRange(lhs);
    std::pair<word_t, word_t> y = MakeRange(rhs);

    // If the result would not be a complete range, we compute it.
    // Check: (lhs.to + rhs.to + 1) - (lhs.from + rhs.from + 1) < max
    // =====> (lhs.to - lhs.from) + (rhs.to - rhs.from) < max
    // =====> (lhs.to - lhs.from) < max - (rhs.to - rhs.from)
    if (distance(x) < std::numeric_limits<word_t>::max() - distance(y)) {
      return type_t::Range(x.first + y.first, x.second + y.second, zone);
    }

    return type_t::Any();
  }

  static Type Subtract(const type_t& lhs, const type_t& rhs, Zone* zone) {
    if (lhs.is_any() || rhs.is_any()) return type_t::Any();

    // If both sides are decently small sets, we produce the product set.
    if (lhs.is_set() && rhs.is_set()) {
      ElementsVector result_elements;
      for (int i = 0; i < lhs.set_size(); ++i) {
        for (int j = 0; j < rhs.set_size(); ++j) {
          result_elements.push_back(lhs.set_element(i) - rhs.set_element(j));
        }
      }
      return FromElements(std::move(result_elements), zone);
    }

    // Otherwise just construct a range.
    std::pair<word_t, word_t> x = MakeRange(lhs);
    std::pair<word_t, word_t> y = MakeRange(rhs);

    if (is_wrapping(x) && is_wrapping(y)) {
      return type_t::Range(x.first - y.second, x.second - y.first, zone);
    }

    // TODO(nicohartmann@): Improve the wrapping cases.
    return type_t::Any();
  }
};

template <size_t Bits>
struct FloatOperationTyper {
  static_assert(Bits == 32 || Bits == 64);
  using float_t = std::conditional_t<Bits == 32, float, double>;
  using type_t = FloatType<Bits>;
  static constexpr int kSetThreshold = type_t::kMaxSetSize;

  static type_t Range(float_t min, float_t max, bool maybe_nan, Zone* zone) {
    DCHECK_LE(min, max);
    if (min == max) return Set({min}, maybe_nan, zone);
    return type_t::Range(
        min, max, maybe_nan ? type_t::kNaN : type_t::kNoSpecialValues, zone);
  }

  static type_t Set(std::vector<float_t> elements, bool maybe_nan, Zone* zone) {
    base::sort(elements);
    elements.erase(std::unique(elements.begin(), elements.end()),
                   elements.end());
    if (base::erase_if(elements, [](float_t v) { return std::isnan(v); }) > 0) {
      maybe_nan = true;
    }
    return type_t::Set(
        elements, maybe_nan ? type_t::kNaN : type_t::kNoSpecialValues, zone);
  }

  // Tries to construct the product of two sets where values are generated using
  // {combine}. Returns Type::Invalid() if a set cannot be constructed (e.g.
  // because the result exceeds the maximal number of set elements).
  static Type ProductSet(const type_t& l, const type_t& r, bool maybe_nan,
                         Zone* zone,
                         std::function<float_t(float_t, float_t)> combine) {
    DCHECK(l.is_set());
    DCHECK(r.is_set());
    std::vector<float_t> results;
    for (int i = 0; i < l.set_size(); ++i) {
      for (int j = 0; j < r.set_size(); ++j) {
        results.push_back(combine(l.set_element(i), r.set_element(j)));
      }
    }
    maybe_nan = (base::erase_if(results,
                                [](float_t v) { return std::isnan(v); }) > 0) ||
                maybe_nan;
    base::sort(results);
    auto it = std::unique(results.begin(), results.end());
    if (std::distance(results.begin(), it) > kSetThreshold)
      return Type::Invalid();
    results.erase(it, results.end());
    return Set(std::move(results),
               maybe_nan ? type_t::kNaN : type_t::kNoSpecialValues, zone);
  }

  static Type Add(const type_t& l, const type_t& r, Zone* zone) {
    if (l.is_only_nan() || r.is_only_nan()) return type_t::NaN();
    bool maybe_nan = l.has_nan() || r.has_nan();

    // If both sides are decently small sets, we produce the product set.
    auto combine = [](float_t a, float_t b) { return a + b; };
    if (l.is_set() && r.is_set()) {
      auto result = ProductSet(l, r, maybe_nan, zone, combine);
      if (!result.IsInvalid()) return result;
    }

    // Otherwise just construct a range.
    auto [l_min, l_max] = l.minmax();
    auto [r_min, r_max] = r.minmax();

    std::array<float_t, 4> results;
    results[0] = l_min + r_min;
    results[1] = l_min + r_max;
    results[2] = l_max + r_min;
    results[3] = l_max + r_max;

    int nans = 0;
    for (int i = 0; i < 4; ++i) {
      if (std::isnan(results[i])) ++nans;
    }
    if (nans >= 4) {
      // All combinations of inputs produce NaN.
      return type_t::NaN();
    }
    maybe_nan = maybe_nan || nans > 0;
    const float_t result_min = array_min(results);
    const float_t result_max = array_max(results);
    return Range(result_min, result_max, maybe_nan, zone);
  }

  static Type Subtract(const type_t& l, const type_t& r, Zone* zone) {
    if (l.is_only_nan() || r.is_only_nan()) return type_t::NaN();
    bool maybe_nan = l.has_nan() || r.has_nan();

    // If both sides are decently small sets, we produce the product set.
    auto combine = [](float_t a, float_t b) { return a - b; };
    if (l.is_set() && r.is_set()) {
      auto result = ProductSet(l, r, maybe_nan, zone, combine);
      if (!result.IsInvalid()) return result;
    }

    // Otherwise just construct a range.
    auto [l_min, l_max] = l.minmax();
    auto [r_min, r_max] = r.minmax();

    std::array<float_t, 4> results;
    results[0] = l_min - r_min;
    results[1] = l_min - r_max;
    results[2] = l_max - r_min;
    results[3] = l_max - r_max;

    int nans = 0;
    for (int i = 0; i < 4; ++i) {
      if (std::isnan(results[i])) ++nans;
    }
    if (nans >= 4) {
      // All combinations of inputs produce NaN.
      return type_t::NaN();
    }
    maybe_nan = maybe_nan || nans > 0;
    const float_t result_min = array_min(results);
    const float_t result_max = array_max(results);
    return Range(result_min, result_max, maybe_nan, zone);
  }
};

class Typer {
 public:
  static Type TypeConstant(ConstantOp::Kind kind, ConstantOp::Storage value) {
    switch (kind) {
      case ConstantOp::Kind::kFloat32:
        if (std::isnan(value.float32)) return Float32Type::NaN();
        return Float32Type::Constant(value.float32);
      case ConstantOp::Kind::kFloat64:
        if (std::isnan(value.float64)) return Float64Type::NaN();
        return Float64Type::Constant(value.float64);
      case ConstantOp::Kind::kWord32:
        return Word32Type::Constant(static_cast<uint32_t>(value.integral));
      case ConstantOp::Kind::kWord64:
        return Word64Type::Constant(static_cast<uint64_t>(value.integral));
      default:
        // TODO(nicohartmann@): Support remaining {kind}s.
        return Type::Invalid();
    }
  }

  static Type LeastUpperBound(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsAny() || rhs.IsAny()) return Type::Any();
    if (lhs.IsNone()) return rhs;
    if (rhs.IsNone()) return lhs;

    // TODO(nicohartmann@): We might use more precise types here but currently
    // there is not much benefit in that.
    if (lhs.kind() != rhs.kind()) return Type::Any();

    switch (lhs.kind()) {
      case Type::Kind::kInvalid:
        UNREACHABLE();
      case Type::Kind::kNone:
        UNREACHABLE();
      case Type::Kind::kWord32:
        return Word32Type::LeastUpperBound(lhs.AsWord32(), rhs.AsWord32(),
                                           zone);
      case Type::Kind::kWord64:
        return Word64Type::LeastUpperBound(lhs.AsWord64(), rhs.AsWord64(),
                                           zone);
      case Type::Kind::kFloat32:
        return Float32Type::LeastUpperBound(lhs.AsFloat32(), rhs.AsFloat32(),
                                            zone);
      case Type::Kind::kFloat64:
        return Float64Type::LeastUpperBound(lhs.AsFloat64(), rhs.AsFloat64(),
                                            zone);
      case Type::Kind::kAny:
        UNREACHABLE();
    }
  }

  static Type TypeWord32Add(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    auto l = TruncateWord32Input(lhs, true, zone);
    auto r = TruncateWord32Input(rhs, true, zone);
    return WordOperationTyper<32>::Add(l, r, zone);
  }

  static Type TypeWord32Sub(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    auto l = TruncateWord32Input(lhs, true, zone);
    auto r = TruncateWord32Input(rhs, true, zone);
    return WordOperationTyper<32>::Subtract(l, r, zone);
  }

  static Type TypeWord64Add(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    if (!InputIs(lhs, Type::Kind::kWord64) ||
        !InputIs(rhs, Type::Kind::kWord64)) {
      return Word64Type::Any();
    }
    const auto& l = lhs.AsWord64();
    const auto& r = rhs.AsWord64();

    return WordOperationTyper<64>::Add(l, r, zone);
  }

  static Type TypeWord64Sub(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    if (!InputIs(lhs, Type::Kind::kWord64) ||
        !InputIs(rhs, Type::Kind::kWord64)) {
      return Word64Type::Any();
    }

    const auto& l = lhs.AsWord64();
    const auto& r = rhs.AsWord64();

    return WordOperationTyper<64>::Subtract(l, r, zone);
  }

  static Type TypeFloat32Add(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    if (!InputIs(lhs, Type::Kind::kFloat32) ||
        !InputIs(rhs, Type::Kind::kFloat32)) {
      return Float32Type::Any();
    }
    const auto& l = lhs.AsFloat32();
    const auto& r = rhs.AsFloat32();

    return FloatOperationTyper<32>::Add(l, r, zone);
  }

  static Type TypeFloat32Sub(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    if (!InputIs(lhs, Type::Kind::kFloat32) ||
        !InputIs(rhs, Type::Kind::kFloat32)) {
      return Float32Type::Any();
    }
    const auto& l = lhs.AsFloat32();
    const auto& r = rhs.AsFloat32();

    return FloatOperationTyper<32>::Subtract(l, r, zone);
  }

  static Type TypeFloat64Add(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    if (!InputIs(lhs, Type::Kind::kFloat64) ||
        !InputIs(rhs, Type::Kind::kFloat64)) {
      return Float64Type::Any();
    }
    const auto& l = lhs.AsFloat64();
    const auto& r = rhs.AsFloat64();

    return FloatOperationTyper<64>::Add(l, r, zone);
  }

  static Type TypeFloat64Sub(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    if (!InputIs(lhs, Type::Kind::kFloat64) ||
        !InputIs(rhs, Type::Kind::kFloat64)) {
      return Float64Type::Any();
    }
    const auto& l = lhs.AsFloat64();
    const auto& r = rhs.AsFloat64();

    return FloatOperationTyper<64>::Subtract(l, r, zone);
  }

  static Word64Type ExtendWord32ToWord64(const Word32Type& t, Zone* zone) {
    // We cannot infer much, but the lower bound of the word32 is also the lower
    // bound of the word64 type.
    if (t.is_wrapping()) return Word64Type::Any();
    return Word64Type::Range(static_cast<uint64_t>(t.unsigned_min()),
                             std::numeric_limits<uint64_t>::max(), zone);
  }

  static Word32Type TruncateWord32Input(const Type& input,
                                        bool implicit_word64_narrowing,
                                        Zone* zone) {
    DCHECK(!input.IsInvalid());
    DCHECK(!input.IsNone());

    if (input.IsAny()) {
      if (allow_invalid_inputs()) return Word32Type::Any();
    } else if (input.IsWord32()) {
      return input.AsWord32();
    } else if (input.IsWord64() && implicit_word64_narrowing) {
      // The input is implicitly converted to word32.
      const auto& w64 = input.AsWord64();
      if (w64.is_set()) {
        WordOperationTyper<32>::ElementsVector elements;
        for (uint64_t e : w64.set_elements()) {
          elements.push_back(static_cast<uint32_t>(e));
        }
        return WordOperationTyper<32>::FromElements(std::move(elements), zone);
      }

      if (w64.is_any() || w64.is_wrapping()) return Word32Type::Any();

      if (w64.range_to() <= std::numeric_limits<uint32_t>::max()) {
        DCHECK_LE(w64.range_from(), std::numeric_limits<uint32_t>::max());
        return Word32Type::Range(static_cast<uint32_t>(w64.range_from()),
                                 static_cast<uint32_t>(w64.range_to()), zone);
      }

      // TODO(nicohartmann@): Might compute a more precise range here.
      return Word32Type::Any();
    }
    UNREACHABLE();
  }

  static bool InputIs(const Type& input, Type::Kind expected) {
    if (input.IsInvalid()) {
      if (allow_invalid_inputs()) return false;
    } else if (input.kind() == expected) {
      return true;
    } else if (input.IsAny()) {
      if (allow_invalid_inputs()) return false;
    }
    UNREACHABLE();
  }

  // For now we allow invalid inputs (which will then just lead to very generic
  // typing). Once all operations are implemented, we are going to disable this.
  static bool allow_invalid_inputs() { return true; }
};

struct TypeInferenceReducerArgs {
  Isolate* isolate;
};

template <class Next>
class TypeInferenceReducer : public Next {
  static_assert(next_is_bottom_of_assembler_stack<Next>::value);
  using table_t = SnapshotTable<Type>;

 public:
  using Next::Asm;
  using ArgT =
      base::append_tuple_type<typename Next::ArgT, TypeInferenceReducerArgs>;

  template <typename... Args>
  explicit TypeInferenceReducer(const std::tuple<Args...>& args)
      : Next(args),
        types_(Asm().output_graph().operation_types()),
        table_(Asm().phase_zone()),
        op_to_key_mapping_(Asm().phase_zone()),
        block_to_snapshot_mapping_(Asm().input_graph().block_count(),
                                   base::nullopt, Asm().phase_zone()),
        predecessors_(Asm().phase_zone()),
        isolate_(std::get<TypeInferenceReducerArgs>(args).isolate) {}

  void Bind(Block* new_block, const Block* origin) {
    Next::Bind(new_block, origin);

    // Seal the current block first.
    if (table_.IsSealed()) {
      DCHECK_NULL(current_block_);
    } else {
      // If we bind a new block while the previous one is still unsealed, we
      // finalize it.
      DCHECK_NOT_NULL(current_block_);
      DCHECK(current_block_->index().valid());
      block_to_snapshot_mapping_[current_block_->index()] = table_.Seal();
      current_block_ = nullptr;
    }

    // Collect the snapshots of all predecessors.
    {
      predecessors_.clear();
      for (const Block* pred = new_block->LastPredecessor(); pred != nullptr;
           pred = pred->NeighboringPredecessor()) {
        base::Optional<table_t::Snapshot> pred_snapshot =
            block_to_snapshot_mapping_[pred->index()];
        DCHECK(pred_snapshot.has_value());
        predecessors_.push_back(pred_snapshot.value());
      }
      std::reverse(predecessors_.begin(), predecessors_.end());
    }

    // Start a new snapshot for this block by merging information from
    // predecessors.
    {
      auto MergeTypes = [&](table_t::Key,
                            base::Vector<Type> predecessors) -> Type {
        DCHECK_GT(predecessors.size(), 0);
        Type result_type = predecessors[0];
        for (size_t i = 1; i < predecessors.size(); ++i) {
          result_type = Typer::LeastUpperBound(result_type, predecessors[i],
                                               Asm().graph_zone());
        }
        return result_type;
      };

      table_.StartNewSnapshot(base::VectorOf(predecessors_), MergeTypes);
    }

    // Check if the predecessor is a branch that allows us to refine a few
    // types.
    if (new_block->HasExactlyNPredecessors(1)) {
      Block* predecessor = new_block->LastPredecessor();
      const Operation& terminator =
          predecessor->LastOperation(Asm().output_graph());
      if (const BranchOp* branch = terminator.TryCast<BranchOp>()) {
        DCHECK(branch->if_true == new_block || branch->if_false == new_block);
        RefineTypesAfterBranch(branch, branch->if_true == new_block);
      }
    }
    current_block_ = new_block;
  }

  void RefineTypesAfterBranch(const BranchOp* branch, bool then_branch) {
    Zone* zone = Asm().graph_zone();
    // Inspect branch condition.
    const Operation& condition = Asm().output_graph().Get(branch->condition());
    if (const ComparisonOp* comparison = condition.TryCast<ComparisonOp>()) {
      Type lhs = GetType(comparison->left());
      Type rhs = GetType(comparison->right());
      // If we don't have proper types, there is nothing we can do.
      if (lhs.IsInvalid() || rhs.IsInvalid()) return;

      // TODO(nicohartmann@): Might get rid of this once everything is properly
      // typed.
      if (lhs.IsAny() || rhs.IsAny()) return;
      DCHECK(!lhs.IsNone());
      DCHECK(!rhs.IsNone());

      const bool is_signed = ComparisonOp::IsSigned(comparison->kind);
      const bool is_less_than = ComparisonOp::IsLessThan(comparison->kind);
      Type l_refined;
      Type r_refined;

      switch (comparison->rep.value()) {
        case RegisterRepresentation::Word32(): {
          if (is_signed) {
            // TODO(nicohartmann@): Support signed comparison.
            return;
          }
          Word32Type l = Typer::TruncateWord32Input(lhs, true, zone).AsWord32();
          Word32Type r = Typer::TruncateWord32Input(rhs, true, zone).AsWord32();
          uint32_t l_min, l_max, r_min, r_max;
          if (then_branch) {
            l_min = 0;
            l_max = r.unsigned_max();
            r_min = l.unsigned_min();
            r_max = std::numeric_limits<uint32_t>::max();
            if (is_less_than) {
              l_max = next_smaller(l_max);
              r_min = next_larger(r_min);
            }
          } else {
            l_min = r.unsigned_min();
            l_max = std::numeric_limits<uint32_t>::max();
            r_min = 0;
            r_max = l.unsigned_max();
            if (!is_less_than) {
              l_min = next_larger(l_min);
              r_max = next_smaller(r_max);
            }
          }
          auto l_restrict = Word32Type::Range(l_min, l_max, zone);
          auto r_restrict = Word32Type::Range(r_min, r_max, zone);
          if (l_restrict.IsWord32() && lhs.IsWord64()) {
            l_refined = Word64Type::Intersect(
                lhs.AsWord64(),
                Typer::ExtendWord32ToWord64(l_restrict.AsWord32(), zone),
                Type::ResolutionMode::kOverApproximate, zone);
          } else {
            l_refined = Word32Type::Intersect(
                l, l_restrict, Type::ResolutionMode::kOverApproximate, zone);
          }
          if (r_restrict.IsWord32() && rhs.IsWord64()) {
            r_refined = Word64Type::Intersect(
                rhs.AsWord64(),
                Typer::ExtendWord32ToWord64(r_restrict.AsWord32(), zone),
                Type::ResolutionMode::kOverApproximate, zone);
          } else {
            r_refined = Word32Type::Intersect(
                r, r_restrict, Type::ResolutionMode::kOverApproximate, zone);
          }
          break;
        }
        case RegisterRepresentation::Float64(): {
          constexpr double infty = std::numeric_limits<double>::infinity();
          Float64Type l = lhs.AsFloat64();
          Float64Type r = rhs.AsFloat64();
          double l_min, l_max, r_min, r_max;
          uint32_t special_values = Float64Type::kNoSpecialValues;
          if (then_branch) {
            l_min = -infty;
            l_max = r.max();
            r_min = l.min();
            r_max = infty;
            if (is_less_than) {
              l_max = next_smaller(l_max);
              r_min = next_larger(r_min);
            }
          } else {
            l_min = r.min();
            l_max = infty;
            r_min = -infty;
            r_max = l.max();
            special_values = Float64Type::kNaN;
            if (!is_less_than) {
              l_min = next_larger(l_min);
              r_max = next_smaller(r_max);
            }
          }
          auto l_restrict =
              Float64Type::Range(l_min, l_max, special_values, zone);
          auto r_restrict =
              Float64Type::Range(r_min, r_max, special_values, zone);
          l_refined = Float64Type::Intersect(l, l_restrict, zone);
          r_refined = Float64Type::Intersect(r, r_restrict, zone);
          break;
        }
        default:
          return;
      }

      // TODO(nicohartmann@):
      // DCHECK(l_refined.IsSubtypeOf(lhs));
      // DCHECK(r_refined.IsSubtypeOf(rhs));
      const std::string branch_str = branch->ToString().substr(0, 40);
      USE(branch_str);
      TRACE_TYPING("\033[32mBr   %3d:%-40s\033[0m\n",
                   Asm().output_graph().Index(*branch).id(),
                   branch_str.c_str());
      RefineOperationType(comparison->left(), l_refined,
                          then_branch ? 'T' : 'F');
      RefineOperationType(comparison->right(), r_refined,
                          then_branch ? 'T' : 'F');
    }
  }

  void RefineOperationType(OpIndex op, const Type& type,
                           char case_for_tracing) {
    DCHECK(op.valid());
    DCHECK(!type.IsInvalid());

    TRACE_TYPING("\033[32m  %c: %3d:%-40s ~~> %s\033[0m\n", case_for_tracing,
                 op.id(),
                 Asm().output_graph().Get(op).ToString().substr(0, 40).c_str(),
                 type.ToString().c_str());

    SetType(op, type);

    // TODO(nicohartmann@): One could push the refined type deeper into the
    // operations.
  }

  Type TypeForRepresentation(RegisterRepresentation rep) {
    switch (rep.value()) {
      case RegisterRepresentation::Word32():
        return Word32Type::Any();
      case RegisterRepresentation::Word64():
        return Word64Type::Any();
      case RegisterRepresentation::Float32():
        return Float32Type::Any();
      case RegisterRepresentation::Float64():
        return Float64Type::Any();

      case RegisterRepresentation::Tagged():
      case RegisterRepresentation::Compressed():
        // TODO(nicohartmann@): Support these representations.
        return Type::Any();
    }
  }

  OpIndex ReducePhi(base::Vector<const OpIndex> inputs,
                    RegisterRepresentation rep) {
    OpIndex index = Next::ReducePhi(inputs, rep);

    Type result_type = Type::None();
    for (const OpIndex input : inputs) {
      Type type = types_[input];
      if (type.IsInvalid()) {
        type = TypeForRepresentation(rep);
      }
      // TODO(nicohartmann@): Should all temporary types be in the
      // graph_zone()?
      result_type =
          Typer::LeastUpperBound(result_type, type, Asm().graph_zone());
    }

    SetType(index, result_type);
    return index;
  }

  OpIndex ReduceConstant(ConstantOp::Kind kind, ConstantOp::Storage value) {
    OpIndex index = Next::ReduceConstant(kind, value);
    if (!index.valid()) return index;

    Type type = Typer::TypeConstant(kind, value);
    SetType(index, type);
    return index;
  }

  OpIndex ReduceWordBinop(OpIndex left, OpIndex right, WordBinopOp::Kind kind,
                          WordRepresentation rep) {
    OpIndex index = Next::ReduceWordBinop(left, right, kind, rep);
    if (!index.valid()) return index;

    Type left_type = GetType(left);
    Type right_type = GetType(right);
    if (left_type.IsInvalid() || right_type.IsInvalid()) return index;

    Zone* zone = Asm().graph_zone();
    Type result_type = Type::Invalid();
    if (rep == WordRepresentation::Word32()) {
      switch (kind) {
        case WordBinopOp::Kind::kAdd:
          result_type = Typer::TypeWord32Add(left_type, right_type, zone);
          break;
        case WordBinopOp::Kind::kSub:
          result_type = Typer::TypeWord32Sub(left_type, right_type, zone);
          break;
        default:
          // TODO(nicohartmann@): Support remaining {kind}s.
          break;
      }
    } else {
      DCHECK_EQ(rep, WordRepresentation::Word64());
      switch (kind) {
        case WordBinopOp::Kind::kAdd:
          result_type = Typer::TypeWord64Add(left_type, right_type, zone);
          break;
        case WordBinopOp::Kind::kSub:
          result_type = Typer::TypeWord64Sub(left_type, right_type, zone);
          break;
        default:
          // TODO(nicohartmann@): Support remaining {kind}s.
          break;
      }
    }

    SetType(index, result_type);
    return index;
  }

  OpIndex ReduceFloatBinop(OpIndex left, OpIndex right, FloatBinopOp::Kind kind,
                           FloatRepresentation rep) {
    OpIndex index = Next::ReduceFloatBinop(left, right, kind, rep);
    if (!index.valid()) return index;

    Type result_type = Type::Invalid();
    Type left_type = GetType(left);
    Type right_type = GetType(right);

    if (!left_type.IsInvalid() && !right_type.IsInvalid()) {
      if (rep == FloatRepresentation::Float32()) {
        switch (kind) {
          case FloatBinopOp::Kind::kAdd:
            result_type = Typer::TypeFloat32Add(left_type, right_type,
                                                Asm().graph_zone());
            break;
          case FloatBinopOp::Kind::kSub:
            result_type = Typer::TypeFloat32Sub(left_type, right_type,
                                                Asm().graph_zone());
            break;
          default:
            // TODO(nicohartmann@): Support remaining {kind}s.
            break;
        }
      } else {
        DCHECK_EQ(rep, FloatRepresentation::Float64());
        switch (kind) {
          case FloatBinopOp::Kind::kAdd:
            result_type = Typer::TypeFloat64Add(left_type, right_type,
                                                Asm().graph_zone());
            break;
          case FloatBinopOp::Kind::kSub:
            result_type = Typer::TypeFloat64Sub(left_type, right_type,
                                                Asm().graph_zone());
            break;
          default:
            // TODO(nicohartmann@): Support remaining {kind}s.
            break;
        }
      }
    }

    SetType(index, result_type);
    return index;
  }

  Type GetType(const OpIndex index) {
    if (auto key = op_to_key_mapping_[index]) return table_.Get(*key);
    return Type::Invalid();
  }

  void SetType(const OpIndex index, const Type& result_type) {
    if (!result_type.IsInvalid()) {
      if (auto key_opt = op_to_key_mapping_[index]) {
        table_.Set(*key_opt, result_type);
        DCHECK(!types_[index].IsInvalid());
      } else {
        auto key = table_.NewKey(Type::None());
        table_.Set(key, result_type);
        types_[index] = result_type;
        op_to_key_mapping_[index] = key;
      }
    }

    TRACE_TYPING(
        "\033[%smType %3d:%-40s ==> %s\033[0m\n",
        (result_type.IsInvalid() ? "31" : "32"), index.id(),
        Asm().output_graph().Get(index).ToString().substr(0, 40).c_str(),
        (result_type.IsInvalid() ? "" : result_type.ToString().c_str()));
  }

 private:
  GrowingSidetable<Type>& types_;
  table_t table_;
  const Block* current_block_ = nullptr;
  GrowingSidetable<base::Optional<table_t::Key>> op_to_key_mapping_;
  GrowingBlockSidetable<base::Optional<table_t::Snapshot>>
      block_to_snapshot_mapping_;
  // {predecessors_} is used during merging, but we use an instance variable for
  // it, in order to save memory and not reallocate it for each merge.
  ZoneVector<table_t::Snapshot> predecessors_;
  Isolate* isolate_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_TYPE_INFERENCE_REDUCER_H_
