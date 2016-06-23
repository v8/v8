// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/store-store-elimination.h"

#include "src/compiler/all-nodes.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE(fmt, ...)                                              \
  do {                                                               \
    if (FLAG_trace_store_elimination) {                              \
      PrintF("StoreStoreElimination::ReduceEligibleNode: " fmt "\n", \
             ##__VA_ARGS__);                                         \
    }                                                                \
  } while (false)

// A simple store-store elimination. When the effect chain contains the
// following sequence,
//
// - StoreField[[+off_1]](x1, y1)
// - StoreField[[+off_2]](x2, y2)
// - StoreField[[+off_3]](x3, y3)
//   ...
// - StoreField[[+off_n]](xn, yn)
//
// where the xes are the objects and the ys are the values to be stored, then
// we are going to say that a store is superfluous if the same offset of the
// same object will be stored to in the future. If off_i == off_j and xi == xj
// and i < j, then we optimize the i'th StoreField away.
//
// This optimization should be initiated on the last StoreField in such a
// sequence.
//
// The algorithm works by walking the effect chain from the last StoreField
// upwards. While walking, we maintain a map {futureStore} from offsets to
// nodes; initially it is empty. As we walk the effect chain upwards, if
// futureStore[off] = n, then any store to node {n} with offset {off} is
// guaranteed to be useless because we do a full-width[1] store to that offset
// of that object in the near future anyway. For example, for this effect
// chain
//
// 71: StoreField(60, 0)
// 72: StoreField(65, 8)
// 73: StoreField(63, 8)
// 74: StoreField(65, 16)
// 75: StoreField(62, 8)
//
// just before we get to 72, we will have futureStore = {8: 63, 16: 65}.
//
// Here is the complete process.
//
// - We are at the end of a sequence of consecutive StoreFields.
// - We start out with futureStore = empty.
// - We then walk the effect chain upwards to find the next StoreField [2].
//
//   1. If the offset is not a key of {futureStore} yet, we put it in.
//   2. If the offset is a key of {futureStore}, but futureStore[offset] is a
//      different node, we overwrite futureStore[offset] with the current node.
//   3. If the offset is a key of {futureStore} and futureStore[offset] equals
//      this node, we eliminate this StoreField.
//
//   As long as the current effect input points to a node with a single effect
//   output, and as long as its opcode is StoreField, we keep traversing
//   upwards.
//
// [1] This optimization is unsound if we optimize away a store to an offset
//   because we store to the same offset in the future, even though the future
//   store is narrower than the store we optimize away. Therefore, in case (1)
//   and (2) we only add/overwrite to the dictionary when the field access has
//   maximal size. For simplicity of implementation, we do not try to detect
//   case (3).
//
// [2] We make sure that we only traverse the linear part, that is, the part
//   where every node has exactly one incoming and one outgoing effect edge.
//   Also, we only keep walking upwards as long as we keep finding consecutive
//   StoreFields on the same node.

StoreStoreElimination::StoreStoreElimination(JSGraph* js_graph, Zone* temp_zone)
    : jsgraph_(js_graph), temp_zone_(temp_zone) {}

StoreStoreElimination::~StoreStoreElimination() {}

void StoreStoreElimination::Run() {
  // The store-store elimination performs work on chains of certain types of
  // nodes. The elimination must be invoked on the lowest node in such a
  // chain; we have a helper function IsEligibleNode that returns true
  // precisely on the lowest node in such a chain.
  //
  // Because the elimination removes nodes from the graph, even remove nodes
  // that the elimination was not invoked on, we cannot use a normal
  // AdvancedReducer but we manually find which nodes to invoke the
  // elimination on. Then in a next step, we invoke the elimination for each
  // node that was eligible.

  NodeVector eligible(temp_zone());  // loops over all nodes
  AllNodes all(temp_zone(), jsgraph()->graph());

  for (Node* node : all.live) {
    if (IsEligibleNode(node)) {
      eligible.push_back(node);
    }
  }

  for (Node* node : eligible) {
    ReduceEligibleNode(node);
  }
}

namespace {

// 16 bits was chosen fairly arbitrarily; it seems enough now. 8 bits is too
// few.
typedef uint16_t Offset;

// To safely cast an offset from a FieldAccess, which has a wider range
// (namely int).
Offset ToOffset(int offset) {
  CHECK(0 <= offset && offset < (1 << 8 * sizeof(Offset)));
  return (Offset)offset;
}

Offset ToOffset(const FieldAccess& access) { return ToOffset(access.offset); }

// If node has a single effect use, return that node. If node has no or
// multiple effect uses, return nullptr.
Node* SingleEffectUse(Node* node) {
  Node* last_use = nullptr;
  for (Edge edge : node->use_edges()) {
    if (!NodeProperties::IsEffectEdge(edge)) {
      continue;
    }
    if (last_use != nullptr) {
      // more than one
      return nullptr;
    }
    last_use = edge.from();
    DCHECK_NOT_NULL(last_use);
  }
  return last_use;
}

// Return true if node is the last consecutive StoreField node in a linear
// part of the effect chain.
bool IsEndOfStoreFieldChain(Node* node) {
  Node* next_on_chain = SingleEffectUse(node);
  return (next_on_chain == nullptr ||
          next_on_chain->op()->opcode() != IrOpcode::kStoreField);
}

// The argument must be a StoreField node. If there is a node before it in the
// effect chain, and if this part of the effect chain is linear (no other
// effect uses of that previous node), then return that previous node.
// Otherwise, return nullptr.
//
// The returned node need not be a StoreField.
Node* PreviousEffectBeforeStoreField(Node* node) {
  DCHECK_EQ(node->op()->opcode(), IrOpcode::kStoreField);
  DCHECK_EQ(node->op()->EffectInputCount(), 1);

  Node* previous = NodeProperties::GetEffectInput(node);
  if (previous != nullptr && node == SingleEffectUse(previous)) {
    return previous;
  } else {
    return nullptr;
  }
}

size_t rep_size_of(MachineRepresentation rep) {
  return ((size_t)1) << ElementSizeLog2Of(rep);
}
size_t rep_size_of(FieldAccess access) {
  return rep_size_of(access.machine_type.representation());
}

}  // namespace

bool StoreStoreElimination::IsEligibleNode(Node* node) {
  return (node->op()->opcode() == IrOpcode::kStoreField) &&
         IsEndOfStoreFieldChain(node);
}

void StoreStoreElimination::ReduceEligibleNode(Node* node) {
  DCHECK(IsEligibleNode(node));

  // if (FLAG_trace_store_elimination) {
  //   PrintF("** StoreStoreElimination::ReduceEligibleNode: activated:
  //   #%d\n",
  //          node->id());
  // }

  TRACE("activated: #%d", node->id());

  // Initialize empty futureStore.
  ZoneMap<Offset, Node*> futureStore(temp_zone());

  Node* current_node = node;

  do {
    FieldAccess access = OpParameter<FieldAccess>(current_node->op());
    Offset offset = ToOffset(access);
    Node* object_input = current_node->InputAt(0);

    Node* previous = PreviousEffectBeforeStoreField(current_node);

    CHECK(rep_size_of(access) <= rep_size_of(MachineRepresentation::kTagged));
    if (rep_size_of(access) == rep_size_of(MachineRepresentation::kTagged)) {
      // Try to insert. If it was present, this will preserve the original
      // value.
      auto insert_result =
          futureStore.insert(std::make_pair(offset, object_input));
      if (insert_result.second) {
        // Key was not present. This means that there is no matching
        // StoreField to this offset in the future, so we cannot optimize
        // current_node away. However, we will record the current StoreField
        // in futureStore, and continue ascending up the chain.
        TRACE("#%d[[+%d]] -- wide, key not present", current_node->id(),
              offset);
      } else if (insert_result.first->second != object_input) {
        // Key was present, and the value did not equal object_input. This
        // means
        // that there is a StoreField to this offset in the future, but the
        // object instance comes from a different Node. We pessimistically
        // assume that we cannot optimize current_node away. However, we will
        // record the current StoreField in futureStore, and continue
        // ascending up the chain.
        insert_result.first->second = object_input;
        TRACE("#%d[[+%d]] -- wide, diff object", current_node->id(), offset);
      } else {
        // Key was present, and the value equalled object_input. This means
        // that soon after in the effect chain, we will do a StoreField to the
        // same object with the same offset, therefore current_node can be
        // optimized away. We don't need to update futureStore.

        Node* previous_effect = NodeProperties::GetEffectInput(current_node);

        NodeProperties::ReplaceUses(current_node, nullptr, previous_effect,
                                    nullptr, nullptr);
        current_node->Kill();
        TRACE("#%d[[+%d]] -- wide, eliminated", current_node->id(), offset);
      }
    } else {
      TRACE("#%d[[+%d]] -- narrow, not eliminated", current_node->id(), offset);
    }

    // Regardless of whether we eliminated node {current}, we want to
    // continue walking up the effect chain.

    current_node = previous;
  } while (current_node != nullptr &&
           current_node->op()->opcode() == IrOpcode::kStoreField);

  TRACE("finished");
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
