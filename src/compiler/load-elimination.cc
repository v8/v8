// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/load-elimination.h"

#include "src/compiler/graph.h"
#include "src/compiler/node-marker.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/simplified-operator.h"

namespace v8 {
namespace internal {
namespace compiler {

#ifdef DEBUG
#define TRACE(...)                                              \
  do {                                                          \
    if (FLAG_trace_turbo_load_elimination) PrintF(__VA_ARGS__); \
  } while (false)
#else
#define TRACE(...)
#endif

namespace {

const size_t kMaxTrackedFields = 16;

Node* ActualValue(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kCheckBounds:
    case IrOpcode::kCheckNumber:
    case IrOpcode::kCheckTaggedPointer:
    case IrOpcode::kCheckTaggedSigned:
    case IrOpcode::kFinishRegion:
      return ActualValue(NodeProperties::GetValueInput(node, 0));
    default:
      return node;
  }
}

enum Aliasing { kNoAlias, kMayAlias, kMustAlias };

Aliasing QueryAlias(Node* a, Node* b) {
  if (a == b) return kMustAlias;
  if (b->opcode() == IrOpcode::kAllocate) {
    switch (a->opcode()) {
      case IrOpcode::kAllocate:
      case IrOpcode::kHeapConstant:
      case IrOpcode::kParameter:
        return kNoAlias;
      default:
        break;
    }
  }
  if (a->opcode() == IrOpcode::kAllocate) {
    switch (b->opcode()) {
      case IrOpcode::kHeapConstant:
      case IrOpcode::kParameter:
        return kNoAlias;
      default:
        break;
    }
  }
  return kMayAlias;
}

bool MayAlias(Node* a, Node* b) { return QueryAlias(a, b) != kNoAlias; }

bool MustAlias(Node* a, Node* b) { return QueryAlias(a, b) == kMustAlias; }

// Abstract state to approximate the current state of a certain field along the
// effect paths through the graph.
class AbstractField final : public ZoneObject {
 public:
  explicit AbstractField(Zone* zone) : info_for_node_(zone) {}
  AbstractField(Node* object, Node* value, Zone* zone) : info_for_node_(zone) {
    info_for_node_.insert(std::make_pair(object, value));
  }

  AbstractField const* Extend(Node* object, Node* value, Zone* zone) const {
    AbstractField* that = new (zone) AbstractField(zone);
    that->info_for_node_ = this->info_for_node_;
    that->info_for_node_.insert(std::make_pair(object, value));
    return that;
  }
  Node* Lookup(Node* object) const {
    for (auto pair : info_for_node_) {
      if (MustAlias(object, pair.first)) return pair.second;
    }
    return nullptr;
  }
  AbstractField const* Kill(Node* object, Zone* zone) const {
    for (auto pair : this->info_for_node_) {
      if (MayAlias(object, pair.first)) {
        AbstractField* that = new (zone) AbstractField(zone);
        for (auto pair : this->info_for_node_) {
          if (!MayAlias(object, pair.first)) that->info_for_node_.insert(pair);
        }
        return that;
      }
    }
    return this;
  }
  bool Equals(AbstractField const* that) const {
    return this == that || this->info_for_node_ == that->info_for_node_;
  }
  AbstractField const* Merge(AbstractField const* that, Zone* zone) const {
    if (this->Equals(that)) return this;
    AbstractField* copy = new (zone) AbstractField(zone);
    for (auto this_it : this->info_for_node_) {
      Node* this_object = this_it.first;
      Node* this_value = this_it.second;
      auto that_it = that->info_for_node_.find(this_object);
      if (that_it != that->info_for_node_.end() &&
          that_it->second == this_value) {
        copy->info_for_node_.insert(this_it);
      }
    }
    return copy;
  }

 private:
  ZoneMap<Node*, Node*> info_for_node_;
};

// Abstract state to track the state of all fields along the effect paths
// through the graph.
class AbstractState final : public ZoneObject {
 public:
  AbstractState() {
    for (size_t i = 0; i < kMaxTrackedFields; ++i) fields_[i] = nullptr;
  }

