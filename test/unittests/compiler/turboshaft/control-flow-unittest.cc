// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/vector.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/branch-elimination-reducer.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/required-optimization-reducer.h"
#include "src/compiler/turboshaft/simplified-lowering-reducer.h"
#include "src/compiler/turboshaft/variable-reducer.h"
#include "test/unittests/compiler/turboshaft/reducer-test.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

class ControlFlowTest : public ReducerTest {};

// This test creates a chain of empty blocks linked by Gotos. CopyingPhase
// should automatically inline them, leading to the graph containing a single
// block after a single CopyingPhase.
TEST_F(ControlFlowTest, DefaultBlockInlining) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    OpIndex cond = Asm.GetParameter(0);
    for (int i = 0; i < 10000; i++) {
      Label<> l(&Asm);
      GOTO(l);
      BIND(l);
    }
    __ Return(cond);
  });

  test.Run<>();

  ASSERT_EQ(test.graph().block_count(), 1u);
}

// This test creates a fairly large graph, where a pattern similar to this is
// repeating:
//
//       B1        B2
//         \      /
//          \    /
//            Phi
//          Branch(Phi)
//          /     \
//         /       \
//        B3        B4
//
// BranchElimination should remove such branches by cloning the block with the
// branch. In the end, the graph should contain (almost) no branches anymore.
TEST_F(ControlFlowTest, BranchElimination) {
  static constexpr int kSize = 10000;

  auto test = CreateFromGraph(1, [](auto& Asm) {
    V<Word32> cond =
        __ TaggedEqual(Asm.GetParameter(0), __ SmiConstant(Smi::FromInt(0)));

    Block* end = __ NewBlock();
    V<Word32> cst1 = __ Word32Constant(42);
    std::vector<Block*> destinations;
    for (int i = 0; i < kSize; i++) destinations.push_back(__ NewBlock());
    ZoneVector<SwitchOp::Case>* cases =
        Asm.graph().graph_zone()->template New<ZoneVector<SwitchOp::Case>>(
            Asm.graph().graph_zone());
    for (int i = 0; i < kSize; i++) {
      cases->push_back({i, destinations[i], BranchHint::kNone});
    }
    __ Switch(cond, base::VectorOf(*cases), end);

    __ Bind(destinations[0]);
    Block* b = __ NewBlock();
    __ Branch(cond, b, end);
    __ Bind(b);

    for (int i = 1; i < kSize; i++) {
      V<Word32> cst2 = __ Word32Constant(1);
      __ Goto(destinations[i]);
      __ Bind(destinations[i]);
      V<Word32> phi = __ Phi({cst1, cst2}, RegisterRepresentation::Word32());
      Block* b1 = __ NewBlock();
      __ Branch(phi, b1, end);
      __ Bind(b1);
    }
    __ Goto(end);
    __ Bind(end);

    __ Return(cond);
  });

  // BranchElimination should remove all branches (except the first one), but
  // will not inline the destinations right away.
  test.Run<BranchEliminationReducer, MachineOptimizationReducer>();

  ASSERT_EQ(test.CountOp(Opcode::kBranch), 1u);

  // An empty phase will then inline the empty intermediate blocks.
  test.Run<>();

  // The graph should now contain 2 blocks per case (1 edge-split + 1 merge),
  // and a few blocks before and after (the switch and the return for
  // instance). To make this test a bit future proof, we just check that the
  // number of block is "number of cases * 2 + a few more blocks" rather than
  // computing the exact expected number of blocks.
  static constexpr int kMaxOtherBlocksCount = 10;
  ASSERT_LE(test.graph().block_count(),
            static_cast<size_t>(kSize * 2 + kMaxOtherBlocksCount));
}

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft
