// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_GRAPH_BUILDER_H_
#define V8_MAGLEV_MAGLEV_GRAPH_BUILDER_H_

#include <type_traits>

#include "src/compiler/bytecode-analysis.h"
#include "src/compiler/bytecode-liveness-map.h"
#include "src/compiler/heap-refs.h"
#include "src/compiler/js-heap-broker.h"
#include "src/maglev/maglev-compilation-data.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-ir.h"
#include "src/utils/memcopy.h"

namespace v8 {
namespace internal {
namespace maglev {

class MaglevGraphBuilder {
 public:
  explicit MaglevGraphBuilder(MaglevCompilationUnit* compilation_unit)
      : compilation_unit_(compilation_unit),
        iterator_(bytecode().object()),
        jump_targets_(zone()->NewArray<BasicBlockRef>(bytecode().length())),
        // Overallocate merge_states_ by one to allow always looking up the
        // next offset.
        merge_states_(zone()->NewArray<MergePointInterpreterFrameState*>(
            bytecode().length() + 1)),
        graph_(zone()),
        current_interpreter_frame_(*compilation_unit_) {
    memset(merge_states_, 0,
           bytecode().length() * sizeof(InterpreterFrameState*));
    // Default construct basic block refs.
    // TODO(leszeks): This could be a memset of nullptr to ..._jump_targets_.
    for (int i = 0; i < bytecode().length(); ++i) {
      new (&jump_targets_[i]) BasicBlockRef();
    }

    CalculatePredecessorCounts();

    for (auto& offset_and_info : bytecode_analysis().GetLoopInfos()) {
      int offset = offset_and_info.first;
      const compiler::LoopInfo& loop_info = offset_and_info.second;

      const compiler::BytecodeLivenessState* liveness =
          bytecode_analysis().GetInLivenessFor(offset);

      merge_states_[offset] = zone()->New<MergePointInterpreterFrameState>(
          *compilation_unit_, offset, NumPredecessors(offset), liveness,
          &loop_info);
    }

    current_block_ = zone()->New<BasicBlock>(nullptr);
    block_offset_ = -1;

    for (int i = 0; i < parameter_count(); i++) {
      interpreter::Register reg = interpreter::Register::FromParameterIndex(i);
      current_interpreter_frame_.set(reg, AddNewNode<InitialValue>({}, reg));
    }

    // TODO(leszeks): Extract out a separate "incoming context/closure" nodes,
    // to be able to read in the machine register but also use the frame-spilled
    // slot.
    interpreter::Register regs[] = {interpreter::Register::current_context(),
                                    interpreter::Register::function_closure()};
    for (interpreter::Register& reg : regs) {
      current_interpreter_frame_.set(reg, AddNewNode<InitialValue>({}, reg));
    }

    interpreter::Register new_target_or_generator_register =
        bytecode().incoming_new_target_or_generator_register();

    const compiler::BytecodeLivenessState* liveness =
        bytecode_analysis().GetInLivenessFor(0);
    int register_index = 0;
    // TODO(leszeks): Don't emit if not needed.
    ValueNode* undefined_value =
        AddNewNode<RootConstant>({}, RootIndex::kUndefinedValue);
    if (new_target_or_generator_register.is_valid()) {
      int new_target_index = new_target_or_generator_register.index();
      for (; register_index < new_target_index; register_index++) {
        StoreRegister(interpreter::Register(register_index), undefined_value,
                      liveness);
      }
      StoreRegister(
          new_target_or_generator_register,
          // TODO(leszeks): Expose in Graph.
          AddNewNode<RegisterInput>({}, kJavaScriptCallNewTargetRegister),
          liveness);
      register_index++;
    }
    for (; register_index < register_count(); register_index++) {
      StoreRegister(interpreter::Register(register_index), undefined_value,
                    liveness);
    }

    BasicBlock* first_block = CreateBlock<Jump>({}, &jump_targets_[0]);
    MergeIntoFrameState(first_block, 0);
  }

  void Build() {
    for (iterator_.Reset(); !iterator_.done(); iterator_.Advance()) {
      VisitSingleBytecode();
      // TODO(v8:7700): Clean up after all bytecodes are supported.
      if (found_unsupported_bytecode()) break;
    }
  }

  Graph* graph() { return &graph_; }