  AbstractState const* Extend(Node* object, size_t index, Node* value,
                              Zone* zone) const {
    AbstractState* that = new (zone) AbstractState(*this);
    AbstractField const* that_field = that->fields_[index];
    if (that_field) {
      that_field = that_field->Extend(object, value, zone);
    } else {
      that_field = new (zone) AbstractField(object, value, zone);
    }
    that->fields_[index] = that_field;
    return that;
  }
  AbstractState const* Kill(Node* object, size_t index, Zone* zone) const {
    if (!this->fields_[index]) return this;
    AbstractState* that = new (zone) AbstractState(*this);
    that->fields_[index] = nullptr;
    return that;
  }
  AbstractState const* Merge(AbstractState const* that, Zone* zone) const {
    if (this->Equals(that)) return this;
    AbstractState* copy = new (zone) AbstractState();
    for (size_t i = 0; i < kMaxTrackedFields; ++i) {
      AbstractField const* this_field = this->fields_[i];
      AbstractField const* that_field = that->fields_[i];
      if (this_field && that_field) {
        copy->fields_[i] = this_field->Merge(that_field, zone);
      }
    }
    return copy;
  }
  Node* Lookup(Node* object, size_t index) const {
    AbstractField const* this_field = this->fields_[index];
    if (this_field) return this_field->Lookup(object);
    return nullptr;
  }
  bool Equals(AbstractState const* that) const {
    if (this == that) return true;
    for (size_t i = 0; i < kMaxTrackedFields; ++i) {
      AbstractField const* this_field = this->fields_[i];
      AbstractField const* that_field = that->fields_[i];
      if (this_field) {
        if (!that_field || !this_field->Equals(that_field)) return false;
      } else if (that_field) {
        return false;
      }
      DCHECK(this_field == that_field || this_field->Equals(that_field));
    }
    return true;
  }

 private:
  AbstractField const* fields_[kMaxTrackedFields];
};

class LoadEliminationAnalysis final {
 public:
  LoadEliminationAnalysis(Graph* graph, Zone* zone)
      : candidates_(zone),
        empty_state_(),
        queue_(zone),
        node_states_(graph->NodeCount(), zone) {}

  void Run(Node* start) {
    TRACE("--{Analysis phase}--\n");
    UpdateState(start, empty_state());
    while (!queue_.empty()) {
      Node* const node = queue_.top();
      queue_.pop();
      VisitNode(node);
    }
    TRACE("--{Replacement phase}--\n");
    ZoneMap<Node*, Node*> replacements(zone());
    for (Node* const node : candidates_) {
      switch (node->opcode()) {
        case IrOpcode::kLoadField: {
          FieldAccess const& access = FieldAccessOf(node->op());
          Node* const object =
              ActualValue(NodeProperties::GetValueInput(node, 0));
          Node* const effect = NodeProperties::GetEffectInput(node);
          AbstractState const* state = GetState(effect);
          int field_index = FieldIndexOf(access);
          DCHECK_LE(0, field_index);
          if (Node* value = state->Lookup(object, field_index)) {
            auto it = replacements.find(value);
            if (it != replacements.end()) value = it->second;
            Type* const value_type = NodeProperties::GetType(value);
            if (value_type->Is(access.type)) {
              replacements.insert(std::make_pair(node, value));
              TRACE(" - Replacing redundant #%d:LoadField with #%d:%s\n",
                    node->id(), value->id(), value->op()->mnemonic());
              NodeProperties::ReplaceUses(node, value, effect);
              node->Kill();
            } else {
              TRACE(
                  " - Cannot replace redundant #%d:LoadField with #%d:%s,"
                  " because types don't agree",
                  node->id(), value->id(), value->op()->mnemonic());
            }
          }
          break;
        }
        case IrOpcode::kStoreField: {
          FieldAccess const& access = FieldAccessOf(node->op());
          Node* const object =
              ActualValue(NodeProperties::GetValueInput(node, 0));
          Node* const value =
              ActualValue(NodeProperties::GetValueInput(node, 1));
          Node* const effect = NodeProperties::GetEffectInput(node);
          AbstractState const* state = GetState(effect);
          int field_index = FieldIndexOf(access);
          DCHECK_LE(0, field_index);
          if (value == state->Lookup(object, field_index)) {
            TRACE(" - Killing redundant #%d:StoreField\n", node->id());
            NodeProperties::ReplaceUses(node, value, effect);
            node->Kill();
          }
          break;
        }
        default:
          UNREACHABLE();
      }
    }
  }

