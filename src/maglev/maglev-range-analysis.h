// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_RANGE_ANALYSIS_H_
#define V8_MAGLEV_MAGLEV_RANGE_ANALYSIS_H_

#include <cstdint>
#include <utility>

#include "src/base/logging.h"
#include "src/common/globals.h"
#include "src/common/operation.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-graph-printer.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir.h"
#include "src/zone/zone-containers.h"

#define TRACE_RANGE(...)                                     \
  do {                                                       \
    if (V8_UNLIKELY(v8_flags.trace_maglev_range_analysis)) { \
      StdoutStream{} << __VA_ARGS__ << std::endl;            \
    }                                                        \
  } while (false)

namespace v8::internal::maglev {

// {lhs_map} becomes the result of the intersection.
template <typename Key, typename Value, typename MergeFunc>
void DestructivelyIntersect(ZoneMap<Key, Value>& lhs_map,
                            const ZoneMap<Key, Value>& rhs_map,
                            MergeFunc&& func) {
  typename ZoneMap<Key, Value>::iterator lhs_it = lhs_map.begin();
  typename ZoneMap<Key, Value>::const_iterator rhs_it = rhs_map.begin();
  while (lhs_it != lhs_map.end() && rhs_it != rhs_map.end()) {
    if (lhs_it->first < rhs_it->first) {
      // Skip over elements that are only in LHS.
      ++lhs_it;
    } else if (rhs_it->first < lhs_it->first) {
      // Skip over elements that are only in RHS.
      ++rhs_it;
    } else {
      lhs_it->second = func(lhs_it->second, rhs_it->second);
      ++lhs_it;
      ++rhs_it;
    }
  }
}

class Range {
 public:
  static const int64_t kInfMin = INT64_MIN;
  static const int64_t kInfMax = INT64_MAX;

  constexpr Range(int64_t min, int64_t max) : min_(min), max_(max) {
    if (!is_empty()) {
      DCHECK_IMPLIES(min != kInfMin, IsSafeInteger(min_));
      DCHECK_IMPLIES(max != kInfMax, IsSafeInteger(max_));
      DCHECK_LE(min_, max_);
    }
  }

  explicit Range(int64_t value) : Range(value, value) {}

  std::optional<int64_t> min() const {
    if (min_ == kInfMin) return {};
    return min_;
  }

  std::optional<int64_t> max() const {
    if (max_ == kInfMax) return {};
    return max_;
  }

#define KNOWN_RANGES(V)                  \
  V(All, kInfMin, kInfMax)               \
  V(Empty, kInfMax, kInfMin)             \
  V(Smi, Smi::kMinValue, Smi::kMaxValue) \
  V(Int32, INT32_MIN, INT32_MAX)         \
  V(Uint32, 0, UINT32_MAX)               \
  V(SafeInt, kMinSafeInteger, kMaxSafeInteger)

#define DEF_RANGE(Name, Min, Max)                           \
  static constexpr Range Name() { return Range(Min, Max); } \
  bool Is##Name() const { return Name().contains(*this); }
  KNOWN_RANGES(DEF_RANGE)
#undef DEF_RANGE
#undef KNOWN_RANGES

  bool is_all() const { return min_ == kInfMin && max_ == kInfMax; }
  constexpr bool is_empty() const { return max_ < min_; }

  bool is_constant() const { return max_ == min_ && max_ != kInfMax; }

  bool contains(int64_t value) const { return min_ <= value && value <= max_; }

  bool contains(Range other) const {
    if (other.is_empty()) return true;
    if (is_empty()) return false;
    return min_ <= other.min_ && other.max_ <= max_;
  }

  bool overlaps(Range other) {
    if (is_empty() || other.is_empty()) return false;
    return min_ <= other.max_ && max_ >= other.min_;
  }

  bool operator==(const Range& other) const = default;

  bool operator<=(const Range& other) const { return max_ <= other.min_; }

  bool operator<(const Range& other) const { return max_ < other.min_; }

  bool operator>=(const Range& other) const { return min_ >= other.max_; }

  bool operator>(const Range& other) const { return min_ < other.max_; }

