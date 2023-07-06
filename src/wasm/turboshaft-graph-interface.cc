// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/turboshaft-graph-interface.h"

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/wasm-compiler-definitions.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-opcodes-inl.h"

namespace v8::internal::wasm {

using Assembler =
    compiler::turboshaft::Assembler<compiler::turboshaft::reducer_list<>>;
using compiler::turboshaft::Graph;
using compiler::turboshaft::OpIndex;
using compiler::turboshaft::PendingLoopPhiOp;
using compiler::turboshaft::RegisterRepresentation;
using TSBlock = compiler::turboshaft::Block;

class TurboshaftGraphBuildingInterface {
 public:
  using ValidationTag = Decoder::FullValidationTag;
  using FullDecoder =
      WasmFullDecoder<ValidationTag, TurboshaftGraphBuildingInterface>;
  static constexpr bool kUsesPoppedArgs = true;

  struct Value : public ValueBase<ValidationTag> {
    OpIndex op = OpIndex::Invalid();
    template <typename... Args>
    explicit Value(Args&&... args) V8_NOEXCEPT
        : ValueBase(std::forward<Args>(args)...) {}
  };

  struct Control : public ControlBase<Value, ValidationTag> {
    TSBlock* false_block = nullptr;  // Only for 'if'.
    TSBlock* merge_block = nullptr;
    TSBlock* loop_block = nullptr;  // Only for loops.

    template <typename... Args>
    explicit Control(Args&&... args) V8_NOEXCEPT
        : ControlBase(std::forward<Args>(args)...) {}
  };

  explicit TurboshaftGraphBuildingInterface(Graph& graph, Zone* zone)
      : asm_(graph, graph, zone, nullptr) {}

  void StartFunction(FullDecoder* decoder) {
    TSBlock* block = asm_.NewBlock();
    asm_.Bind(block);
    ssa_env_.resize(decoder->num_locals());
    uint32_t index = 0;
    for (; index < decoder->sig_->parameter_count(); index++) {
      // Parameter indices are shifted by 1 because parameter 0 is the instance.
      ssa_env_[index] = asm_.Parameter(
          index + 1,
          RepresentationFor(decoder, decoder->sig_->GetParam(index)));
    }
    while (index < decoder->num_locals()) {
      ValueType type = decoder->local_type(index);
      if (!type.is_defaultable()) {
        BailoutWithoutOpcode(decoder, "non-defaultable local");
        return;
      }
      OpIndex op = DefaultValue(decoder, type);
      while (index < decoder->num_locals() &&
             decoder->local_type(index) == type) {
        ssa_env_[index++] = op;
      }
    }
  }

  void StartFunctionBody(FullDecoder* decoder, Control* block) {}

  void FinishFunction(FullDecoder* decoder) {}

  void OnFirstError(FullDecoder*) {}

  void NextInstruction(FullDecoder*, WasmOpcode) {}

  // ******** Control Flow ********
  // The basic structure of control flow is {block_phis_}. It contains a mapping
  // from blocks to phi inputs corresponding to the SSA values plus the stack
  // merge values at the beginning of the block.
  // - When we create a new block (to be bound in the future), we register it to
  //   {block_phis_} with {NewBlock}.
  // - When we encounter an jump to a block, we invoke {SetupControlFlowEdge}.
  // - Finally, when we bind a block, we setup its phis, the SSA environment,
  //   and its merge values, with {EnterBlock}.
  // - When we create a loop, we generate PendingLoopPhis for the SSA state and
  //   the incoming stack values. We also create a block which will act as a
  //   merge block for all loop backedges (since a loop in Turboshaft can only
  //   have one backedge). When we PopControl a loop, we enter the merge block
  //   to create its Phis for all backedges as necessary, and use those values
  //   to patch the backedge of the PendingLoopPhis of the loop.

  void Block(FullDecoder* decoder, Control* block) {
    block->merge_block = NewBlock(decoder, block->br_merge());
  }

  void Loop(FullDecoder* decoder, Control* block) {
    TSBlock* loop = asm_.NewLoopHeader();
    asm_.Goto(loop);
    asm_.Bind(loop);
    for (uint32_t i = 0; i < decoder->num_locals(); i++) {
      OpIndex phi = asm_.PendingLoopPhi(
          ssa_env_[i], PendingLoopPhiOp::Kind::kFromSeaOfNodes,
          RepresentationFor(decoder, decoder->local_type(i)),
          PendingLoopPhiOp::Data{
              PendingLoopPhiOp::PhiIndex{static_cast<int>(i)}});
      ssa_env_[i] = phi;
    }
    uint32_t arity = block->start_merge.arity;
    Value* stack_base = arity > 0 ? decoder->stack_value(arity) : nullptr;
    for (uint32_t i = 0; i < arity; i++) {
      OpIndex phi = asm_.PendingLoopPhi(
          stack_base[i].op, PendingLoopPhiOp::Kind::kFromSeaOfNodes,
          RepresentationFor(decoder, stack_base[i].type),
          PendingLoopPhiOp::Data{PendingLoopPhiOp::PhiIndex{
              static_cast<int>(decoder->num_locals() + i)}});
      block->start_merge[i].op = phi;
    }

    TSBlock* loop_merge = NewBlock(decoder, &block->start_merge);
    block->merge_block = loop_merge;
    block->loop_block = loop;
  }

  void If(FullDecoder* decoder, const Value& cond, Control* if_block) {
    TSBlock* true_block = NewBlock(decoder, nullptr);
    TSBlock* false_block = NewBlock(decoder, nullptr);
    TSBlock* merge_block = NewBlock(decoder, &if_block->end_merge);
    if_block->false_block = false_block;
    if_block->merge_block = merge_block;
    // TODO(14108): Branch hints.
    asm_.Branch(compiler::turboshaft::ConditionWithHint(cond.op), true_block,
                false_block);
    SetupControlFlowEdge(decoder, true_block);
    SetupControlFlowEdge(decoder, false_block);
    EnterBlock(decoder, true_block, nullptr);
  }

  void Else(FullDecoder* decoder, Control* if_block) {
    if (if_block->reachable()) {
      SetupControlFlowEdge(decoder, if_block->merge_block);
      asm_.Goto(if_block->merge_block);
    }
    EnterBlock(decoder, if_block->false_block, nullptr);
  }

