// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_ADAPTER_H_
#define V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_ADAPTER_H_

#include "src/compiler/backend/instruction.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/schedule.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"

// TODO(nicohartmann@):
// During the transition period to a generic instruction selector, some
// instantiations with TurboshaftAdapter will still call functions with
// Node* arguments. Use `DECLARE_UNREACHABLE_TURBOSHAFT_FALLBACK` to define
// a temporary fallback for these functions such that compilation is possible
// while transitioning the instruction selector incrementally. Once all uses
// of Node*, BasicBlock*, ... have been replaced, remove those fallbacks.
#define DECLARE_UNREACHABLE_TURBOSHAFT_FALLBACK(ret, name)             \
  template <typename... Args>                                          \
  std::enable_if_t<Adapter::IsTurboshaft &&                            \
                       detail::AnyTurbofanNodeOrBlock<Args...>::value, \
                   ret>                                                \
  name(Args...) {                                                      \
    UNREACHABLE();                                                     \
  }

namespace v8::internal::compiler {
namespace detail {
template <typename...>
struct AnyTurbofanNodeOrBlock;
template <typename Head, typename... Tail>
struct AnyTurbofanNodeOrBlock<Head, Tail...> {
  static constexpr bool value = std::is_same_v<Head, Node*> ||
                                std::is_same_v<Head, BasicBlock*> ||
                                AnyTurbofanNodeOrBlock<Tail...>::value;
};
template <>
struct AnyTurbofanNodeOrBlock<> {
  static constexpr bool value = false;
};
}  // namespace detail

struct TurbofanAdapter {
  static constexpr bool IsTurbofan = true;
  static constexpr bool IsTurboshaft = false;
  using schedule_t = Schedule*;
  using block_t = BasicBlock*;
  using block_range_t = ZoneVector<block_t>;
  using node_t = Node*;
  using inputs_t = Node::Inputs;
  using opcode_t = IrOpcode::Value;
  using id_t = uint32_t;

  class CallView {
   public:
    explicit CallView(node_t node) : node_(node) {
      DCHECK(node_->opcode() == IrOpcode::kCall ||
             node_->opcode() == IrOpcode::kTailCall);
    }

    int return_count() const { return node_->op()->ValueOutputCount(); }
    node_t callee() const { return node_->InputAt(0); }
    node_t frame_state() const {
      return node_->InputAt(static_cast<int>(call_descriptor()->InputCount()));
    }
    base::Vector<node_t> arguments() const {
      base::Vector<node_t> inputs = node_->inputs_vector();
      return inputs.SubVector(1, inputs.size());
    }
    const CallDescriptor* call_descriptor() const {
      return CallDescriptorOf(node_->op());
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
  };

  class BranchView {
   public:
    explicit BranchView(node_t node) : node_(node) {
      DCHECK_EQ(node_->opcode(), IrOpcode::kBranch);
    }

    node_t condition() const { return node_->InputAt(0); }

    operator node_t() const { return node_; }

   private:
    node_t node_;
  };

  class WordBinopView {
   public:
    explicit WordBinopView(node_t node) : node_(node), m_(node) {}

    void EnsureConstantIsRightIfCommutative() {
      // Nothing to do. Matcher already ensures that.
    }

    node_t left() const { return m_.left().node(); }
    node_t right() const { return m_.right().node(); }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    Int32BinopMatcher m_;
  };

  class LoadView {
   public:
    explicit LoadView(node_t node) : node_(node) {
      DCHECK(node_->opcode() == IrOpcode::kLoad ||
             node_->opcode() == IrOpcode::kLoadImmutable ||
             node_->opcode() == IrOpcode::kProtectedLoad ||
             node_->opcode() == IrOpcode::kLoadTrapOnNull);
    }

    LoadRepresentation loaded_rep() const {
      return LoadRepresentationOf(node_->op());
    }

    node_t base() const { return node_->InputAt(0); }
    node_t index() const { return node_->InputAt(1); }

