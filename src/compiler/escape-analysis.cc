// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/escape-analysis.h"

#include "src/base/flags.h"
#include "src/bootstrapper.h"
#include "src/compilation-dependencies.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/objects-inl.h"
#include "src/type-cache.h"

namespace v8 {
namespace internal {
namespace compiler {


// ------------------------------VirtualObject----------------------------------


class VirtualObject : public ZoneObject {
 public:
  enum Status { kUntracked = 0, kTracked = 1 };
  VirtualObject(NodeId id, Zone* zone)
      : id_(id), status_(kUntracked), fields_(zone), replacement_(nullptr) {}

  VirtualObject(const VirtualObject& other)
      : id_(other.id_),
        status_(other.status_),
        fields_(other.fields_),
        replacement_(other.replacement_) {}

  VirtualObject(NodeId id, Zone* zone, size_t field_number)
      : id_(id), status_(kTracked), fields_(zone), replacement_(nullptr) {
    fields_.resize(field_number);
  }

  Node* GetField(size_t offset) {
    if (offset < fields_.size()) {
      return fields_[offset];
    }
    return nullptr;
  }

  bool SetField(size_t offset, Node* node) {
    bool changed = fields_[offset] != node;
    fields_[offset] = node;
    return changed;
  }
  bool IsVirtual() const { return status_ == kTracked; }
  bool IsTracked() const { return status_ != kUntracked; }
  Node* GetReplacement() { return replacement_; }
  bool SetReplacement(Node* node) {
    bool changed = replacement_ != node;
    replacement_ = node;
    return changed;
  }

  Node** fields_array() { return &fields_.front(); }
  size_t field_count() { return fields_.size(); }
  bool ResizeFields(size_t field_count) {
    if (field_count != fields_.size()) {
      fields_.resize(field_count);
      return true;
    }
    return false;
  }
  bool ClearAllFields() {
    bool changed = false;
    for (size_t i = 0; i < fields_.size(); ++i) {
      if (fields_[i] != nullptr) {
        fields_[i] = nullptr;
        changed = true;
      }
    }
    return changed;
  }
  bool UpdateFrom(const VirtualObject& other);

  NodeId id() { return id_; }
  void id(NodeId id) { id_ = id; }

 private:
  NodeId id_;
  Status status_;
  ZoneVector<Node*> fields_;
  Node* replacement_;
};


bool VirtualObject::UpdateFrom(const VirtualObject& other) {
  bool changed = status_ != other.status_;
  status_ = other.status_;
  changed = replacement_ != other.replacement_ || changed;
  replacement_ = other.replacement_;
  if (fields_.size() != other.fields_.size()) {
    fields_ = other.fields_;
    return true;
  }
  for (size_t i = 0; i < fields_.size(); ++i) {
    if (fields_[i] != other.fields_[i]) {
      changed = true;
      fields_[i] = other.fields_[i];
    }
  }
  return changed;
}


// ------------------------------VirtualState-----------------------------------


class VirtualState : public ZoneObject {
 public:
  VirtualState(Zone* zone, size_t size);
  VirtualState(const VirtualState& states);

  VirtualObject* ResolveVirtualObject(Node* node);
  VirtualObject* GetVirtualObject(Node* node);
  VirtualObject* GetVirtualObject(size_t id);
  VirtualObject* GetOrCreateTrackedVirtualObject(NodeId id, Zone* zone);
  void SetVirtualObject(NodeId id, VirtualObject* state);
  void LastChangedAt(Node* node) { last_changed_ = node; }
  Node* GetLastChanged() { return last_changed_; }
  bool UpdateFrom(NodeId id, VirtualObject* state, Zone* zone);
  Node* ResolveReplacement(Node* node);
  bool UpdateReplacement(Node* node, Node* rep, Zone* zone);
  bool UpdateFrom(VirtualState* state, Zone* zone);
  bool MergeFrom(VirtualState* left, VirtualState* right, Zone* zone,
                 Graph* graph, CommonOperatorBuilder* common, Node* control);

  size_t size() { return info_.size(); }