  void BrOrRet(FullDecoder* decoder, uint32_t depth, uint32_t drop_values) {
    if (depth == decoder->control_depth() - 1) {
      DoReturn(decoder, drop_values);
    } else {
      Control* target = decoder->control_at(depth);
      SetupControlFlowEdge(decoder, target->merge_block);
      asm_.Goto(target->merge_block);
    }
  }

  void BrIf(FullDecoder* decoder, const Value& cond, uint32_t depth) {
    if (depth == decoder->control_depth() - 1) {
      TSBlock* return_block = NewBlock(decoder, nullptr);
      SetupControlFlowEdge(decoder, return_block);
      TSBlock* non_branching = NewBlock(decoder, nullptr);
      SetupControlFlowEdge(decoder, non_branching);
      asm_.Branch(compiler::turboshaft::ConditionWithHint(cond.op),
                  return_block, non_branching);
      EnterBlock(decoder, return_block, nullptr);
      DoReturn(decoder, 0);
      EnterBlock(decoder, non_branching, nullptr);
    } else {
      Control* target = decoder->control_at(depth);
      SetupControlFlowEdge(decoder, target->merge_block);
      TSBlock* non_branching = NewBlock(decoder, nullptr);
      SetupControlFlowEdge(decoder, non_branching);
      asm_.Branch(compiler::turboshaft::ConditionWithHint(cond.op),
                  target->merge_block, non_branching);
      EnterBlock(decoder, non_branching, nullptr);
    }
  }

  void BrTable(FullDecoder* decoder, const BranchTableImmediate& imm,
               const Value& key) {
    compiler::turboshaft::SwitchOp::Case* cases =
        asm_.output_graph()
            .graph_zone()
            ->NewArray<compiler::turboshaft::SwitchOp::Case>(imm.table_count);
    BranchTableIterator<ValidationTag> new_block_iterator(decoder, imm);
    std::vector<TSBlock*> intermediate_blocks;
    TSBlock* default_case = nullptr;
    while (new_block_iterator.has_next()) {
      TSBlock* intermediate = NewBlock(decoder, nullptr);
      SetupControlFlowEdge(decoder, intermediate);
      intermediate_blocks.emplace_back(intermediate);
      uint32_t i = new_block_iterator.cur_index();
      if (i == imm.table_count) {
        default_case = intermediate;
      } else {
        cases[i] = {static_cast<int>(i), intermediate, BranchHint::kNone};
      }
      new_block_iterator.next();
    }
    DCHECK_NOT_NULL(default_case);
    asm_.Switch(key.op, base::VectorOf(cases, imm.table_count), default_case);

    int i = 0;
    BranchTableIterator<ValidationTag> branch_iterator(decoder, imm);
    while (branch_iterator.has_next()) {
      TSBlock* intermediate = intermediate_blocks[i];
      i++;
      EnterBlock(decoder, intermediate, nullptr);
      BrOrRet(decoder, branch_iterator.next(), 0);
    }
  }

  void FallThruTo(FullDecoder* decoder, Control* block) { Bailout(decoder); }

  void PopControl(FullDecoder* decoder, Control* block) {
    switch (block->kind) {
      case kControlIf:
        if (block->reachable()) {
          SetupControlFlowEdge(decoder, block->merge_block);
          asm_.Goto(block->merge_block);
        }
        EnterBlock(decoder, block->false_block, nullptr);
        // Exceptionally for one-armed if, we cannot take the values from the
        // stack; we have to pass the stack values at the beginning of the
        // if-block.
        SetupControlFlowEdge(decoder, block->merge_block, &block->start_merge);
        asm_.Goto(block->merge_block);
        EnterBlock(decoder, block->merge_block, block->br_merge());
        break;
      case kControlIfElse:
      case kControlBlock:
        if (block->reachable()) {
          SetupControlFlowEdge(decoder, block->merge_block);
          asm_.Goto(block->merge_block);
        }
        EnterBlock(decoder, block->merge_block, block->br_merge());
        break;
      case kControlLoop: {
        TSBlock* post_loop = NewBlock(decoder, nullptr);
        if (block->reachable()) {
          SetupControlFlowEdge(decoder, post_loop);
          asm_.Goto(post_loop);
        }
        if (block->merge_block->PredecessorCount() == 0) {
          // Turns out, the loop has no backedges, i.e. it is not quite a loop
          // at all. Replace it with a merge, and its PendingPhis with one-input
          // phis.
          block->loop_block->SetKind(compiler::turboshaft::Block::Kind::kMerge);
          auto to = asm_.output_graph().operations(*block->loop_block).begin();
          for (uint32_t i = 0; i < ssa_env_.size() + block->br_merge()->arity;
               ++i, ++to) {
            // TODO(manoskouk): Add `->` operator to the iterator.
            PendingLoopPhiOp& pending_phi = (*to).Cast<PendingLoopPhiOp>();
            OpIndex replaced = asm_.output_graph().Index(*to);
            asm_.output_graph().Replace<compiler::turboshaft::PhiOp>(
                replaced, base::VectorOf({pending_phi.first()}),
                pending_phi.rep);
          }
        } else {
          // We abuse the start merge of the loop, which is not used otherwise
          // anymore, to store backedge inputs for the pending phi stack values
          // of the loop.
          EnterBlock(decoder, block->merge_block, block->br_merge());
          asm_.Goto(block->loop_block);
          auto to = asm_.output_graph().operations(*block->loop_block).begin();
          for (uint32_t i = 0; i < ssa_env_.size(); ++i, ++to) {
            PendingLoopPhiOp& pending_phi = (*to).Cast<PendingLoopPhiOp>();
            OpIndex replaced = asm_.output_graph().Index(*to);
            asm_.output_graph().Replace<compiler::turboshaft::PhiOp>(
                replaced, base::VectorOf({pending_phi.first(), ssa_env_[i]}),
                pending_phi.rep);
          }
          for (uint32_t i = 0; i < block->br_merge()->arity; ++i, ++to) {
            PendingLoopPhiOp& pending_phi = (*to).Cast<PendingLoopPhiOp>();
            OpIndex replaced = asm_.output_graph().Index(*to);
            asm_.output_graph().Replace<compiler::turboshaft::PhiOp>(
                replaced,
                base::VectorOf(
                    {pending_phi.first(), (*block->br_merge())[i].op}),
                pending_phi.rep);
          }
        }
        EnterBlock(decoder, post_loop, nullptr);
        break;
      }
      case kControlTry:
      case kControlTryCatch:
      case kControlTryCatchAll:
        Bailout(decoder);
        break;
    }
  }