    operator node_t() const { return node_; }

   private:
    node_t node_;
  };

  CallView call_view(node_t node) { return CallView{node}; }
  BranchView branch_view(node_t node) { return BranchView(node); }
  WordBinopView word_binop_view(node_t node) { return WordBinopView(node); }
  LoadView load_view(node_t node) { return LoadView(node); }

  void InitializeAdapter(schedule_t) {}

  block_t block(schedule_t schedule, node_t node) const {
    return schedule->block(node);
  }

  RpoNumber rpo_number(block_t block) const {
    return RpoNumber::FromInt(block->rpo_number());
  }

  const block_range_t& rpo_order(schedule_t schedule) const {
    return *schedule->rpo_order();
  }

  bool IsLoopHeader(block_t block) const { return block->IsLoopHeader(); }

  size_t PredecessorCount(block_t block) const {
    return block->PredecessorCount();
  }
  block_t PredecessorAt(block_t block, size_t index) const {
    return block->PredecessorAt(index);
  }

  base::iterator_range<NodeVector::iterator> nodes(block_t block) {
    return {block->begin(), block->end()};
  }

  bool IsPhi(node_t node) const { return node->opcode() == IrOpcode::kPhi; }
  bool IsRetain(node_t node) const {
    return node->opcode() == IrOpcode::kRetain;
  }
  bool IsHeapConstant(node_t node) const {
    return node->opcode() == IrOpcode::kHeapConstant;
  }
  bool IsExternalConstant(node_t node) const {
    return node->opcode() == IrOpcode::kExternalConstant;
  }
  bool IsRelocatableWasmConstant(node_t node) const {
    return node->opcode() == IrOpcode::kRelocatableInt32Constant ||
           node->opcode() == IrOpcode::kRelocatableInt64Constant;
  }
  bool IsLoadOrLoadImmutable(node_t node) const {
    return node->opcode() == IrOpcode::kLoad ||
           node->opcode() == IrOpcode::kLoadImmutable;
  }

  int value_input_count(node_t node) const {
    return node->op()->ValueInputCount();
  }
  node_t input_at(node_t node, size_t index) const {
    return node->InputAt(static_cast<int>(index));
  }
  inputs_t inputs(node_t node) const { return node->inputs(); }
  opcode_t opcode(node_t node) const { return node->opcode(); }
  bool is_exclusive_user_of(node_t user, node_t value) const {
    for (Edge const edge : value->use_edges()) {
      if (edge.from() != user && NodeProperties::IsValueEdge(edge)) {
        return false;
      }
    }
    return true;
  }

  id_t id(node_t node) const { return node->id(); }
  static bool valid(node_t node) { return node != nullptr; }

  node_t block_terminator(block_t block) const {
    return block->control_input();
  }
  node_t parent_frame_state(node_t node) const {
    DCHECK_EQ(node->opcode(), IrOpcode::kFrameState);
    DCHECK_EQ(FrameState{node}.outer_frame_state(),
              NodeProperties::GetFrameStateInput(node));
    return NodeProperties::GetFrameStateInput(node);
  }
  int parameter_index_of(node_t node) const {
    DCHECK(node->opcode() == IrOpcode::kParameter);
    return ParameterIndexOf(node->op());
  }

  bool IsRequiredWhenUnused(node_t node) const {
    return !node->op()->HasProperty(Operator::kEliminatable);
  }
  bool IsCommutative(node_t node) const {
    return node->op()->HasProperty(Operator::kCommutative);
  }
};

struct TurboshaftAdapter {
  static constexpr bool IsTurbofan = false;
  static constexpr bool IsTurboshaft = true;
  // TODO(nicohartmann@): Rename schedule_t once Turbofan is gone.
  using schedule_t = turboshaft::Graph*;
  using block_t = turboshaft::Block*;
  using block_range_t = ZoneVector<block_t>;
  using node_t = turboshaft::OpIndex;
  using inputs_t = base::Vector<const node_t>;
  using opcode_t = turboshaft::Opcode;
  using id_t = uint32_t;

