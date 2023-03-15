// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-phi-representation-selector.h"

#include "src/handles/handles-inl.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

namespace {

constexpr int representation_mask_for(ValueRepresentation r) {
  if (r == ValueRepresentation::kTagged) return 0;
  return 1 << static_cast<int>(r);
}

}  // namespace

void MaglevPhiRepresentationSelector::Process(Phi* node,
                                              const ProcessingState&) {
  DCHECK_EQ(node->value_representation(), ValueRepresentation::kTagged);

  // We negate the Uint32 from {representation_mask} from the start in order to
  // disable Uint32 untagged Phis for now, because they don't seem very useful,
  // and this simplifies things a little bit.
  // TODO(dmercadier): evaluate what benefits untagged Uint32 Phis would bring,
  // and consider allowing them.
  unsigned int representation_mask =
      ~representation_mask_for(ValueRepresentation::kUint32);

  for (int i = 0; i < node->input_count(); i++) {
    Node* input = node->input(i).node();
    if (input->Is<SmiConstant>()) {
      // Could be any representation
    } else if (Constant* constant = input->TryCast<Constant>()) {
      if (constant->object().IsHeapNumber()) {
        representation_mask &=
            representation_mask_for(ValueRepresentation::kFloat64);
      } else {
        // Not a Constant that we can untag.
        representation_mask = 0;
        break;
      }
    } else if (input->properties().is_conversion()) {
      DCHECK_EQ(input->input_count(), 1);
      representation_mask &= representation_mask_for(
          input->input(0).node()->properties().value_representation());
    } else {
      representation_mask = 0;
      break;
    }
  }

  if (base::bits::CountPopulation(representation_mask) == 1) {
    ConvertTaggedPhiTo(
        node, static_cast<ValueRepresentation>(
                  base::bits::CountTrailingZeros(representation_mask)));
  } else {
    // No possible representations, or more than 1 possible representation.
    // We'll typically end up in this case if:
    //   - All of the inputs of the Phi are SmiConstant (which can have any
    //     representation)
    //   - Some inputs are Int32 tagging and other inputs are Float64 tagging.
    // If we had information about uses of the node, then we could chose a
    // representation over the other (eg, Float64 rather than Int32), but
    // since we don't, we just bail out to avoid choosing the wrong
    // representation.
    EnsurePhiInputsTagged(node);
  }
}

void MaglevPhiRepresentationSelector::EnsurePhiInputsTagged(Phi* phi) {
  // Since we are untagging some Phis, it's possible that one of the inputs of
  // {phi} is an untagged Phi. However, if this function is called, then we've
  // decided that {phi} is going to stay tagged, and thus, all of its inputs
  // should be tagged. We'll thus insert tagging operation on the untagged phi
  // inputs of {phi}.

  for (int i = 0; i < phi->input_count(); i++) {
    ValueNode* input = phi->input(i).node();
    if (Phi* phi_input = input->TryCast<Phi>()) {
      phi->set_input(i, EnsurePhiTagged(phi_input, phi->predecessor_at(i),
                                        NewNodePosition::kEnd));
    } else {
      // Inputs of Phis that aren't Phi should always be tagged (except for the
      // phis untagged by this class, but {phi} isn't one of them).
      DCHECK(input->is_tagged());
    }
  }
}

void MaglevPhiRepresentationSelector::ConvertTaggedPhiTo(
    Phi* phi, ValueRepresentation repr) {
  phi->change_representation(repr);

  for (int i = 0; i < phi->input_count(); i++) {
    ValueNode* input = phi->input(i).node();
    if (input->Is<SmiConstant>()) {
      switch (repr) {
        case ValueRepresentation::kInt32:
          phi->change_input(i,
                            builder_->GetInt32Constant(
                                input->Cast<SmiConstant>()->value().value()));
          break;
        case ValueRepresentation::kFloat64:
          phi->change_input(i,
                            builder_->GetFloat64Constant(
                                input->Cast<SmiConstant>()->value().value()));
          break;
        case ValueRepresentation::kUint32:
          UNIMPLEMENTED();
        default:
          UNREACHABLE();
      }
    } else if (Constant* constant = input->TryCast<Constant>()) {
      DCHECK(constant->object().IsHeapNumber());
      DCHECK_EQ(repr, ValueRepresentation::kFloat64);
      phi->change_input(i, builder_->GetFloat64Constant(
                               constant->object().AsHeapNumber().value()));
    } else if (input->properties().is_conversion()) {
      // Unwrapping the conversion.
      DCHECK_EQ(input->value_representation(), ValueRepresentation::kTagged);
      DCHECK_EQ(input->input(0).node()->value_representation(), repr);
      phi->set_input(i, input->input(0).node());
    } else {
      DCHECK_EQ(input->value_representation(), repr);
    }
  }
}