 private:
  ZoneVector<VirtualObject*> info_;
  Node* last_changed_;
};


VirtualState::VirtualState(Zone* zone, size_t size)
    : info_(zone), last_changed_(nullptr) {
  info_.resize(size);
}


VirtualState::VirtualState(const VirtualState& state)
    : info_(state.info_.get_allocator().zone()),
      last_changed_(state.last_changed_) {
  info_.resize(state.info_.size());
  for (size_t i = 0; i < state.info_.size(); ++i) {
    if (state.info_[i] && state.info_[i]->id() == i) {
      info_[i] = new (state.info_.get_allocator().zone())
          VirtualObject(*state.info_[i]);
    }
  }
  for (size_t i = 0; i < state.info_.size(); ++i) {
    if (state.info_[i] && state.info_[i]->id() != i) {
      info_[i] = info_[state.info_[i]->id()];
    }
  }
}


VirtualObject* VirtualState::GetVirtualObject(size_t id) {
  if (id >= info_.size()) return nullptr;
  return info_[id];
}


VirtualObject* VirtualState::GetVirtualObject(Node* node) {
  return GetVirtualObject(node->id());
}


VirtualObject* VirtualState::ResolveVirtualObject(Node* node) {
  VirtualObject* obj = GetVirtualObject(node->id());
  while (obj && !obj->IsTracked() && obj->GetReplacement() &&
         GetVirtualObject(obj->GetReplacement())) {
    obj = GetVirtualObject(obj->GetReplacement());
  }
  return obj;
}


VirtualObject* VirtualState::GetOrCreateTrackedVirtualObject(NodeId id,
                                                             Zone* zone) {
  if (VirtualObject* obj = GetVirtualObject(id)) {
    return obj;
  }
  VirtualObject* obj = new (zone) VirtualObject(id, zone, 0);
  SetVirtualObject(id, obj);
  return obj;
}


void VirtualState::SetVirtualObject(NodeId id, VirtualObject* obj) {
  info_[id] = obj;
}


bool VirtualState::UpdateFrom(NodeId id, VirtualObject* fromObj, Zone* zone) {
  VirtualObject* obj = GetVirtualObject(id);
  if (!obj) {
    obj = new (zone) VirtualObject(*fromObj);
    SetVirtualObject(id, obj);
    if (FLAG_trace_turbo_escape) {
      PrintF("  Taking field for #%d from %p\n", id,
             static_cast<void*>(fromObj));
    }
    return true;
  }

  if (obj->UpdateFrom(*fromObj)) {
    if (FLAG_trace_turbo_escape) {
      PrintF("  Updating field for #%d from %p\n", id,
             static_cast<void*>(fromObj));
    }
    return true;
  }

  return false;
}


bool VirtualState::UpdateFrom(VirtualState* from, Zone* zone) {
  DCHECK_EQ(size(), from->size());
  bool changed = false;
  for (NodeId id = 0; id < size(); ++id) {
    VirtualObject* ls = GetVirtualObject(id);
    VirtualObject* rs = from->GetVirtualObject(id);

    if (rs == nullptr) {
      continue;
    }

    if (ls == nullptr) {
      ls = new (zone) VirtualObject(*rs);
      SetVirtualObject(id, ls);
      changed = true;
      continue;
    }

    if (FLAG_trace_turbo_escape) {
      PrintF("  Updating fields of #%d\n", id);
    }

    changed = ls->UpdateFrom(*rs) || changed;
  }
  return false;
}


bool VirtualState::MergeFrom(VirtualState* left, VirtualState* right,
                             Zone* zone, Graph* graph,
                             CommonOperatorBuilder* common, Node* control) {
  bool changed = false;
  for (NodeId id = 0; id < std::min(left->size(), right->size()); ++id) {
    VirtualObject* ls = left->GetVirtualObject(id);
    VirtualObject* rs = right->GetVirtualObject(id);

    if (ls != nullptr && rs != nullptr) {
      if (FLAG_trace_turbo_escape) {
        PrintF("  Merging fields of #%d\n", id);
      }
      VirtualObject* mergeObject = GetOrCreateTrackedVirtualObject(id, zone);
      size_t fields = std::max(ls->field_count(), rs->field_count());
      changed = mergeObject->ResizeFields(fields) || changed;
      for (size_t i = 0; i < fields; ++i) {
        if (ls->GetField(i) == rs->GetField(i)) {
          changed = mergeObject->SetField(i, ls->GetField(i)) || changed;
          if (FLAG_trace_turbo_escape && ls->GetField(i)) {
            PrintF("    Field %zu agree on rep #%d\n", i,
                   ls->GetField(i)->id());
          }
        } else if (ls->GetField(i) != nullptr && rs->GetField(i) != nullptr) {
          Node* rep = mergeObject->GetField(i);
          if (!rep || rep->opcode() != IrOpcode::kPhi ||
              NodeProperties::GetValueInput(rep, 0) != ls->GetField(i) ||
              NodeProperties::GetValueInput(rep, 1) != rs->GetField(i)) {
            Node* phi =
                graph->NewNode(common->Phi(MachineRepresentation::kTagged, 2),
                               ls->GetField(i), rs->GetField(i), control);
            if (mergeObject->SetField(i, phi)) {
              if (FLAG_trace_turbo_escape) {
                PrintF("    Creating Phi #%d as merge of #%d and #%d\n",
                       phi->id(), ls->GetField(i)->id(), rs->GetField(i)->id());
              }
              changed = true;
            }
          } else {
            if (FLAG_trace_turbo_escape) {
              PrintF("    Retaining Phi #%d as merge of #%d and #%d\n",
                     rep->id(), ls->GetField(i)->id(), rs->GetField(i)->id());
            }
          }
        } else {
          changed = mergeObject->SetField(i, nullptr) || changed;
        }
      }
    } else if (ls) {
      VirtualObject* mergeObject = GetOrCreateTrackedVirtualObject(id, zone);
      changed = mergeObject->UpdateFrom(*ls) || changed;
    } else if (rs) {
      VirtualObject* mergeObject = GetOrCreateTrackedVirtualObject(id, zone);
      changed = mergeObject->UpdateFrom(*rs) || changed;
    }
  }
  return changed;
}


Node* VirtualState::ResolveReplacement(Node* node) {
  Node* replacement = node;
  VirtualObject* obj = GetVirtualObject(node);
  while (obj != nullptr && obj->GetReplacement()) {
    replacement = obj->GetReplacement();
    obj = GetVirtualObject(replacement);
  }
  return replacement;
}


bool VirtualState::UpdateReplacement(Node* node, Node* rep, Zone* zone) {
  if (!GetVirtualObject(node)) {
    SetVirtualObject(node->id(), new (zone) VirtualObject(node->id(), zone));
  }
  if (GetVirtualObject(node)->SetReplacement(rep)) {
    LastChangedAt(node);
    if (FLAG_trace_turbo_escape) {
      PrintF("Representation of #%d is #%d (%s)\n", node->id(), rep->id(),
             rep->op()->mnemonic());
    }
    return true;
  }
  return false;
}


// ------------------------------EscapeStatusAnalysis---------------------------


EscapeStatusAnalysis::EscapeStatusAnalysis(EscapeAnalysis* object_analysis,
                                           Graph* graph, Zone* zone)
    : object_analysis_(object_analysis),
      graph_(graph),
      zone_(zone),
      info_(zone),
      queue_(zone) {}


EscapeStatusAnalysis::~EscapeStatusAnalysis() {}


bool EscapeStatusAnalysis::HasEntry(Node* node) {
  return info_[node->id()] != kUnknown;
}


bool EscapeStatusAnalysis::IsVirtual(Node* node) {
  if (node->id() >= info_.size()) {
    return false;
  }
  return info_[node->id()] == kVirtual;
}


bool EscapeStatusAnalysis::IsEscaped(Node* node) {
  return info_[node->id()] == kEscaped;
}


bool EscapeStatusAnalysis::SetEscaped(Node* node) {
  bool changed = info_[node->id()] != kEscaped;
  info_[node->id()] = kEscaped;
  return changed;
}


void EscapeStatusAnalysis::Run() {
  info_.resize(graph()->NodeCount());
  ZoneVector<bool> visited(zone());
  visited.resize(graph()->NodeCount());
  queue_.push_back(graph()->end());
  while (!queue_.empty()) {
    Node* node = queue_.front();
    queue_.pop_front();
    Process(node);
    if (!visited[node->id()]) {
      RevisitInputs(node);
    }
    visited[node->id()] = true;
  }
  if (FLAG_trace_turbo_escape) {
    DebugPrint();
  }
}


void EscapeStatusAnalysis::RevisitInputs(Node* node) {
  for (Edge edge : node->input_edges()) {
    Node* input = edge.to();
    queue_.push_back(input);
  }
}


void EscapeStatusAnalysis::RevisitUses(Node* node) {
  for (Edge edge : node->use_edges()) {
    Node* use = edge.from();
    queue_.push_back(use);
  }
}


void EscapeStatusAnalysis::Process(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kAllocate:
      ProcessAllocate(node);
      break;
    case IrOpcode::kFinishRegion:
      ProcessFinishRegion(node);
      break;
    case IrOpcode::kStoreField:
      ProcessStoreField(node);
      break;
    case IrOpcode::kStoreElement:
      ProcessStoreElement(node);
      break;
    case IrOpcode::kLoadField:
    case IrOpcode::kLoadElement: {
      if (Node* rep = object_analysis_->GetReplacement(node, node->id())) {
        if (rep->opcode() == IrOpcode::kAllocate ||
            rep->opcode() == IrOpcode::kFinishRegion) {
          if (CheckUsesForEscape(node, rep)) {
            RevisitInputs(rep);
            RevisitUses(rep);
          }
        }
      }
      break;
    }
    case IrOpcode::kPhi:
      if (!HasEntry(node)) {
        info_[node->id()] = kVirtual;
      }
      CheckUsesForEscape(node);
    default:
      break;
  }
}


void EscapeStatusAnalysis::ProcessStoreField(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kStoreField);
  Node* to = NodeProperties::GetValueInput(node, 0);
  Node* val = NodeProperties::GetValueInput(node, 1);
  if (IsEscaped(to) && SetEscaped(val)) {
    RevisitUses(val);
    if (FLAG_trace_turbo_escape) {
      PrintF("Setting #%d (%s) to escaped because of store to field of #%d\n",
             val->id(), val->op()->mnemonic(), to->id());
    }
  }
}


void EscapeStatusAnalysis::ProcessStoreElement(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kStoreElement);
  Node* to = NodeProperties::GetValueInput(node, 0);
  Node* val = NodeProperties::GetValueInput(node, 2);
  if (IsEscaped(to) && SetEscaped(val)) {
    RevisitUses(val);
    if (FLAG_trace_turbo_escape) {
      PrintF("Setting #%d (%s) to escaped because of store to field of #%d\n",
             val->id(), val->op()->mnemonic(), to->id());
    }
  }
}


void EscapeStatusAnalysis::ProcessAllocate(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kAllocate);
  if (!HasEntry(node)) {
    info_[node->id()] = kVirtual;
    if (FLAG_trace_turbo_escape) {
      PrintF("Created status entry for node #%d (%s)\n", node->id(),
             node->op()->mnemonic());
    }
    NumberMatcher size(node->InputAt(0));
    if (!size.HasValue() && SetEscaped(node)) {
      RevisitUses(node);
      if (FLAG_trace_turbo_escape) {
        PrintF("Setting #%d to escaped because of non-const alloc\n",
               node->id());
      }
      // This node is known to escape, uses do not have to be checked.
      return;
    }
  }
  if (CheckUsesForEscape(node, true)) {
    RevisitUses(node);
  }
}


bool EscapeStatusAnalysis::CheckUsesForEscape(Node* uses, Node* rep,
                                              bool phi_escaping) {
  for (Edge edge : uses->use_edges()) {
    Node* use = edge.from();
    if (!NodeProperties::IsValueEdge(edge) &&
        !NodeProperties::IsContextEdge(edge))
      continue;
    switch (use->opcode()) {
      case IrOpcode::kStoreField:
      case IrOpcode::kLoadField:
      case IrOpcode::kStoreElement:
      case IrOpcode::kLoadElement:
      case IrOpcode::kFrameState:
      case IrOpcode::kStateValues:
      case IrOpcode::kReferenceEqual:
      case IrOpcode::kFinishRegion:
      case IrOpcode::kPhi:
        if (HasEntry(use) && IsEscaped(use) && SetEscaped(rep)) {
          if (FLAG_trace_turbo_escape) {
            PrintF(
                "Setting #%d (%s) to escaped because of use by escaping node "
                "#%d (%s)\n",
                rep->id(), rep->op()->mnemonic(), use->id(),
                use->op()->mnemonic());
          }
          return true;
        }
        if (phi_escaping && use->opcode() == IrOpcode::kPhi &&
            SetEscaped(rep)) {
          if (FLAG_trace_turbo_escape) {
            PrintF(
                "Setting #%d (%s) to escaped because of use by phi node "
                "#%d (%s)\n",
                rep->id(), rep->op()->mnemonic(), use->id(),
                use->op()->mnemonic());
          }
          return true;
        }
        break;
      default:
        if (SetEscaped(rep)) {
          if (FLAG_trace_turbo_escape) {
            PrintF("Setting #%d (%s) to escaped because of use by #%d (%s)\n",
                   rep->id(), rep->op()->mnemonic(), use->id(),
                   use->op()->mnemonic());
          }
          return true;
        }
        if (use->op()->EffectInputCount() == 0 &&
            uses->op()->EffectInputCount() > 0 &&
            uses->opcode() != IrOpcode::kLoadField) {
          if (FLAG_trace_turbo_escape) {
            PrintF("Encountered unaccounted use by #%d (%s)\n", use->id(),
                   use->op()->mnemonic());
          }
          UNREACHABLE();
        }
    }
  }
  return false;
}