  class CallView {
   public:
    explicit CallView(turboshaft::Graph* graph, node_t node) : node_(node) {
      if (graph->Get(node_).Is<turboshaft::TailCallOp>()) {
        UNIMPLEMENTED();
      }
      op_ = &graph->Get(node_).Cast<turboshaft::CallOp>();
    }

    int return_count() const {
      return static_cast<int>(op_->outputs_rep().size());
    }
    node_t callee() const { return op_->callee(); }
    node_t frame_state() const { return op_->frame_state(); }
    base::Vector<const node_t> arguments() const { return op_->arguments(); }
    const CallDescriptor* call_descriptor() const {
      return op_->descriptor->descriptor;
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    const turboshaft::CallOp* op_;
  };

  class BranchView {
   public:
    explicit BranchView(turboshaft::Graph* graph, node_t node) : node_(node) {
      op_ = &graph->Get(node_).Cast<turboshaft::BranchOp>();
    }

    node_t condition() const { return op_->condition(); }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    const turboshaft::BranchOp* op_;
  };

  class WordBinopView {
   public:
    explicit WordBinopView(turboshaft::Graph* graph, node_t node)
        : node_(node) {
      op_ = &graph->Get(node_).Cast<turboshaft::WordBinopOp>();
      left_ = op_->left();
      right_ = op_->right();
      can_put_constant_right_ =
          op_->IsCommutative(op_->kind) &&
          graph->Get(left_).Is<turboshaft::ConstantOp>() &&
          !graph->Get(right_).Is<turboshaft::ConstantOp>();
    }

    void EnsureConstantIsRightIfCommutative() {
      if (can_put_constant_right_) {
        std::swap(left_, right_);
        can_put_constant_right_ = false;
      }
    }

    node_t left() const { return left_; }
    node_t right() const { return right_; }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    const turboshaft::WordBinopOp* op_;
    node_t left_;
    node_t right_;
    bool can_put_constant_right_;
  };

  class LoadView {
   public:
    explicit LoadView(turboshaft::Graph* graph, node_t node) : node_(node) {
      op_ = &graph->Get(node_).Cast<turboshaft::LoadOp>();
    }

    LoadRepresentation loaded_rep() const {
      return op_->loaded_rep.ToMachineType();
    }

    node_t base() const { return op_->base(); }
    node_t index() const { return op_->index(); }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    const turboshaft::LoadOp* op_;
  };

  CallView call_view(node_t node) { return CallView{graph_, node}; }
  BranchView branch_view(node_t node) { return BranchView(graph_, node); }
  WordBinopView word_binop_view(node_t node) {
    return WordBinopView(graph_, node);
  }
  LoadView load_view(node_t node) { return LoadView(graph_, node); }

  void InitializeAdapter(schedule_t schedule) { graph_ = schedule; }
  turboshaft::Graph* turboshaft_graph() const { return graph_; }

  block_t block(schedule_t schedule, node_t node) const {
    // TODO(nicohartmann@): This might be too slow and we should consider
    // precomputing.
    return &schedule->Get(schedule->BlockOf(node));
  }

  RpoNumber rpo_number(block_t block) const {
    return RpoNumber::FromInt(block->index().id());
  }

  const block_range_t& rpo_order(schedule_t schedule) {
    return schedule->blocks_vector();
  }

  bool IsLoopHeader(block_t block) const { return block->IsLoop(); }

  size_t PredecessorCount(block_t block) const {
    return block->PredecessorCount();
  }
  block_t PredecessorAt(block_t block, size_t index) const {
    return block->Predecessors()[index];
  }

  base::iterator_range<turboshaft::Graph::OpIndexIterator> nodes(
      block_t block) {
    return graph_->OperationIndices(*block);
  }