  static Range Union(Range r1, Range r2) {
    if (r1.is_empty()) return r2;
    if (r2.is_empty()) return r1;
    int64_t min = std::min(r1.min_, r2.min_);
    int64_t max = std::max(r1.max_, r2.max_);
    return Range(min, max);
  }

  static Range Intersect(Range r1, Range r2) {
    if (r1.is_empty() || r2.is_empty()) return Range::Empty();
    int64_t min = std::max(r1.min_, r2.min_);
    int64_t max = std::min(r1.max_, r2.max_);
    if (min <= max) return Range(min, max);
    return Range::Empty();
  }

  static Range Widen(Range range, Range new_range) {
    if (range.is_empty()) return new_range;
    if (new_range.is_empty()) return range;
    int64_t min = (new_range.min_ < range.min_) ? kInfMin : range.min_;
    int64_t max = (new_range.max_ > range.max_) ? kInfMax : range.max_;
    Range widened_range = Range(min, max);
    // For soundness, widen operation must be an over-approximation.
    DCHECK(widened_range.contains(Range::Union(range, new_range)));
    return widened_range;
  }

  static Range Negate(Range r) {
    if (r.is_empty()) return Range::Empty();
    int64_t min = r.max_ == kInfMax ? kInfMin : -r.max_;
    int64_t max = r.min_ == kInfMin ? kInfMax : -r.min_;
    return Range(min, max);
  }

  // [a, b] + [c, d] = [a+c, b+d]
  static Range Add(Range r1, Range r2) {
    if (r1.is_empty() || r2.is_empty()) return Range::Empty();
    int64_t min = kInfMin;
    if (r1.min_ != kInfMin && r2.min_ != kInfMin) {
      min = r1.min_ + r2.min_;
      if (!IsSafeInteger(min)) min = kInfMin;
    }
    int64_t max = kInfMax;
    if (r1.max_ != kInfMax && r2.max_ != kInfMax) {
      max = r1.max_ + r2.max_;
      if (!IsSafeInteger(max)) max = kInfMax;
    }
    return Range(min, max);
  }

  // [a, b] - [c, d] = [a-c, b-d]
  static Range Sub(Range r1, Range r2) {
    return Range::Add(r1, Range::Negate(r2));
  }

  // [a, b] * [c, d] = [min(ac,ad,bc,bd), max(ac,ad,bc,bd)]
  static Range Mul(Range r1, Range r2) { UNIMPLEMENTED(); }

  // [a, b] / [c, d] = [min(a/c,a/d,b/c,b/d), max(a/c,a/d,b/c,b/d)]
  // If [c, d] contains 0, then return All().
  static Range Div(Range r1, Range r2) { UNIMPLEMENTED(); }

  static Range BitwiseAnd(Range r1, Range r2) {
    // TODO(victorgomes): This is copying OperationTyper::NumberBitwiseAnd. Not
    // sure if we need to force both sides to be int32s. I guess a safe int
    // would be enough.
    if (!r1.IsInt32() || !r2.IsInt32()) return Range::All();
    int64_t lmin = r1.min_;
    int64_t rmin = r2.min_;
    int64_t lmax = r1.max_;
    int64_t rmax = r2.max_;
    int64_t min = INT32_MIN;
    // And-ing any two values results in a value no larger than their maximum.
    // Even no larger than their minimum if both values are non-negative.
    int64_t max =
        lmin >= 0 && rmin >= 0 ? std::min(lmax, rmax) : std::max(lmax, rmax);
    // And-ing with a non-negative value x causes the result to be between
    // zero and x.
    if (lmin >= 0) {
      min = 0;
      max = std::min(max, lmax);
    }
    if (rmin >= 0) {
      min = 0;
      max = std::min(max, rmax);
    }
    return Range(min, max);
  }

  Range ConstrainLessEqual(Range upper_bound) {
    if (is_empty() || upper_bound.is_empty()) return Range::Empty();
    max_ = std::min(max_, upper_bound.max_);
    return *this;
  }

  Range ConstraintGreater(Range lower_bound) {
    if (is_empty() || lower_bound.is_empty()) return Range::Empty();
    min_ = std::max(min_, lower_bound.min_);
    return *this;
  }