void EscapeStatusAnalysis::ProcessFinishRegion(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kFinishRegion);
  if (!HasEntry(node)) {
    info_[node->id()] = kVirtual;
    RevisitUses(node);
  }
  if (CheckUsesForEscape(node, true)) {
    RevisitInputs(node);
  }
}


void EscapeStatusAnalysis::DebugPrint() {
  for (NodeId id = 0; id < info_.size(); id++) {
    if (info_[id] != kUnknown) {
      PrintF("Node #%d is %s\n", id,
             info_[id] == kEscaped ? "escaping" : "virtual");
    }
  }
}


// -----------------------------EscapeAnalysis----------------------------------


EscapeAnalysis::EscapeAnalysis(Graph* graph, CommonOperatorBuilder* common,
                               Zone* zone)
    : graph_(graph),
      common_(common),
      zone_(zone),
      virtual_states_(zone),
      escape_status_(this, graph, zone) {}


EscapeAnalysis::~EscapeAnalysis() {}


void EscapeAnalysis::Run() {
  RunObjectAnalysis();
  escape_status_.Run();
}


void EscapeAnalysis::RunObjectAnalysis() {
  virtual_states_.resize(graph()->NodeCount());
  ZoneVector<Node*> stack(zone());
  stack.push_back(graph()->start());
  while (!stack.empty()) {
    Node* node = stack.back();
    stack.pop_back();
    if (Process(node)) {
      for (Edge edge : node->use_edges()) {
        if (NodeProperties::IsEffectEdge(edge)) {
          Node* use = edge.from();
          if ((use->opcode() != IrOpcode::kLoadField &&
               use->opcode() != IrOpcode::kLoadElement) ||
              !IsDanglingEffectNode(use)) {
            stack.push_back(use);
          }
        }
      }
      // First process loads: dangling loads are a problem otherwise.
      for (Edge edge : node->use_edges()) {
        if (NodeProperties::IsEffectEdge(edge)) {
          Node* use = edge.from();
          if ((use->opcode() == IrOpcode::kLoadField ||
               use->opcode() == IrOpcode::kLoadElement) &&


              IsDanglingEffectNode(use)) {
            stack.push_back(use);
          }
        }
      }
    }
  }
  if (FLAG_trace_turbo_escape) {
    DebugPrint();
  }
}