  bool IsPhi(node_t node) const {
    return graph_->Get(node).Is<turboshaft::PhiOp>();
  }
  bool IsRetain(node_t node) const {
    return graph_->Get(node).Is<turboshaft::RetainOp>();
  }
  bool IsHeapConstant(node_t node) const {
    turboshaft::ConstantOp* constant =
        graph_->Get(node).TryCast<turboshaft::ConstantOp>();
    if (constant == nullptr) return false;
    return constant->kind == turboshaft::ConstantOp::Kind::kHeapObject;
  }
  bool IsExternalConstant(node_t node) const {
    turboshaft::ConstantOp* constant =
        graph_->Get(node).TryCast<turboshaft::ConstantOp>();
    if (constant == nullptr) return false;
    return constant->kind == turboshaft::ConstantOp::Kind::kExternal;
  }
  bool IsRelocatableWasmConstant(node_t node) const {
    turboshaft::ConstantOp* constant =
        graph_->Get(node).TryCast<turboshaft::ConstantOp>();
    if (constant == nullptr) return false;
    return constant->kind ==
           turboshaft::any_of(
               turboshaft::ConstantOp::Kind::kRelocatableWasmCall,
               turboshaft::ConstantOp::Kind::kRelocatableWasmStubCall);
  }
  bool IsLoadOrLoadImmutable(node_t node) const {
    return graph_->Get(node).opcode == turboshaft::Opcode::kLoad;
  }

  int value_input_count(node_t node) const {
    return graph_->Get(node).input_count;
  }
  node_t input_at(node_t node, size_t index) const {
    return graph_->Get(node).input(index);
  }
  inputs_t inputs(node_t node) const { return graph_->Get(node).inputs(); }
  opcode_t opcode(node_t node) const { return graph_->Get(node).opcode; }
  bool is_exclusive_user_of(node_t user, node_t value) const {
    DCHECK(valid(user));
    DCHECK(valid(value));
    const size_t use_count = base::count_if(
        graph_->Get(user).inputs(),
        [value](turboshaft::OpIndex input) { return input == value; });
    DCHECK_LT(0, use_count);
    DCHECK_LE(use_count, graph_->Get(value).saturated_use_count.Get());
    const turboshaft::Operation& value_op = graph_->Get(value);
    return (value_op.saturated_use_count.Get() == use_count) &&
           !value_op.saturated_use_count.IsSaturated();
  }

  id_t id(node_t node) const { return node.id(); }
  static bool valid(node_t node) { return node.valid(); }

  node_t block_terminator(block_t block) const {
    return graph_->PreviousIndex(block->end());
  }
  node_t parent_frame_state(node_t node) const {
    const turboshaft::FrameStateOp& frame_state =
        graph_->Get(node).Cast<turboshaft::FrameStateOp>();
    return frame_state.parent_frame_state();
  }
  int parameter_index_of(node_t node) const {
    const turboshaft::ParameterOp& parameter =
        graph_->Get(node).Cast<turboshaft::ParameterOp>();
    return parameter.parameter_index;
  }

  bool IsRequiredWhenUnused(node_t node) const {
    return graph_->Get(node).IsRequiredWhenUnused();
  }
  bool IsCommutative(node_t node) const {
    const turboshaft::Operation& op = graph_->Get(node);
    if (const auto binop = op.TryCast<turboshaft::WordBinopOp>()) {
      return turboshaft::WordBinopOp::IsCommutative(binop->kind);
    } else if (const auto binop =
                   op.TryCast<turboshaft::OverflowCheckedBinopOp>()) {
      return turboshaft::OverflowCheckedBinopOp::IsCommutative(binop->kind);
    } else if (const auto binop = op.TryCast<turboshaft::FloatBinopOp>()) {
      return turboshaft::FloatBinopOp::IsCommutative(binop->kind);
    } else if (op.Is<turboshaft::EqualOp>()) {
      return turboshaft::EqualOp::IsCommutative();
    }
    return false;
  }

 private:
  turboshaft::Graph* graph_;
};

}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_ADAPTER_H_
