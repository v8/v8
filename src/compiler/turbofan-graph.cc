// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turbofan-graph.h"

#include <algorithm>

#include "src/compiler/node.h"
#include "src/compiler/turbofan-graph-visualizer.h"
#include "src/compiler/verifier.h"

namespace v8 {
namespace internal {
namespace compiler {

TFGraph::TFGraph(Zone* zone)
    : zone_(zone),
      start_(nullptr),
      end_(nullptr),
      mark_max_(0),
      next_node_id_(0),
      decorators_(zone),
      has_simd_(false),
      simd_stores_(zone) {}

void TFGraph::Decorate(Node* node) {
  for (GraphDecorator* const decorator : decorators_) {
    decorator->Decorate(node);
  }
}

void TFGraph::AddDecorator(GraphDecorator* decorator) {
  decorators_.push_back(decorator);
}

void TFGraph::RemoveDecorator(GraphDecorator* decorator) {
  auto const it = std::find(decorators_.begin(), decorators_.end(), decorator);
  DCHECK(it != decorators_.end());
  decorators_.erase(it);
}

Node* TFGraph::NewNode(const Operator* op, int input_count, Node* const* inputs,
                       bool incomplete) {
  Node* node = NewNodeUnchecked(op, input_count, inputs, incomplete);
  Verifier::VerifyNode(node);
  return node;
}

Node* TFGraph::NewNodeUnchecked(const Operator* op, int input_count,
                                Node* const* inputs, bool incomplete) {
  Node* const node =
      Node::New(zone(), NextNodeId(), op, input_count, inputs, incomplete);
  Decorate(node);
  return node;
}

Node* TFGraph::CloneNode(const Node* node) {
  DCHECK_NOT_NULL(node);
  Node* const clone = Node::Clone(zone(), NextNodeId(), node);
  Decorate(clone);
  return clone;
}

NodeId TFGraph::NextNodeId() {
  // A node's id is internally stored in a bit field using fewer bits than
  // NodeId (see Node::IdField). Hence the addition below won't ever overflow.
  DCHECK_LT(next_node_id_, std::numeric_limits<NodeId>::max());
  return next_node_id_++;
}

void TFGraph::Print() const { StdoutStream{} << AsRPO(*this); }

void TFGraph::RecordSimdStore(Node* store) { simd_stores_.push_back(store); }

ZoneVector<Node*> const& TFGraph::GetSimdStoreNodes() { return simd_stores_; }

}  // namespace compiler
}  // namespace internal
}  // namespace v8
