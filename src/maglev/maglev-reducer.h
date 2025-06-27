// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_REDUCER_H_
#define V8_MAGLEV_MAGLEV_REDUCER_H_

#include <utility>

#include "src/base/logging.h"
#include "src/codegen/source-position.h"
#include "src/compiler/feedback-source.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace maglev {

// TODO(victorgomes): Unify ScopedModification with Turboshaft one.
// Set `*ptr` to `new_value` while the scope is active, reset to the previous
// value upon destruction.
template <class T>
class ScopedModification {
 public:
  ScopedModification(T* ptr, T new_value)
      : ptr_(ptr), old_value_(std::move(*ptr)) {
    *ptr = std::move(new_value);
  }

  ~ScopedModification() { *ptr_ = std::move(old_value_); }

  const T& old_value() const { return old_value_; }

 private:
  T* ptr_;
  T old_value_;
};

template <typename BaseT>
concept ReducerBaseWithKNA = requires(BaseT* b) { b->known_node_aspects(); };

template <typename BaseT>
concept ReducerBaseWithEagerDeopt =
    requires(BaseT* b) { b->GetDeoptFrameForEagerDeopt(); };

template <typename BaseT>
concept ReducerBaseWithLazyDeopt = requires(BaseT* b) {
  b->GetDeoptFrameForLazyDeopt();
  // TODO(victorgomes): Bring exception handler logic to the reducer?
  b->AttachExceptionHandlerInfo(std::declval<Node*>());
};

template <typename NodeT, typename BaseT>
concept ReducerBaseWithEffectTracking = requires(BaseT* b) {
  b->template MarkPossibleSideEffect<NodeT>(std::declval<NodeT*>());
};

enum class UseReprHintRecording { kRecord, kDoNotRecord };

// TODO(victorgomes): We'll need a more fine grain position, like At(index) or
// After(node).
enum class BasicBlockPosition {
  kStart,
  kEnd,
};

template <typename BaseT>
class MaglevReducer {
 public:
  MaglevReducer(BaseT* base, Graph* graph,
                MaglevCompilationUnit* compilation_unit = nullptr)
      : base_(base),
        graph_(graph),
        zone_(graph->zone()),
        broker_(graph->broker()),
        current_provenance_{compilation_unit, BytecodeOffset::None(),
                            SourcePosition::Unknown()},
#ifdef DEBUG
        new_nodes_current_period_(zone()),
#endif  // DEBUG
        new_nodes_at_start_(zone()),
        new_nodes_at_end_(zone()) {
    new_nodes_at_start_.reserve(8);
    new_nodes_at_end_.reserve(32);
  }

  ~MaglevReducer() {
    DCHECK(new_nodes_at_start_.empty());
    DCHECK(new_nodes_at_end_.empty());
  }

  // Add a new node with a dynamic set of inputs which are initialized by the
  // `post_create_input_initializer` function before the node is added to the
  // graph.
  template <typename NodeT, typename Function, typename... Args>
  NodeT* AddNewNode(size_t input_count,
                    Function&& post_create_input_initializer, Args&&... args);
  // Add a new node with a static set of inputs.
  template <typename NodeT, typename... Args>
  NodeT* AddNewNode(std::initializer_list<ValueNode*> inputs, Args&&... args);
  template <typename NodeT, typename... Args>
  NodeT* AddUnbufferedNewNode(BasicBlock* block,
                              std::initializer_list<ValueNode*> inputs,
                              Args&&... args);

  void AddInitializedNodeToGraph(Node* node);

  std::optional<int32_t> TryGetInt32Constant(ValueNode* value);
  std::optional<double> TryGetFloat64Constant(
      ValueNode* value, TaggedToFloat64ConversionType conversion_type);

  ValueNode* BuildSmiUntag(ValueNode* node);

  ValueNode* BuildNumberOrOddballToFloat64(
      ValueNode* node, NodeType allowed_input_type,
      TaggedToFloat64ConversionType conversion_type);

  // Get a tagged representation node whose value is equivalent to the given
  // node.
  ValueNode* GetTaggedValue(ValueNode* value,
                            UseReprHintRecording record_use_repr_hint =
                                UseReprHintRecording::kRecord);

  // Get an Int32 representation node whose value is equivalent to the given
  // node.
  //
  // Deopts if the value is not exactly representable as an Int32.
  ValueNode* GetInt32(ValueNode* value, bool can_be_heap_number = false);

  // Get a Float64 representation node whose value is equivalent to the given
  // node.
  //
  // Deopts if the value is not exactly representable as a Float64.
  ValueNode* GetFloat64(ValueNode* value);

  ValueNode* GetFloat64ForToNumber(
      ValueNode* value, NodeType allowed_input_type,
      TaggedToFloat64ConversionType conversion_type);

  BasicBlock* current_block() const { return current_block_; }
  void set_current_block(BasicBlock* block) {
    DCHECK(new_nodes_at_start_.empty());
    DCHECK(new_nodes_at_end_.empty());
    current_block_ = block;
  }

  const MaglevGraphLabeller::Provenance& current_provenance() const {
    return current_provenance_;
  }

  void SetBytecodeOffset(int offset) {
    current_provenance_.bytecode_offset = BytecodeOffset(offset);
  }

  void SetSourcePosition(int pos, int inlining_id) {
    current_provenance_.position = SourcePosition(pos, inlining_id);
  }

  void SetStartSourcePosition(int inlining_id) {
    SetSourcePosition(
        current_provenance_.unit->shared_function_info().StartPosition(),
        inlining_id);
  }

  void RegisterNode(NodeBase* node) {
    graph_labeller()->RegisterNode(node, &current_provenance_);
  }

