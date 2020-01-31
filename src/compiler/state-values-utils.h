// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_STATE_VALUES_UTILS_H_
#define V8_COMPILER_STATE_VALUES_UTILS_H_

#include <array>
#include "src/common/globals.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/js-graph.h"

namespace v8 {
namespace internal {

class BitVector;

namespace compiler {

class Graph;

class V8_EXPORT_PRIVATE StateValuesCache {
 public:
  explicit StateValuesCache(JSGraph* js_graph);

  Node* GetNodeForValues(Node** values, size_t count,
                         const BitVector* liveness = nullptr,
                         int liveness_offset = 0);

 private:
  static const size_t kMaxInputCount = 8;
  using WorkingBuffer = std::array<Node*, kMaxInputCount>;

  struct NodeKey {
    Node* node;

    explicit NodeKey(Node* node) : node(node) {}
  };

  struct StateValuesKey : public NodeKey {
    // ValueArray - array of nodes ({node} has to be nullptr).
    size_t count;
    SparseInputMask mask;
    Node** values;

    StateValuesKey(size_t count, SparseInputMask mask, Node** values)
        : NodeKey(nullptr), count(count), mask(mask), values(values) {}
  };

  static bool AreKeysEqual(void* key1, void* key2);
  static bool IsKeysEqualToNode(StateValuesKey* key, Node* node);
  static bool AreValueKeysEqual(StateValuesKey* key1, StateValuesKey* key2);

  // Fills {node_buffer}, starting from {node_count}, with {values}, starting
  // at {values_idx}, sparsely encoding according to {liveness}. {node_count} is
  // updated with the new number of inputs in {node_buffer}, and a bitmask of
  // the sparse encoding is returned.
  SparseInputMask::BitMaskType FillBufferWithValues(WorkingBuffer* node_buffer,
                                                    size_t* node_count,
                                                    size_t* values_idx,
                                                    Node** values, size_t count,
                                                    const BitVector* liveness,
                                                    int liveness_offset);

  Node* BuildTree(size_t* values_idx, Node** values, size_t count,
                  const BitVector* liveness, int liveness_offset, size_t level);

  WorkingBuffer* GetWorkingSpace(size_t level);
  Node* GetEmptyStateValues();
  Node* GetValuesNodeFromCache(Node** nodes, size_t count,
                               SparseInputMask mask);

  Graph* graph() { return js_graph_->graph(); }
  CommonOperatorBuilder* common() { return js_graph_->common(); }

  Zone* zone() { return graph()->zone(); }

  JSGraph* js_graph_;
  CustomMatcherZoneHashMap hash_map_;
  ZoneVector<WorkingBuffer> working_space_;  // One working space per level.
  Node* empty_state_values_;
};

class V8_EXPORT_PRIVATE StateValuesAccess {
 public:
  struct TypedNode {
    Node* node;
    MachineType type;
    TypedNode(Node* node, MachineType type) : node(node), type(type) {}
  };

  class V8_EXPORT_PRIVATE iterator {
   public:
    // Bare minimum of operators needed for range iteration.
    bool operator!=(iterator const& other) {
      // We only allow comparison with end().
      CHECK(other.done());
      return !done();
    }

    iterator& operator++() {
      Advance();
      return *this;
    }

    // TypedNode operator*();
    TypedNode operator*() { return TypedNode(node(), type()); }

   private:
    friend class StateValuesAccess;

    iterator() : current_depth_(-1) {}
    explicit iterator(Node* node) : current_depth_(0) {
      stack_[current_depth_] =
          SparseInputMaskOf(node->op()).IterateOverInputs(node);
      EnsureValid();
    }

    Node* node() { return Top()->Get(nullptr); }
    MachineType type() {
      Node* parent = Top()->parent();
      if (parent->opcode() == IrOpcode::kStateValues) {
        return MachineType::AnyTagged();
      } else {
        DCHECK_EQ(IrOpcode::kTypedStateValues, parent->opcode());

        if (Top()->IsEmpty()) {
          return MachineType::None();
        } else {
          ZoneVector<MachineType> const* types = MachineTypesOf(parent->op());
          return (*types)[Top()->real_index()];
        }
      }
    }
    bool done() const { return current_depth_ < 0; }

    void Advance() {
      Top()->Advance();
      EnsureValid();
    }

    void EnsureValid() {
      while (true) {
        SparseInputMask::InputIterator* top = Top();

        if (top->IsEmpty()) {
          // We are on a valid (albeit optimized out) node.
          return;
        }

        if (top->IsEnd()) {
          // We have hit the end of this iterator. Pop the stack and move to the
          // next sibling iterator.
          Pop();
          if (done()) {
            // Stack is exhausted, we have reached the end.
            return;
          }
          Top()->Advance();
          continue;
        }

        // At this point the value is known to be live and within our input
        // nodes.
        Node* value_node = top->GetReal();

        if (value_node->opcode() == IrOpcode::kStateValues ||
            value_node->opcode() == IrOpcode::kTypedStateValues) {
          // Nested state, we need to push to the stack.
          Push(value_node);
          continue;
        }

        // We are on a valid node, we can stop the iteration.
        return;
      }
    }

    SparseInputMask::InputIterator* Top() {
      DCHECK_LE(0, current_depth_);
      DCHECK_GT(kMaxInlineDepth, current_depth_);
      return &(stack_[current_depth_]);
    }

    void Push(Node* node) {
      current_depth_++;
      CHECK_GT(kMaxInlineDepth, current_depth_);
      stack_[current_depth_] =
          SparseInputMaskOf(node->op()).IterateOverInputs(node);
    }

    void Pop() {
      DCHECK_LE(0, current_depth_);
      current_depth_--;
    }

    static const int kMaxInlineDepth = 8;
    SparseInputMask::InputIterator stack_[kMaxInlineDepth];
    int current_depth_;
  };

  explicit StateValuesAccess(Node* node) : node_(node) {}

  size_t size();
  iterator begin() { return iterator(node_); }
  iterator end() { return iterator(); }

 private:
  Node* node_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_STATE_VALUES_UTILS_H_
