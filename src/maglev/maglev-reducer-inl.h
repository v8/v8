// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_REDUCER_INL_H_
#define V8_MAGLEV_MAGLEV_REDUCER_INL_H_

#include "src/maglev/maglev-reducer.h"

namespace v8 {
namespace internal {
namespace maglev {

// Some helpers for CSE
namespace cse {
static inline size_t fast_hash_combine(size_t seed, size_t h) {
  // Implementation from boost. Good enough for GVN.
  return h + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template <typename T>
static inline size_t gvn_hash_value(const T& in) {
  return base::hash_value(in);
}

static inline size_t gvn_hash_value(const compiler::MapRef& map) {
  return map.hash_value();
}

static inline size_t gvn_hash_value(const interpreter::Register& reg) {
  return base::hash_value(reg.index());
}

static inline size_t gvn_hash_value(const Representation& rep) {
  return base::hash_value(rep.kind());
}

static inline size_t gvn_hash_value(const ExternalReference& ref) {
  return base::hash_value(ref.address());
}

static inline size_t gvn_hash_value(const PolymorphicAccessInfo& access_info) {
  return access_info.hash_value();
}

template <typename T>
static inline size_t gvn_hash_value(
    const v8::internal::ZoneCompactSet<T>& vector) {
  size_t hash = base::hash_value(vector.size());
  for (auto e : vector) {
    hash = fast_hash_combine(hash, gvn_hash_value(e));
  }
  return hash;
}

template <typename T>
static inline size_t gvn_hash_value(const v8::internal::ZoneVector<T>& vector) {
  size_t hash = base::hash_value(vector.size());
  for (auto e : vector) {
    hash = fast_hash_combine(hash, gvn_hash_value(e));
  }
  return hash;
}
}  // namespace cse

template <typename BaseT>
template <typename NodeT, typename... Args>
NodeT* MaglevReducer<BaseT>::AddNewNodeOrGetEquivalent(
    std::initializer_list<ValueNode*> raw_inputs, Args&&... args) {
  DCHECK(v8_flags.maglev_cse);
  static constexpr Opcode op = Node::opcode_of<NodeT>;
  static_assert(Node::participate_in_cse(op));
  using options_result =
      std::invoke_result_t<decltype(&NodeT::options), const NodeT>;
  static_assert(std::is_assignable_v<options_result, std::tuple<Args...>>,
                "Instruction participating in CSE needs options() returning "
                "a tuple matching the constructor arguments");
  static_assert(IsFixedInputNode<NodeT>());
  static_assert(NodeT::kInputCount <= 3);

  std::array<ValueNode*, NodeT::kInputCount> inputs;
  // Nodes with zero input count don't have kInputTypes defined.
  if constexpr (NodeT::kInputCount > 0) {
    int i = 0;
    constexpr UseReprHintRecording hint = ShouldRecordUseReprHint<NodeT>();
    for (ValueNode* raw_input : raw_inputs) {
      // TODO(marja): Here we might already have the empty type for the
      // node. Generate a deopt and make callers handle it.
      inputs[i] = ConvertInputTo<hint>(raw_input, NodeT::kInputTypes[i]);
      i++;
    }
    if constexpr (IsCommutativeNode(Node::opcode_of<NodeT>)) {
      static_assert(NodeT::kInputCount == 2);
      if ((IsConstantNode(inputs[0]->opcode()) || inputs[0] > inputs[1]) &&
          !IsConstantNode(inputs[1]->opcode())) {
        std::swap(inputs[0], inputs[1]);
      }
    }
  }

  uint32_t value_number;
  {
    size_t tmp_value_number = base::hash_value(op);
    (
        [&] {
          tmp_value_number = cse::fast_hash_combine(tmp_value_number,
                                                    cse::gvn_hash_value(args));
        }(),
        ...);
    for (const auto& inp : inputs) {
      tmp_value_number =
          cse::fast_hash_combine(tmp_value_number, base::hash_value(inp));
    }
    value_number = static_cast<uint32_t>(tmp_value_number);
  }

  auto exists = known_node_aspects().available_expressions.find(value_number);
  if (exists != known_node_aspects().available_expressions.end()) {
    auto candidate = exists->second.node;
    const bool sanity_check =
        candidate->template Is<NodeT>() &&
        static_cast<size_t>(candidate->input_count()) == inputs.size();
    DCHECK_IMPLIES(sanity_check,
                   (StaticPropertiesForOpcode(op) & candidate->properties()) ==
                       candidate->properties());
    const bool epoch_check =
        !Node::needs_epoch_check(op) ||
        known_node_aspects().effect_epoch() <= exists->second.effect_epoch;
    if (sanity_check && epoch_check) {
      if (static_cast<NodeT*>(candidate)->options() ==
          std::tuple{std::forward<Args>(args)...}) {
        int i = 0;
        for (const auto& inp : inputs) {
          if (inp != candidate->input(i).node()) {
            break;
          }
          i++;
        }
        if (static_cast<size_t>(i) == inputs.size()) {
          return static_cast<NodeT*>(candidate);
        }
      }
    }
    if (!epoch_check) {
      known_node_aspects().available_expressions.erase(exists);
    }
  }
  NodeT* node =
      NodeBase::New<NodeT>(zone(), inputs.size(), std::forward<Args>(args)...);
  int i = 0;
  for (ValueNode* input : inputs) {
    DCHECK_NOT_NULL(input);
    node->set_input(i++, input);
  }
  DCHECK_EQ(node->options(), std::tuple{std::forward<Args>(args)...});
  uint32_t epoch = Node::needs_epoch_check(op)
                       ? known_node_aspects().effect_epoch()
                       : KnownNodeAspects::kEffectEpochForPureInstructions;
  if (epoch != KnownNodeAspects::kEffectEpochOverflow) {
    known_node_aspects().available_expressions[value_number] = {node, epoch};
  }
  return AttachExtraInfoAndAddToGraph(node);
}

template <typename BaseT>
void MaglevReducer<BaseT>::AddInitializedNodeToGraph(Node* node) {
  // VirtualObjects should never be add to the Maglev graph.
  DCHECK(!node->Is<VirtualObject>());
  switch (current_block_position_) {
    case BasicBlockPosition::kStart:
      DCHECK_EQ(add_new_node_mode_, AddNewNodeMode::kBuffered);
      new_nodes_at_start_.push_back(node);
      break;
    case BasicBlockPosition::kEnd:
      if (V8_UNLIKELY(add_new_node_mode_ == AddNewNodeMode::kUnbuffered)) {
        current_block_->nodes().push_back(node);
      } else {
        new_nodes_at_end_.push_back(node);
      }
      break;
  }
  node->set_owner(current_block());
  if (V8_UNLIKELY(has_graph_labeller())) {
    graph_labeller()->RegisterNode(node, &current_provenance_);
  }
}

template <typename BaseT>
std::optional<ValueNode*> MaglevReducer<BaseT>::TryGetConstantAlternative(
    ValueNode* node) {
  const NodeInfo* info = known_node_aspects().TryGetInfoFor(node);
  if (info) {
    if (auto c = info->alternative().checked_value()) {
      if (IsConstantNode(c->opcode())) {
        return c;
      }
    }
  }
  return {};
}

template <typename BaseT>
ValueNode* MaglevReducer<BaseT>::GetTaggedValue(
    ValueNode* value, UseReprHintRecording record_use_repr_hint) {
  if (V8_LIKELY(record_use_repr_hint == UseReprHintRecording::kRecord)) {
    value->RecordUseReprHintIfPhi(UseRepresentation::kTagged);
  }

  ValueRepresentation representation =
      value->properties().value_representation();
  if (representation == ValueRepresentation::kTagged) return value;

  if (Int32Constant* as_int32_constant = value->TryCast<Int32Constant>();
      as_int32_constant && Smi::IsValid(as_int32_constant->value())) {
    return graph()->GetSmiConstant(as_int32_constant->value());
  }

  NodeInfo* node_info =
      known_node_aspects().GetOrCreateInfoFor(broker(), value);
  auto& alternative = node_info->alternative();

  if (ValueNode* alt = alternative.tagged()) {
    return alt;
  }

  // This is called when converting inputs in AddNewNode. We might already have
  // an empty type for `value` here. Make sure we don't add unsafe conversion
  // nodes in that case by checking for the empty node type explicitly.
  // TODO(marja): The checks can be removed after we're able to bail out
  // earlier.
  switch (representation) {
    case ValueRepresentation::kInt32: {
      if (!IsEmptyNodeType(node_info->type()) &&
          NodeTypeIsSmi(node_info->type())) {
        return alternative.set_tagged(AddNewNode<UnsafeSmiTagInt32>({value}));
      }
      return alternative.set_tagged(AddNewNode<Int32ToNumber>({value}));
    }
    case ValueRepresentation::kUint32: {
      if (!IsEmptyNodeType(node_info->type()) &&
          NodeTypeIsSmi(node_info->type())) {
        return alternative.set_tagged(AddNewNode<UnsafeSmiTagUint32>({value}));
      }
      return alternative.set_tagged(AddNewNode<Uint32ToNumber>({value}));
    }
    case ValueRepresentation::kFloat64: {
      return alternative.set_tagged(AddNewNode<Float64ToTagged>(
          {value}, Float64ToTagged::ConversionMode::kCanonicalizeSmi));
    }
    case ValueRepresentation::kHoleyFloat64: {
      return alternative.set_tagged(AddNewNode<HoleyFloat64ToTagged>(
          {value}, HoleyFloat64ToTagged::ConversionMode::kForceHeapNumber));
    }

    case ValueRepresentation::kIntPtr:
      if (!IsEmptyNodeType(node_info->type()) &&
          NodeTypeIsSmi(node_info->type())) {
        return alternative.set_tagged(AddNewNode<UnsafeSmiTagIntPtr>({value}));
      }
      return alternative.set_tagged(AddNewNode<IntPtrToNumber>({value}));

    case ValueRepresentation::kTagged:
      UNREACHABLE();
  }
  UNREACHABLE();
}

template <typename BaseT>
ValueNode* MaglevReducer<BaseT>::GetInt32(ValueNode* value,
                                          bool can_be_heap_number) {
  value->RecordUseReprHintIfPhi(UseRepresentation::kInt32);

  ValueRepresentation representation =
      value->properties().value_representation();
  if (representation == ValueRepresentation::kInt32) return value;

  // Process constants first to avoid allocating NodeInfo for them.
  if (auto cst = TryGetInt32Constant(value)) {
    return graph()->GetInt32Constant(cst.value());
  }
  // We could emit unconditional eager deopts for other kinds of constant, but
  // it's not necessary, the appropriate checking conversion nodes will deopt.

  NodeInfo* node_info =
      known_node_aspects().GetOrCreateInfoFor(broker(), value);
  auto& alternative = node_info->alternative();

  if (ValueNode* alt = alternative.int32()) {
    return alt;
  }

  switch (representation) {
    case ValueRepresentation::kTagged: {
      if (can_be_heap_number &&
          !known_node_aspects().CheckType(broker(), value, NodeType::kSmi)) {
        return alternative.set_int32(AddNewNode<CheckedNumberToInt32>({value}));
      }
      return alternative.set_int32(BuildSmiUntag(value));
    }
    case ValueRepresentation::kUint32: {
      if (!IsEmptyNodeType(known_node_aspects().GetType(broker(), value)) &&
          node_info->is_smi()) {
        return alternative.set_int32(
            AddNewNode<TruncateUint32ToInt32>({value}));
      }
      return alternative.set_int32(AddNewNode<CheckedUint32ToInt32>({value}));
    }
    case ValueRepresentation::kFloat64:
    // The check here will also work for the hole NaN, so we can treat
    // HoleyFloat64 as Float64.
    case ValueRepresentation::kHoleyFloat64: {
      return alternative.set_int32(
          AddNewNode<CheckedTruncateFloat64ToInt32>({value}));
    }

    case ValueRepresentation::kIntPtr:
      return alternative.set_int32(AddNewNode<CheckedIntPtrToInt32>({value}));

    case ValueRepresentation::kInt32:
      UNREACHABLE();
  }
  UNREACHABLE();
}

template <typename BaseT>
std::optional<int32_t> MaglevReducer<BaseT>::TryGetInt32Constant(
    ValueNode* value) {
  switch (value->opcode()) {
    case Opcode::kInt32Constant:
      return value->Cast<Int32Constant>()->value();
    case Opcode::kUint32Constant: {
      uint32_t uint32_value = value->Cast<Uint32Constant>()->value();
      if (uint32_value <= INT32_MAX) {
        return static_cast<int32_t>(uint32_value);
      }
      return {};
    }
    case Opcode::kSmiConstant:
      return value->Cast<SmiConstant>()->value().value();
    case Opcode::kFloat64Constant: {
      double double_value =
          value->Cast<Float64Constant>()->value().get_scalar();
      if (!IsInt32Double(double_value)) return {};
      return FastD2I(value->Cast<Float64Constant>()->value().get_scalar());
    }
    default:
      break;
  }
  if (auto c = TryGetConstantAlternative(value)) {
    return TryGetInt32Constant(*c);
  }
  return {};
}

template <typename BaseT>
ValueNode* MaglevReducer<BaseT>::GetFloat64(ValueNode* value) {
  value->RecordUseReprHintIfPhi(UseRepresentation::kFloat64);
  return GetFloat64ForToNumber(value, NodeType::kNumber,
                               TaggedToFloat64ConversionType::kOnlyNumber);
}

template <typename BaseT>
ValueNode* MaglevReducer<BaseT>::GetFloat64ForToNumber(
    ValueNode* value, NodeType allowed_input_type,
    TaggedToFloat64ConversionType conversion_type) {
  ValueRepresentation representation =
      value->properties().value_representation();
  if (representation == ValueRepresentation::kFloat64) return value;

  // Process constants first to avoid allocating NodeInfo for them.
  if (auto cst = TryGetFloat64Constant(value, conversion_type)) {
    return graph()->GetFloat64Constant(cst.value());
  }
  // We could emit unconditional eager deopts for other kinds of constant, but
  // it's not necessary, the appropriate checking conversion nodes will deopt.

  NodeInfo* node_info =
      known_node_aspects().GetOrCreateInfoFor(broker(), value);
  auto& alternative = node_info->alternative();

  if (ValueNode* alt = alternative.float64()) {
    return alt;
  }

  // This is called when converting inputs in AddNewNode. We might already have
  // an empty type for `value` here. Make sure we don't add unsafe conversion
  // nodes in that case by checking for the empty node type explicitly.
  // TODO(marja): The checks can be removed after we're able to bail out
  // earlier.
  switch (representation) {
    case ValueRepresentation::kTagged: {
      auto combined_type = IntersectType(allowed_input_type, node_info->type());
      if (!IsEmptyNodeType(node_info->type()) &&
          NodeTypeIs(combined_type, NodeType::kSmi)) {
        // Get the float64 value of a Smi value its int32 representation.
        return GetFloat64(GetInt32(value));
      }
      if (!IsEmptyNodeType(node_info->type()) &&
          NodeTypeIs(combined_type, NodeType::kNumber)) {
        // Number->Float64 conversions are exact alternatives, so they can
        // also become the canonical float64_alternative.
        return alternative.set_float64(BuildNumberOrOddballToFloat64(
            value, NodeType::kNumber,
            TaggedToFloat64ConversionType::kOnlyNumber));
      }
      if (!IsEmptyNodeType(node_info->type()) &&
          NodeTypeIs(combined_type, NodeType::kNumberOrOddball)) {
        // NumberOrOddball->Float64 conversions are not exact alternatives,
        // since they lose the information that this is an oddball, so they
        // can only become the canonical float64_alternative if they are a
        // known number (and therefore not oddball).
        return BuildNumberOrOddballToFloat64(value, combined_type,
                                             conversion_type);
      }
      // The type is impossible. We could generate an unconditional deopt here,
      // but it's too invasive. So we just generate a check which will always
      // deopt.
      return BuildNumberOrOddballToFloat64(value, allowed_input_type,
                                           conversion_type);
    }
    case ValueRepresentation::kInt32:
      return alternative.set_float64(AddNewNode<ChangeInt32ToFloat64>({value}));
    case ValueRepresentation::kUint32:
      return alternative.set_float64(
          AddNewNode<ChangeUint32ToFloat64>({value}));
    case ValueRepresentation::kHoleyFloat64: {
      switch (allowed_input_type) {
        case NodeType::kSmi:
        case NodeType::kNumber:
        case NodeType::kNumberOrBoolean:
          // Number->Float64 conversions are exact alternatives, so they can
          // also become the canonical float64_alternative. The HoleyFloat64
          // representation can represent undefined but no other oddballs, so
          // booleans cannot occur here and kNumberOrBoolean can be grouped with
          // kNumber.
          return alternative.set_float64(
              AddNewNode<CheckedHoleyFloat64ToFloat64>({value}));
        case NodeType::kNumberOrOddball:
          // NumberOrOddball->Float64 conversions are not exact alternatives,
          // since they lose the information that this is an oddball, so they
          // cannot become the canonical float64_alternative.
          return AddNewNode<HoleyFloat64ToMaybeNanFloat64>({value});
        default:
          UNREACHABLE();
      }
    }
    case ValueRepresentation::kIntPtr:
      return alternative.set_float64(
          AddNewNode<ChangeIntPtrToFloat64>({value}));
    case ValueRepresentation::kFloat64:
      UNREACHABLE();
  }
  UNREACHABLE();
}

template <typename BaseT>
std::optional<double> MaglevReducer<BaseT>::TryGetFloat64Constant(
    ValueNode* value, TaggedToFloat64ConversionType conversion_type) {
  switch (value->opcode()) {
    case Opcode::kConstant: {
      compiler::ObjectRef object = value->Cast<Constant>()->object();
      if (object.IsHeapNumber()) {
        return object.AsHeapNumber().value();
      }
      // Oddballs should be RootConstants.
      DCHECK(!IsOddball(*object.object()));
      return {};
    }
    case Opcode::kInt32Constant:
      return value->Cast<Int32Constant>()->value();
    case Opcode::kSmiConstant:
      return value->Cast<SmiConstant>()->value().value();
    case Opcode::kFloat64Constant:
      return value->Cast<Float64Constant>()->value().get_scalar();
    case Opcode::kRootConstant: {
      Tagged<Object> root_object =
          broker()->local_isolate()->root(value->Cast<RootConstant>()->index());
      if (conversion_type == TaggedToFloat64ConversionType::kNumberOrBoolean &&
          IsBoolean(root_object)) {
        return Cast<Oddball>(root_object)->to_number_raw();
      }
      if (conversion_type == TaggedToFloat64ConversionType::kNumberOrOddball &&
          IsOddball(root_object)) {
#ifdef V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE
        if (IsUndefined(root_object)) {
          // We use the undefined nan and silence it to produce the same result
          // as a computation from non-constants would.
          auto ud = Float64::FromBits(kUndefinedNanInt64);
          return ud.to_quiet_nan().get_scalar();
        }
#endif  // V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE
        return Cast<Oddball>(root_object)->to_number_raw();
      }
      if (IsHeapNumber(root_object)) {
        return Cast<HeapNumber>(root_object)->value();
      }
      return {};
    }
    default:
      break;
  }
  if (auto c = TryGetConstantAlternative(value)) {
    return TryGetFloat64Constant(*c, conversion_type);
  }
  return {};
}

template <typename BaseT>
ValueNode* MaglevReducer<BaseT>::BuildSmiUntag(ValueNode* node) {
  // This is called when converting inputs in AddNewNode. We might already have
  // an empty type for `node` here. Make sure we don't add unsafe conversion
  // nodes in that case by checking for the empty node type explicitly.
  // TODO(marja): The checks can be removed after we're able to bail out
  // earlier.
  if (!IsEmptyNodeType(GetType(node)) && EnsureType(node, NodeType::kSmi)) {
    if (SmiValuesAre31Bits()) {
      if (auto phi = node->TryCast<Phi>()) {
        phi->SetUseRequires31BitValue();
      }
    }
    return AddNewNode<UnsafeSmiUntag>({node});
  } else {
    return AddNewNode<CheckedSmiUntag>({node});
  }
}

template <typename BaseT>
ValueNode* MaglevReducer<BaseT>::BuildNumberOrOddballToFloat64(
    ValueNode* node, NodeType allowed_input_type,
    TaggedToFloat64ConversionType conversion_type) {
  NodeType old_type;
  if (EnsureType(node, allowed_input_type, &old_type)) {
    if (old_type == NodeType::kSmi) {
      ValueNode* untagged_smi = BuildSmiUntag(node);
      return AddNewNode<ChangeInt32ToFloat64>({untagged_smi});
    }
    return AddNewNode<UncheckedNumberOrOddballToFloat64>({node},
                                                         conversion_type);
  } else {
    return AddNewNode<CheckedNumberOrOddballToFloat64>({node}, conversion_type);
  }
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_REDUCER_INL_H_