bool MaglevPhiRepresentationSelector::IsUntagging(Opcode op) {
  switch (op) {
    case Opcode::kCheckedSmiUntag:
    case Opcode::kUnsafeSmiUntag:
    case Opcode::kCheckedObjectToIndex:
    case Opcode::kCheckedTruncateNumberOrOddballToInt32:
    case Opcode::kTruncateNumberOrOddballToInt32:
    case Opcode::kCheckedNumberOrOddballToFloat64:
    case Opcode::kUncheckedNumberOrOddballToFloat64:
      return true;
    default:
      return false;
  }
}

namespace {

base::Optional<Opcode> GetOpcodeForCheckedConversion(ValueRepresentation from,
                                                     ValueRepresentation to,
                                                     bool truncating) {
  DCHECK_NE(from, ValueRepresentation::kTagged);
  DCHECK_NE(to, ValueRepresentation::kTagged);

  static_assert(static_cast<int>(ValueRepresentation::kInt32) == 1);
  static_assert(static_cast<int>(ValueRepresentation::kUint32) == 2);
  static_assert(static_cast<int>(ValueRepresentation::kFloat64) == 3);

  constexpr int kNumberOfValueRepresentation = 3;

  int truncate_offset = 0;
  if (from == ValueRepresentation::kFloat64 && truncating) {
    truncate_offset = kNumberOfValueRepresentation;
  }

  static base::Optional<Opcode> conversion_table[] = {
      // Int32 ->
      base::nullopt,                  // -> Int32
      Opcode::kCheckedInt32ToUint32,  // -> Uint32
      Opcode::kChangeInt32ToFloat64,  // -> Float64

      // Uint32 ->
      Opcode::kCheckedUint32ToInt32,   // -> Int32
      base::nullopt,                   // -> Uint32
      Opcode::kChangeUint32ToFloat64,  // -> Float64

      // Float64 ->
      Opcode::kCheckedTruncateFloat64ToInt32,  // -> Int32
      base::nullopt,                           // -> Uint32
      // ^ The graph builder never inserts Tagged->Uint32 conversions, so we
      // don't have to handle this case.
      base::nullopt,  // -> Float64

      // Truncating Float64 ->
      Opcode::kTruncateFloat64ToInt32,  // -> Int32
      base::nullopt,                    // -> Uint32
      // ^ The graph builder never inserts Tagged->Uint32 conversions, so we
      // don't have to handle this case.
      base::nullopt  // -> Float64
  };

  // The `-1` are because we don't have ->Tagged and Tagged-> conversions, so
  // since kInt32 is 1, kUint32 is 2 and kFloat64 is 3, we subtract 1 so that
  // they start at 0.
  return conversion_table[(static_cast<int>(from) - 1) *
                              kNumberOfValueRepresentation +
                          static_cast<int>(to) - 1 + truncate_offset];
}

}  // namespace

