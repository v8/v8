// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_NODE_H_
#define V8_COMPILER_NODE_H_

#include <deque>
#include <set>
#include <vector>

#include "src/compiler/generic-algorithm.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/types.h"
#include "src/zone.h"
#include "src/zone-allocator.h"
#include "src/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

// A Node is the basic primitive of an IR graph. In addition to the graph
// book-keeping Nodes only contain a mutable Operator that may change
// during compilation, e.g. during lowering passes.  Other information that
// needs to be associated with Nodes during compilation must be stored
// out-of-line indexed by the Node's id.
class Node FINAL {
 public:
  Node(GenericGraphBase* graph, int input_count, int reserve_input_count);

  void Initialize(const Operator* op) { set_op(op); }

  bool IsDead() const { return InputCount() > 0 && InputAt(0) == NULL; }
  void Kill();

  void CollectProjections(ZoneVector<Node*>* projections);
  Node* FindProjection(size_t projection_index);

  const Operator* op() const { return op_; }
  void set_op(const Operator* op) { op_ = op; }

  IrOpcode::Value opcode() const {
    DCHECK(op_->opcode() <= IrOpcode::kLast);
    return static_cast<IrOpcode::Value>(op_->opcode());
  }

  inline NodeId id() const { return id_; }

  int InputCount() const { return input_count_; }
  Node* InputAt(int index) const {
    return static_cast<Node*>(GetInputRecordPtr(index)->to);
  }

  void ReplaceInput(int index, Node* new_input);
  void AppendInput(Zone* zone, Node* new_input);
  void InsertInput(Zone* zone, int index, Node* new_input);
  void RemoveInput(int index);

  int UseCount() { return use_count_; }
  Node* UseAt(int index) {
    DCHECK(index < use_count_);
    Use* current = first_use_;
    while (index-- != 0) {
      current = current->next;
    }
    return current->from;
  }
  void ReplaceUses(Node* replace_to);
  template <class UnaryPredicate>
  inline void ReplaceUsesIf(UnaryPredicate pred, Node* replace_to);
  void RemoveAllInputs();

  void TrimInputCount(int input_count);

  class Inputs {
   public:
    class iterator;
    iterator begin();
    iterator end();

    explicit Inputs(Node* node) : node_(node) {}

   private:
    Node* node_;
  };

  Inputs inputs() { return Inputs(this); }

  class Uses {
   public:
    class iterator;
    iterator begin();
    iterator end();
    bool empty();  // { return begin() == end(); }

    explicit Uses(Node* node) : node_(node) {}

   private:
    Node* node_;
  };

  Uses uses() { return Uses(this); }

  class Edge;

  bool OwnedBy(Node* owner) const;

  static Node* New(GenericGraphBase* graph, int input_count, Node** inputs,
                   bool has_extensible_inputs);


 private:
  const Operator* op_;
  Bounds bounds_;

  friend class NodeProperties;
  Bounds bounds() { return bounds_; }
  void set_bounds(Bounds b) { bounds_ = b; }

  friend class GenericGraphBase;

  class Use : public ZoneObject {
   public:
    Node* from;
    Use* next;
    Use* prev;
    int input_index;
  };

  class Input {
   public:
    Node* to;
    Use* use;

    void Update(Node* new_to);
  };

  void EnsureAppendableInputs(Zone* zone);

  Input* GetInputRecordPtr(int index) const {
    if (has_appendable_inputs_) {
      return &((*inputs_.appendable_)[index]);
    } else {
      return inputs_.static_ + index;
    }
  }

  void AppendUse(Use* use);
  void RemoveUse(Use* use);

  void* operator new(size_t, void* location) { return location; }

  void AssignUniqueID(GenericGraphBase* graph);

  typedef ZoneDeque<Input> InputDeque;

  static const int kReservedInputCountBits = 2;
  static const int kMaxReservedInputs = (1 << kReservedInputCountBits) - 1;
  static const int kDefaultReservedInputs = kMaxReservedInputs;

  NodeId id_;
  int input_count_ : 29;
  unsigned int reserve_input_count_ : kReservedInputCountBits;
  bool has_appendable_inputs_ : 1;
  union {
    // When a node is initially allocated, it uses a static buffer to hold its
    // inputs under the assumption that the number of outputs will not increase.
    // When the first input is appended, the static buffer is converted into a
    // deque to allow for space-efficient growing.
    Input* static_;
    InputDeque* appendable_;
  } inputs_;
  int use_count_;
  Use* first_use_;
  Use* last_use_;