bool EscapeAnalysis::IsDanglingEffectNode(Node* node) {
  if (node->op()->EffectInputCount() == 0) return false;
  if (node->op()->EffectOutputCount() == 0) return false;
  for (Edge edge : node->use_edges()) {
    if (NodeProperties::IsEffectEdge(edge)) {
      return false;
    }
  }
  return true;
}


bool EscapeAnalysis::Process(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kAllocate:
      ProcessAllocation(node);
      break;
    case IrOpcode::kBeginRegion:
      ForwardVirtualState(node);
      break;
    case IrOpcode::kFinishRegion:
      ProcessFinishRegion(node);
      break;
    case IrOpcode::kStoreField:
      ProcessStoreField(node);
      break;
    case IrOpcode::kLoadField:
      ProcessLoadField(node);
      break;
    case IrOpcode::kStoreElement:
      ProcessStoreElement(node);
      break;
    case IrOpcode::kLoadElement:
      ProcessLoadElement(node);
      break;
    case IrOpcode::kStart:
      ProcessStart(node);
      break;
    case IrOpcode::kEffectPhi:
      return ProcessEffectPhi(node);
      break;
    default:
      if (node->op()->EffectInputCount() > 0) {
        ForwardVirtualState(node);
      }
      ProcessAllocationUsers(node);
      break;
  }
  return true;
}


void EscapeAnalysis::ProcessAllocationUsers(Node* node) {
  for (Edge edge : node->input_edges()) {
    Node* input = edge.to();
    if (!NodeProperties::IsValueEdge(edge) &&
        !NodeProperties::IsContextEdge(edge))
      continue;
    switch (node->opcode()) {
      case IrOpcode::kStoreField:
      case IrOpcode::kLoadField:
      case IrOpcode::kStoreElement:
      case IrOpcode::kLoadElement:
      case IrOpcode::kFrameState:
      case IrOpcode::kStateValues:
      case IrOpcode::kReferenceEqual:
      case IrOpcode::kFinishRegion:
      case IrOpcode::kPhi:
        break;
      default:
        VirtualState* states = virtual_states_[node->id()];
        if (VirtualObject* obj = states->ResolveVirtualObject(input)) {
          if (obj->ClearAllFields()) {
            states->LastChangedAt(node);
          }
        }
        break;
    }
  }
}


bool EscapeAnalysis::IsEffectBranchPoint(Node* node) {
  int count = 0;
  for (Edge edge : node->use_edges()) {
    Node* use = edge.from();
    if (NodeProperties::IsEffectEdge(edge) &&
        use->opcode() != IrOpcode::kLoadField) {
      if (++count > 1) {
        return true;
      }
    }
  }
  return false;
}