  // TODO(v8:7700): Clean up after all bytecodes are supported.
  bool found_unsupported_bytecode() const {
    return found_unsupported_bytecode_;
  }

 private:
  BasicBlock* CreateEmptyBlock(int offset, BasicBlock* predecessor) {
    DCHECK_NULL(current_block_);
    current_block_ = zone()->New<BasicBlock>(nullptr);
    BasicBlock* result = CreateBlock<Jump>({}, &jump_targets_[offset]);
    result->set_empty_block_predecessor(predecessor);
    return result;
  }

  void ProcessMergePoint(int offset) {
    // First copy the merge state to be the current state.
    MergePointInterpreterFrameState& merge_state = *merge_states_[offset];
    current_interpreter_frame_.CopyFrom(*compilation_unit_, merge_state);

    if (merge_state.predecessor_count() == 1) return;

    // Set up edge-split.
    int predecessor_index = merge_state.predecessor_count() - 1;
    BasicBlockRef* old_jump_targets = jump_targets_[offset].Reset();
    while (old_jump_targets != nullptr) {
      BasicBlock* predecessor = merge_state.predecessor_at(predecessor_index);
      ControlNode* control = predecessor->control_node();
      if (control->Is<ConditionalControlNode>()) {
        // CreateEmptyBlock automatically registers itself with the offset.
        predecessor = CreateEmptyBlock(offset, predecessor);
        // Set the old predecessor's (the conditional block) reference to
        // point to the new empty predecessor block.
        old_jump_targets =
            old_jump_targets->SetToBlockAndReturnNext(predecessor);
      } else {
        // Re-register the block in the offset's ref list.
        old_jump_targets =
            old_jump_targets->MoveToRefList(&jump_targets_[offset]);
      }
      predecessor->set_predecessor_id(predecessor_index--);
    }
#ifdef DEBUG
    if (bytecode_analysis().IsLoopHeader(offset)) {
      // For loops, the JumpLoop block hasn't been generated yet, and so isn't
      // in the list of jump targets. It's defined to be at index 0, so once
      // we've processed all the jump targets, the 0 index should be the one
      // remaining.
      DCHECK_EQ(predecessor_index, 0);
    } else {
      DCHECK_EQ(predecessor_index, -1);
    }
#endif
    if (has_graph_labeller()) {
      for (Phi* phi : *merge_states_[offset]->phis()) {
        graph_labeller()->RegisterNode(phi);
      }
    }
  }

  void VisitSingleBytecode() {
    int offset = iterator_.current_offset();
    if (V8_UNLIKELY(merge_states_[offset] != nullptr)) {
      if (current_block_ != nullptr) {
        DCHECK(!current_block_->nodes().is_empty());
        FinishBlock<Jump>(offset, {}, &jump_targets_[offset]);

        merge_states_[offset]->Merge(*compilation_unit_,
                                     current_interpreter_frame_,
                                     graph_.last_block(), offset);
      }
      ProcessMergePoint(offset);
      StartNewBlock(offset);
    }
    DCHECK_NOT_NULL(current_block_);
    switch (iterator_.current_bytecode()) {
#define BYTECODE_CASE(name, ...)       \
  case interpreter::Bytecode::k##name: \
    Visit##name();                     \
    break;
      BYTECODE_LIST(BYTECODE_CASE)
#undef BYTECODE_CASE
    }
  }

#define BYTECODE_VISITOR(name, ...) void Visit##name();
  BYTECODE_LIST(BYTECODE_VISITOR)
#undef BYTECODE_VISITOR

  template <typename NodeT>
  NodeT* AddNode(NodeT* node) {
    current_block_->nodes().Add(node);
    return node;
  }

  template <typename NodeT, typename... Args>
  NodeT* NewNode(size_t input_count, Args&&... args) {
    NodeT* node =
        Node::New<NodeT>(zone(), input_count, std::forward<Args>(args)...);
    if (has_graph_labeller()) graph_labeller()->RegisterNode(node);
    return node;
  }

  template <typename NodeT, typename... Args>
  NodeT* AddNewNode(size_t input_count, Args&&... args) {
    return AddNode(NewNode<NodeT>(input_count, std::forward<Args>(args)...));
  }