  void DoReturn(FullDecoder* decoder, uint32_t drop_values) {
    size_t return_count = decoder->sig_->return_count();
    if (return_count == 0) {
      asm_.Return(asm_.Word32Constant(0), {});
    } else if (return_count == 1) {
      asm_.Return(decoder->stack_value(1 + drop_values)->op);
    } else {
      base::SmallVector<OpIndex, 8> return_values(return_count);
      Value* stack_base = decoder->stack_value(
          static_cast<uint32_t>(return_count + drop_values));
      for (size_t i = 0; i < return_count; i++) {
        return_values[i] = stack_base[i].op;
      }
      asm_.Return(asm_.Word32Constant(0), base::VectorOf(return_values));
    }
  }

  void UnOp(FullDecoder* decoder, WasmOpcode opcode, const Value& value,
            Value* result) {
    Bailout(decoder);
  }

  void BinOp(FullDecoder* decoder, WasmOpcode opcode, const Value& lhs,
             const Value& rhs, Value* result) {
    result->op = BinOpImpl(decoder, opcode, lhs.op, rhs.op);
  }

  void TraceInstruction(FullDecoder* decoder, uint32_t markid) {
    // TODO(14108): Implement.
  }

  void I32Const(FullDecoder* decoder, Value* result, int32_t value) {
    result->op = asm_.Word32Constant(value);
  }

  void I64Const(FullDecoder* decoder, Value* result, int64_t value) {
    result->op = asm_.Word64Constant(value);
  }

  void F32Const(FullDecoder* decoder, Value* result, float value) {
    result->op = asm_.FloatConstant(
        value, compiler::turboshaft::FloatRepresentation::Float32());
  }

  void F64Const(FullDecoder* decoder, Value* result, double value) {
    result->op = asm_.FloatConstant(
        value, compiler::turboshaft::FloatRepresentation::Float64());
  }

  void S128Const(FullDecoder* decoder, const Simd128Immediate& imm,
                 Value* result) {
    Bailout(decoder);
  }

  void RefNull(FullDecoder* decoder, ValueType type, Value* result) {
    Bailout(decoder);
  }

  void RefFunc(FullDecoder* decoder, uint32_t function_index, Value* result) {
    Bailout(decoder);
  }

  void RefAsNonNull(FullDecoder* decoder, const Value& arg, Value* result) {
    Bailout(decoder);
  }

  void Drop(FullDecoder* decoder) {}

  void LocalGet(FullDecoder* decoder, Value* result,
                const IndexImmediate& imm) {
    result->op = ssa_env_[imm.index];
  }

  void LocalSet(FullDecoder* decoder, const Value& value,
                const IndexImmediate& imm) {
    ssa_env_[imm.index] = value.op;
  }

  void LocalTee(FullDecoder* decoder, const Value& value, Value* result,
                const IndexImmediate& imm) {
    ssa_env_[imm.index] = result->op = value.op;
  }

  void GlobalGet(FullDecoder* decoder, Value* result,
                 const GlobalIndexImmediate& imm) {
    Bailout(decoder);
  }

  void GlobalSet(FullDecoder* decoder, const Value& value,
                 const GlobalIndexImmediate& imm) {
    Bailout(decoder);
  }

  void TableGet(FullDecoder* decoder, const Value& index, Value* result,
                const IndexImmediate& imm) {
    Bailout(decoder);
  }

  void TableSet(FullDecoder* decoder, const Value& index, const Value& value,
                const IndexImmediate& imm) {
    Bailout(decoder);
  }

  void Trap(FullDecoder* decoder, TrapReason reason) { Bailout(decoder); }

  void AssertNullTypecheck(FullDecoder* decoder, const Value& obj,
                           Value* result) {
    Bailout(decoder);
  }

  void AssertNotNullTypecheck(FullDecoder* decoder, const Value& obj,
                              Value* result) {
    Bailout(decoder);
  }

  void NopForTestingUnsupportedInLiftoff(FullDecoder* decoder) {
    Bailout(decoder);
  }

  void Select(FullDecoder* decoder, const Value& cond, const Value& fval,
              const Value& tval, Value* result) {
    Bailout(decoder);
  }

  void LoadMem(FullDecoder* decoder, LoadType type,
               const MemoryAccessImmediate& imm, const Value& index,
               Value* result) {
    Bailout(decoder);
  }

  void LoadTransform(FullDecoder* decoder, LoadType type,
                     LoadTransformationKind transform,
                     const MemoryAccessImmediate& imm, const Value& index,
                     Value* result) {
    Bailout(decoder);
  }

  void LoadLane(FullDecoder* decoder, LoadType type, const Value& value,
                const Value& index, const MemoryAccessImmediate& imm,
                const uint8_t laneidx, Value* result) {
    Bailout(decoder);
  }

  void StoreMem(FullDecoder* decoder, StoreType type,
                const MemoryAccessImmediate& imm, const Value& index,
                const Value& value) {
    Bailout(decoder);
  }

  void StoreLane(FullDecoder* decoder, StoreType type,
                 const MemoryAccessImmediate& imm, const Value& index,
                 const Value& value, const uint8_t laneidx) {
    Bailout(decoder);
  }

  void CurrentMemoryPages(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                          Value* result) {
    Bailout(decoder);
  }

  void MemoryGrow(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                  const Value& value, Value* result) {
    Bailout(decoder);
  }

  void CallDirect(FullDecoder* decoder, const CallFunctionImmediate& imm,
                  const Value args[], Value returns[]) {
    Bailout(decoder);
  }

  void ReturnCall(FullDecoder* decoder, const CallFunctionImmediate& imm,
                  const Value args[]) {
    Bailout(decoder);
  }