void EscapeAnalysis::ForwardVirtualState(Node* node) {
  DCHECK_EQ(node->op()->EffectInputCount(), 1);
  if (node->opcode() != IrOpcode::kLoadField &&
      node->opcode() != IrOpcode::kLoadElement &&
      node->opcode() != IrOpcode::kLoad && IsDanglingEffectNode(node)) {
    PrintF("Dangeling effect node: #%d (%s)\n", node->id(),
           node->op()->mnemonic());
    UNREACHABLE();
  }
  Node* effect = NodeProperties::GetEffectInput(node);
  // Break the cycle for effect phis.
  if (effect->opcode() == IrOpcode::kEffectPhi) {
    if (virtual_states_[effect->id()] == nullptr) {
      virtual_states_[effect->id()] =
          new (zone()) VirtualState(zone(), graph()->NodeCount());
    }
  }
  DCHECK_NOT_NULL(virtual_states_[effect->id()]);
  if (IsEffectBranchPoint(effect)) {
    if (!virtual_states_[node->id()]) {
      virtual_states_[node->id()] =
          new (zone()) VirtualState(*virtual_states_[effect->id()]);
    } else {
      virtual_states_[node->id()]->UpdateFrom(virtual_states_[effect->id()],
                                              zone());
    }
    if (FLAG_trace_turbo_escape) {
      PrintF("Copying object state %p from #%d (%s) to #%d (%s)\n",
             static_cast<void*>(virtual_states_[effect->id()]), effect->id(),
             effect->op()->mnemonic(), node->id(), node->op()->mnemonic());
    }
  } else {
    virtual_states_[node->id()] = virtual_states_[effect->id()];
    if (FLAG_trace_turbo_escape) {
      PrintF("Forwarding object state %p from #%d (%s) to #%d (%s)\n",
             static_cast<void*>(virtual_states_[effect->id()]), effect->id(),
             effect->op()->mnemonic(), node->id(), node->op()->mnemonic());
    }
  }
}


void EscapeAnalysis::ProcessStart(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kStart);
  virtual_states_[node->id()] =
      new (zone()) VirtualState(zone(), graph()->NodeCount());
}


bool EscapeAnalysis::ProcessEffectPhi(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kEffectPhi);
  // For now only support binary phis.
  CHECK_EQ(node->op()->EffectInputCount(), 2);
  Node* left = NodeProperties::GetEffectInput(node, 0);
  Node* right = NodeProperties::GetEffectInput(node, 1);
  bool changed = false;

  VirtualState* mergeState = virtual_states_[node->id()];
  if (!mergeState) {
    mergeState = new (zone()) VirtualState(zone(), graph()->NodeCount());
    virtual_states_[node->id()] = mergeState;
    changed = true;
    if (FLAG_trace_turbo_escape) {
      PrintF("Effect Phi #%d got new states map %p.\n", node->id(),
             static_cast<void*>(mergeState));
    }
  } else if (mergeState->GetLastChanged() != node) {
    changed = true;
  }

  VirtualState* l = virtual_states_[left->id()];
  VirtualState* r = virtual_states_[right->id()];

  if (l == nullptr && r == nullptr) {
    return changed;
  }

  if (FLAG_trace_turbo_escape) {
    PrintF(
        "At Effect Phi #%d, merging states %p (from #%d) and %p (from #%d) "
        "into %p\n",
        node->id(), static_cast<void*>(l), left->id(), static_cast<void*>(r),
        right->id(), static_cast<void*>(mergeState));
  }

  if (r && l == nullptr) {
    changed = mergeState->UpdateFrom(r, zone()) || changed;
  } else if (l && r == nullptr) {
    changed = mergeState->UpdateFrom(l, zone()) || changed;
  } else {
    changed = mergeState->MergeFrom(l, r, zone(), graph(), common(),
                                    NodeProperties::GetControlInput(node)) ||
              changed;
  }
  if (FLAG_trace_turbo_escape) {
    PrintF("Merge %s the node.\n", changed ? "changed" : "did not change");
  }
  if (changed) {
    mergeState->LastChangedAt(node);
  }
  return changed;
}


void EscapeAnalysis::ProcessAllocation(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kAllocate);
  ForwardVirtualState(node);

  // Check if we have already processed this node.
  if (virtual_states_[node->id()]->GetVirtualObject(node)) return;

  NumberMatcher size(node->InputAt(0));
  if (size.HasValue()) {
    virtual_states_[node->id()]->SetVirtualObject(
        node->id(), new (zone()) VirtualObject(node->id(), zone(),
                                               size.Value() / kPointerSize));
  } else {
    virtual_states_[node->id()]->SetVirtualObject(
        node->id(), new (zone()) VirtualObject(node->id(), zone()));
  }
  virtual_states_[node->id()]->LastChangedAt(node);
}


