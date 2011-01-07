// Copyright 2011 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "lithium.h"

namespace v8 {
namespace internal {


class LGapNode: public ZoneObject {
 public:
  explicit LGapNode(LOperand* operand)
      : operand_(operand), resolved_(false), visited_id_(-1) { }

  LOperand* operand() const { return operand_; }
  bool IsResolved() const { return !IsAssigned() || resolved_; }
  void MarkResolved() {
    ASSERT(!IsResolved());
    resolved_ = true;
  }
  int visited_id() const { return visited_id_; }
  void set_visited_id(int id) {
    ASSERT(id > visited_id_);
    visited_id_ = id;
  }

  bool IsAssigned() const { return assigned_from_.is_set(); }
  LGapNode* assigned_from() const { return assigned_from_.get(); }
  void set_assigned_from(LGapNode* n) { assigned_from_.set(n); }

 private:
  LOperand* operand_;
  SetOncePointer<LGapNode> assigned_from_;
  bool resolved_;
  int visited_id_;
};


LGapResolver::LGapResolver(const ZoneList<LMoveOperands>* moves,
                           LOperand* marker_operand)
    : nodes_(4),
      identified_cycles_(4),
      result_(4),
      marker_operand_(marker_operand),
      next_visited_id_(0) {
  for (int i = 0; i < moves->length(); ++i) {
    LMoveOperands move = moves->at(i);
    if (!move.IsRedundant()) RegisterMove(move);
  }
}


const ZoneList<LMoveOperands>* LGapResolver::ResolveInReverseOrder() {
  for (int i = 0; i < identified_cycles_.length(); ++i) {
    ResolveCycle(identified_cycles_[i]);
  }

  int unresolved_nodes;
  do {
    unresolved_nodes = 0;
    for (int j = 0; j < nodes_.length(); j++) {
      LGapNode* node = nodes_[j];
      if (!node->IsResolved() && node->assigned_from()->IsResolved()) {
        AddResultMove(node->assigned_from(), node);
        node->MarkResolved();
      }
      if (!node->IsResolved()) ++unresolved_nodes;
    }
  } while (unresolved_nodes > 0);
  return &result_;
}


void LGapResolver::AddResultMove(LGapNode* from, LGapNode* to) {
  AddResultMove(from->operand(), to->operand());
}


void LGapResolver::AddResultMove(LOperand* from, LOperand* to) {
  result_.Add(LMoveOperands(from, to));
}


void LGapResolver::ResolveCycle(LGapNode* start) {
  ZoneList<LOperand*> circle_operands(8);
  circle_operands.Add(marker_operand_);
  LGapNode* cur = start;
  do {
    cur->MarkResolved();
    circle_operands.Add(cur->operand());
    cur = cur->assigned_from();
  } while (cur != start);
  circle_operands.Add(marker_operand_);

  for (int i = circle_operands.length() - 1; i > 0; --i) {
    LOperand* from = circle_operands[i];
    LOperand* to = circle_operands[i - 1];
    AddResultMove(from, to);
  }
}


bool LGapResolver::CanReach(LGapNode* a, LGapNode* b, int visited_id) {
  ASSERT(a != b);
  LGapNode* cur = a;
  while (cur != b && cur->visited_id() != visited_id && cur->IsAssigned()) {
    cur->set_visited_id(visited_id);
    cur = cur->assigned_from();
  }

  return cur == b;
}


bool LGapResolver::CanReach(LGapNode* a, LGapNode* b) {
  ASSERT(a != b);
  return CanReach(a, b, next_visited_id_++);
}


void LGapResolver::RegisterMove(LMoveOperands move) {
  if (move.from()->IsConstantOperand()) {
    // Constant moves should be last in the machine code. Therefore add them
    // first to the result set.
    AddResultMove(move.from(), move.to());
  } else {
    LGapNode* from = LookupNode(move.from());
    LGapNode* to = LookupNode(move.to());
    if (to->IsAssigned() && to->assigned_from() == from) {
      move.Eliminate();
      return;
    }
    ASSERT(!to->IsAssigned());
    if (CanReach(from, to)) {
      // This introduces a circle. Save.
      identified_cycles_.Add(from);
    }
    to->set_assigned_from(from);
  }
}


LGapNode* LGapResolver::LookupNode(LOperand* operand) {
  for (int i = 0; i < nodes_.length(); ++i) {
    if (nodes_[i]->operand()->Equals(operand)) return nodes_[i];
  }

  // No node found => create a new one.
  LGapNode* result = new LGapNode(operand);
  nodes_.Add(result);
  return result;
}


} }  // namespace v8::internal
