// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_DEAD_CODE_ELIMINATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_DEAD_CODE_ELIMINATION_REDUCER_H_

#include <iomanip>

#include "src/common/globals.h"
#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8::internal::compiler::turboshaft {

// General overview
//
// DeadCodeAnalysis iterates the graph backwards to propagate liveness
// information. This information consists of the ControlState and the
// OperationState.
//
// OperationState reflects the liveness of operations. An operation is live if
//
//   1) The operation has the `is_required_when_unused` property
//   2) Any of its outputs is live (is used in a live operation).
//
// We introduce the concept of `weak live` which only differs from (strong)
// liveness on how it impacts the ControlState, but is otherwise identical. On
// operation is weak live if
//
//   Any of its outputs is weak live (is used in a weak live operation) and the
//   operation is not (strong) live.
//
// If the operation is neither strong nor weak live, the operation is dead and
// can be eliminated.
//
// ControlState describes to which block we could jump immediately without
// changing the program semantics. That is missing any side effects, required
// control flow or any strong(!) live operations. This information is then used
// at BranchOps to rewrite them to a GotoOp towards the corresponding block.
// Weak live operations thus are not eliminated but allow control flow to be
// rewritten around them. By marking stack checks (and all operations that they
// depend on) as weak live, this allows otherwise empty loops to be eliminated.
// From the output control state(s) c after an operation, the control state c'
// before the operation is computed as follows:
//
//                           | Bi               if ct, cf are Bi or Unreachable
//   c' = [Branch](ct, cf) = {
//                           | NotEliminatable  otherwise
//
// And if c' = Bi, then the BranchOp can be rewritten into GotoOp(Bi).
//
//                           | NotEliminatable  if Op is strong live
//            c' = [Op](c) = {
//                           | c                otherwise
//
//                           | Bk               if c = Bk
//       c' = [Merge i](c) = { Bi               if Merge i has no live phis
//                           | NotEliminatable  otherwise
//
// Where Merge is an imaginary operation at the start of every merge block. This
// is the important part for the analysis. If block `Merge i` does not have any
// strong live phi operations, then we don't necessarily need to distinguish the
// control flow paths going into that block and if we further don't encounter
// any (strong) live operations along any of the paths leading to `Merge i`
// starting at some BranchOp, we can skip both branches and eliminate the
// control flow entirely by rewriting the BranchOp into a GotoOp(Bi). Notice
// that if the control state already describes a potential Goto-target Bk, then
// we do not replace that in order to track the farthest block we can jump to.

struct ControlState {
  // Lattice:
  //
  //  NotEliminatable
  //     /  |  \
  //    B1 ... Bn
  //     \  |  /
  //    Unreachable
  //
  // We use ControlState to propagate information during the analysis about how
  // branches can be rewritten. Read the values like this:
  // - NotEliminatable: We cannot rewrite a branch, because we need the control
  // flow (e.g. because we have seen live operations on either branch or need
  // the phi at the merge).
  // - Bj: Control can be rewritten to go directly to Block Bj, because all
  // paths to that block are free of live operations.
  // - Unreachable: This is the bottom element and it represents that we haven't
  // seen anything live yet and are free to rewrite branches to any block
  // reachable from the current block.
  enum Kind {
    kUnreachable,
    kBlock,
    kNotEliminatable,
  };

  static ControlState NotEliminatable() {
    return ControlState{kNotEliminatable};
  }
  static ControlState Block(BlockIndex block) {
    return ControlState{kBlock, block};
  }
  static ControlState Unreachable() { return ControlState{kUnreachable}; }

  explicit ControlState(Kind kind, BlockIndex block = BlockIndex::Invalid())
      : kind(kind), block(block) {}

  static ControlState LeastUpperBound(const ControlState& lhs,
                                      const ControlState& rhs) {
    switch (lhs.kind) {
      case Kind::kUnreachable:
        return rhs;
      case Kind::kBlock: {
        if (rhs.kind == Kind::kUnreachable) return lhs;
        if (rhs.kind == Kind::kNotEliminatable) return rhs;
        if (lhs.block == rhs.block) return lhs;
        return NotEliminatable();
      }
      case Kind::kNotEliminatable:
        return lhs;
    }
  }