  void CallIndirect(FullDecoder* decoder, const Value& index,
                    const CallIndirectImmediate& imm, const Value args[],
                    Value returns[]) {
    Bailout(decoder);
  }

  void ReturnCallIndirect(FullDecoder* decoder, const Value& index,
                          const CallIndirectImmediate& imm,
                          const Value args[]) {
    Bailout(decoder);
  }

  void CallRef(FullDecoder* decoder, const Value& func_ref,
               const FunctionSig* sig, uint32_t sig_index, const Value args[],
               Value returns[]) {
    Bailout(decoder);
  }

  void ReturnCallRef(FullDecoder* decoder, const Value& func_ref,
                     const FunctionSig* sig, uint32_t sig_index,
                     const Value args[]) {
    Bailout(decoder);
  }

  void BrOnNull(FullDecoder* decoder, const Value& ref_object, uint32_t depth,
                bool pass_null_along_branch, Value* result_on_fallthrough) {
    Bailout(decoder);
  }

  void BrOnNonNull(FullDecoder* decoder, const Value& ref_object, Value* result,
                   uint32_t depth, bool /* drop_null_on_fallthrough */) {
    Bailout(decoder);
  }

  void SimdOp(FullDecoder* decoder, WasmOpcode opcode, const Value* args,
              Value* result) {
    Bailout(decoder);
  }

  void SimdLaneOp(FullDecoder* decoder, WasmOpcode opcode,
                  const SimdLaneImmediate& imm,
                  base::Vector<const Value> inputs, Value* result) {
    Bailout(decoder);
  }

  void Simd8x16ShuffleOp(FullDecoder* decoder, const Simd128Immediate& imm,
                         const Value& input0, const Value& input1,
                         Value* result) {
    Bailout(decoder);
  }

  void Try(FullDecoder* decoder, Control* block) { Bailout(decoder); }

  void Throw(FullDecoder* decoder, const TagIndexImmediate& imm,
             const Value arg_values[]) {
    Bailout(decoder);
  }

  void Rethrow(FullDecoder* decoder, Control* block) { Bailout(decoder); }

  void CatchException(FullDecoder* decoder, const TagIndexImmediate& imm,
                      Control* block, base::Vector<Value> values) {
    Bailout(decoder);
  }

  void Delegate(FullDecoder* decoder, uint32_t depth, Control* block) {
    Bailout(decoder);
  }

  void CatchAll(FullDecoder* decoder, Control* block) { Bailout(decoder); }

  void AtomicOp(FullDecoder* decoder, WasmOpcode opcode, const Value args[],
                const size_t argc, const MemoryAccessImmediate& imm,
                Value* result) {
    Bailout(decoder);
  }

  void AtomicFence(FullDecoder* decoder) { Bailout(decoder); }

  void MemoryInit(FullDecoder* decoder, const MemoryInitImmediate& imm,
                  const Value& dst, const Value& src, const Value& size) {
    Bailout(decoder);
  }

  void DataDrop(FullDecoder* decoder, const IndexImmediate& imm) {
    Bailout(decoder);
  }

  void MemoryCopy(FullDecoder* decoder, const MemoryCopyImmediate& imm,
                  const Value& dst, const Value& src, const Value& size) {
    Bailout(decoder);
  }

  void MemoryFill(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                  const Value& dst, const Value& value, const Value& size) {
    Bailout(decoder);
  }

  void TableInit(FullDecoder* decoder, const TableInitImmediate& imm,
                 const Value* args) {
    Bailout(decoder);
  }

  void ElemDrop(FullDecoder* decoder, const IndexImmediate& imm) {
    Bailout(decoder);
  }

  void TableCopy(FullDecoder* decoder, const TableCopyImmediate& imm,
                 const Value args[]) {
    Bailout(decoder);
  }

  void TableGrow(FullDecoder* decoder, const IndexImmediate& imm,
                 const Value& value, const Value& delta, Value* result) {
    Bailout(decoder);
  }

  void TableSize(FullDecoder* decoder, const IndexImmediate& imm,
                 Value* result) {
    Bailout(decoder);
  }

  void TableFill(FullDecoder* decoder, const IndexImmediate& imm,
                 const Value& start, const Value& value, const Value& count) {
    Bailout(decoder);
  }

  void StructNew(FullDecoder* decoder, const StructIndexImmediate& imm,
                 const Value args[], Value* result) {
    Bailout(decoder);
  }
  void StructNewDefault(FullDecoder* decoder, const StructIndexImmediate& imm,
                        Value* result) {
    Bailout(decoder);
  }

  void StructGet(FullDecoder* decoder, const Value& struct_object,
                 const FieldImmediate& field, bool is_signed, Value* result) {
    Bailout(decoder);
  }

  void StructSet(FullDecoder* decoder, const Value& struct_object,
                 const FieldImmediate& field, const Value& field_value) {
    Bailout(decoder);
  }

  void ArrayNew(FullDecoder* decoder, const ArrayIndexImmediate& imm,
                const Value& length, const Value& initial_value,
                Value* result) {
    Bailout(decoder);
  }

  void ArrayNewDefault(FullDecoder* decoder, const ArrayIndexImmediate& imm,
                       const Value& length, Value* result) {
    Bailout(decoder);
  }

  void ArrayGet(FullDecoder* decoder, const Value& array_obj,
                const ArrayIndexImmediate& imm, const Value& index,
                bool is_signed, Value* result) {
    Bailout(decoder);
  }

  void ArraySet(FullDecoder* decoder, const Value& array_obj,
                const ArrayIndexImmediate& imm, const Value& index,
                const Value& value) {
    Bailout(decoder);
  }

  void ArrayLen(FullDecoder* decoder, const Value& array_obj, Value* result) {
    Bailout(decoder);
  }

  void ArrayCopy(FullDecoder* decoder, const Value& dst, const Value& dst_index,
                 const Value& src, const Value& src_index,
                 const ArrayIndexImmediate& src_imm, const Value& length) {
    Bailout(decoder);
  }

  void ArrayFill(FullDecoder* decoder, ArrayIndexImmediate& imm,
                 const Value& array, const Value& index, const Value& value,
                 const Value& length) {
    Bailout(decoder);
  }

