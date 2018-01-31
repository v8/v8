// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BRANCH_CONDITION_ELIMINATION_H_
#define V8_COMPILER_BRANCH_CONDITION_ELIMINATION_H_

#include "src/base/compiler-specific.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/node-aux-data.h"
#include "src/globals.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declarations.
class CommonOperatorBuilder;
class JSGraph;

// A generic stack implemented as a purely functional singly-linked list, which
// results in an O(1) copy operation. It is the equivalent of functional lists
// in ML-like languages, with the only difference that it also caches the length
// of the list in each node.
// TODO(tebbi): Use this implementation also for RedundancyElimination.
template <class A>
class FunctionalList {
 private:
  struct Cons : ZoneObject {
    Cons(A top, Cons* rest)
        : top(std::move(top)), rest(rest), size(1 + (rest ? rest->size : 0)) {}
    A const top;
    Cons* const rest;
    size_t const size;
  };

 public:
  FunctionalList() : elements_(nullptr) {}

  bool operator==(const FunctionalList<A>& other) const {
    if (Size() != other.Size()) return false;
    iterator it = begin();
    iterator other_it = other.begin();
    while (true) {
      if (it == other_it) return true;
      if (*it != *other_it) return false;
      ++it;
      ++other_it;
    }
  }
  bool operator!=(const FunctionalList<A>& other) const {
    return !(*this == other);
  }

  const A& Front() const {
    DCHECK_GT(Size(), 0);
    return elements_->top;
  }

  FunctionalList Rest() const {
    FunctionalList result = *this;
    result.DropFront();
    return result;
  }

  void DropFront() {
    CHECK_GT(Size(), 0);
    elements_ = elements_->rest;
  }

  void PushFront(A a, Zone* zone) {
    elements_ = new (zone) Cons(std::move(a), elements_);
  }

  // If {hint} happens to be exactly what we want to allocate, avoid allocation
  // by reusing {hint}.
  void PushFront(A a, Zone* zone, FunctionalList hint) {
    if (hint.Size() == Size() + 1 && hint.Front() == a &&
        hint.Rest() == *this) {
      *this = hint;
    } else {
      PushFront(a, zone);
    }
  }

  // Drop elements until the current stack is equal to the tail shared with
  // {other}. The shared tail must not only be equal, but also refer to the
  // same memory.
  void ResetToCommonAncestor(FunctionalList other) {
    while (other.Size() > Size()) other.DropFront();
    while (other.Size() < Size()) DropFront();
    while (elements_ != other.elements_) {
      DropFront();
      other.DropFront();
    }
  }

  size_t Size() const { return elements_ ? elements_->size : 0; }

  class iterator {
   public:
    explicit iterator(Cons* cur) : current_(cur) {}

    const A& operator*() const { return current_->top; }
    iterator& operator++() {
      current_ = current_->rest;
      return *this;
    }
    bool operator==(const iterator& other) const {
      return this->current_ == other.current_;
    }
    bool operator!=(const iterator& other) const { return !(*this == other); }

   private:
    Cons* current_;
  };

  iterator begin() const { return iterator(elements_); }
  iterator end() const { return iterator(nullptr); }

 private:
  Cons* elements_;
};

class V8_EXPORT_PRIVATE BranchElimination final
    : public NON_EXPORTED_BASE(AdvancedReducer) {
 public:
  BranchElimination(Editor* editor, JSGraph* js_graph, Zone* zone);
  ~BranchElimination() final;

  const char* reducer_name() const override { return "BranchElimination"; }

  Reduction Reduce(Node* node) final;

 private:
  struct BranchCondition {
    Node* condition;
    bool is_true;

    bool operator==(BranchCondition other) const {
      return condition == other.condition && is_true == other.is_true;
    }
    bool operator!=(BranchCondition other) const { return !(*this == other); }
  };

  // Class for tracking information about branch conditions.
  // At the moment it is a linked list of conditions and their values
  // (true or false).
  class ControlPathConditions : public FunctionalList<BranchCondition> {
   public:
    Maybe<bool> LookupCondition(Node* condition) const;
    void AddCondition(Zone* zone, Node* condition, bool is_true,
                      ControlPathConditions hint);

   private:
    using FunctionalList<BranchCondition>::PushFront;
  };

  Reduction ReduceBranch(Node* node);
  Reduction ReduceDeoptimizeConditional(Node* node);
  Reduction ReduceIf(Node* node, bool is_true_branch);
  Reduction ReduceLoop(Node* node);
  Reduction ReduceMerge(Node* node);
  Reduction ReduceStart(Node* node);
  Reduction ReduceOtherControl(Node* node);

  Reduction TakeConditionsFromFirstControl(Node* node);
  Reduction UpdateConditions(Node* node, ControlPathConditions conditions);
  Reduction UpdateConditions(Node* node, ControlPathConditions prev_conditions,
                             Node* current_condition, bool is_true_branch);

  Node* dead() const { return dead_; }
  Graph* graph() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  CommonOperatorBuilder* common() const;

  JSGraph* const jsgraph_;

  // Maps each control node to the condition information known about the node.
  // If the information is nullptr, then we have not calculated the information
  // yet.
  NodeAuxData<ControlPathConditions> node_conditions_;
  NodeAuxData<bool> reduced_;
  Zone* zone_;
  Node* dead_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BRANCH_CONDITION_ELIMINATION_H_