  Kind kind;
  BlockIndex block;
};

inline std::ostream& operator<<(std::ostream& stream,
                                const ControlState& state) {
  switch (state.kind) {
    case ControlState::kNotEliminatable:
      return stream << "NotEliminatable";
    case ControlState::kBlock:
      return stream << "Block(" << state.block << ")";
    case ControlState::kUnreachable:
      return stream << "Unreachable";
  }
}

inline bool operator==(const ControlState& lhs, const ControlState& rhs) {
  if (lhs.kind != rhs.kind) return false;
  if (lhs.kind == ControlState::kBlock) {
    DCHECK_EQ(rhs.kind, ControlState::kBlock);
    return lhs.block == rhs.block;
  }
  return true;
}

inline bool operator!=(const ControlState& lhs, const ControlState& rhs) {
  return !(lhs == rhs);
}

struct OperationState {
  // Lattice:
  //
  //   Live
  //    |
  // WeakLive
  //    |
  //   Dead
  //
  // Describes the liveness state of an operation. We use the notion of weak
  // liveness to express that an operation needs to be kept if we cannot
  // eliminate (jump over) the entire basic block. In other words: A weak live
  // operation will not be eliminated, but it doesn't prevent the propagation of
  // the control state to allow to jump over the block if it contains no
  // (strong) live operations. This will be useful to eliminate loops that are
  // kept alive only by the contained stack checks.
  enum Liveness : uint8_t {
    kDead,
    kWeakLive,
    kLive,
  };

  static Liveness LeastUpperBound(Liveness lhs, Liveness rhs) {
    static_assert(kLive > kWeakLive && kWeakLive > kDead);
    return std::max(lhs, rhs);
  }
};

inline std::ostream& operator<<(std::ostream& stream,
                                OperationState::Liveness liveness) {
  switch (liveness) {
    case OperationState::kDead:
      return stream << "Dead";
    case OperationState::kWeakLive:
      return stream << "WeakLive";
    case OperationState::kLive:
      return stream << "Live";
  }
  UNREACHABLE();
}

class DeadCodeAnalysis {
 public:
  explicit DeadCodeAnalysis(Graph& graph, Zone* phase_zone)
      : graph_(graph),
        liveness_(graph.op_id_count(), OperationState::kDead, phase_zone),
        entry_control_state_(graph.block_count(), ControlState::Unreachable(),
                             phase_zone),
        rewritable_branch_targets_(phase_zone) {}

  template <bool trace_analysis>
  std::pair<FixedSidetable<OperationState::Liveness>,
            ZoneMap<uint32_t, BlockIndex>>
  Run() {
    if constexpr (trace_analysis) {
      std::cout << "===== Running Dead Code Analysis =====\n";
    }
    for (uint32_t unprocessed_count = graph_.block_count();
         unprocessed_count > 0;) {
      BlockIndex block_index = static_cast<BlockIndex>(unprocessed_count - 1);
      --unprocessed_count;

      const Block& block = graph_.Get(block_index);
      ProcessBlock<trace_analysis>(block, &unprocessed_count);
    }

    if constexpr (trace_analysis) {
      std::cout << "===== Results =====\n== Operation State ==\n";
      for (Block b : graph_.blocks()) {
        std::cout << PrintAsBlockHeader{b} << ":\n";
        for (OpIndex index : graph_.OperationIndices(b)) {
          std::cout << " " << std::setw(8) << liveness_[index] << " "
                    << std::setw(3) << index.id() << ": " << graph_.Get(index)
                    << "\n";
        }
      }

      std::cout << "== Rewritable Branches ==\n";
      for (auto [branch_id, target] : rewritable_branch_targets_) {
        DCHECK(target.valid());
        std::cout << " " << std::setw(3) << branch_id << ": Branch ==> Goto "
                  << target.id() << "\n";
      }
      std::cout << "==========\n";
    }

    return {std::move(liveness_), std::move(rewritable_branch_targets_)};
  }