  friend std::ostream& operator<<(std::ostream& os, const Range& r) {
    if (r.is_empty()) {
      os << "[]";
      return os;
    }
    os << "[";
    if (r.min_ == kInfMin) {
      os << "-∞";
    } else {
      os << r.min_;
    }
    os << ", ";
    if (r.max_ == kInfMax) {
      os << "+∞";
    } else {
      os << r.max_;
    }
    os << "]";
    return os;
  }

 private:
  // These values are either in the safe integer range or they are kInfMin
  // and/or kInfMax.
  int64_t min_;
  int64_t max_;
};

class NodeRanges {
 public:
  explicit NodeRanges(Graph* graph)
      : graph_(graph), ranges_(graph->max_block_id(), graph->zone()) {}

  Range Get(BasicBlock* block, ValueNode* node) {
    auto*& map = ranges_[block->id()];
    DCHECK_NOT_NULL(map);
    auto it = map->find(node);
    if (it == map->end()) {
      if (IsConstantNode(node->opcode())) {
        return GetConstantRange(node);
      }
      if (SameRangeAsFirstInput(node->opcode())) {
        return Get(block, node->input_node(0));
      }
      return Range::All();
    }
    return it->second;
  }

  void UnionUpdate(BasicBlock* block, ValueNode* node, Range range) {
    auto* map = ranges_[block->id()];
    DCHECK_NOT_NULL(map);
    auto it = map->find(node);
    if (it == map->end()) {
      map->emplace(node, range);
    } else {
      Range new_range = Range::Union(it->second, range);
      TRACE_RANGE("[range]: Union update: "
                  << PrintNodeLabel(node) << ": " << PrintNode(node)
                  << ", from: " << it->second << ", to: " << new_range);

      it->second = new_range;
    }
  }

  inline void ProcessGraph();

  void Print() {
    std::cout << "Node ranges:\n";
    for (BasicBlock* block : graph_->blocks()) {
      int id = block->id();
      std::cout << "Block b" << id << ":\n";
      auto* map = ranges_[id];
      if (!map) continue;
      for (auto& [node, range] : *map) {
        std::cout << "  " << PrintNodeLabel(node) << ": " << PrintNode(node)
                  << ": " << range << std::endl;
      }
    }
  }

  void EnsureMapExistsFor(BasicBlock* block) {
    if (ranges_[block->id()] == nullptr) {
      ranges_[block->id()] = zone()->New<ZoneMap<ValueNode*, Range>>(zone());
    }
  }

  void Join(BasicBlock* block, BasicBlock* pred) {
    auto*& map = ranges_[block->id()];
    auto*& pred_map = ranges_[pred->id()];
    DCHECK_NOT_NULL(pred_map);
    if (map == nullptr) {
      map = zone()->New<ZoneMap<ValueNode*, Range>>(*pred_map);
      return;
    }
    DestructivelyIntersect(*map, *pred_map, [&](Range& r1, const Range& r2) {
      return Range::Union(r1, r2);
    });
  }

  void NarrowUpdate(BasicBlock* block, ValueNode* node, Range narrowed_range) {
    if (IsConstantNode(node->opcode())) {
      return;
    }
    auto* map = ranges_[block->id()];
    DCHECK_NOT_NULL(map);
    auto it = map->find(node);
    if (it == map->end()) {
      TRACE_RANGE("[range]: Narrow update: " << PrintNodeLabel(node) << ": "
                                             << PrintNode(node) << ": "
                                             << narrowed_range);
      map->emplace(node, narrowed_range);
    } else {
      if (!narrowed_range.is_empty()) {
        TRACE_RANGE("[range]: Narrow update: "
                    << PrintNodeLabel(node) << ": " << PrintNode(node)
                    << ", from: " << it->second << ", to: " << narrowed_range);
        it->second = narrowed_range;
      } else {
        TRACE_RANGE("[range]: Failed narrowing update: "
                    << PrintNodeLabel(node) << ": " << PrintNode(node)
                    << ", from: " << it->second << ", to: " << narrowed_range);
      }
    }
  }

 private:
  Graph* graph_;
  // TODO(victorgomes): Use SnapshotTable.
  ZoneVector<ZoneMap<ValueNode*, Range>*> ranges_;
  Zone* zone() const { return graph_->zone(); }

  static bool SameRangeAsFirstInput(Opcode opcode) {
    switch (opcode) {
      case Opcode::kIdentity:
      case Opcode::kReturnedValue:
      case Opcode::kInt32ToNumber:
        return true;
      default:
        return false;
    }
  }

