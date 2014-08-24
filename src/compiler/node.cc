// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/node.h"

#include "src/compiler/generic-node-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

void Node::CollectProjections(NodeVector* projections) {
  for (UseIter i = uses().begin(); i != uses().end(); ++i) {
    if ((*i)->opcode() != IrOpcode::kProjection) continue;
    DCHECK_GE(OpParameter<int32_t>(*i), 0);
    projections->push_back(*i);
  }
}


Node* Node::FindProjection(int32_t projection_index) {
  for (UseIter i = uses().begin(); i != uses().end(); ++i) {
    if ((*i)->opcode() == IrOpcode::kProjection &&
        OpParameter<int32_t>(*i) == projection_index) {
      return *i;
    }
  }
  return NULL;
}


OStream& operator<<(OStream& os, const Operator& op) { return op.PrintTo(os); }


OStream& operator<<(OStream& os, const Node& n) {
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