void MaglevPhiRepresentationSelector::UpdateUntaggingOfPhi(
    ValueNode* old_untagging) {
  DCHECK_EQ(old_untagging->input_count(), 1);
  DCHECK(old_untagging->input(0).node()->Is<Phi>());

  ValueRepresentation from_repr =
      old_untagging->input(0).node()->value_representation();
  ValueRepresentation to_repr = old_untagging->value_representation();

  // Since initially Phis are tagged, it would make not sense for
  // {old_conversion} to convert a Phi to a Tagged value.
  DCHECK_NE(to_repr, ValueRepresentation::kTagged);
  // The graph builder never inserts Tagged->Uint32 conversions (and thus, we
  // don't handle them in GetOpcodeForCheckedConversion).
  DCHECK_NE(to_repr, ValueRepresentation::kUint32);

  if (from_repr == ValueRepresentation::kTagged) {
    // The Phi hasn't been untagged, so we leave the conversion as it is.
    return;
  }

  if (from_repr == to_repr) {
    old_untagging->OverwriteWith<Identity>();
    return;
  }

  if (old_untagging->Is<UnsafeSmiUntag>()) {
    // UnsafeSmiTag are only inserted when the node is a known Smi. If the
    // current phi has a Float64/Uint32 representation, then we can safely
    // truncate it to Int32, because we know that the Float64/Uint32 fits in a
    // Smi, and therefore in a Int32.
    if (from_repr == ValueRepresentation::kFloat64) {
      old_untagging->OverwriteWith<UnsafeTruncateFloat64ToInt32>();
    } else if (from_repr == ValueRepresentation::kUint32) {
      old_untagging->OverwriteWith<UnsafeTruncateUint32ToInt32>();
    } else {
      DCHECK_EQ(from_repr, ValueRepresentation::kInt32);
      old_untagging->OverwriteWith<Identity>();
    }
    return;
  }

  // The graph builder inserts 3 kind of Tagged->Int32 conversions that can have
  // heap number as input: CheckedTruncateNumberToInt32, which truncates its
  // input (and deopts if it's not a HeapNumber), TruncateNumberToInt32, which
  // truncates its input (assuming that it's indeed a HeapNumber) and
  // CheckedSmiTag, which deopts on non-smi inputs. The first 2 cannot deopt if
  // we have Float64 phi and will happily truncate it, but the 3rd one should
  // deopt if it cannot be converted without loss of precision.
  bool conversion_is_truncating_float64 =
      old_untagging->Is<CheckedTruncateNumberOrOddballToInt32>() ||
      old_untagging->Is<TruncateNumberOrOddballToInt32>();

  base::Optional<Opcode> needed_conversion = GetOpcodeForCheckedConversion(
      from_repr, to_repr, conversion_is_truncating_float64);

  DCHECK(needed_conversion.has_value());

  if (*needed_conversion != old_untagging->opcode()) {
    old_untagging->OverwriteWith(*needed_conversion);
  }
}

// If the input of a StoreTaggedFieldWithWriteBarrier was a Phi that got
// untagged, then we need to retag it, and we might need to actually use a write
// barrier.
void MaglevPhiRepresentationSelector::UpdateNodePhiInput(
    StoreTaggedFieldWithWriteBarrier* node, Phi* phi, int input_index,
    const ProcessingState& state) {
  if (input_index == StoreTaggedFieldWithWriteBarrier::kObjectIndex) {
    // The 1st input of a Store should usually not be untagged. However, it is
    // possible to write `let x = a ? 4 : 2; x.c = 10`, which will produce a
    // store whose receiver could be an untagged Phi. So, for such cases, we use
    // the generic UpdateNodePhiInput method to tag `phi` if needed.
    UpdateNodePhiInput(static_cast<NodeBase*>(node), phi, input_index, state);
    return;
  }
  DCHECK_EQ(input_index, StoreTaggedFieldNoWriteBarrier::kValueIndex);

  if (phi->value_representation() != ValueRepresentation::kTagged) {
    // We need to tag {phi}. However, this could turn it into a HeapObject
    // rather than a Smi (either because {phi} is a Float64 phi, or because it's
    // a Int32/Uint32 phi that doesn't fit on 31 bits), so we need can't drop
    // the write barrier.
    node->change_input(input_index, EnsurePhiTagged(phi, current_block_,
                                                    NewNodePosition::kStart));
    static_assert(StoreTaggedFieldNoWriteBarrier::kObjectIndex ==
                  StoreTaggedFieldWithWriteBarrier::kObjectIndex);
    static_assert(StoreTaggedFieldNoWriteBarrier::kValueIndex ==
                  StoreTaggedFieldWithWriteBarrier::kValueIndex);
    node->OverwriteWith<StoreTaggedFieldWithWriteBarrier>();
  }
}