void EscapeAnalysis::ProcessFinishRegion(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kFinishRegion);
  ForwardVirtualState(node);
  Node* allocation = NodeProperties::GetValueInput(node, 0);
  if (allocation->opcode() == IrOpcode::kAllocate) {
    VirtualState* states = virtual_states_[node->id()];
    DCHECK_NOT_NULL(states->GetVirtualObject(allocation));
    if (!states->GetVirtualObject(node->id())) {
      states->SetVirtualObject(node->id(),
                               states->GetVirtualObject(allocation));
      if (FLAG_trace_turbo_escape) {
        PrintF("Linked finish region node #%d to node #%d\n", node->id(),
               allocation->id());
      }
      states->LastChangedAt(node);
    }
  }
}


Node* EscapeAnalysis::GetReplacement(Node* at, NodeId id) {
  VirtualState* states = virtual_states_[at->id()];
  if (VirtualObject* obj = states->GetVirtualObject(id)) {
    return obj->GetReplacement();
  }
  return nullptr;
}


bool EscapeAnalysis::IsVirtual(Node* node) {
  return escape_status_.IsVirtual(node);
}


bool EscapeAnalysis::IsEscaped(Node* node) {
  return escape_status_.IsEscaped(node);
}


VirtualObject* EscapeAnalysis::GetVirtualObject(Node* at, NodeId id) {
  if (VirtualState* states = virtual_states_[at->id()]) {
    return states->GetVirtualObject(id);
  }
  return nullptr;
}


int EscapeAnalysis::OffsetFromAccess(Node* node) {
  DCHECK(OpParameter<FieldAccess>(node).offset % kPointerSize == 0);
  return OpParameter<FieldAccess>(node).offset / kPointerSize;
}


void EscapeAnalysis::ProcessLoadFromPhi(int offset, Node* from, Node* node,
                                        VirtualState* state) {
  // Only binary phis are supported for now.
  CHECK_EQ(from->op()->ValueInputCount(), 2);
  if (FLAG_trace_turbo_escape) {
    PrintF("Load #%d from phi #%d", node->id(), from->id());
  }
  Node* left = NodeProperties::GetValueInput(from, 0);
  Node* right = NodeProperties::GetValueInput(from, 1);
  VirtualObject* l = state->GetVirtualObject(left);
  VirtualObject* r = state->GetVirtualObject(right);
  if (l && r) {
    Node* lv = l->GetField(offset);
    Node* rv = r->GetField(offset);
    if (lv && rv) {
      if (!state->GetVirtualObject(node)) {
        state->SetVirtualObject(node->id(),
                                new (zone()) VirtualObject(node->id(), zone()));
      }
      Node* rep = state->GetVirtualObject(node)->GetReplacement();
      if (!rep || rep->opcode() != IrOpcode::kPhi ||
          NodeProperties::GetValueInput(rep, 0) != lv ||
          NodeProperties::GetValueInput(rep, 1) != rv) {
        Node* phi =
            graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                             lv, rv, NodeProperties::GetControlInput(from));
        state->GetVirtualObject(node)->SetReplacement(phi);
        state->LastChangedAt(node);
        if (FLAG_trace_turbo_escape) {
          PrintF(" got phi of #%d is #%d created.\n", lv->id(), rv->id());
        }
      } else if (FLAG_trace_turbo_escape) {
        PrintF(" has already the right phi representation.\n");
      }
    } else if (FLAG_trace_turbo_escape) {
      PrintF(" has incomplete field info: %p %p\n", static_cast<void*>(lv),
             static_cast<void*>(rv));
    }
  } else if (FLAG_trace_turbo_escape) {
    PrintF(" has incomplete virtual object info: %p %p\n",
           static_cast<void*>(l), static_cast<void*>(r));
  }
}


void EscapeAnalysis::ProcessLoadField(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kLoadField);
  ForwardVirtualState(node);
  Node* from = NodeProperties::GetValueInput(node, 0);
  VirtualState* state = virtual_states_[node->id()];
  if (VirtualObject* object = state->ResolveVirtualObject(from)) {
    int offset = OffsetFromAccess(node);
    if (!object->IsTracked()) return;
    Node* value = object->GetField(offset);
    if (value) {
      value = state->ResolveReplacement(value);
      // Record that the load has this alias.
      state->UpdateReplacement(node, value, zone());
    } else if (FLAG_trace_turbo_escape) {
      PrintF("No field %d on record for #%d\n", offset, from->id());
    }
  } else {
    if (from->opcode() == IrOpcode::kPhi) {
      int offset = OffsetFromAccess(node);
      // Only binary phis are supported for now.
      ProcessLoadFromPhi(offset, from, node, state);
    }
  }
}


