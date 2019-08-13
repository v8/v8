// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>

#include "src/compiler/store-store-elimination.h"

#include "src/codegen/tick-counter.h"
#include "src/compiler/all-nodes.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE(fmt, ...)                                         \
  do {                                                          \
    if (FLAG_trace_store_elimination) {                         \
      PrintF("RedundantStoreFinder: " fmt "\n", ##__VA_ARGS__); \
    }                                                           \
  } while (false)

// CHECK_EXTRA is like CHECK, but has two or more arguments: a boolean
// expression, a format string, and any number of extra arguments. The boolean
// expression will be evaluated at runtime. If it evaluates to false, then an
// error message will be shown containing the condition, as well as the extra
// info formatted like with printf.
#define CHECK_EXTRA(condition, fmt, ...)                                      \
  do {                                                                        \
    if (V8_UNLIKELY(!(condition))) {                                          \
      FATAL("Check failed: %s. Extra info: " fmt, #condition, ##__VA_ARGS__); \
    }                                                                         \
  } while (false)

#ifdef DEBUG
#define DCHECK_EXTRA(condition, fmt, ...) \
  CHECK_EXTRA(condition, fmt, ##__VA_ARGS__)
#else
#define DCHECK_EXTRA(condition, fmt, ...) ((void)0)
#endif

// Store-store elimination.
//
// The aim of this optimization is to detect the following pattern in the
// effect graph:
//
// - StoreField[+24, kRepTagged](263, ...)
//
//   ... lots of nodes from which the field at offset 24 of the object
//       returned by node #263 cannot be observed ...
//
// - StoreField[+24, kRepTagged](263, ...)
//
// In such situations, the earlier StoreField cannot be observed, and can be
// eliminated. This optimization should work for any offset and input node, of
// course.
//
// The optimization also works across splits. It currently does not work for
// loops, because we tend to put a stack check in loops, and like deopts,
// stack checks can observe anything.

// Assumption: every byte of a JS object is only ever accessed through one
// offset. For instance, byte 15 of a given object may be accessed using a
// two-byte read at offset 14, or a four-byte read at offset 12, but never
// both in the same program.
//
// This implementation needs all dead nodes removed from the graph, and the
// graph should be trimmed.

namespace {

using StoreOffset = uint32_t;

struct UnobservableStore {
  NodeId id_;
  StoreOffset offset_;

  bool operator==(const UnobservableStore other) const {
    return (id_ == other.id_) && (offset_ == other.offset_);
  }

  bool operator<(const UnobservableStore other) const {
    return (id_ < other.id_) || (id_ == other.id_ && offset_ < other.offset_);
  }
};

// Instances of UnobservablesSet are immutable. They represent either a set of
// UnobservableStores, or the "unvisited empty set".
//
// We apply some sharing to save memory. The class UnobservablesSet is only a
// pointer wide, and a copy does not use any heap (or temp_zone) memory. Most
// changes to an UnobservablesSet might allocate in the temp_zone.
//
// The size of an instance should be the size of a pointer, plus additional
// space in the zone in the case of non-unvisited UnobservablesSets. Copying
// an UnobservablesSet allocates no memory.
class UnobservablesSet final {
 public:
  static UnobservablesSet Unvisited() { return UnobservablesSet(); }
  static UnobservablesSet VisitedEmpty(Zone* zone);
  UnobservablesSet(const UnobservablesSet& other) V8_NOEXCEPT = default;
  UnobservablesSet Intersect(const UnobservablesSet& other,
                             const UnobservablesSet& empty, Zone* zone) const;
  UnobservablesSet Add(UnobservableStore obs, Zone* zone) const;
  UnobservablesSet RemoveSameOffset(StoreOffset off, Zone* zone) const;

  const ZoneSet<UnobservableStore>* set() const { return set_; }

  bool IsUnvisited() const { return set_ == nullptr; }
  bool IsEmpty() const { return set_ == nullptr || set_->empty(); }
  bool Contains(UnobservableStore obs) const {
    return set_ != nullptr && (set_->find(obs) != set_->end());
  }

  bool operator==(const UnobservablesSet& other) const {
    if (IsUnvisited() || other.IsUnvisited()) {
      return IsUnvisited() && other.IsUnvisited();
    } else {
      // Both pointers guaranteed not to be nullptrs.
      return *set() == *(other.set());
    }
  }

  bool operator!=(const UnobservablesSet& other) const {
    return !(*this == other);
  }

 private:
  UnobservablesSet();
  explicit UnobservablesSet(const ZoneSet<UnobservableStore>* set)
      : set_(set) {}
  const ZoneSet<UnobservableStore>* set_;
};

class RedundantStoreFinder final {
 public:
  RedundantStoreFinder(JSGraph* js_graph, TickCounter* tick_counter,
                       Zone* temp_zone);

  void Find();

  const ZoneSet<Node*>& to_remove_const() { return to_remove_; }

  void Visit(Node* node);

 private:
  void VisitEffectfulNode(Node* node);
  UnobservablesSet RecomputeUseIntersection(Node* node);
  UnobservablesSet RecomputeSet(Node* node, const UnobservablesSet& uses);
  static bool CannotObserveStoreField(Node* node);

  void MarkForRevisit(Node* node);
  bool HasBeenVisited(Node* node);

  JSGraph* jsgraph() const { return jsgraph_; }
  Isolate* isolate() { return jsgraph()->isolate(); }
  Zone* temp_zone() const { return temp_zone_; }
  ZoneVector<UnobservablesSet>& unobservable() { return unobservable_; }
  UnobservablesSet& unobservable_for_id(NodeId id) {
    DCHECK_LT(id, unobservable().size());
    return unobservable()[id];
  }
  ZoneSet<Node*>& to_remove() { return to_remove_; }

  JSGraph* const jsgraph_;
  TickCounter* const tick_counter_;
  Zone* const temp_zone_;

  ZoneStack<Node*> revisit_;
  ZoneVector<bool> in_revisit_;
  // Maps node IDs to UnobservableNodeSets.
  ZoneVector<UnobservablesSet> unobservable_;
  ZoneSet<Node*> to_remove_;
  const UnobservablesSet unobservables_visited_empty_;
};

// To safely cast an offset from a FieldAccess, which has a potentially wider
// range (namely int).
StoreOffset ToOffset(int offset) {
  CHECK_LE(0, offset);
  return static_cast<StoreOffset>(offset);
}

StoreOffset ToOffset(const FieldAccess& access) {
  return ToOffset(access.offset);
}

}  // namespace

void RedundantStoreFinder::Find() {
  Visit(jsgraph()->graph()->end());

  while (!revisit_.empty()) {
    tick_counter_->DoTick();
    Node* next = revisit_.top();
    revisit_.pop();
    DCHECK_LT(next->id(), in_revisit_.size());
    in_revisit_[next->id()] = false;
    Visit(next);
  }

#ifdef DEBUG
  // Check that we visited all the StoreFields
  AllNodes all(temp_zone(), jsgraph()->graph());
  for (Node* node : all.reachable) {
    if (node->op()->opcode() == IrOpcode::kStoreField) {
      DCHECK_EXTRA(HasBeenVisited(node), "#%d:%s", node->id(),
                   node->op()->mnemonic());
    }
  }
#endif
}

void RedundantStoreFinder::MarkForRevisit(Node* node) {
  DCHECK_LT(node->id(), in_revisit_.size());
  if (!in_revisit_[node->id()]) {
    revisit_.push(node);
    in_revisit_[node->id()] = true;
  }
}

bool RedundantStoreFinder::HasBeenVisited(Node* node) {
  return !unobservable_for_id(node->id()).IsUnvisited();
}

void StoreStoreElimination::Run(JSGraph* js_graph, TickCounter* tick_counter,
                                Zone* temp_zone) {
  // Find superfluous nodes
  RedundantStoreFinder finder(js_graph, tick_counter, temp_zone);
  finder.Find();

  // Remove superfluous nodes
  for (Node* node : finder.to_remove_const()) {
    if (FLAG_trace_store_elimination) {
      PrintF("StoreStoreElimination::Run: Eliminating node #%d:%s\n",
             node->id(), node->op()->mnemonic());
    }
    Node* previous_effect = NodeProperties::GetEffectInput(node);
    NodeProperties::ReplaceUses(node, nullptr, previous_effect, nullptr,
                                nullptr);
    node->Kill();
  }
}

// Recompute unobservables-set for a node. Will also mark superfluous nodes
// as to be removed.
UnobservablesSet RedundantStoreFinder::RecomputeSet(
    Node* node, const UnobservablesSet& uses) {
  switch (node->op()->opcode()) {
    case IrOpcode::kStoreField: {
      Node* stored_to = node->InputAt(0);
      const FieldAccess& access = FieldAccessOf(node->op());
      StoreOffset offset = ToOffset(access);

      UnobservableStore observation = {stored_to->id(), offset};
      bool is_not_observable = uses.Contains(observation);

      if (is_not_observable) {
        TRACE("  #%d is StoreField[+%d,%s](#%d), unobservable", node->id(),
              offset, MachineReprToString(access.machine_type.representation()),
              stored_to->id());
        to_remove().insert(node);
        return uses;
      } else {
        TRACE("  #%d is StoreField[+%d,%s](#%d), observable, recording in set",
              node->id(), offset,
              MachineReprToString(access.machine_type.representation()),
              stored_to->id());
        return uses.Add(observation, temp_zone());
      }
    }
    case IrOpcode::kLoadField: {
      Node* loaded_from = node->InputAt(0);
      const FieldAccess& access = FieldAccessOf(node->op());
      StoreOffset offset = ToOffset(access);

      TRACE(
          "  #%d is LoadField[+%d,%s](#%d), removing all offsets [+%d] from "
          "set",
          node->id(), offset,
          MachineReprToString(access.machine_type.representation()),
          loaded_from->id(), offset);

      return uses.RemoveSameOffset(offset, temp_zone());
    }
    default:
      if (CannotObserveStoreField(node)) {
        TRACE("  #%d:%s can observe nothing, set stays unchanged", node->id(),
              node->op()->mnemonic());
        return uses;
      } else {
        TRACE("  #%d:%s might observe anything, recording empty set",
              node->id(), node->op()->mnemonic());
        return unobservables_visited_empty_;
      }
  }
  UNREACHABLE();
}

bool RedundantStoreFinder::CannotObserveStoreField(Node* node) {
  return node->opcode() == IrOpcode::kLoadElement ||
         node->opcode() == IrOpcode::kLoad ||
         node->opcode() == IrOpcode::kStore ||
         node->opcode() == IrOpcode::kEffectPhi ||
         node->opcode() == IrOpcode::kStoreElement ||
         node->opcode() == IrOpcode::kUnsafePointerAdd ||
         node->opcode() == IrOpcode::kRetain;
}

// Initialize unobservable_ with js_graph->graph->NodeCount() empty sets.
RedundantStoreFinder::RedundantStoreFinder(JSGraph* js_graph,
                                           TickCounter* tick_counter,
                                           Zone* temp_zone)
    : jsgraph_(js_graph),
      tick_counter_(tick_counter),
      temp_zone_(temp_zone),
      revisit_(temp_zone),
      in_revisit_(js_graph->graph()->NodeCount(), temp_zone),
      unobservable_(js_graph->graph()->NodeCount(),
                    UnobservablesSet::Unvisited(), temp_zone),
      to_remove_(temp_zone),
      unobservables_visited_empty_(UnobservablesSet::VisitedEmpty(temp_zone)) {}

void RedundantStoreFinder::Visit(Node* node) {
  // All effectful nodes should be reachable from End via a sequence of
  // control, then a sequence of effect edges. In VisitEffectfulNode we mark
  // all effect inputs for revisiting (if they might have stale state); here
  // we mark all control inputs at least once.

  if (!HasBeenVisited(node)) {
    for (int i = 0; i < node->op()->ControlInputCount(); i++) {
      Node* control_input = NodeProperties::GetControlInput(node, i);
      if (!HasBeenVisited(control_input)) {
        MarkForRevisit(control_input);
      }
    }
  }

  bool is_effectful = node->op()->EffectInputCount() >= 1;
  if (is_effectful) {
    VisitEffectfulNode(node);
    DCHECK(HasBeenVisited(node));
  } else if (!HasBeenVisited(node)) {
    // Mark as visited.
    unobservable_for_id(node->id()) = unobservables_visited_empty_;
  }
}

void RedundantStoreFinder::VisitEffectfulNode(Node* node) {
  if (HasBeenVisited(node)) {
    TRACE("- Revisiting: #%d:%s", node->id(), node->op()->mnemonic());
  }
  UnobservablesSet after_set = RecomputeUseIntersection(node);
  UnobservablesSet before_set = RecomputeSet(node, after_set);
  DCHECK(!before_set.IsUnvisited());

  UnobservablesSet stored_for_node = unobservable_for_id(node->id());
  bool cur_set_changed =
      stored_for_node.IsUnvisited() || stored_for_node != before_set;
  if (!cur_set_changed) {
    // We will not be able to update the part of this chain above any more.
    // Exit.
    TRACE("+ No change: stabilized. Not visiting effect inputs.");
  } else {
    unobservable_for_id(node->id()) = before_set;

    // Mark effect inputs for visiting.
    for (int i = 0; i < node->op()->EffectInputCount(); i++) {
      Node* input = NodeProperties::GetEffectInput(node, i);
      TRACE("    marking #%d:%s for revisit", input->id(),
            input->op()->mnemonic());
      MarkForRevisit(input);
    }
  }
}

// Compute the intersection of the UnobservablesSets of all effect uses and
// return it. This function only works if {node} has an effect use.
//
// The result UnobservablesSet will always be visited.
UnobservablesSet RedundantStoreFinder::RecomputeUseIntersection(Node* node) {
  // There were no effect uses.
  if (node->op()->EffectOutputCount() == 0) {
    IrOpcode::Value opcode = node->opcode();
    // List of opcodes that may end this effect chain. The opcodes are not
    // important to the soundness of this optimization; this serves as a
    // general sanity check. Add opcodes to this list as it suits you.
    //
    // Everything is observable after these opcodes; return the empty set.
    DCHECK_EXTRA(
        opcode == IrOpcode::kReturn || opcode == IrOpcode::kTerminate ||
            opcode == IrOpcode::kDeoptimize || opcode == IrOpcode::kThrow,
        "for #%d:%s", node->id(), node->op()->mnemonic());
    USE(opcode);

    return unobservables_visited_empty_;
  }

  // {first} == true indicates that we haven't looked at any elements yet.
  // {first} == false indicates that cur_set is the intersection of at least one
  // thing.
  bool first = true;
  UnobservablesSet cur_set = UnobservablesSet::Unvisited();  // irrelevant
  for (Edge edge : node->use_edges()) {
    // Skip non-effect edges
    if (!NodeProperties::IsEffectEdge(edge)) {
      continue;
    }

    // Intersect with the new use node.
    Node* use = edge.from();
    UnobservablesSet new_set = unobservable_for_id(use->id());
    if (first) {
      first = false;
      cur_set = new_set;
      if (cur_set.IsUnvisited()) {
        cur_set = unobservables_visited_empty_;
      }
    } else {
      cur_set =
          cur_set.Intersect(new_set, unobservables_visited_empty_, temp_zone());
    }

    // Break fast for the empty set since the intersection will always be empty.
    if (cur_set.IsEmpty()) {
      break;
    }
  }

  DCHECK(!cur_set.IsUnvisited());
  return cur_set;
}

UnobservablesSet::UnobservablesSet() : set_(nullptr) {}

// Create a new empty UnobservablesSet. This allocates in the zone, and
// can probably be optimized to use a global singleton.
UnobservablesSet UnobservablesSet::VisitedEmpty(Zone* zone) {
  ZoneSet<UnobservableStore>* empty_set =
      new (zone->New(sizeof(ZoneSet<UnobservableStore>)))
          ZoneSet<UnobservableStore>(zone);
  return UnobservablesSet(empty_set);
}

// Computes the intersection of two UnobservablesSets. If one of the sets is
// empty, will return empty.
UnobservablesSet UnobservablesSet::Intersect(const UnobservablesSet& other,
                                             const UnobservablesSet& empty,
                                             Zone* zone) const {
  if (IsEmpty() || other.IsEmpty()) {
    return empty;
  } else {
    ZoneSet<UnobservableStore>* intersection =
        new (zone->New(sizeof(ZoneSet<UnobservableStore>)))
            ZoneSet<UnobservableStore>(zone);
    // Put the intersection of set() and other.set() in intersection.
    set_intersection(set()->begin(), set()->end(), other.set()->begin(),
                     other.set()->end(),
                     std::inserter(*intersection, intersection->end()));

    return UnobservablesSet(intersection);
  }
}

UnobservablesSet UnobservablesSet::Add(UnobservableStore obs,
                                       Zone* zone) const {
  bool found = set()->find(obs) != set()->end();
  if (found) {
    return *this;
  } else {
    // Make a new empty set.
    ZoneSet<UnobservableStore>* new_set =
        new (zone->New(sizeof(ZoneSet<UnobservableStore>)))
            ZoneSet<UnobservableStore>(zone);
    // Copy the old elements over.
    *new_set = *set();
    // Add the new element.
    bool inserted = new_set->insert(obs).second;
    DCHECK(inserted);
    USE(inserted);  // silence warning about unused variable

    return UnobservablesSet(new_set);
  }
}

UnobservablesSet UnobservablesSet::RemoveSameOffset(StoreOffset offset,
                                                    Zone* zone) const {
  // Make a new empty set.
  ZoneSet<UnobservableStore>* new_set =
      new (zone->New(sizeof(ZoneSet<UnobservableStore>)))
          ZoneSet<UnobservableStore>(zone);
  // Copy all elements over that have a different offset.
  for (auto obs : *set()) {
    if (obs.offset_ != offset) {
      new_set->insert(obs);
    }
  }

  return UnobservablesSet(new_set);
}

#undef TRACE
#undef CHECK_EXTRA
#undef DCHECK_EXTRA

}  // namespace compiler
}  // namespace internal
}  // namespace v8