  void ArrayNewFixed(FullDecoder* decoder, const ArrayIndexImmediate& array_imm,
                     const IndexImmediate& length_imm, const Value elements[],
                     Value* result) {
    Bailout(decoder);
  }

  void ArrayNewSegment(FullDecoder* decoder,
                       const ArrayIndexImmediate& array_imm,
                       const IndexImmediate& segment_imm, const Value& offset,
                       const Value& length, Value* result) {
    Bailout(decoder);
  }

  void ArrayInitSegment(FullDecoder* decoder,
                        const ArrayIndexImmediate& array_imm,
                        const IndexImmediate& segment_imm, const Value& array,
                        const Value& array_index, const Value& segment_offset,
                        const Value& length) {
    Bailout(decoder);
  }

  void I31New(FullDecoder* decoder, const Value& input, Value* result) {
    Bailout(decoder);
  }

  void I31GetS(FullDecoder* decoder, const Value& input, Value* result) {
    Bailout(decoder);
  }

  void I31GetU(FullDecoder* decoder, const Value& input, Value* result) {
    Bailout(decoder);
  }

  void RefTest(FullDecoder* decoder, uint32_t ref_index, const Value& object,
               Value* result, bool null_succeeds) {
    Bailout(decoder);
  }

  void RefTestAbstract(FullDecoder* decoder, const Value& object,
                       wasm::HeapType type, Value* result, bool null_succeeds) {
    Bailout(decoder);
  }

  void RefCast(FullDecoder* decoder, uint32_t ref_index, const Value& object,
               Value* result, bool null_succeeds) {
    Bailout(decoder);
  }

  // TODO(jkummerow): {type} is redundant.
  void RefCastAbstract(FullDecoder* decoder, const Value& object,
                       wasm::HeapType type, Value* result, bool null_succeeds) {
    Bailout(decoder);
  }

  void BrOnCast(FullDecoder* decoder, uint32_t ref_index, const Value& object,
                Value* value_on_branch, uint32_t br_depth, bool null_succeeds) {
    Bailout(decoder);
  }

  void BrOnCastFail(FullDecoder* decoder, uint32_t ref_index,
                    const Value& object, Value* value_on_fallthrough,
                    uint32_t br_depth, bool null_succeeds) {
    Bailout(decoder);
  }

  void BrOnCastAbstract(FullDecoder* decoder, const Value& object,
                        HeapType type, Value* value_on_branch,
                        uint32_t br_depth, bool null_succeeds) {
    Bailout(decoder);
  }

  void BrOnCastFailAbstract(FullDecoder* decoder, const Value& object,
                            HeapType type, Value* value_on_fallthrough,
                            uint32_t br_depth, bool null_succeeds) {
    Bailout(decoder);
  }

  void RefIsStruct(FullDecoder* decoder, const Value& object, Value* result) {
    Bailout(decoder);
  }

  void RefAsStruct(FullDecoder* decoder, const Value& object, Value* result) {
    Bailout(decoder);
  }

  void BrOnStruct(FullDecoder* decoder, const Value& object,
                  Value* value_on_branch, uint32_t br_depth,
                  bool null_succeeds) {
    Bailout(decoder);
  }

  void BrOnNonStruct(FullDecoder* decoder, const Value& object,
                     Value* value_on_fallthrough, uint32_t br_depth,
                     bool null_succeeds) {
    Bailout(decoder);
  }

  void RefIsArray(FullDecoder* decoder, const Value& object, Value* result) {
    Bailout(decoder);
  }

  void RefAsArray(FullDecoder* decoder, const Value& object, Value* result) {
    Bailout(decoder);
  }

  void BrOnArray(FullDecoder* decoder, const Value& object,
                 Value* value_on_branch, uint32_t br_depth,
                 bool null_succeeds) {
    Bailout(decoder);
  }

  void BrOnNonArray(FullDecoder* decoder, const Value& object,
                    Value* value_on_fallthrough, uint32_t br_depth,
                    bool null_succeeds) {
    Bailout(decoder);
  }

  void RefIsI31(FullDecoder* decoder, const Value& object, Value* result) {
    Bailout(decoder);
  }

  void RefAsI31(FullDecoder* decoder, const Value& object, Value* result) {
    Bailout(decoder);
  }

  void BrOnI31(FullDecoder* decoder, const Value& object,
               Value* value_on_branch, uint32_t br_depth, bool null_succeeds) {
    Bailout(decoder);
  }

  void BrOnNonI31(FullDecoder* decoder, const Value& object,
                  Value* value_on_fallthrough, uint32_t br_depth,
                  bool null_succeeds) {
    Bailout(decoder);
  }

  void BrOnString(FullDecoder* decoder, const Value& object,
                  Value* value_on_branch, uint32_t br_depth,
                  bool null_succeeds) {
    Bailout(decoder);
  }

  void BrOnNonString(FullDecoder* decoder, const Value& object,
                     Value* value_on_fallthrough, uint32_t br_depth,
                     bool null_succeeds) {
    Bailout(decoder);
  }

  void StringNewWtf8(FullDecoder* decoder, const MemoryIndexImmediate& memory,
                     const unibrow::Utf8Variant variant, const Value& offset,
                     const Value& size, Value* result) {
    Bailout(decoder);
  }

  void StringNewWtf8Array(FullDecoder* decoder,
                          const unibrow::Utf8Variant variant,
                          const Value& array, const Value& start,
                          const Value& end, Value* result) {
    Bailout(decoder);
  }

  void StringNewWtf16(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                      const Value& offset, const Value& size, Value* result) {
    Bailout(decoder);
  }

  void StringNewWtf16Array(FullDecoder* decoder, const Value& array,
                           const Value& start, const Value& end,
                           Value* result) {
    Bailout(decoder);
  }

  void StringConst(FullDecoder* decoder, const StringConstImmediate& imm,
                   Value* result) {
    Bailout(decoder);
  }

  void StringMeasureWtf8(FullDecoder* decoder,
                         const unibrow::Utf8Variant variant, const Value& str,
                         Value* result) {
    Bailout(decoder);
  }