// CheckedStoreSmiField is a bit of a special node, because it expects its input
// to be a Smi, and not just any Object. The comments in SmiTagPhi explain what
// this means for untagged Phis.
void MaglevPhiRepresentationSelector::UpdateNodePhiInput(
    CheckedStoreSmiField* node, Phi* phi, int input_index,
    const ProcessingState& state) {
  if (input_index == CheckedStoreSmiField::kValueIndex) {
    node->change_input(input_index, SmiTagPhi(phi, node, state));
  } else {
    DCHECK_EQ(input_index, CheckedStoreSmiField::kObjectIndex);
    // The 1st input of a Store should usually not be untagged. However, it is
    // possible to write `let x = a ? 4 : 2; x.c = 10`, which will produce a
    // store whose receiver could be an untagged Phi. So, for such cases, we use
    // the generic UpdateNodePhiInput method to tag `phi` if needed.
    UpdateNodePhiInput(static_cast<NodeBase*>(node), phi, input_index, state);
    return;
  }
}

// {node} was using {phi} without any untagging, which means that it was using
// {phi} as a tagged value, so, if we've untagged {phi}, we need to re-tag it
// for {node}.
void MaglevPhiRepresentationSelector::UpdateNodePhiInput(
    NodeBase* node, Phi* phi, int input_index, const ProcessingState&) {
  if (node->properties().is_conversion()) {
    // {node} can't be an Untagging if we reached this point (because
    // UpdateNodePhiInput is not called on untagging nodes).
    DCHECK(!IsUntagging(node->opcode()));
    // So, {node} has to be a conversion that takes an input an untagged node,
    // and this input happens to be {phi}, which means that {node} is aware that
    // {phi} isn't tagged. This means that {node} was inserted during the
    // current phase. In this case, we don't do anything.
    DCHECK_NE(phi->value_representation(), ValueRepresentation::kTagged);
    DCHECK_NE(new_nodes_.find(node), new_nodes_.end());
  } else {
    node->change_input(input_index, EnsurePhiTagged(phi, current_block_,
                                                    NewNodePosition::kStart));
  }
}

ValueNode* MaglevPhiRepresentationSelector::SmiTagPhi(
    Phi* phi, CheckedStoreSmiField* user_node, const ProcessingState& state) {
  // The input graph was something like:
  //
  //                            Tagged Phi
  //                                │
  //                                │
  //                                ▼
  //                       CheckedStoreSmiField
  //
  // If the phi has been untagged, we have to retag it to a Smi, after which we
  // can omit the "CheckedSmi" part of the CheckedStoreSmiField, which we do by
  // replacing the CheckedStoreSmiField by a StoreTaggedFieldNoWriteBarrier:
  //
  //                           Untagged Phi
  //                                │
  //                                │
  //                                ▼
  //            CheckedSmiTagFloat64/CheckedSmiTagInt32
  //                                │
  //                                │
  //                                ▼
  //                  StoreTaggedFieldNoWriteBarrier

  // Since we're planning on replacing CheckedStoreSmiField by a
  // StoreTaggedFieldNoWriteBarrier, it's important to ensure that they have the
  // same layout. OverwriteWith will check the sizes and properties of the
  // operators, but isn't aware of which inputs are at which index, so we
  // static_assert that both operators have the same inputs at the same index.
  static_assert(StoreTaggedFieldNoWriteBarrier::kObjectIndex ==
                CheckedStoreSmiField::kObjectIndex);
  static_assert(StoreTaggedFieldNoWriteBarrier::kValueIndex ==
                CheckedStoreSmiField::kValueIndex);

  ValueNode* tagged;
  switch (phi->value_representation()) {
#define TAG_INPUT(tagging_op)                                  \
  tagged = NodeBase::New<tagging_op>(builder_->zone(), {phi}); \
  break;
    case ValueRepresentation::kFloat64:
      TAG_INPUT(CheckedSmiTagFloat64)
    case ValueRepresentation::kInt32:
      TAG_INPUT(CheckedSmiTagInt32)
    case ValueRepresentation::kUint32:
      TAG_INPUT(CheckedSmiTagUint32)
    case ValueRepresentation::kTagged:
      return phi;
    case ValueRepresentation::kWord64:
      UNREACHABLE();
#undef TAG_INPUT
  }

  tagged->CopyEagerDeoptInfoOf(user_node, builder_->zone());

  state.node_it()->InsertBefore(tagged);
  if (builder_->has_graph_labeller()) {
    builder_->graph_labeller()->RegisterNode(tagged);
  }
#ifdef DEBUG
  new_nodes_.insert(tagged);
#endif
  user_node->OverwriteWith<StoreTaggedFieldNoWriteBarrier>();
  return tagged;
}

