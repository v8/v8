// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/generic-node-inl.h"
#include "src/compiler/node-matchers.h"

namespace v8 {
namespace internal {
namespace compiler {

const int ScaleFactorMatcher::kMatchedFactors[] = {1, 2, 4, 8};


ScaleFactorMatcher::ScaleFactorMatcher(Node* node)
    : NodeMatcher(node), left_(NULL), power_(0) {
  if (opcode() != IrOpcode::kInt32Mul) return;
  // TODO(dcarney): should test 64 bit ints as well.
  Int32BinopMatcher m(this->node());
  if (!m.right().HasValue()) return;
  int32_t value = m.right().Value();
  switch (value) {
    case 8:
      power_++;  // Fall through.
    case 4:
      power_++;  // Fall through.
    case 2:
      power_++;  // Fall through.
    case 1:
      break;
    default:
      return;
  }
  left_ = m.left().node();
}


IndexAndDisplacementMatcher::IndexAndDisplacementMatcher(Node* node)
    : NodeMatcher(node), index_node_(node), displacement_(0), power_(0) {
  if (opcode() == IrOpcode::kInt32Add) {
    Int32BinopMatcher m(this->node());
    if (m.right().HasValue()) {
      displacement_ = m.right().Value();
      index_node_ = m.left().node();
    }
  }
  // Test scale factor.
  ScaleFactorMatcher scale_matcher(index_node_);
  if (scale_matcher.Matches()) {
    index_node_ = scale_matcher.Left();
    power_ = scale_matcher.Power();
  }
}


const int LeaMultiplyMatcher::kMatchedFactors[7] = {1, 2, 3, 4, 5, 8, 9};


LeaMultiplyMatcher::LeaMultiplyMatcher(Node* node)
    : NodeMatcher(node), left_(NULL), power_(0), displacement_(0) {
  if (opcode() != IrOpcode::kInt32Mul && opcode() != IrOpcode::kInt64Mul) {
    return;
  }
  int64_t value;
  Node* left = NULL;
  {
    Int32BinopMatcher m(this->node());
    if (m.right().HasValue()) {
      value = m.right().Value();
      left = m.left().node();
    } else {
      Int64BinopMatcher m(this->node());
      if (m.right().HasValue()) {
        value = m.right().Value();
        left = m.left().node();
      } else {
        return;
      }
    }
  }
  switch (value) {
    case 9:
    case 8:
      power_++;  // Fall through.
    case 5:
    case 4:
      power_++;  // Fall through.
    case 3:
    case 2:
      power_++;  // Fall through.
    case 1:
      break;
    default:
      return;
  }
  if (!base::bits::IsPowerOfTwo64(value)) {
    displacement_ = 1;
  }
  left_ = left;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