  void StringMeasureWtf16(FullDecoder* decoder, const Value& str,
                          Value* result) {
    Bailout(decoder);
  }

  void StringEncodeWtf8(FullDecoder* decoder,
                        const MemoryIndexImmediate& memory,
                        const unibrow::Utf8Variant variant, const Value& str,
                        const Value& offset, Value* result) {
    Bailout(decoder);
  }

  void StringEncodeWtf8Array(FullDecoder* decoder,
                             const unibrow::Utf8Variant variant,
                             const Value& str, const Value& array,
                             const Value& start, Value* result) {
    Bailout(decoder);
  }

  void StringEncodeWtf16(FullDecoder* decoder, const MemoryIndexImmediate& imm,
                         const Value& str, const Value& offset, Value* result) {
    Bailout(decoder);
  }

  void StringEncodeWtf16Array(FullDecoder* decoder, const Value& str,
                              const Value& array, const Value& start,
                              Value* result) {
    Bailout(decoder);
  }

  void StringConcat(FullDecoder* decoder, const Value& head, const Value& tail,
                    Value* result) {
    Bailout(decoder);
  }

  void StringEq(FullDecoder* decoder, const Value& a, const Value& b,
                Value* result) {
    Bailout(decoder);
  }

  void StringIsUSVSequence(FullDecoder* decoder, const Value& str,
                           Value* result) {
    Bailout(decoder);
  }

  void StringAsWtf8(FullDecoder* decoder, const Value& str, Value* result) {
    Bailout(decoder);
  }

  void StringViewWtf8Advance(FullDecoder* decoder, const Value& view,
                             const Value& pos, const Value& bytes,
                             Value* result) {
    Bailout(decoder);
  }

  void StringViewWtf8Encode(FullDecoder* decoder,
                            const MemoryIndexImmediate& memory,
                            const unibrow::Utf8Variant variant,
                            const Value& view, const Value& addr,
                            const Value& pos, const Value& bytes,
                            Value* next_pos, Value* bytes_written) {
    Bailout(decoder);
  }

  void StringViewWtf8Slice(FullDecoder* decoder, const Value& view,
                           const Value& start, const Value& end,
                           Value* result) {
    Bailout(decoder);
  }

  void StringAsWtf16(FullDecoder* decoder, const Value& str, Value* result) {
    Bailout(decoder);
  }

  void StringViewWtf16GetCodeUnit(FullDecoder* decoder, const Value& view,
                                  const Value& pos, Value* result) {
    Bailout(decoder);
  }

  void StringViewWtf16Encode(FullDecoder* decoder,
                             const MemoryIndexImmediate& imm, const Value& view,
                             const Value& offset, const Value& pos,
                             const Value& codeunits, Value* result) {
    Bailout(decoder);
  }

  void StringViewWtf16Slice(FullDecoder* decoder, const Value& view,
                            const Value& start, const Value& end,
                            Value* result) {
    Bailout(decoder);
  }

  void StringAsIter(FullDecoder* decoder, const Value& str, Value* result) {
    Bailout(decoder);
  }

  void StringViewIterNext(FullDecoder* decoder, const Value& view,
                          Value* result) {
    Bailout(decoder);
  }

  void StringViewIterAdvance(FullDecoder* decoder, const Value& view,
                             const Value& codepoints, Value* result) {
    Bailout(decoder);
  }

  void StringViewIterRewind(FullDecoder* decoder, const Value& view,
                            const Value& codepoints, Value* result) {
    Bailout(decoder);
  }

  void StringViewIterSlice(FullDecoder* decoder, const Value& view,
                           const Value& codepoints, Value* result) {
    Bailout(decoder);
  }

  void StringCompare(FullDecoder* decoder, const Value& lhs, const Value& rhs,
                     Value* result) {
    Bailout(decoder);
  }

  void StringFromCodePoint(FullDecoder* decoder, const Value& code_point,
                           Value* result) {
    Bailout(decoder);
  }

  void StringHash(FullDecoder* decoder, const Value& string, Value* result) {
    Bailout(decoder);
  }

  void Forward(FullDecoder* decoder, const Value& from, Value* to) {
    Bailout(decoder);
  }

  bool did_bailout() { return did_bailout_; }

 private:
  // Holds phi inputs for a specific block. These include SSA values as well as
  // stack merge values.
  struct BlockPhis {
    // The first vector corresponds to all inputs of the first phi etc.
    std::vector<std::vector<OpIndex>> phi_inputs;
    std::vector<ValueType> phi_types;

    explicit BlockPhis(int total_arity)
        : phi_inputs(total_arity), phi_types(total_arity) {}
  };

  void Bailout(FullDecoder* decoder) {
    decoder->errorf("Unsupported Turboshaft operation: %s",
                    decoder->SafeOpcodeNameAt(decoder->pc()));
    did_bailout_ = true;
  }

  void BailoutWithoutOpcode(FullDecoder* decoder, const char* message) {
    decoder->errorf("Unsupported operation: %s", message);
    did_bailout_ = true;
  }

  // Creates a new block, initializes a {BlockPhis} for it, and registers it
  // with block_phis_. We pass a {merge} only if we later need to recover values
  // for that merge.
  TSBlock* NewBlock(FullDecoder* decoder, Merge<Value>* merge) {
    TSBlock* block = asm_.NewBlock();
    BlockPhis block_phis(decoder->num_locals() +
                         (merge != nullptr ? merge->arity : 0));
    for (uint32_t i = 0; i < decoder->num_locals(); i++) {
      block_phis.phi_types[i] = decoder->local_type(i);
    }
    if (merge != nullptr) {
      for (uint32_t i = 0; i < merge->arity; i++) {
        block_phis.phi_types[decoder->num_locals() + i] = (*merge)[i].type;
      }
    }
    block_phis_.emplace(block, std::move(block_phis));
    return block;
  }

