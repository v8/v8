// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_BRANCH_CONDITION_DUPLICATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_BRANCH_CONDITION_DUPLICATION_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/value-numbering-reducer.h"

namespace v8::internal::compiler::turboshaft {

// BranchConditionDuplication makes sure that the condition nodes of branches
// are used only once. When it finds a branch node whose condition has multiples
// uses, this condition is duplicated.
//
// Doing this enables the InstructionSelector to generate more efficient code
// for branches. For instance, consider this code:
//
//     c = a + b;
//     if (c == 0) { /* some code */ }
//     if (c == 0) { /* more code */ }
//
// Then the generated code will be something like (using registers "ra" for "a"
// and "rb" for "b", and "rt" a temporary register):
//
//     add ra, rb  ; a + b
//     cmp ra, 0   ; a + b == 0
//     sete rt     ; rt = (a + b == 0)
//     cmp rt, 0   ; rt == 0
//     jz
//     ...
//     cmp rt, 0   ; rt == 0
//     jz
//
// As you can see, TurboFan materialized the == bit into a temporary register.
// However, since the "add" instruction sets the ZF flag (on x64), it can be
// used to determine wether the jump should be taken or not. The code we'd like
// to generate instead if thus:
//
//     add ra, rb
//     jnz
//     ...
//     add ra, rb
//     jnz
//
// However, this requires to generate twice the instruction "add ra, rb". Due to
// how virtual registers are assigned in TurboFan (there is a map from node ID
// to virtual registers), both "add" instructions will use the same virtual
// register as output, which will break SSA.
//
// In order to overcome this issue, BranchConditionDuplicator duplicates branch
// conditions that are used more than once, so that they can be generated right
// before each branch without worrying about breaking SSA.

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class Next>
class BranchConditionDuplicationReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()

  OpIndex REDUCE_INPUT_GRAPH(Branch)(OpIndex ig_index, const BranchOp& branch) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphBranch(ig_index, branch);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    const Operation& cond = __ input_graph().Get(branch.condition());
    if (cond.saturated_use_count.IsOne()) goto no_change;

    OpIndex new_cond = OpIndex::Invalid();
    switch (cond.opcode) {
      case Opcode::kComparison:
        new_cond = MaybeDuplicateComparison(cond.Cast<ComparisonOp>(),
                                            branch.condition());
        break;
      case Opcode::kWordBinop:
        new_cond = MaybeDuplicateWordBinop(cond.Cast<WordBinopOp>(),
                                           branch.condition());
        break;
      case Opcode::kShift:
        new_cond =
            MaybeDuplicateShift(cond.Cast<ShiftOp>(), branch.condition());
        break;
      default:
        goto no_change;
    }

    if (!new_cond.valid()) goto no_change;
    __ Branch(new_cond, __ MapToNewGraph(branch.if_true),
              __ MapToNewGraph(branch.if_false), branch.hint);
    return OpIndex::Invalid();
  }

 private:
  bool MaybeCanDuplicateGenericBinop(OpIndex input_idx, OpIndex left,
                                     OpIndex right) {
    if (__ input_graph().Get(left).saturated_use_count.IsOne() &&
        __ input_graph().Get(right).saturated_use_count.IsOne()) {
      // We don't duplicate binops when all of their inputs are used a single
      // time (this would increase register pressure by keeping 2 values alive
      // instead of 1).
      return false;
    }
    OpIndex binop_output_idx = __ MapToNewGraph(input_idx);
    if (__ Get(binop_output_idx).saturated_use_count.IsZero()) {
      // This is the 1st use of {binop} in the output graph, so there is no need
      // to duplicate it just yet.
      return false;
    }
    return true;
  }

  OpIndex MaybeDuplicateWordBinop(const WordBinopOp& binop, OpIndex input_idx) {
    if (!MaybeCanDuplicateGenericBinop(input_idx, binop.left(),
                                       binop.right())) {
      return OpIndex::Invalid();
    }

    switch (binop.kind) {
      case WordBinopOp::Kind::kSignedDiv:
      case WordBinopOp::Kind::kUnsignedDiv:
      case WordBinopOp::Kind::kSignedMod:
      case WordBinopOp::Kind::kUnsignedMod:
        // These operations are somewhat expensive, and duplicating them is
        // probably not worth it.
        return OpIndex::Invalid();
      default:
        break;
    }

    DisableValueNumbering disable_gvn(this);
    return __ WordBinop(__ MapToNewGraph(binop.left()),
                        __ MapToNewGraph(binop.right()), binop.kind, binop.rep);
  }

  OpIndex MaybeDuplicateComparison(const ComparisonOp& comp,
                                   OpIndex input_idx) {
    if (!MaybeCanDuplicateGenericBinop(input_idx, comp.left(), comp.right())) {
      return OpIndex::Invalid();
    }

    DisableValueNumbering disable_gvn(this);
    return __ Comparison(__ MapToNewGraph(comp.left()),
                         __ MapToNewGraph(comp.right()), comp.kind, comp.rep);
  }

  OpIndex MaybeDuplicateShift(const ShiftOp& shift, OpIndex input_idx) {
    if (!MaybeCanDuplicateGenericBinop(input_idx, shift.left(),
                                       shift.right())) {
      return OpIndex::Invalid();
    }

    DisableValueNumbering disable_gvn(this);
    return __ Shift(__ MapToNewGraph(shift.left()),
                    __ MapToNewGraph(shift.right()), shift.kind, shift.rep);
  }
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_BRANCH_CONDITION_DUPLICATION_REDUCER_H_