  template <bool trace_analysis>
  void ProcessBlock(const Block& block, uint32_t* unprocessed_count) {
    if constexpr (trace_analysis) {
      std::cout << "\n==========\n=== Processing " << PrintAsBlockHeader{block}
                << ":\n==========\nEXIT CONTROL STATE\n";
    }
    auto successors = SuccessorBlocks(block.LastOperation(graph_));
    ControlState control_state = ControlState::Unreachable();
    for (size_t i = 0; i < successors.size(); ++i) {
      const auto& r = entry_control_state_[successors[i]->index()];
      if constexpr (trace_analysis) {
        std::cout << " Successor " << successors[i]->index() << ": " << r
                  << "\n";
      }
      control_state = ControlState::LeastUpperBound(control_state, r);
    }
    if constexpr (trace_analysis)
      std::cout << "Combined: " << control_state << "\n";

    // If control_state == ControlState::Block(b), then the merge block b is
    // reachable through every path starting at the current block without any
    // live operations.

    if constexpr (trace_analysis) std::cout << "OPERATION STATE\n";
    auto op_range = graph_.OperationIndices(block);
    bool has_live_phis = false;
    for (auto it = op_range.end(); it != op_range.begin();) {
      --it;
      OpIndex index = *it;
      const Operation& op = graph_.Get(index);
      if constexpr (trace_analysis) std::cout << index << ":" << op << "\n";
      OperationState::Liveness op_state = liveness_[index];

      if (op.Is<BranchOp>()) {
        if (control_state != ControlState::NotEliminatable()) {
          // Branch is still dead.
          op_state = OperationState::kWeakLive;
          // If we know a target block we can rewrite into a goto.
          if (control_state.kind == ControlState::kBlock) {
            BlockIndex target = control_state.block;
            DCHECK(target.valid());
            rewritable_branch_targets_[index.id()] = target;
          }
        } else {
          // Branch is live. We cannot rewrite it.
          op_state = OperationState::kLive;
          auto it = rewritable_branch_targets_.find(index.id());
          if (it != rewritable_branch_targets_.end()) {
            rewritable_branch_targets_.erase(it);
          }
        }
      } else if (op.saturated_use_count == 0) {
        // Operation is already recognized as dead by a previous analysis.
        DCHECK_EQ(op_state, OperationState::kDead);
      } else if (op.Is<GotoOp>()) {
        // Gotos are WeakLive.
        op_state = OperationState::kWeakLive;
      } else if (op.Properties().is_required_when_unused) {
        op_state = OperationState::kLive;
      } else if (op.Is<PhiOp>()) {
        has_live_phis = has_live_phis || (op_state == OperationState::kLive);

        if (block.IsLoop()) {
          const PhiOp& phi = op.Cast<PhiOp>();
          // Check if the operation state of the input coming from the backedge
          // changes the liveness of the phi. In that case, trigger a revisit of
          // the loop.
          if (liveness_[phi.inputs()[PhiOp::kLoopPhiBackEdgeIndex]] <
              op_state) {
            if constexpr (trace_analysis) {
              std::cout
                  << "Operation state has changed. Need to revisit loop.\n";
            }
            Block* backedge = block.LastPredecessor();
            // Revisit the loop by increasing the {unprocessed_count} to include
            // all blocks of the loop.
            *unprocessed_count =
                std::max(*unprocessed_count, backedge->index().id() + 1);
          }
        }
      }

      // TODO(nicohartmann@): Handle Stack Guards to allow elimination of
      // otherwise empty loops.
      //
      // if(const CallOp* call = op.TryCast<CallOp>()) {
      //   if(std::string(call->descriptor->descriptor->debug_name())
      //     == "StackGuard") {
      //       DCHECK_EQ(op_state, OperationState::kLive);
      //       op_state = OperationState::kWeakLive;
      //     }
      // }

      DCHECK_LE(liveness_[index], op_state);
      // If everything is still dead. We don't need to update anything.
      if (op_state == OperationState::kDead) continue;

      // We have a (possibly weak) live operation.
      if constexpr (trace_analysis) {
        std::cout << " " << op_state << " <== " << liveness_[index] << "\n";
      }
      liveness_[index] = op_state;

      if constexpr (trace_analysis) {
        if (op.input_count > 0) std::cout << " Updating inputs:\n";
      }
      for (OpIndex input : op.inputs()) {
        auto old_input_state = liveness_[input];
        auto new_input_state =
            OperationState::LeastUpperBound(old_input_state, op_state);
        if constexpr (trace_analysis) {
          std::cout << "  " << input << ": " << new_input_state
                    << " <== " << old_input_state << " || " << op_state << "\n";
        }
        liveness_[input] = new_input_state;
      }

      if (op_state == OperationState::kLive &&
          control_state != ControlState::NotEliminatable()) {
        // This block has live operations, which means that we can't skip it.
        // Reset the ControlState to NotEliminatable.
        if constexpr (trace_analysis) {
          std::cout << "Block has live operations. New control state: "
                    << ControlState::NotEliminatable() << "\n";
        }
        control_state = ControlState::NotEliminatable();
      }
    }

    if constexpr (trace_analysis) {
      std::cout << "ENTRY CONTROL STATE\nAfter operations: " << control_state
                << "\n";
    }

    // If this block is a merge and we don't have any live phis, it is a
    // potential target for branch redirection.
    if (block.IsLoopOrMerge()) {
      if (!has_live_phis) {
        if (control_state.kind != ControlState::kBlock) {
          control_state = ControlState::Block(block.index());
          if constexpr (trace_analysis) {
            std::cout
                << "Block is loop or merge and has no live phi operations.\n";
          }
        } else if constexpr (trace_analysis) {
          std::cout << "Block is loop or merge and has no live phi "
                       "operations.\nControl state already has a goto block: "
                    << control_state << "\n";
        }
      }
      if (block.IsLoop() &&
          entry_control_state_[block.index()] != control_state) {
        if constexpr (trace_analysis) {
          std::cout << "Control state has changed. Need to revisit loop.\n";
        }
        Block* backedge = block.LastPredecessor();
        DCHECK_NOT_NULL(backedge);
        // Revisit the loop by increasing the {unprocessed_count} to include
        // all blocks of the loop.
        *unprocessed_count =
            std::max(*unprocessed_count, backedge->index().id() + 1);
      }
    }

    if constexpr (trace_analysis) {
      std::cout << "Final: " << control_state << "\n";
    }
    entry_control_state_[block.index()] = control_state;
  }

 private:
  Graph& graph_;
  FixedSidetable<OperationState::Liveness> liveness_;
  FixedBlockSidetable<ControlState> entry_control_state_;
  ZoneMap<uint32_t, BlockIndex> rewritable_branch_targets_;
};

template <class Next>
class DeadCodeEliminationReducer : public Next {
 public:
  using Next::Asm;

  template <class... Args>
  explicit DeadCodeEliminationReducer(const std::tuple<Args...>& args)
      : Next(args),
        branch_rewrite_targets_(Asm().phase_zone()),
        analyzer_(Asm().modifiable_input_graph(), Asm().phase_zone()) {}

  void Analyze() {
    // TODO(nicohartmann@): We might want to make this a flag.
    constexpr bool trace_analysis = false;
    std::tie(liveness_, branch_rewrite_targets_) =
        analyzer_.Run<trace_analysis>();
    Next::Analyze();
  }

  bool ShouldEliminateOperation(OpIndex index, const Operation& op) {
    DCHECK(!op.Is<BranchOp>());
    return (*liveness_)[index] == OperationState::kDead;
  }

  bool ShouldEliminateBranch(OpIndex index, const BranchOp& op,
                             BlockIndex& goto_target) {
    auto it = branch_rewrite_targets_.find(index.id());
    if (it == branch_rewrite_targets_.end()) return false;
    goto_target = it->second;
    return true;
  }

 private:
  base::Optional<FixedSidetable<OperationState::Liveness>> liveness_;
  ZoneMap<uint32_t, BlockIndex> branch_rewrite_targets_;
  DeadCodeAnalysis analyzer_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_DEAD_CODE_ELIMINATION_REDUCER_H_