void EscapeAnalysis::ProcessLoadElement(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kLoadElement);
  ForwardVirtualState(node);
  Node* from = NodeProperties::GetValueInput(node, 0);
  VirtualState* state = virtual_states_[node->id()];
  if (VirtualObject* object = state->ResolveVirtualObject(from)) {
    NumberMatcher index(node->InputAt(1));
    ElementAccess access = OpParameter<ElementAccess>(node);
    if (index.HasValue()) {
      CHECK_EQ(ElementSizeLog2Of(access.machine_type.representation()),
               kPointerSizeLog2);
      CHECK_EQ(access.header_size % kPointerSize, 0);
      int offset = index.Value() + access.header_size / kPointerSize;
      if (!object->IsTracked()) return;
      Node* value = object->GetField(offset);
      if (value) {
        value = state->ResolveReplacement(value);
        // Record that the load has this alias.
        state->UpdateReplacement(node, value, zone());
      } else if (FLAG_trace_turbo_escape) {
        PrintF("No field %d on record for #%d\n", offset, from->id());
      }
    }
  } else {
    if (from->opcode() == IrOpcode::kPhi) {
      NumberMatcher index(node->InputAt(1));
      ElementAccess access = OpParameter<ElementAccess>(node);
      int offset = index.Value() + access.header_size / kPointerSize;
      if (index.HasValue()) {
        ProcessLoadFromPhi(offset, from, node, state);
      }
    }
  }
}


void EscapeAnalysis::ProcessStoreField(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kStoreField);
  ForwardVirtualState(node);
  Node* to = NodeProperties::GetValueInput(node, 0);
  Node* val = NodeProperties::GetValueInput(node, 1);
  int offset = OffsetFromAccess(node);
  VirtualState* states = virtual_states_[node->id()];
  if (VirtualObject* obj = states->ResolveVirtualObject(to)) {
    if (!obj->IsTracked()) return;
    if (obj->SetField(offset, states->ResolveReplacement(val))) {
      states->LastChangedAt(node);
    }
  }
}


void EscapeAnalysis::ProcessStoreElement(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kStoreElement);
  ForwardVirtualState(node);
  Node* to = NodeProperties::GetValueInput(node, 0);
  NumberMatcher index(node->InputAt(1));
  ElementAccess access = OpParameter<ElementAccess>(node);
  Node* val = NodeProperties::GetValueInput(node, 2);
  if (index.HasValue()) {
    CHECK_EQ(ElementSizeLog2Of(access.machine_type.representation()),
             kPointerSizeLog2);
    CHECK_EQ(access.header_size % kPointerSize, 0);
    int offset = index.Value() + access.header_size / kPointerSize;
    VirtualState* states = virtual_states_[node->id()];
    if (VirtualObject* obj = states->ResolveVirtualObject(to)) {
      if (!obj->IsTracked()) return;
      if (obj->SetField(offset, states->ResolveReplacement(val))) {
        states->LastChangedAt(node);
      }
    }
  }
}


void EscapeAnalysis::DebugPrintObject(VirtualObject* object, NodeId id) {
  PrintF("  Object #%d with %zu fields", id, object->field_count());
  if (Node* rep = object->GetReplacement()) {
    PrintF(", rep = #%d (%s)", rep->id(), rep->op()->mnemonic());
  }
  PrintF("\n");
  for (size_t i = 0; i < object->field_count(); ++i) {
    if (Node* f = object->GetField(i)) {
      PrintF("    Field %zu = #%d (%s)\n", i, f->id(), f->op()->mnemonic());
    }
  }
}


void EscapeAnalysis::DebugPrintState(VirtualState* state) {
  PrintF("Dumping object state %p\n", static_cast<void*>(state));
  for (size_t id = 0; id < state->size(); id++) {
    if (VirtualObject* object = state->GetVirtualObject(id)) {
      if (object->id() == id) {
        DebugPrintObject(object, static_cast<int>(id));
      } else {
        PrintF("  Object #%zu links to object #%d\n", id, object->id());
      }
    }
  }
}


void EscapeAnalysis::DebugPrint() {
  ZoneVector<VirtualState*> object_states(zone());
  for (NodeId id = 0; id < virtual_states_.size(); id++) {
    if (VirtualState* states = virtual_states_[id]) {
      if (std::find(object_states.begin(), object_states.end(), states) ==
          object_states.end()) {
        object_states.push_back(states);
      }
    }
  }
  for (size_t n = 0; n < object_states.size(); n++) {
    DebugPrintState(object_states[n]);
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
