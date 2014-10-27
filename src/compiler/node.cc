// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/compiler/node.h"

namespace v8 {
namespace internal {
namespace compiler {

void Node::Kill() {
  DCHECK_NOT_NULL(op());
  RemoveAllInputs();
  DCHECK(uses().empty());
}


Node::Node(GenericGraphBase* graph, int input_count, int reserve_input_count)
    : input_count_(input_count),
      reserve_input_count_(reserve_input_count),
      has_appendable_inputs_(false),
      use_count_(0),
      first_use_(NULL),
      last_use_(NULL) {
  DCHECK(reserve_input_count <= kMaxReservedInputs);
  inputs_.static_ = reinterpret_cast<Input*>(this + 1);
  AssignUniqueID(graph);
}


void Node::CollectProjections(NodeVector* projections) {
  for (size_t i = 0; i < projections->size(); i++) {
    (*projections)[i] = NULL;
  }
  for (UseIter i = uses().begin(); i != uses().end(); ++i) {
    if ((*i)->opcode() != IrOpcode::kProjection) continue;
    size_t index = OpParameter<size_t>(*i);
    DCHECK_LT(index, projections->size());
    DCHECK_EQ(NULL, (*projections)[index]);
    (*projections)[index] = *i;
  }
}


Node* Node::FindProjection(size_t projection_index) {
  for (UseIter i = uses().begin(); i != uses().end(); ++i) {
    if ((*i)->opcode() == IrOpcode::kProjection &&
        OpParameter<size_t>(*i) == projection_index) {
      return *i;
    }
  }
  return NULL;
}


void Node::AssignUniqueID(GenericGraphBase* graph) {
  id_ = graph->NextNodeID();
}


Node::Inputs::iterator Node::Inputs::begin() {
  return Node::Inputs::iterator(this->node_, 0);
}


Node::Inputs::iterator Node::Inputs::end() {
  return Node::Inputs::iterator(this->node_, this->node_->InputCount());
}


Node::Uses::iterator Node::Uses::begin() {
  return Node::Uses::iterator(this->node_);
}


Node::Uses::iterator Node::Uses::end() { return Node::Uses::iterator(); }


void Node::ReplaceUses(Node* replace_to) {
  for (Use* use = first_use_; use != NULL; use = use->next) {
    use->from->GetInputRecordPtr(use->input_index)->to = replace_to;
  }
  if (replace_to->last_use_ == NULL) {
    DCHECK_EQ(NULL, replace_to->first_use_);
    replace_to->first_use_ = first_use_;
    replace_to->last_use_ = last_use_;
  } else if (first_use_ != NULL) {
    DCHECK_NE(NULL, replace_to->first_use_);
    replace_to->last_use_->next = first_use_;
    first_use_->prev = replace_to->last_use_;
    replace_to->last_use_ = last_use_;
  }
  replace_to->use_count_ += use_count_;
  use_count_ = 0;
  first_use_ = NULL;
  last_use_ = NULL;
}


void Node::RemoveAllInputs() {
  for (Inputs::iterator iter(inputs().begin()); iter != inputs().end();
       ++iter) {
    iter.GetInput()->Update(NULL);
  }
}


void Node::TrimInputCount(int new_input_count) {
  if (new_input_count == input_count_) return;  // Nothing to do.

  DCHECK(new_input_count < input_count_);

  // Update inline inputs.
  for (int i = new_input_count; i < input_count_; i++) {
    Node::Input* input = GetInputRecordPtr(i);
    input->Update(NULL);
  }
  input_count_ = new_input_count;
}


void Node::ReplaceInput(int index, Node* new_to) {
  Input* input = GetInputRecordPtr(index);
  input->Update(new_to);
}


void Node::Input::Update(Node* new_to) {
  Node* old_to = this->to;
  if (new_to == old_to) return;  // Nothing to do.
  // Snip out the use from where it used to be
  if (old_to != NULL) {
    old_to->RemoveUse(use);
  }
  to = new_to;
  // And put it into the new node's use list.
  if (new_to != NULL) {
    new_to->AppendUse(use);
  } else {
    use->next = NULL;
    use->prev = NULL;
  }
}


void Node::EnsureAppendableInputs(Zone* zone) {
  if (!has_appendable_inputs_) {
    void* deque_buffer = zone->New(sizeof(InputDeque));
    InputDeque* deque = new (deque_buffer) InputDeque(zone);
    for (int i = 0; i < input_count_; ++i) {
      deque->push_back(inputs_.static_[i]);
    }
    inputs_.appendable_ = deque;
    has_appendable_inputs_ = true;
  }
}


void Node::AppendInput(Zone* zone, Node* to_append) {
  Use* new_use = new (zone) Use;
  Input new_input;
  new_input.to = to_append;
  new_input.use = new_use;
  if (reserve_input_count_ > 0) {
    DCHECK(!has_appendable_inputs_);
    reserve_input_count_--;
    inputs_.static_[input_count_] = new_input;
  } else {
    EnsureAppendableInputs(zone);
    inputs_.appendable_->push_back(new_input);
  }
  new_use->input_index = input_count_;
  new_use->from = this;
  to_append->AppendUse(new_use);
  input_count_++;
}


void Node::InsertInput(Zone* zone, int index, Node* to_insert) {
  DCHECK(index >= 0 && index < InputCount());
  // TODO(turbofan): Optimize this implementation!
  AppendInput(zone, InputAt(InputCount() - 1));
  for (int i = InputCount() - 1; i > index; --i) {
    ReplaceInput(i, InputAt(i - 1));
  }
  ReplaceInput(index, to_insert);
}


void Node::RemoveInput(int index) {
  DCHECK(index >= 0 && index < InputCount());
  // TODO(turbofan): Optimize this implementation!
  for (; index < InputCount() - 1; ++index) {
    ReplaceInput(index, InputAt(index + 1));
  }
  TrimInputCount(InputCount() - 1);
}


void Node::AppendUse(Use* use) {
  use->next = NULL;
  use->prev = last_use_;
  if (last_use_ == NULL) {
    first_use_ = use;
  } else {
    last_use_->next = use;
  }
  last_use_ = use;
  ++use_count_;
}


void Node::RemoveUse(Use* use) {
  if (last_use_ == use) {
    last_use_ = use->prev;
  }
  if (use->prev != NULL) {
    use->prev->next = use->next;
  } else {
    first_use_ = use->next;
  }
  if (use->next != NULL) {
    use->next->prev = use->prev;
  }
  --use_count_;
}


bool Node::OwnedBy(Node* owner) const {
  return first_use_ != NULL && first_use_->from == owner &&
         first_use_->next == NULL;
}


Node* Node::New(GenericGraphBase* graph, int input_count, Node** inputs,
                bool has_extensible_inputs) {
  size_t node_size = sizeof(Node);
  int reserve_input_count = has_extensible_inputs ? kDefaultReservedInputs : 0;
  size_t inputs_size = (input_count + reserve_input_count) * sizeof(Input);
  size_t uses_size = input_count * sizeof(Use);
  int size = static_cast<int>(node_size + inputs_size + uses_size);
  Zone* zone = graph->zone();
  void* buffer = zone->New(size);
  Node* result = new (buffer) Node(graph, input_count, reserve_input_count);
  Input* input =
      reinterpret_cast<Input*>(reinterpret_cast<char*>(buffer) + node_size);
  Use* use =
      reinterpret_cast<Use*>(reinterpret_cast<char*>(input) + inputs_size);

  for (int current = 0; current < input_count; ++current) {
    Node* to = *inputs++;
    input->to = to;
    input->use = use;
    use->input_index = current;
    use->from = result;
    to->AppendUse(use);
    ++use;
    ++input;
  }
  return result;
}


bool Node::Uses::empty() { return begin() == end(); }


std::ostream& operator<<(std::ostream& os, const Node& n) {
  os << n.id() << ": " << *n.op();
  if (n.op()->InputCount() != 0) {
    os << "(";
    for (int i = 0; i < n.op()->InputCount(); ++i) {
      if (i != 0) os << ", ";
      os << n.InputAt(i)->id();
    }
    os << ")";
  }
  return os;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