 private:
  void VisitNode(Node* node) {
    TRACE(" - Visiting node #%d:%s\n", node->id(), node->op()->mnemonic());
    switch (node->opcode()) {
      case IrOpcode::kEffectPhi:
        return VisitEffectPhi(node);
      case IrOpcode::kLoadField:
        return VisitLoadField(node);
      case IrOpcode::kStoreElement:
        return VisitStoreElement(node);
      case IrOpcode::kStoreField:
        return VisitStoreField(node);
      case IrOpcode::kDeoptimize:
      case IrOpcode::kReturn:
      case IrOpcode::kTerminate:
      case IrOpcode::kThrow:
        break;
      default:
        return VisitOtherNode(node);
    }
  }

  void VisitEffectPhi(Node* node) {
    int const input_count = node->InputCount() - 1;
    DCHECK_LT(0, input_count);
    Node* const control = NodeProperties::GetControlInput(node);
    Node* const effect0 = NodeProperties::GetEffectInput(node, 0);
    AbstractState const* state = GetState(effect0);
    if (state == nullptr) return;
    if (control->opcode() == IrOpcode::kMerge) {
      for (int i = 1; i < input_count; ++i) {
        Node* const effecti = NodeProperties::GetEffectInput(node, i);
        if (GetState(effecti) == nullptr) return;
      }
    }
    for (int i = 1; i < input_count; ++i) {
      Node* const effecti = NodeProperties::GetEffectInput(node, i);
      if (AbstractState const* statei = GetState(effecti)) {
        state = state->Merge(statei, zone());
      }
    }
    UpdateState(node, state);
  }

  void VisitLoadField(Node* node) {
    FieldAccess const& access = FieldAccessOf(node->op());
    Node* const object = ActualValue(NodeProperties::GetValueInput(node, 0));
    Node* const effect = NodeProperties::GetEffectInput(node);
    AbstractState const* state = GetState(effect);
    int field_index = FieldIndexOf(access);
    if (field_index >= 0) {
      Node* const value = state->Lookup(object, field_index);
      if (!value) {
        TRACE("   Node #%d:LoadField is not redundant\n", node->id());
        state = state->Extend(object, field_index, node, zone());
      } else if (!NodeProperties::GetType(value)->Is(access.type)) {
        TRACE(
            "   Node #%d:LoadField is redundant for #%d:%s, but"
            " types don't agree\n",
            node->id(), value->id(), value->op()->mnemonic());
        state = state->Extend(object, field_index, node, zone());
      } else if (value) {
        TRACE("   Node #%d:LoadField is fully redundant for #%d:%s\n",
              node->id(), value->id(), value->op()->mnemonic());
        candidates_.insert(node);
      }
    } else {
      TRACE("   Node #%d:LoadField is unsupported\n", node->id());
    }
    UpdateState(node, state);
  }

