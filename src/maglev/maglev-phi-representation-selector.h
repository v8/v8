// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_PHI_REPRESENTATION_SELECTOR_H_
#define V8_MAGLEV_MAGLEV_PHI_REPRESENTATION_SELECTOR_H_

#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-graph-builder.h"

namespace v8 {
namespace internal {
namespace maglev {

class Graph;

class MaglevPhiRepresentationSelector {
 public:
  explicit MaglevPhiRepresentationSelector(MaglevGraphBuilder* builder)
      : builder_(builder),
        new_nodes_current_block_start_(builder->zone()),
        new_nodes_current_block_end_(builder->zone()) {}

  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) { MergeNewNodesInBlock(current_block_); }
  void PreProcessBasicBlock(BasicBlock* block) {
    MergeNewNodesInBlock(current_block_);
    current_block_ = block;
  }

  void Process(Phi* node, const ProcessingState&);

  void Process(JumpLoop* node, const ProcessingState&) {
    FixLoopPhisBackedge(node->target());
  }

  template <class NodeT>
  void Process(NodeT* node, const ProcessingState&) {
    UpdateNodeInputs(node);
  }

 private:
  // Update the inputs of {phi} so that they all have {repr} representation, and
  // updates {phi}'s representation to {repr}.
  void ConvertTaggedPhiTo(Phi* phi, ValueRepresentation repr);

  // Since this pass changes the representation of Phis, it makes some untagging
  // operations outdated: if we've decided that a Phi should have Int32
  // representation, then we don't need to do a kCheckedSmiUntag before using
  // it. UpdateNodeInputs(n) removes such untagging from {n}'s input (and insert
  // new conversions if needed, from Int32 to Float64 for instance).
  template <class NodeT>
  void UpdateNodeInputs(NodeT* n) {
    NodeBase* node = static_cast<NodeBase*>(n);

    if (IsUntagging(n->opcode()) && node->input(0).node()->Is<Phi>() &&
        node->input(0).node()->value_representation() !=
            ValueRepresentation::kTagged) {
      DCHECK_EQ(node->input_count(), 1);
      // This untagging conversion is outdated, since its input has been
      // untagged. Depending on the conversion, it might need to be replaced by
      // another untagged->untagged conversion, or it might need to be removed
      // alltogether (or rather, replaced by an identity node).
      UpdateUntagging(n->template Cast<ValueNode>());
    } else {
      for (int i = 0; i < n->input_count(); i++) {
        ValueNode* input = node->input(i).node();
        if (input->Is<Identity>()) {
          // Bypassing the identity
          node->change_input(i, input->input(0).node());
        } else if (Phi* phi = input->TryCast<Phi>()) {
          // If the input is a Phi and it was used without any untagging, then
          // we need to retag it.
          // Note that it would be bad to retag the input of an untagging node,
          // but untagging nodes are dealt with earlier in this function, so {n}
          // can't be an untagging of an untagged phi.
          DCHECK_IMPLIES(
              IsUntagging(n->opcode()),
              phi->value_representation() == ValueRepresentation::kTagged);
          // If {n} is a conversion that isn't an untagging, then it has to
          // have been inserted during this phase, because it knows that {phi}
          // isn't tagged. As such, we don't do anything in that case.
          if (!n->properties().is_conversion()) {
            node->change_input(
                i, TagPhi(phi, current_block_, NewNodePosition::kStart));
          }
        }
      }
    }
  }

  void EnsurePhiInputsTagged(Phi* phi);

  // Returns true if {op} is an untagging node.
  bool IsUntagging(Opcode op);

  // Updates {old_untagging} to reflect that its Phi input has been untagged and
  // that a different conversion is now needed.
  void UpdateUntagging(ValueNode* old_untagging);

  // NewNodePosition is used to represent where a new node should be inserted:
  // at the start of a block (kStart), at the end of a block (kEnd).
  enum class NewNodePosition { kStart, kEnd };

  // Returns a tagged node that represents a tagged version of {phi}.
  ValueNode* TagPhi(Phi* phi, BasicBlock* block, NewNodePosition pos);

  ValueNode* AddNode(ValueNode* node, BasicBlock* block, NewNodePosition pos);

  // Merges the nodes from {new_nodes_current_block_start_} and
  // {new_nodes_current_block_end_} into their destinations.
  void MergeNewNodesInBlock(BasicBlock* block);

  // If {block} jumps back to the start of a loop header, FixLoopPhisBackedge
  // inserts the necessary tagging on the backedge of the loop Phis of the loop
  // header.
  void FixLoopPhisBackedge(BasicBlock* block);

  MaglevGraphBuilder* builder_ = nullptr;
  BasicBlock* current_block_ = nullptr;

  // {new_nodes_current_block_start_}, {new_nodes_current_block_end_} and
  // are used to store new nodes added by this pass, but to delay their
  // insertion into their destination, in order to not mutate the linked list of
  // nodes of the current block while also iterating it. Nodes in
  // {new_nodes_current_block_start_} and {new_nodes_current_block_end_} will be
  // inserted respectively at the begining and the end of the current block.
  ZoneVector<Node*> new_nodes_current_block_start_;
  ZoneVector<Node*> new_nodes_current_block_end_;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_PHI_REPRESENTATION_SELECTOR_H_