ValueNode* MaglevPhiRepresentationSelector::EnsurePhiTagged(
    Phi* phi, BasicBlock* block, NewNodePosition pos) {
  switch (phi->value_representation()) {
    case ValueRepresentation::kFloat64:
      return AddNode(NodeBase::New<Float64Box>(builder_->zone(), {phi}), block,
                     pos);
    case ValueRepresentation::kInt32:
      return AddNode(NodeBase::New<Int32ToNumber>(builder_->zone(), {phi}),
                     block, pos);
    case ValueRepresentation::kUint32:
      return AddNode(NodeBase::New<Uint32ToNumber>(builder_->zone(), {phi}),
                     block, pos);
    case ValueRepresentation::kTagged:
      return phi;
    case ValueRepresentation::kWord64:
      UNREACHABLE();
  }
}

void MaglevPhiRepresentationSelector::FixLoopPhisBackedge(BasicBlock* block) {
  // TODO(dmercadier): it would be interesting to compute a fix point for loop
  // phis, or at least to go over the loop header twice.
  if (!block->has_phi()) return;
  for (Phi* phi : *block->phis()) {
    if (phi->value_representation() == ValueRepresentation::kTagged) {
      int last_input_idx = phi->input_count() - 1;
      if (phi->input(last_input_idx).node()->value_representation() !=
          ValueRepresentation::kTagged) {
        // Since all Phi inputs are initially tagged, the fact that the backedge
        // is not tagged means that it's a Phi that we recently untagged.
        DCHECK(phi->input(last_input_idx).node()->Is<Phi>());
        phi->set_input(
            last_input_idx,
            EnsurePhiTagged(phi->input(last_input_idx).node()->Cast<Phi>(),
                            current_block_, NewNodePosition::kEnd));
      }
    }
  }
}

ValueNode* MaglevPhiRepresentationSelector::AddNode(ValueNode* node,
                                                    BasicBlock* block,
                                                    NewNodePosition pos) {
  if (block == current_block_) {
    // When adding an Node in the current block, we delay until we've finished
    // processing the current block, to avoid mutating the list of nodes while
    // we're iterating it.
    if (pos == NewNodePosition::kStart) {
      new_nodes_current_block_start_.push_back(node);
    } else {
      new_nodes_current_block_end_.push_back(node);
    }
  } else {
    // However, when adding a node in a predecessor, the node won't be used
    // until the current block, and it might be using nodes computed in the
    // predecessor, so we add it at the end of the predecessor.
    DCHECK_EQ(pos, NewNodePosition::kEnd);
    block->nodes().Add(node);
  }
  if (builder_->has_graph_labeller()) {
    builder_->graph_labeller()->RegisterNode(node);
  }
#ifdef DEBUG
  new_nodes_.insert(node);
#endif
  return node;
}

void MaglevPhiRepresentationSelector::MergeNewNodesInBlock(BasicBlock* block) {
  if (block != nullptr && !new_nodes_current_block_start_.empty()) {
    for (Node* node : new_nodes_current_block_start_) {
      block->nodes().AddFront(node);
    }
  }
  new_nodes_current_block_start_.clear();

  if (block != nullptr && !new_nodes_current_block_end_.empty()) {
    for (Node* node : new_nodes_current_block_end_) {
      block->nodes().Add(node);
    }
  }
  new_nodes_current_block_end_.clear();
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