  // TODO(victorgomes): Delete these access (or move to private) when the
  // speculation scope is moved inside MaglevReducer.
  compiler::FeedbackSource current_speculation_feedback() const {
    return current_speculation_feedback_;
  }
  void set_current_speculation_feedback(
      compiler::FeedbackSource feedback_source) {
    current_speculation_feedback_ = feedback_source;
  }

  void FlushNodesToBlock();

  void SetNewNodePosition(BasicBlockPosition position) {
    current_block_position_ = position;
  }

#ifdef DEBUG
  // TODO(victorgomes): Investigate if we can create a better API for this!
  void StartNewPeriod() { new_nodes_current_period_.clear(); }
  bool WasNodeCreatedDuringCurrentPeriod(ValueNode* node) const {
    return new_nodes_current_period_.count(node) > 0;
  }

  Node* GetLastNewNodeInCurrentBlockPosition() const {
    switch (current_block_position_) {
      case BasicBlockPosition::kStart:
        DCHECK(!new_nodes_at_start_.empty());
        return new_nodes_at_start_.back();
      case BasicBlockPosition::kEnd:
        DCHECK(!new_nodes_at_end_.empty());
        return new_nodes_at_end_.back();
    }
  }
#endif  // DEBUG

 protected:
  class LazyDeoptResultLocationScope;

  enum class AddNewNodeMode {
    kBuffered,
    kUnbuffered,  // Only support insertion at block end.
  };

  template <typename NodeT, typename... Args>
  NodeT* AddNewNodeOrGetEquivalent(std::initializer_list<ValueNode*> raw_inputs,
                                   Args&&... args);

  template <UseReprHintRecording hint = UseReprHintRecording::kRecord>
  ValueNode* ConvertInputTo(ValueNode* input, ValueRepresentation expected);

  template <typename NodeT>
  static constexpr UseReprHintRecording ShouldRecordUseReprHint() {
    // We do not record a Tagged use on Return, since they are never on the hot
    // path, and will lead to a maximum of one additional Tagging operation in
    // the worst case. This allows loop accumulator to be untagged even if they
    // are later returned.
    if constexpr (std::is_same_v<NodeT, Return>) {
      return UseReprHintRecording::kDoNotRecord;
    } else {
      return UseReprHintRecording::kRecord;
    }
  }

  template <typename NodeT>
  void SetNodeInputs(NodeT* node, std::initializer_list<ValueNode*> inputs);
  template <typename NodeT>
  NodeT* AttachExtraInfoAndAddToGraph(NodeT* node);
  template <typename NodeT>
  void AttachDeoptCheckpoint(NodeT* node);
  template <typename NodeT>
  void AttachEagerDeoptInfo(NodeT* node);
  template <typename NodeT>
  void AttachLazyDeoptInfo(NodeT* node);
  template <typename NodeT>
  void AttachExceptionHandlerInfo(NodeT* node);
  template <typename NodeT>
  void MarkPossibleSideEffect(NodeT* node);

  std::optional<ValueNode*> TryGetConstantAlternative(ValueNode* node);

  bool CheckType(ValueNode* node, NodeType type, NodeType* old = nullptr) {
    return known_node_aspects().CheckType(broker(), node, type, old);
  }
  NodeType CheckTypes(ValueNode* node, std::initializer_list<NodeType> types) {
    return known_node_aspects().CheckTypes(broker(), node, types);
  }
  bool EnsureType(ValueNode* node, NodeType type, NodeType* old = nullptr) {
    return known_node_aspects().EnsureType(broker(), node, type, old);
  }
  NodeType GetType(ValueNode* node) {
    return known_node_aspects().GetType(broker(), node);
  }
  NodeInfo* GetOrCreateInfoFor(ValueNode* node) {
    return known_node_aspects().GetOrCreateInfoFor(broker(), node);
  }
  // Returns true if we statically know that {lhs} and {rhs} have disjoint
  // types.
  bool HaveDisjointTypes(ValueNode* lhs, ValueNode* rhs) {
    return known_node_aspects().HaveDisjointTypes(broker(), lhs, rhs);
  }
  bool HasDisjointType(ValueNode* lhs, NodeType rhs_type) {
    return known_node_aspects().HasDisjointType(broker(), lhs, rhs_type);
  }

  KnownNodeAspects& known_node_aspects() {
    static_assert(ReducerBaseWithKNA<BaseT>);
    return base_->known_node_aspects();
  }

  Zone* zone() const { return zone_; }
  Graph* graph() const { return graph_; }
  compiler::JSHeapBroker* broker() const { return broker_; }

  bool has_graph_labeller() const { return graph()->has_graph_labeller(); }
  MaglevGraphLabeller* graph_labeller() const {
    return graph()->graph_labeller();
  }

 private:
  BaseT* base_;

  Graph* graph_;
  Zone* zone_;
  compiler::JSHeapBroker* broker_;

  MaglevGraphLabeller::Provenance current_provenance_;
  BasicBlock* current_block_ = nullptr;
  BasicBlockPosition current_block_position_ = BasicBlockPosition::kEnd;
  AddNewNodeMode add_new_node_mode_ = AddNewNodeMode::kBuffered;

#ifdef DEBUG
  // This is used for dcheck purposes, it is the set of all nodes created in
  // the current "period". Where period is defined by Base. In the case of the
  // GraphBuilder is all nodes created while visiting a bytecode.
  ZoneSet<Node*> new_nodes_current_period_;
#endif  // DEBUG

  // New node buffers.
  ZoneVector<Node*> new_nodes_at_start_;
  ZoneVector<Node*> new_nodes_at_end_;

  compiler::FeedbackSource current_speculation_feedback_ = {};
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_REDUCER_H_