  template <typename NodeT, typename... Args>
  NodeT* NewNode(std::initializer_list<ValueNode*> inputs, Args&&... args) {
    NodeT* node = Node::New<NodeT>(zone(), inputs, std::forward<Args>(args)...);
    if (has_graph_labeller()) graph_labeller()->RegisterNode(node);
    return node;
  }

  template <typename NodeT, typename... Args>
  NodeT* AddNewNode(std::initializer_list<ValueNode*> inputs, Args&&... args) {
    return AddNode(NewNode<NodeT>(inputs, std::forward<Args>(args)...));
  }

  ValueNode* GetContext() const {
    return current_interpreter_frame_.get(
        interpreter::Register::current_context());
  }

  FeedbackSlot GetSlotOperand(int operand_index) {
    return iterator_.GetSlotOperand(operand_index);
  }

  template <class T, typename = std::enable_if_t<
                         std::is_convertible<T*, Object*>::value>>
  typename compiler::ref_traits<T>::ref_type GetRefOperand(int operand_index) {
    return MakeRef(broker(),
                   Handle<T>::cast(iterator_.GetConstantForIndexOperand(
                       operand_index, isolate())));
  }

  void SetAccumulator(ValueNode* node) {
    current_interpreter_frame_.set_accumulator(node);
  }

  ValueNode* GetAccumulator() const {
    return current_interpreter_frame_.accumulator();
  }

  ValueNode* LoadRegister(int operand_index) {
    interpreter::Register source = iterator_.GetRegisterOperand(operand_index);
    return current_interpreter_frame_.get(source);
  }

  void StoreRegister(interpreter::Register target, ValueNode* value,
                     const compiler::BytecodeLivenessState* liveness) {
    if (target.index() >= 0 && !liveness->RegisterIsLive(target.index())) {
      return;
    }
    current_interpreter_frame_.set(target, value);
    AddNewNode<StoreToFrame>({}, value, target);
  }

  void AddCheckpoint() {
    // TODO(v8:7700): Verify this calls the initializer list overload.
    AddNewNode<Checkpoint>({}, iterator_.current_offset(),
                           GetInLiveness()->AccumulatorIsLive(),
                           GetAccumulator());
    has_valid_checkpoint_ = true;
  }

  void EnsureCheckpoint() {
    if (!has_valid_checkpoint_) AddCheckpoint();
  }

  void MarkPossibleSideEffect() {
    // If there was a potential side effect, invalidate the previous checkpoint.
    has_valid_checkpoint_ = false;
  }

  int next_offset() const {
    return iterator_.current_offset() + iterator_.current_bytecode_size();
  }
  const compiler::BytecodeLivenessState* GetInLiveness() const {
    return bytecode_analysis().GetInLivenessFor(iterator_.current_offset());
  }
  const compiler::BytecodeLivenessState* GetOutLiveness() const {
    return bytecode_analysis().GetOutLivenessFor(iterator_.current_offset());
  }

  void StartNewBlock(int offset) {
    DCHECK_NULL(current_block_);
    current_block_ = zone()->New<BasicBlock>(merge_states_[offset]);
    block_offset_ = offset;
  }

  template <typename ControlNodeT, typename... Args>
  BasicBlock* CreateBlock(std::initializer_list<ValueNode*> control_inputs,
                          Args&&... args) {
    current_block_->set_control_node(NodeBase::New<ControlNodeT>(
        zone(), control_inputs, std::forward<Args>(args)...));

    BasicBlock* block = current_block_;
    current_block_ = nullptr;

    graph_.Add(block);
    if (has_graph_labeller()) {
      graph_labeller()->RegisterBasicBlock(block);
    }
    return block;
  }

  template <typename ControlNodeT, typename... Args>
  BasicBlock* FinishBlock(int next_block_offset,
                          std::initializer_list<ValueNode*> control_inputs,
                          Args&&... args) {
    BasicBlock* block =
        CreateBlock<ControlNodeT>(control_inputs, std::forward<Args>(args)...);

    // Resolve pointers to this basic block.
    BasicBlockRef* jump_target_refs_head =
        jump_targets_[block_offset_].SetToBlockAndReturnNext(block);
    while (jump_target_refs_head != nullptr) {
      jump_target_refs_head =
          jump_target_refs_head->SetToBlockAndReturnNext(block);
    }
    DCHECK_EQ(jump_targets_[block_offset_].block_ptr(), block);

    // If the next block has merge states, then it's not a simple fallthrough,
    // and we should reset the checkpoint validity.
    if (merge_states_[next_block_offset] != nullptr) {
      has_valid_checkpoint_ = false;
    }
    // Start a new block for the fallthrough path, unless it's a merge point, in
    // which case we merge our state into it. That merge-point could also be a
    // loop header, in which case the merge state might not exist yet (if the
    // only predecessors are this path and the JumpLoop).
    if (std::is_base_of<ConditionalControlNode, ControlNodeT>::value) {
      if (NumPredecessors(next_block_offset) == 1) {
        StartNewBlock(next_block_offset);
      } else {
        DCHECK_NULL(current_block_);
        MergeIntoFrameState(block, next_block_offset);
      }
    }
    return block;
  }

