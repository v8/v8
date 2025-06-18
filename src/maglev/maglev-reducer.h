// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_REDUCER_H_
#define V8_MAGLEV_MAGLEV_REDUCER_H_

#include <utility>

#include "src/codegen/source-position.h"
#include "src/compiler/feedback-source.h"
#include "src/flags/flags.h"
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
concept ReducerBase = requires(BaseT b) {
  b.GetDeoptFrameForEagerDeopt();
  b.GetDeoptFrameForLazyDeopt();
  // TODO(victorgomes): Bring exception handler logic to the reducer?
  b.AttachExceptionHandlerInfo(std::declval<Node*>());
  b.known_node_aspects();
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
    requires ReducerBase<BaseT>
      : base_(base),
        graph_(graph),
        zone_(graph->zone()),
        broker_(graph->broker()),
        current_provenance_{compilation_unit, BytecodeOffset::None(),
                            SourcePosition::Unknown()},
        new_nodes_at_start_(zone()),
        new_nodes_at_end_(zone()),
        is_cse_enabled_(v8_flags.maglev_cse) {
    new_nodes_at_start_.reserve(8);
    new_nodes_at_end_.reserve(32);
  }

  // Add a new node with a dynamic set of inputs which are initialized by the
  // `post_create_input_initializer` function before the node is added to the
  // graph.
  template <typename NodeT, typename Function, typename... Args>
  NodeT* AddNewNode(size_t input_count,
                    Function&& post_create_input_initializer, Args&&... args) {
    NodeT* node =
        NodeBase::New<NodeT>(zone(), input_count, std::forward<Args>(args)...);
    post_create_input_initializer(node);
    return AttachExtraInfoAndAddToGraph(node);
  }

  // Add a new node with a static set of inputs.
  template <typename NodeT, typename... Args>
  NodeT* AddNewNode(std::initializer_list<ValueNode*> inputs, Args&&... args) {
    static_assert(IsFixedInputNode<NodeT>());
    if constexpr (Node::participate_in_cse(Node::opcode_of<NodeT>)) {
      if (is_cse_enabled_) {
        return AddNewNodeOrGetEquivalent<NodeT>(inputs,
                                                std::forward<Args>(args)...);
      }
    }
    NodeT* node = NodeBase::New<NodeT>(zone(), inputs.size(),
                                       std::forward<Args>(args)...);
    SetNodeInputs(node, inputs);
    return AttachExtraInfoAndAddToGraph(node);
  }

  template <typename NodeT, typename... Args>
  NodeT* AddUnbufferedNewNode(BasicBlock* block,
                              std::initializer_list<ValueNode*> inputs,
                              Args&&... args) {
    ScopedModification<BasicBlock*> save_block(&current_block_, block);
    DCHECK_EQ(add_new_node_mode_, AddNewNodeMode::kBuffered);
    add_new_node_mode_ = AddNewNodeMode::kUnbuffered;
    NodeT* node = AddNewNode<NodeT>(inputs, std::forward<Args>(args)...);
    add_new_node_mode_ = AddNewNodeMode::kBuffered;
    return node;
  }

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

  void FlushNodesToBlock() {
    ZoneVector<Node*>& nodes = current_block()->nodes();
    if (new_nodes_at_end_.size() > 0) {
      size_t old_size = nodes.size();
      nodes.resize(old_size + new_nodes_at_end_.size());
      std::copy(new_nodes_at_end_.begin(), new_nodes_at_end_.end(),
                nodes.begin() + old_size);
      new_nodes_at_end_.clear();
    }

    if (new_nodes_at_start_.size() > 0) {
      size_t diff = new_nodes_at_start_.size();
      if (diff == 0) return;
      size_t old_size = nodes.size();
      nodes.resize(old_size + new_nodes_at_start_.size());
      auto begin = nodes.begin();
      std::copy_backward(begin, begin + old_size, nodes.end());
      std::copy(new_nodes_at_start_.begin(), new_nodes_at_start_.end(), begin);
      new_nodes_at_start_.clear();
    }
  }

  void SetNewNodePosition(BasicBlockPosition position) {
    current_block_position_ = position;
  }

  void SetEnableCSE(bool value) { is_cse_enabled_ = value; }

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
  ValueNode* ConvertInputTo(ValueNode* input, ValueRepresentation expected) {
    ValueRepresentation repr = input->properties().value_representation();
    if (repr == expected) return input;
    switch (expected) {
      case ValueRepresentation::kTagged:
        return GetTaggedValue(input, hint);
      case ValueRepresentation::kInt32:
        return GetInt32(input);
      case ValueRepresentation::kFloat64:
      case ValueRepresentation::kHoleyFloat64:
        return GetFloat64(input);
      case ValueRepresentation::kUint32:
      case ValueRepresentation::kIntPtr:
        // These conversion should be explicitly done beforehand.
        UNREACHABLE();
    }
  }

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
  void SetNodeInputs(NodeT* node, std::initializer_list<ValueNode*> inputs) {
    // Nodes with zero input count don't have kInputTypes defined.
    if constexpr (NodeT::kInputCount > 0) {
      constexpr UseReprHintRecording hint = ShouldRecordUseReprHint<NodeT>();
      int i = 0;
      for (ValueNode* input : inputs) {
        DCHECK_NOT_NULL(input);
        node->set_input(i, ConvertInputTo<hint>(input, NodeT::kInputTypes[i]));
        i++;
      }
    }
  }

  template <typename NodeT>
  NodeT* AttachExtraInfoAndAddToGraph(NodeT* node) {
    static_assert(NodeT::kProperties.is_deopt_checkpoint() +
                      NodeT::kProperties.can_eager_deopt() +
                      NodeT::kProperties.can_lazy_deopt() <=
                  1);
    AttachDeoptCheckpoint(node);
    AttachEagerDeoptInfo(node);
    AttachLazyDeoptInfo(node);
    AttachExceptionHandlerInfo(node);
    AddInitializedNodeToGraph(node);
    MarkPossibleSideEffect(node);
    return node;
  }

  template <typename NodeT>
  void AttachDeoptCheckpoint(NodeT* node) {
    if constexpr (NodeT::kProperties.is_deopt_checkpoint()) {
      node->SetEagerDeoptInfo(zone(), base_->GetDeoptFrameForEagerDeopt());
    }
  }

  template <typename NodeT>
  void AttachEagerDeoptInfo(NodeT* node) {
    if constexpr (NodeT::kProperties.can_eager_deopt()) {
      node->SetEagerDeoptInfo(zone(), base_->GetDeoptFrameForEagerDeopt(),
                              current_speculation_feedback_);
    }
  }

  template <typename NodeT>
  void AttachLazyDeoptInfo(NodeT* node) {
    if constexpr (NodeT::kProperties.can_lazy_deopt()) {
      auto [deopt_frame, result_location, result_size] =
          base_->GetDeoptFrameForLazyDeopt();
      new (node->lazy_deopt_info())
          LazyDeoptInfo(zone(), deopt_frame, result_location, result_size,
                        current_speculation_feedback_);
    }
  }

  template <typename NodeT>
  void AttachExceptionHandlerInfo(NodeT* node) {
    if constexpr (NodeT::kProperties.can_throw()) {
      base_->AttachExceptionHandlerInfo(node);
    }
  }

  template <typename NodeT>
  void MarkPossibleSideEffect(NodeT* node) {
    // Don't do anything for nodes without side effects.
    if constexpr (!NodeT::kProperties.can_write()) return;

    if (is_cse_enabled_) {
      known_node_aspects().increment_effect_epoch();
    }
  }

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

  KnownNodeAspects& known_node_aspects() { return base_->known_node_aspects(); }

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

  // New node buffers.
  ZoneVector<Node*> new_nodes_at_start_;
  ZoneVector<Node*> new_nodes_at_end_;

  bool is_cse_enabled_;

  compiler::FeedbackSource current_speculation_feedback_ = {};
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_REDUCER_H_