  Range GetConstantRange(ValueNode* node) {
    // TODO(victorgomes): Support other constant nodes.
    switch (node->opcode()) {
      case Opcode::kInt32Constant:
        return Range(node->Cast<Int32Constant>()->value());
      case Opcode::kUint32Constant:
        return Range(node->Cast<Uint32Constant>()->value());
      case Opcode::kSmiConstant:
        return Range(node->Cast<SmiConstant>()->value().value());
      case Opcode::kFloat64Constant: {
        double value = node->Cast<Float64Constant>()->value().get_scalar();
        if (!IsSafeInteger(value)) return Range::All();
        int64_t int_value = static_cast<int64_t>(value);
        return Range{int_value, int_value};
      }
      default:
        return Range::All();
    }
  }
};

class RangeProcessor {
 public:
  explicit RangeProcessor(NodeRanges& node_ranges) : ranges_(node_ranges) {}

  void PreProcessGraph(Graph* graph) { is_done_ = true; }
  void PostProcessGraph(Graph* graph) {}

  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    current_block_ = block;
    ranges_.EnsureMapExistsFor(block);
    return BlockProcessResult::kContinue;
  }
  void PostProcessBasicBlock(BasicBlock* block) {
    if (JumpLoop* control = block->control_node()->TryCast<JumpLoop>()) {
      if (!ProcessLoopPhisBackedge(control->target(), block)) {
        // We didn't reach a fixpoint for this loop, try this loop header
        // again.
        is_done_ = false;
      }
    } else if (UnconditionalControlNode* unconditional =
                   block->control_node()->TryCast<UnconditionalControlNode>()) {
      BasicBlock* succ = unconditional->target();
      ranges_.Join(succ, block);
      if (succ->has_state() && succ->has_phi()) {
        ProcessPhis(succ, block);
      }
    } else {
      block->ForEachSuccessor([&](BasicBlock* succ) {
        ranges_.Join(succ, block);
        // Because of split-edge, {succ} cannot have phis.
        DCHECK_IMPLIES(succ->has_state(), !succ->has_phi());
        ProcessNodeBase(block->control_node(), succ);
      });
    }
  }

  void PostPhiProcessing() {}

  ProcessResult Process(UnsafeSmiUntag* node, const ProcessingState&) {
    UnionUpdate(node, Range::Intersect(Get(node->input_node(0)), Range::Smi()));
    return ProcessResult::kContinue;
  }
  ProcessResult Process(CheckedSmiUntag* node, const ProcessingState&) {
    UnionUpdate(node, Range::Intersect(Get(node->input_node(0)), Range::Smi()));
    return ProcessResult::kContinue;
  }
  ProcessResult Process(CheckedSmiSizedInt32* node, const ProcessingState&) {
    UnionUpdate(node, Range::Intersect(Get(node->input_node(0)), Range::Smi()));
    return ProcessResult::kContinue;
  }
  ProcessResult Process(TruncateCheckedNumberOrOddballToInt32* node) {
    UnionUpdate(node,
                Range::Intersect(Get(node->input_node(0)), Range::Int32()));
    return ProcessResult::kContinue;
  }
  ProcessResult Process(Int32IncrementWithOverflow* node,
                        const ProcessingState&) {
    UnionUpdate(node, Range::Add(Get(node->input_node(0)), Range(1)));
    return ProcessResult::kContinue;
  }
  ProcessResult Process(Int32AddWithOverflow* node, const ProcessingState&) {
    UnionUpdate(node,
                Range::Add(Get(node->input_node(0)), Get(node->input_node(1))));
    return ProcessResult::kContinue;
  }
  ProcessResult Process(Int32BitwiseAnd* node, const ProcessingState&) {
    UnionUpdate(node, Range::BitwiseAnd(Get(node->input_node(0)),
                                        Get(node->input_node(1))));
    return ProcessResult::kContinue;
  }

  ProcessResult Process(NodeBase* node, const ProcessingState&) {
    return ProcessResult::kContinue;
  }