  template <typename RelNodeT>
  void VisitRelNode();

  void MergeIntoFrameState(BasicBlock* block, int target);
  void BuildBranchIfTrue(ValueNode* node, int true_target, int false_target);
  void BuildBranchIfToBooleanTrue(ValueNode* node, int true_target,
                                  int false_target);

  void CalculatePredecessorCounts() {
    // Add 1 after the end of the bytecode so we can always write to the offset
    // after the last bytecode.
    size_t array_length = bytecode().length() + 1;
    predecessors_ = zone()->NewArray<uint32_t>(array_length);
    MemsetUint32(predecessors_, 1, array_length);

    interpreter::BytecodeArrayIterator iterator(bytecode().object());
    for (; !iterator.done(); iterator.Advance()) {
      interpreter::Bytecode bytecode = iterator.current_bytecode();
      if (interpreter::Bytecodes::IsJump(bytecode)) {
        predecessors_[iterator.GetJumpTargetOffset()]++;
        if (!interpreter::Bytecodes::IsConditionalJump(bytecode)) {
          predecessors_[iterator.next_offset()]--;
        }
      } else if (interpreter::Bytecodes::IsSwitch(bytecode)) {
        for (auto offset : iterator.GetJumpTableTargetOffsets()) {
          predecessors_[offset.target_offset]++;
        }
      } else if (interpreter::Bytecodes::Returns(bytecode) ||
                 interpreter::Bytecodes::UnconditionallyThrows(bytecode)) {
        predecessors_[iterator.next_offset()]--;
      }
      // TODO(leszeks): Also consider handler entries (the bytecode analysis)
      // will do this automatically I guess if we merge this into that.
    }
    DCHECK_EQ(0, predecessors_[bytecode().length()]);
  }

  int NumPredecessors(int offset) { return predecessors_[offset]; }

  compiler::JSHeapBroker* broker() const { return compilation_unit_->broker(); }
  const compiler::FeedbackVectorRef& feedback() const {
    return compilation_unit_->feedback;
  }
  const compiler::BytecodeArrayRef& bytecode() const {
    return compilation_unit_->bytecode;
  }
  const compiler::BytecodeAnalysis& bytecode_analysis() const {
    return compilation_unit_->bytecode_analysis;
  }
  Isolate* isolate() const { return compilation_unit_->isolate(); }
  Zone* zone() const { return compilation_unit_->zone(); }
  int parameter_count() const { return compilation_unit_->parameter_count(); }
  int register_count() const { return compilation_unit_->register_count(); }
  bool has_graph_labeller() const {
    return compilation_unit_->has_graph_labeller();
  }
  MaglevGraphLabeller* graph_labeller() const {
    return compilation_unit_->graph_labeller();
  }

  MaglevCompilationUnit* const compilation_unit_;
  interpreter::BytecodeArrayIterator iterator_;
  uint32_t* predecessors_;

  // Current block information.
  BasicBlock* current_block_ = nullptr;
  int block_offset_ = 0;
  bool has_valid_checkpoint_ = false;

  BasicBlockRef* jump_targets_;
  MergePointInterpreterFrameState** merge_states_;

  Graph graph_;
  InterpreterFrameState current_interpreter_frame_;

  // Allow marking some bytecodes as unsupported during graph building, so that
  // we can test maglev incrementally.
  // TODO(v8:7700): Clean up after all bytecodes are supported.
  bool found_unsupported_bytecode_ = false;
  bool this_field_will_be_unused_once_all_bytecodes_are_supported_;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_GRAPH_BUILDER_H_
