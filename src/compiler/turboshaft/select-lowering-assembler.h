// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_SELECT_LOWERING_ASSEMBLER_H_
#define V8_COMPILER_TURBOSHAFT_SELECT_LOWERING_ASSEMBLER_H_

#include "src/base/vector.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8::internal::compiler::turboshaft {

// Lowers Select operations to diamonds.
//
// A Select is conceptually somewhat similar to a ternary if:
//
//       res = Select(cond, val_true, val_false)
//
// means:
//
//       res = cond ? val_true : val_false
//
// SelectLoweringAssembler lowers such operations into:
//
//     if (cond) {
//         res = val_true
//     } else {
//         res = val_false
//     }

template <class Base>
class SelectLoweringAssembler
    : public AssemblerInterface<SelectLoweringAssembler<Base>, Base> {
 public:
  SelectLoweringAssembler(Graph* graph, Zone* phase_zone)
      : AssemblerInterface<SelectLoweringAssembler, Base>(graph, phase_zone) {}

  OpIndex Select(OpIndex cond, OpIndex vtrue, OpIndex vfalse,
                 RegisterRepresentation rep, BranchHint hint,
                 SelectOp::Implementation implem) {
    if (implem == SelectOp::Implementation::kCMove) {
      // We do not lower Select operations that should be implemented with
      // CMove.
      return Base::Select(cond, vtrue, vfalse, rep, hint, implem);
    }
    Block* true_block = this->NewBlock(Block::Kind::kBranchTarget);
    Block* false_block = this->NewBlock(Block::Kind::kBranchTarget);
    Block* merge_block = this->NewBlock(Block::Kind::kMerge);

    if (hint == BranchHint::kTrue) {
      false_block->SetDeferred(true);
    } else if (hint == BranchHint::kFalse) {
      true_block->SetDeferred(true);
    }

    this->Branch(cond, true_block, false_block);

    this->Bind(true_block);
    this->Goto(merge_block);

    this->Bind(false_block);
    this->Goto(merge_block);

    this->Bind(merge_block);
    return this->Phi(base::VectorOf({vtrue, vfalse}), rep);
  }
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_SELECT_LOWERING_ASSEMBLER_H_