  DISALLOW_COPY_AND_ASSIGN(Node);
};

class Node::Edge {
 public:
  Node* from() const { return input_->use->from; }
  Node* to() const { return input_->to; }
  int index() const {
    int index = input_->use->input_index;
    DCHECK(index < input_->use->from->input_count_);
    return index;
  }

 private:
  friend class Node::Uses::iterator;
  friend class Node::Inputs::iterator;

  explicit Edge(Node::Input* input) : input_(input) {}

  Node::Input* input_;
};


// A forward iterator to visit the nodes which are depended upon by a node
// in the order of input.
class Node::Inputs::iterator {
 public:
  iterator(const Node::Inputs::iterator& other)  // NOLINT
      : node_(other.node_),
        index_(other.index_) {}

  Node* operator*() { return GetInput()->to; }
  Node::Edge edge() { return Node::Edge(GetInput()); }
  bool operator==(const iterator& other) const {
    return other.index_ == index_ && other.node_ == node_;
  }
  bool operator!=(const iterator& other) const { return !(other == *this); }
  iterator& operator++() {
    DCHECK(node_ != NULL);
    DCHECK(index_ < node_->input_count_);
    ++index_;
    return *this;
  }
  iterator& UpdateToAndIncrement(Node* new_to) {
    Node::Input* input = GetInput();
    input->Update(new_to);
    index_++;
    return *this;
  }
  int index() { return index_; }

 private:
  friend class Node;

  explicit iterator(Node* node, int index) : node_(node), index_(index) {}

  Input* GetInput() const { return node_->GetInputRecordPtr(index_); }

  Node* node_;
  int index_;
};

// A forward iterator to visit the uses of a node. The uses are returned in
// the order in which they were added as inputs.
class Node::Uses::iterator {
 public:
  iterator(const Node::Uses::iterator& other)  // NOLINT
      : current_(other.current_),
        index_(other.index_) {}

  Node* operator*() { return current_->from; }
  Node::Edge edge() { return Node::Edge(CurrentInput()); }

  bool operator==(const iterator& other) { return other.current_ == current_; }
  bool operator!=(const iterator& other) { return other.current_ != current_; }
  iterator& operator++() {
    DCHECK(current_ != NULL);
    index_++;
    current_ = current_->next;
    return *this;
  }
  iterator& UpdateToAndIncrement(Node* new_to) {
    DCHECK(current_ != NULL);
    index_++;
    Node::Input* input = CurrentInput();
    current_ = current_->next;
    input->Update(new_to);
    return *this;
  }
  int index() const { return index_; }

 private:
  friend class Node::Uses;

  iterator() : current_(NULL), index_(0) {}
  explicit iterator(Node* node) : current_(node->first_use_), index_(0) {}

  Input* CurrentInput() const {
    return current_->from->GetInputRecordPtr(current_->input_index);
  }

  Node::Use* current_;
  int index_;
};


template <class UnaryPredicate>
inline void Node::ReplaceUsesIf(UnaryPredicate pred, Node* replace_to) {
  for (Use* use = first_use_; use != NULL;) {
    Use* next = use->next;
    if (pred(use->from)) {
      RemoveUse(use);
      replace_to->AppendUse(use);
      use->from->GetInputRecordPtr(use->input_index)->to = replace_to;
    }
    use = next;
  }
}

std::ostream& operator<<(std::ostream& os, const Node& n);

typedef GenericGraphVisit::NullNodeVisitor<Node> NullNodeVisitor;

typedef std::set<Node*, std::less<Node*>, zone_allocator<Node*> > NodeSet;
typedef NodeSet::iterator NodeSetIter;
typedef NodeSet::reverse_iterator NodeSetRIter;

typedef ZoneVector<Node*> NodeVector;
typedef NodeVector::iterator NodeVectorIter;
typedef NodeVector::const_iterator NodeVectorConstIter;
typedef NodeVector::reverse_iterator NodeVectorRIter;

typedef ZoneVector<NodeVector> NodeVectorVector;
typedef NodeVectorVector::iterator NodeVectorVectorIter;
typedef NodeVectorVector::reverse_iterator NodeVectorVectorRIter;

typedef Node::Uses::iterator UseIter;
typedef Node::Inputs::iterator InputIter;

// Helper to extract parameters from Operator1<*> nodes.
template <typename T>
static inline const T& OpParameter(const Node* node) {
  return OpParameter<T>(node->op());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_NODE_H_
