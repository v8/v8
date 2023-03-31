// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-phi-representation-selector.h"

#include "src/base/enum-set.h"
#include "src/flags/flags.h"
#include "src/handles/handles-inl.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-ir-inl.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

#define TRACE_UNTAGGING(...)                      \
  do {                                            \
    if (v8_flags.trace_maglev_phi_untagging) {    \
      StdoutStream{} << __VA_ARGS__ << std::endl; \
    }                                             \
  } while (false)

void MaglevPhiRepresentationSelector::Process(Phi* node,
                                              const ProcessingState&) {
  DCHECK_EQ(node->value_representation(), ValueRepresentation::kTagged);

  if (node->is_exception_phi()) {
    // Exception phis have no inputs (or, at least, none accessible through
    // `node->input(...)`), so we don't know if the inputs could be untagged or
    // not, so we just keep those Phis tagged.
    return;
  }

  TRACE_UNTAGGING(
      "Considering for untagging: " << PrintNodeLabel(graph_labeller(), node));

  // {input_mask} represents the ValueRepresentation that {node} could have,
  // based on the ValueRepresentation of its inputs.
  ValueRepresentationSet input_reprs;

  for (int i = 0; i < node->input_count(); i++) {
    ValueNode* input = node->input(i).node();
    if (input->Is<SmiConstant>()) {
      // Could be any representation. We treat such inputs as Int32, since we
      // later allow ourselves to promote Int32 to Float64 if needed (but we
      // never downgrade Float64 to Int32, as it could cause deopt loops).
      input_reprs.Add(ValueRepresentation::kInt32);
    } else if (Constant* constant = input->TryCast<Constant>()) {
      if (constant->object().IsHeapNumber()) {
        input_reprs.Add(ValueRepresentation::kFloat64);
      } else {
        // Not a Constant that we can untag.
        input_reprs.RemoveAll();
        break;
      }
    } else if (input->properties().is_conversion()) {
      DCHECK_EQ(input->input_count(), 1);
      // The graph builder tags all Phi inputs, so this conversion should
      // produce a tagged value.
      DCHECK_EQ(input->value_representation(), ValueRepresentation::kTagged);
      // If we want to untag {node}, then we'll drop the conversion and use its
      // input instead.
      input_reprs.Add(
          input->input(0).node()->properties().value_representation());
    } else if (Phi* input_phi = input->TryCast<Phi>()) {
      if (input_phi->value_representation() != ValueRepresentation::kTagged) {
        input_reprs.Add(input_phi->value_representation());
      } else {
        // An untagged phi is an input of the current phi.
        // TODO(dmercadier): for the backedge, consider not setting {input_mask}
        // to zero, in order to allow speculatively untagging it.
        input_reprs.RemoveAll();
        break;
      }
    } else {
      // This input is tagged (and didn't require a tagging operation to be
      // tagged); we won't untag {node}.
      // TODO(dmercadier): this is a bit suboptimal, because some nodes start
      // tagged, and later become untagged (parameters for instance). Such nodes
      // will have their untagged alternative passed to {node} without any
      // explicit conversion, and we thus won't untag {node} even though we
      // could have.
      input_reprs.RemoveAll();
      break;
    }
  }

  UseRepresentationSet use_reprs = node->get_uses_repr_hints();

  TRACE_UNTAGGING("  + use_reprs  : " << std::hex << use_reprs.ToIntegral()
                                      << std::dec);
  TRACE_UNTAGGING("  + input_reprs: " << std::hex << input_reprs.ToIntegral()
                                      << std::dec);

  if (use_reprs.contains(UseRepresentation::kTagged) ||
      use_reprs.contains(UseRepresentation::kUint32) || use_reprs.empty()) {
    // We don't untag phis that are used as tagged (because we'd have to retag
    // them later). We also ignore phis that are used as Uint32, because this is
    // a fairly rare case and supporting it doesn't improve performance all that
    // much but will increase code complexity.
    // TODO(dmercadier): consider taking into account where those Tagged uses
    // are: Tagged uses outside of a loop or for a Return could probably be
    // ignored.
    TRACE_UNTAGGING("  => Leaving tagged [incompatible uses]");
    return EnsurePhiInputsTagged(node);
  }

  if (input_reprs.contains(ValueRepresentation::kTagged) ||
      input_reprs.contains(ValueRepresentation::kUint32) ||
      input_reprs.empty()) {
    TRACE_UNTAGGING("  => Leaving tagged [tagged or uint32 inputs]");
    return EnsurePhiInputsTagged(node);
  }

  // Only allowed to have Int32 and Float64 inputs from here.
  DCHECK_EQ(
      input_reprs - ValueRepresentationSet({ValueRepresentation::kInt32,
                                            ValueRepresentation::kFloat64}),
      ValueRepresentationSet());

  DCHECK_EQ(
      use_reprs - UseRepresentationSet({UseRepresentation::kInt32,
                                        UseRepresentation::kTruncatedInt32,
                                        UseRepresentation::kFloat64}),
      UseRepresentationSet());

  // The rules for untagging are that we can only widen input representations,
  // i.e. promote Int32 -> Float64.
  //
  // Inputs can always be used as more generic uses, and tighter uses always
  // block more generic inputs. So, we can find the minimum generic use and
  // maximum generic input, extend inputs upwards, uses downwards, and convert
  // to the most generic use in the intersection.
  //
  // Of interest is the fact that we don't want to insert conversions which
  // reduce genericity, e.g. Float64->Int32 conversions, since they could deopt
  // and lead to deopt loops. The above logic ensures that if a Phi has Float64
  // inputs and Int32 uses, we simply don't untag it.
  //
  // TODO(leszeks): The above logic could be implemented with bit magic if the
  // representations were contiguous.

  base::EnumSet<ValueRepresentation> possible_inputs;
  if (input_reprs.contains(ValueRepresentation::kFloat64)) {
    possible_inputs = {ValueRepresentation::kFloat64};
  } else {
    DCHECK(input_reprs.contains_only(ValueRepresentation::kInt32));
    possible_inputs = {ValueRepresentation::kInt32,
                       ValueRepresentation::kFloat64};
  }

  base::EnumSet<ValueRepresentation> allowed_inputs_for_uses;
  if (use_reprs.contains(UseRepresentation::kInt32)) {
    allowed_inputs_for_uses = {ValueRepresentation::kInt32};
  } else {
    DCHECK(!use_reprs.empty() &&
           use_reprs.is_subset_of({UseRepresentation::kFloat64,
                                   UseRepresentation::kTruncatedInt32}));
    allowed_inputs_for_uses = {ValueRepresentation::kInt32,
                               ValueRepresentation::kFloat64};
  }

  auto intersection = possible_inputs & allowed_inputs_for_uses;

  TRACE_UNTAGGING("  + intersection reprs: "
                  << std::hex << intersection.ToIntegral() << std::dec);
  if (intersection.contains(ValueRepresentation::kFloat64)) {
    TRACE_UNTAGGING("  => Untagging to kFloat64");
    return ConvertTaggedPhiTo(node, ValueRepresentation::kFloat64);
  } else if (intersection.contains(ValueRepresentation::kInt32)) {
    TRACE_UNTAGGING("  => Untagging to Int32");
    return ConvertTaggedPhiTo(node, ValueRepresentation::kInt32);
  }

  DCHECK(intersection.empty());
  // We don't untag the Phi.
  TRACE_UNTAGGING("  => Leaving tagged [incompatible inputs/uses]");
  EnsurePhiInputsTagged(node);
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

namespace {

Opcode GetOpcodeForConversion(ValueRepresentation from, ValueRepresentation to,
                              bool truncating) {
  DCHECK_NE(from, ValueRepresentation::kTagged);
  DCHECK_NE(to, ValueRepresentation::kTagged);

  switch (from) {
    case ValueRepresentation::kInt32:
      switch (to) {
        case ValueRepresentation::kUint32:
          return Opcode::kCheckedInt32ToUint32;
        case ValueRepresentation::kFloat64:
          return Opcode::kChangeInt32ToFloat64;

        case ValueRepresentation::kInt32:
        case ValueRepresentation::kTagged:
        case ValueRepresentation::kWord64:
          UNREACHABLE();
      }
    case ValueRepresentation::kUint32:
      switch (to) {
        case ValueRepresentation::kInt32:
          return Opcode::kCheckedUint32ToInt32;

        case ValueRepresentation::kFloat64:
          return Opcode::kChangeUint32ToFloat64;

        case ValueRepresentation::kUint32:
        case ValueRepresentation::kTagged:
        case ValueRepresentation::kWord64:
          UNREACHABLE();
      }
    case ValueRepresentation::kFloat64:
      switch (to) {
        case ValueRepresentation::kInt32:
          if (truncating) {
            return Opcode::kTruncateFloat64ToInt32;
          }
          return Opcode::kCheckedTruncateFloat64ToInt32;
        case ValueRepresentation::kUint32:
          // The graph builder never inserts Tagged->Uint32 conversions, so we
          // don't have to handle this case.
          UNREACHABLE();

        case ValueRepresentation::kFloat64:
        case ValueRepresentation::kTagged:
        case ValueRepresentation::kWord64:
          UNREACHABLE();
      }
    case ValueRepresentation::kTagged:
    case ValueRepresentation::kWord64:
      UNREACHABLE();
  }
  UNREACHABLE();
}

}  // namespace

void MaglevPhiRepresentationSelector::ConvertTaggedPhiTo(
    Phi* phi, ValueRepresentation repr) {
  // We currently only support Int32 and Float64 untagged phis.
  DCHECK(repr == ValueRepresentation::kInt32 ||
         repr == ValueRepresentation::kFloat64);
  phi->change_representation(repr);

  for (int i = 0; i < phi->input_count(); i++) {
    ValueNode* input = phi->input(i).node();
#define TRACE_INPUT_LABEL \
  "    @ Input " << i << " (" << PrintNodeLabel(graph_labeller(), input) << ")"

    if (input->Is<SmiConstant>()) {
      switch (repr) {
        case ValueRepresentation::kInt32:
          TRACE_UNTAGGING(TRACE_INPUT_LABEL << ": Making Int32 instead of Smi");
          phi->change_input(i,
                            builder_->GetInt32Constant(
                                input->Cast<SmiConstant>()->value().value()));
          break;
        case ValueRepresentation::kFloat64:
          TRACE_UNTAGGING(TRACE_INPUT_LABEL
                          << ": Making Float64 instead of Smi");
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
      TRACE_UNTAGGING(TRACE_INPUT_LABEL
                      << ": Making Float64 instead of Constant");
      DCHECK(constant->object().IsHeapNumber());
      DCHECK_EQ(repr, ValueRepresentation::kFloat64);
      phi->change_input(i, builder_->GetFloat64Constant(
                               constant->object().AsHeapNumber().value()));
    } else if (input->properties().is_conversion()) {
      // Unwrapping the conversion.
      DCHECK_EQ(input->value_representation(), ValueRepresentation::kTagged);
      if (input->input(0).node()->value_representation() == repr) {
        TRACE_UNTAGGING(TRACE_INPUT_LABEL << ": Bypassing conversion");
        phi->set_input(i, input->input(0).node());
      } else {
        // Needs to insert a new conversion.
        ValueRepresentation from_repr =
            input->input(0).node()->value_representation();
        Opcode conv_opcode =
            GetOpcodeForConversion(from_repr, repr, /*truncating*/ false);
        ValueNode* new_node;
        switch (conv_opcode) {
          case Opcode::kChangeInt32ToFloat64:
            TRACE_UNTAGGING(
                TRACE_INPUT_LABEL
                << ": Replacing old conversion with a ChangeInt32ToFloat64");
            new_node = NodeBase::New<ChangeInt32ToFloat64>(
                builder_->zone(), {input->input(0).node()});
            break;
          default:
            UNREACHABLE();
        }
        phi->set_input(i, AddNode(new_node, phi->predecessor_at(i),
                                  NewNodePosition::kEnd));
      }
    } else if (Phi* input_phi = input->TryCast<Phi>()) {
      if (input_phi->value_representation() != repr) {
        // We allow widening of Int32 inputs to Float64, which can lead to the
        // current Phi having a Float64 representation but having some Int32
        // inputs, which will require a Int32ToFloat64 conversion.
        DCHECK_EQ(repr, ValueRepresentation::kFloat64);
        DCHECK_EQ(input_phi->value_representation(),
                  ValueRepresentation::kInt32);
        phi->set_input(
            i, AddNode(NodeBase::New<ChangeInt32ToFloat64>(builder_->zone(),
                                                           {input_phi}),
                       phi->predecessor_at(i), NewNodePosition::kEnd));
        TRACE_UNTAGGING(
            TRACE_INPUT_LABEL
            << ": Converting phi input with a ChangeInt32ToFloat64");
      } else {
        TRACE_UNTAGGING(TRACE_INPUT_LABEL
                        << ": Keeping untagged Phi input as-is");
      }
    } else {
      TRACE_UNTAGGING(TRACE_INPUT_LABEL << ": Invalid input for untagged phi");
      UNREACHABLE();
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

  Opcode needed_conversion = GetOpcodeForConversion(
      from_repr, to_repr, conversion_is_truncating_float64);

  if (needed_conversion != old_untagging->opcode()) {
    old_untagging->OverwriteWith(needed_conversion);
  }
}

// If the input of a StoreTaggedFieldNoWriteBarrier was a Phi that got
// untagged, then we need to retag it, and we might need to actually use a write
// barrier.
void MaglevPhiRepresentationSelector::UpdateNodePhiInput(
    StoreTaggedFieldNoWriteBarrier* node, Phi* phi, int input_index,
    const ProcessingState& state) {
  if (input_index == StoreTaggedFieldNoWriteBarrier::kObjectIndex) {
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
    // a Int32/Uint32 phi that doesn't fit on 31 bits), so we need the write
    // barrier.
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

// If the input of a StoreFixedArrayElementNoWriteBarrier was a Phi that got
// untagged, then we need to retag it, and we might need to actually use a write
// barrier.
void MaglevPhiRepresentationSelector::UpdateNodePhiInput(
    StoreFixedArrayElementNoWriteBarrier* node, Phi* phi, int input_index,
    const ProcessingState& state) {
  if (input_index != StoreFixedArrayElementNoWriteBarrier::kValueIndex) {
    UpdateNodePhiInput(static_cast<NodeBase*>(node), phi, input_index, state);
    return;
  }

  if (phi->value_representation() != ValueRepresentation::kTagged) {
    // We need to tag {phi}. However, this could turn it into a HeapObject
    // rather than a Smi (either because {phi} is a Float64 phi, or because it's
    // a Int32/Uint32 phi that doesn't fit on 31 bits), so we need the write
    // barrier.
    node->change_input(input_index, EnsurePhiTagged(phi, current_block_,
                                                    NewNodePosition::kStart));
    static_assert(StoreFixedArrayElementNoWriteBarrier::kElementsIndex ==
                  StoreFixedArrayElementWithWriteBarrier::kElementsIndex);
    static_assert(StoreFixedArrayElementNoWriteBarrier::kIndexIndex ==
                  StoreFixedArrayElementWithWriteBarrier::kIndexIndex);
    static_assert(StoreFixedArrayElementNoWriteBarrier::kValueIndex ==
                  StoreFixedArrayElementWithWriteBarrier::kValueIndex);
    node->OverwriteWith<StoreFixedArrayElementWithWriteBarrier>();
  }
}

// CheckedStoreFixedArraySmiElement is a bit of a special node, because it
// expects its input to be a Smi, and not just any Object. The comments in
// SmiTagPhi explain what this means for untagged Phis.
void MaglevPhiRepresentationSelector::UpdateNodePhiInput(
    CheckedStoreFixedArraySmiElement* node, Phi* phi, int input_index,
    const ProcessingState& state) {
  if (input_index == CheckedStoreFixedArraySmiElement::kValueIndex) {
    node->change_input(input_index, SmiTagPhi(phi, node, state));
  } else {
    UpdateNodePhiInput(static_cast<NodeBase*>(node), phi, input_index, state);
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

template <class ToNodeT, class FromNodeT>
ValueNode* MaglevPhiRepresentationSelector::SmiTagPhi(
    Phi* phi, FromNodeT* user_node, const ProcessingState& state) {
  // The input graph was something like:
  //
  //                            Tagged Phi
  //                                │
  //                                │
  //                                ▼
  //                            FromNodeT
  //
  // If the phi has been untagged, we have to retag it to a Smi, after which we
  // can omit the "CheckedSmi" part of the FromNodeT, which we do by
  // replacing the FromNodeT by a ToNodeT:
  //
  //                           Untagged Phi
  //                                │
  //                                │
  //                                ▼
  //            CheckedSmiTagFloat64/CheckedSmiTagInt32
  //                                │
  //                                │
  //                                ▼
  //                             ToNodeT

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
  user_node->template OverwriteWith<ToNodeT>();
  return tagged;
}

ValueNode* MaglevPhiRepresentationSelector::SmiTagPhi(
    Phi* phi, CheckedStoreSmiField* user_node, const ProcessingState& state) {
  // Since we're planning on replacing CheckedStoreSmiField by a
  // StoreTaggedFieldNoWriteBarrier, it's important to ensure that they have the
  // same layout. OverwriteWith will check the sizes and properties of the
  // operators, but isn't aware of which inputs are at which index, so we
  // static_assert that both operators have the same inputs at the same index.
  static_assert(StoreTaggedFieldNoWriteBarrier::kObjectIndex ==
                CheckedStoreSmiField::kObjectIndex);
  static_assert(StoreTaggedFieldNoWriteBarrier::kValueIndex ==
                CheckedStoreSmiField::kValueIndex);
  return SmiTagPhi<StoreTaggedFieldNoWriteBarrier>(phi, user_node, state);
}

ValueNode* MaglevPhiRepresentationSelector::SmiTagPhi(
    Phi* phi, CheckedStoreFixedArraySmiElement* user_node,
    const ProcessingState& state) {
  // Since we're planning on replacing CheckedStoreFixedArraySmiElement by a
  // StoreFixedArrayElementNoWriteBarrier, it's important to ensure that they
  // have the same layout. OverwriteWith will check the sizes and properties of
  // the operators, but isn't aware of which inputs are at which index, so we
  // static_assert that both operators have the same inputs at the same index.
  static_assert(StoreFixedArrayElementNoWriteBarrier::kElementsIndex ==
                CheckedStoreFixedArraySmiElement::kElementsIndex);
  static_assert(StoreFixedArrayElementNoWriteBarrier::kIndexIndex ==
                CheckedStoreFixedArraySmiElement::kIndexIndex);
  static_assert(StoreFixedArrayElementNoWriteBarrier::kValueIndex ==
                CheckedStoreFixedArraySmiElement::kValueIndex);
  return SmiTagPhi<StoreFixedArrayElementNoWriteBarrier>(phi, user_node, state);
}

ValueNode* MaglevPhiRepresentationSelector::EnsurePhiTagged(
    Phi* phi, BasicBlock* block, NewNodePosition pos) {
  switch (phi->value_representation()) {
    case ValueRepresentation::kFloat64:
      return AddNode(NodeBase::New<Float64ToTagged>(builder_->zone(), {phi}),
                     block, pos);
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
    int last_input_idx = phi->input_count() - 1;
    ValueNode* backedge = phi->input(last_input_idx).node();
    if (phi->value_representation() == ValueRepresentation::kTagged) {
      // If the backedge is a Phi that was untagged, but {phi} is tagged, then
      // we need to retag the backedge.

      // Identity nodes are used to replace outdated untagging nodes after a phi
      // has been untagged. Here, since the backedge was initially tagged, it
      // couldn't have been such an untagging node, so it shouldn't be an
      // Identity node now.
      DCHECK(!backedge->Is<Identity>());

      if (backedge->value_representation() != ValueRepresentation::kTagged) {
        // Since all Phi inputs are initially tagged, the fact that the backedge
        // is not tagged means that it's a Phi that we recently untagged.
        DCHECK(backedge->Is<Phi>());
        phi->set_input(last_input_idx,
                       EnsurePhiTagged(backedge->Cast<Phi>(), current_block_,
                                       NewNodePosition::kEnd));
      }
    } else {
      // If {phi} was untagged and its backedge became Identity, then we need to
      // unwrap it.
      DCHECK_NE(phi->value_representation(), ValueRepresentation::kTagged);
      if (backedge->Is<Identity>()) {
        DCHECK_EQ(backedge->input(0).node()->value_representation(),
                  phi->value_representation());
        phi->set_input(last_input_idx, backedge->input(0).node());
      }
    }
  }
}

template <typename DeoptInfoT>
void MaglevPhiRepresentationSelector::BypassIdentities(
    const DeoptInfoT* deopt_info) {
  detail::DeepForEachInput(deopt_info,
                           [&](ValueNode*& node, InputLocation* input) {
                             if (node->Is<Identity>()) {
                               node = node->input(0).node();
                             }
                           });
}
template void MaglevPhiRepresentationSelector::BypassIdentities<EagerDeoptInfo>(
    const EagerDeoptInfo*);
template void MaglevPhiRepresentationSelector::BypassIdentities<LazyDeoptInfo>(
    const LazyDeoptInfo*);

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