  void ProcessControlNodeFor(BranchIfInt32Compare* node, BasicBlock* succ) {
    // TODO(victorgomes): Implement more operations.
    if (node->operation() == Operation::kLessThanOrEqual) {
      ValueNode* lhs = node->input_node(0);
      ValueNode* rhs = node->input_node(1);
      Range lhs_range = ranges_.Get(succ, lhs);
      Range rhs_range = ranges_.Get(succ, rhs);
      if (node->if_true() == succ) {
        ranges_.NarrowUpdate(succ, lhs,
                             lhs_range.ConstrainLessEqual(rhs_range));
        ranges_.NarrowUpdate(succ, rhs, rhs_range.ConstraintGreater(lhs_range));
      } else {
        DCHECK_EQ(node->if_false(), succ);
        ranges_.NarrowUpdate(succ, lhs, lhs_range.ConstraintGreater(rhs_range));
        ranges_.NarrowUpdate(succ, rhs,
                             rhs_range.ConstrainLessEqual(lhs_range));
      }
    }
  }

  void ProcessControlNodeFor(ControlNode* node, BasicBlock* succ) {}

  void ProcessNodeBase(ControlNode* node, BasicBlock* succ) {
    switch (node->opcode()) {
#define CASE(OPCODE)                                   \
  case Opcode::k##OPCODE:                              \
    ProcessControlNodeFor(node->Cast<OPCODE>(), succ); \
    break;
      CONTROL_NODE_LIST(CASE)
#undef CASE
      default:
        UNREACHABLE();
    }
  }

  bool is_done() const { return is_done_; }

 private:
  NodeRanges& ranges_;
  BasicBlock* current_block_ = nullptr;
  bool is_done_ = false;

  Range Get(ValueNode* node) {
    DCHECK_NOT_NULL(current_block_);
    return ranges_.Get(current_block_, node);
  }

  void UnionUpdate(ValueNode* node, Range range) {
    DCHECK_NOT_NULL(current_block_);
    ranges_.UnionUpdate(current_block_, node, range);
  }

  void ProcessPhis(BasicBlock* block, BasicBlock* pred) {
    int predecessor_id = -1;
    for (int i = 0; i < block->predecessor_count(); ++i) {
      if (block->predecessor_at(i) == pred) {
        predecessor_id = i;
        break;
      }
    }
    DCHECK_NE(predecessor_id, -1);
    for (Phi* phi : *block->phis()) {
      ranges_.UnionUpdate(block, phi,
                          ranges_.Get(pred, phi->input_node(predecessor_id)));
    }
  }

  // Returns true if the loop reach a fixpoint.
  bool ProcessLoopPhisBackedge(BasicBlock* block, BasicBlock* backedge_pred) {
    if (!block->has_phi()) return true;
    DCHECK_EQ(backedge_pred, block->backedge_predecessor());
    ranges_.EnsureMapExistsFor(block);  // TODO: not sure if needed
    TRACE_RANGE("[range] >>> Processing backedges for block b" << block->id());
    int backedge_id = block->state()->predecessor_count() - 1;
    bool is_done = true;
    for (Phi* phi : *block->phis()) {
      Range range = ranges_.Get(block, phi);
      Range backedge = ranges_.Get(backedge_pred, phi->input_node(backedge_id));
      Range widened = Range::Widen(range, backedge);
      TRACE_RANGE("[ranges]: Processing " << PrintNodeLabel(phi) << ": "
                                          << PrintNode(phi) << ":");
      TRACE_RANGE("  before = " << range);
      TRACE_RANGE("  new    = " << backedge);
      TRACE_RANGE("  widen  = " << widened);
      if (range != widened) {
        TRACE_RANGE("[range] FIXPOINT NOT REACHED");
        is_done = false;
        ranges_.UnionUpdate(block, phi, widened);
      }
      TRACE_RANGE("[range] <<<< End of processing backedges for block b"
                  << block->id());
    }
    return is_done;
  }
};

inline void NodeRanges::ProcessGraph() {
  // TODO(victorgomes): The first pass could be shared with another
  // optimization.
  GraphProcessor<RangeProcessor> processor(*this);
  while (!processor.node_processor().is_done()) {
    processor.ProcessGraph(graph_);
  }
}

}  // namespace v8::internal::maglev

#endif  // V8_MAGLEV_MAGLEV_RANGE_ANALYSIS_H_