  // Sets up a control flow edge from the current SSA environment and a stack to
  // {block}. The stack is {stack_values} if present, otherwise the current
  // decoder stack.
  void SetupControlFlowEdge(FullDecoder* decoder, TSBlock* block,
                            Merge<Value>* stack_values = nullptr) {
    // It is guaranteed that this element exists.
    BlockPhis& phis_for_block = block_phis_.find(block)->second;
    uint32_t merge_arity =
        static_cast<uint32_t>(phis_for_block.phi_inputs.size()) -
        decoder->num_locals();
    for (size_t i = 0; i < ssa_env_.size(); i++) {
      phis_for_block.phi_inputs[i].emplace_back(ssa_env_[i]);
    }
    Value* stack_base = merge_arity == 0 ? nullptr
                        : stack_values != nullptr
                            ? &(*stack_values)[0]
                            : decoder->stack_value(merge_arity);
    for (size_t i = 0; i < merge_arity; i++) {
      phis_for_block.phi_inputs[decoder->num_locals() + i].emplace_back(
          stack_base[i].op);
    }
  }

  OpIndex MaybePhi(FullDecoder* decoder, std::vector<OpIndex>& elements,
                   ValueType type) {
    if (elements.empty()) return OpIndex::Invalid();
    for (size_t i = 1; i < elements.size(); i++) {
      if (elements[i] != elements[0]) {
        return asm_.Phi(base::VectorOf(elements),
                        RepresentationFor(decoder, type));
      }
    }
    return elements[0];
  }

  // Binds a block, initializes phis for its SSA environment from its entry in
  // {block_phis_}, and sets values to its {merge} (if available) from the
  // its entry in {block_phis_}.
  void EnterBlock(FullDecoder* decoder, TSBlock* tsblock, Merge<Value>* merge) {
    asm_.Bind(tsblock);
    BlockPhis& block_phis = block_phis_.at(tsblock);
    for (uint32_t i = 0; i < decoder->num_locals(); i++) {
      ssa_env_[i] =
          MaybePhi(decoder, block_phis.phi_inputs[i], block_phis.phi_types[i]);
    }
    DCHECK_EQ(decoder->num_locals() + (merge != nullptr ? merge->arity : 0),
              block_phis.phi_inputs.size());
    if (merge != nullptr) {
      for (uint32_t i = 0; i < merge->arity; i++) {
        (*merge)[i].op =
            MaybePhi(decoder, block_phis.phi_inputs[decoder->num_locals() + i],
                     block_phis.phi_types[decoder->num_locals() + i]);
      }
    }
    block_phis_.erase(tsblock);
  }

  OpIndex DefaultValue(FullDecoder* decoder, ValueType type) {
    switch (type.kind()) {
      case kI32:
        return asm_.Word32Constant(0);
      case kI64:
        return asm_.Word64Constant(int64_t{0});
      case kF32:
        return asm_.Float32Constant(0.0f);
      case kF64:
        return asm_.Float64Constant(0.0);
      case kI8:
      case kI16:
      case kRefNull:
      case kS128:
        BailoutWithoutOpcode(decoder, "unimplemented type");
        return OpIndex::Invalid();
      case kVoid:
      case kRtt:
      case kRef:
      case kBottom:
        UNREACHABLE();
    }
  }

  RegisterRepresentation RepresentationFor(FullDecoder* decoder,
                                           ValueType type) {
    switch (type.kind()) {
      case kI32:
        return RegisterRepresentation::Word32();
      case kI64:
        return RegisterRepresentation::Word64();
      case kF32:
        return RegisterRepresentation::Float32();
      case kF64:
        return RegisterRepresentation::Float64();
      case kI8:
      case kI16:
      case kRefNull:
      case kRef:
      case kS128:
        BailoutWithoutOpcode(decoder, "unimplemented type");
        return RegisterRepresentation::Word32();
      case kVoid:
      case kRtt:
      case kBottom:
        UNREACHABLE();
    }
  }