  void VisitStoreField(Node* node) {
    FieldAccess const& access = FieldAccessOf(node->op());
    Node* const object = ActualValue(NodeProperties::GetValueInput(node, 0));
    Node* const new_value = NodeProperties::GetValueInput(node, 1);
    Node* const effect = NodeProperties::GetEffectInput(node);
    AbstractState const* state = GetState(effect);
    int field_index = FieldIndexOf(access);
    if (field_index >= 0) {
      Node* const old_value = state->Lookup(object, field_index);
      if (old_value == new_value) {
        TRACE("  Node #%d:StoreField is fully redundant, storing #%d:%s\n",
              node->id(), new_value->id(), new_value->op()->mnemonic());
        candidates_.insert(node);
      }
      TRACE("  Killing all potentially aliasing stores for %d on #%d:%s\n",
            field_index, object->id(), object->op()->mnemonic());
      state = state->Kill(object, field_index, zone());
      TRACE("  Node #%d:StoreField provides #%d:%s for %d on #%d:%s\n",
            node->id(), new_value->id(), new_value->op()->mnemonic(),
            field_index, object->id(), object->op()->mnemonic());
      state = state->Extend(object, field_index, new_value, zone());
    } else {
      TRACE("   Node #%d:StoreField is unsupported\n", node->id());
      state = empty_state();
    }
    UpdateState(node, state);
  }

  void VisitStoreElement(Node* node) {
    Node* const effect = NodeProperties::GetEffectInput(node);
    AbstractState const* state = GetState(effect);
    UpdateState(node, state);
  }

  void VisitOtherNode(Node* node) {
    DCHECK_EQ(1, node->op()->EffectInputCount());
    DCHECK_EQ(1, node->op()->EffectOutputCount());
    Node* const effect = NodeProperties::GetEffectInput(node);
    AbstractState const* state = node->op()->HasProperty(Operator::kNoWrite)
                                     ? GetState(effect)
                                     : empty_state();
    UpdateState(node, state);
  }

  int FieldIndexOf(FieldAccess const& access) const {
    switch (access.machine_type.representation()) {
      case MachineRepresentation::kNone:
      case MachineRepresentation::kBit:
        UNREACHABLE();
        break;
      case MachineRepresentation::kWord8:
      case MachineRepresentation::kWord16:
      case MachineRepresentation::kWord32:
      case MachineRepresentation::kWord64:
      case MachineRepresentation::kFloat32:
        return -1;  // Currently untracked.
      case MachineRepresentation::kFloat64:
      case MachineRepresentation::kSimd128:
      case MachineRepresentation::kTagged:
        // TODO(bmeurer): Check that we never do overlapping load/stores of
        // individual parts of Float64/Simd128 values.
        break;
    }
    DCHECK_EQ(kTaggedBase, access.base_is_tagged);
    DCHECK_EQ(0, access.offset % kPointerSize);
    int field_index = access.offset / kPointerSize;
    if (field_index >= static_cast<int>(kMaxTrackedFields)) return -1;
    return field_index;
  }

  AbstractState const* GetState(Node* node) const {
    return node_states_[node->id()];
  }
  void SetState(Node* node, AbstractState const* state) {
    node_states_[node->id()] = state;
  }

  void UpdateState(Node* node, AbstractState const* new_state) {
    AbstractState const* old_state = GetState(node);
    if (old_state && old_state->Equals(new_state)) return;
    SetState(node, new_state);
    EnqueueUses(node);
  }

  void EnqueueUses(Node* node) {
    for (Edge const edge : node->use_edges()) {
      if (NodeProperties::IsEffectEdge(edge)) {
        queue_.push(edge.from());
      }
    }
  }

  AbstractState const* empty_state() const { return &empty_state_; }
  Zone* zone() const { return node_states_.get_allocator().zone(); }

  ZoneSet<Node*> candidates_;
  AbstractState const empty_state_;
  ZoneStack<Node*> queue_;
  ZoneVector<AbstractState const*> node_states_;

  DISALLOW_COPY_AND_ASSIGN(LoadEliminationAnalysis);
};

}  // namespace

void LoadElimination::Run() {
  LoadEliminationAnalysis analysis(graph(), zone());
  analysis.Run(graph()->start());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