  OpIndex BinOpImpl(FullDecoder* decoder, WasmOpcode opcode, OpIndex lhs,
                    OpIndex rhs) {
    switch (opcode) {
      case kExprI32Add:
        return asm_.Word32Add(lhs, rhs);
      case kExprI32Sub:
        return asm_.Word32Sub(lhs, rhs);
      case kExprI32Mul:
        return asm_.Word32Mul(lhs, rhs);
      case kExprI32DivS:
      case kExprI32DivU:
      case kExprI32RemS:
      case kExprI32RemU:
        Bailout(decoder);
        return OpIndex::Invalid();
      case kExprI32And:
        return asm_.Word32BitwiseAnd(lhs, rhs);
      case kExprI32Ior:
        return asm_.Word32BitwiseOr(lhs, rhs);
      case kExprI32Xor:
        return asm_.Word32BitwiseXor(lhs, rhs);
      case wasm::kExprI32Shl:
      case wasm::kExprI32ShrU:
      case wasm::kExprI32ShrS:
      case wasm::kExprI32Ror:
      case wasm::kExprI32Rol:
        Bailout(decoder);
        return OpIndex::Invalid();
      case wasm::kExprI32Eq:
        return asm_.Word32Equal(lhs, rhs);
      case wasm::kExprI32Ne:
        return asm_.Word32Equal(asm_.Word32Equal(lhs, rhs), 0);
      case wasm::kExprI32LtS:
        return asm_.Int32LessThan(lhs, rhs);
      case wasm::kExprI32LeS:
        return asm_.Int32LessThanOrEqual(lhs, rhs);
      case wasm::kExprI32LtU:
        return asm_.Uint32LessThan(lhs, rhs);
      case wasm::kExprI32LeU:
        return asm_.Uint32LessThanOrEqual(lhs, rhs);
      case wasm::kExprI32GtS:
        return asm_.Int32LessThan(rhs, lhs);
      case wasm::kExprI32GeS:
        return asm_.Int32LessThanOrEqual(rhs, lhs);
      case wasm::kExprI32GtU:
        return asm_.Uint32LessThan(rhs, lhs);
      case wasm::kExprI32GeU:
        return asm_.Uint32LessThanOrEqual(rhs, lhs);
      case wasm::kExprI64Add:
        return asm_.Word64Add(lhs, rhs);
      case wasm::kExprI64Sub:
        return asm_.Word64Sub(lhs, rhs);
      case wasm::kExprI64Mul:
        return asm_.Word64Mul(lhs, rhs);
      case wasm::kExprI64DivS:
      case wasm::kExprI64DivU:
      case wasm::kExprI64RemS:
      case wasm::kExprI64RemU:
        Bailout(decoder);
        return OpIndex::Invalid();
      case wasm::kExprI64And:
        return asm_.Word64BitwiseAnd(lhs, rhs);
      case wasm::kExprI64Ior:
        return asm_.Word64BitwiseOr(lhs, rhs);
      case wasm::kExprI64Xor:
        return asm_.Word64BitwiseXor(lhs, rhs);
      case wasm::kExprI64Shl:
      case wasm::kExprI64ShrU:
      case wasm::kExprI64ShrS:
        Bailout(decoder);
        return OpIndex::Invalid();
      case wasm::kExprI64Eq:
        return asm_.Word64Equal(lhs, rhs);
      case wasm::kExprI64Ne:
        return asm_.Word32Equal(asm_.Word64Equal(lhs, rhs), 0);
      case wasm::kExprI64LtS:
        return asm_.Int64LessThan(lhs, rhs);
      case wasm::kExprI64LeS:
        return asm_.Int64LessThanOrEqual(lhs, rhs);
      case wasm::kExprI64LtU:
        return asm_.Uint64LessThan(lhs, rhs);
      case wasm::kExprI64LeU:
        return asm_.Uint64LessThanOrEqual(lhs, rhs);
      case wasm::kExprI64GtS:
        return asm_.Int64LessThan(rhs, lhs);
      case wasm::kExprI64GeS:
        return asm_.Int64LessThanOrEqual(rhs, lhs);
      case wasm::kExprI64GtU:
        return asm_.Uint64LessThan(rhs, lhs);
      case wasm::kExprI64GeU:
        return asm_.Uint64LessThanOrEqual(rhs, lhs);
      case wasm::kExprI64Ror:
      case wasm::kExprI64Rol:
      case wasm::kExprF32CopySign:
        Bailout(decoder);
        return OpIndex::Invalid();
      case wasm::kExprF32Add:
        return asm_.Float32Add(lhs, rhs);
      case wasm::kExprF32Sub:
        return asm_.Float32Sub(lhs, rhs);
      case wasm::kExprF32Mul:
        return asm_.Float32Mul(lhs, rhs);
      case wasm::kExprF32Div:
        return asm_.Float32Div(lhs, rhs);
      case wasm::kExprF32Eq:
        return asm_.Float32Equal(lhs, rhs);
      case wasm::kExprF32Ne:
        return asm_.Word32Equal(asm_.Float32Equal(lhs, rhs), 0);
      case wasm::kExprF32Lt:
        return asm_.Float32LessThan(lhs, rhs);
      case wasm::kExprF32Le:
        return asm_.Float32LessThanOrEqual(lhs, rhs);
      case wasm::kExprF32Gt:
        return asm_.Float32LessThan(rhs, lhs);
      case wasm::kExprF32Ge:
        return asm_.Float32LessThanOrEqual(rhs, lhs);
      case wasm::kExprF32Min:
        return asm_.Float32Min(rhs, lhs);
      case wasm::kExprF32Max:
        return asm_.Float32Max(rhs, lhs);
      case wasm::kExprF64CopySign:
        Bailout(decoder);
        return OpIndex::Invalid();
      case wasm::kExprF64Add:
        return asm_.Float64Add(lhs, rhs);
      case wasm::kExprF64Sub:
        return asm_.Float64Sub(lhs, rhs);
      case wasm::kExprF64Mul:
        return asm_.Float64Mul(lhs, rhs);
      case wasm::kExprF64Div:
        return asm_.Float64Div(lhs, rhs);
      case wasm::kExprF64Eq:
        return asm_.Float64Equal(lhs, rhs);
      case wasm::kExprF64Ne:
        return asm_.Word32Equal(asm_.Float64Equal(lhs, rhs), 0);
      case wasm::kExprF64Lt:
        return asm_.Float64LessThan(lhs, rhs);
      case wasm::kExprF64Le:
        return asm_.Float64LessThanOrEqual(lhs, rhs);
      case wasm::kExprF64Gt:
        return asm_.Float64LessThan(rhs, lhs);
      case wasm::kExprF64Ge:
        return asm_.Float64LessThanOrEqual(rhs, lhs);
      case wasm::kExprF64Min:
        return asm_.Float64Min(lhs, rhs);
      case wasm::kExprF64Max:
        return asm_.Float64Max(lhs, rhs);
      case wasm::kExprF64Pow:
        return asm_.Float64Power(lhs, rhs);
      case wasm::kExprF64Atan2:
        return asm_.Float64Atan2(lhs, rhs);
      case wasm::kExprF64Mod:
      case wasm::kExprRefEq:
      case wasm::kExprI32AsmjsDivS:
      case wasm::kExprI32AsmjsDivU:
      case wasm::kExprI32AsmjsRemS:
      case wasm::kExprI32AsmjsRemU:
      case wasm::kExprI32AsmjsStoreMem8:
      case wasm::kExprI32AsmjsStoreMem16:
      case wasm::kExprI32AsmjsStoreMem:
      case wasm::kExprF32AsmjsStoreMem:
      case wasm::kExprF64AsmjsStoreMem:
        Bailout(decoder);
        return OpIndex::Invalid();
      default:
        UNREACHABLE();
    }
  }

  std::unordered_map<TSBlock*, BlockPhis> block_phis_;
  Assembler asm_;
  std::vector<OpIndex> ssa_env_;
  bool did_bailout_ = false;
};

V8_EXPORT_PRIVATE bool BuildTSGraph(AccountingAllocator* allocator,
                                    const WasmFeatures& enabled,
                                    const WasmModule* module,
                                    WasmFeatures* detected,
                                    const FunctionBody& body, Graph& graph) {
  Zone zone(allocator, ZONE_NAME);
  WasmFullDecoder<Decoder::FullValidationTag, TurboshaftGraphBuildingInterface>
      decoder(&zone, module, enabled, detected, body, graph, &zone);
  decoder.Decode();
  // Turboshaft runs with validation, but the function should already be
  // validated, so graph building must always succeed, unless we bailed out.
  DCHECK_IMPLIES(!decoder.ok(), decoder.interface().did_bailout());
  return decoder.ok();
}

}  // namespace v8::internal::wasm
