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
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-opcodes-inl.h"

namespace v8::internal::wasm {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

using Assembler =
    compiler::turboshaft::Assembler<compiler::turboshaft::reducer_list<>>;
using compiler::CallDescriptor;
using compiler::LinkageLocation;
using compiler::LocationSignature;
using compiler::turboshaft::ConditionWithHint;
using compiler::turboshaft::Float32;
using compiler::turboshaft::Float64;
using compiler::turboshaft::Graph;
using compiler::turboshaft::Label;
using compiler::turboshaft::LoadOp;
using compiler::turboshaft::MemoryRepresentation;
using compiler::turboshaft::OpIndex;
using compiler::turboshaft::PendingLoopPhiOp;
using compiler::turboshaft::RegisterRepresentation;
using compiler::turboshaft::StoreOp;
using compiler::turboshaft::SupportedOperations;
using TSBlock = compiler::turboshaft::Block;
using compiler::turboshaft::TSCallDescriptor;
using compiler::turboshaft::V;
using compiler::turboshaft::Word32;
using compiler::turboshaft::Word64;
using compiler::turboshaft::WordPtr;

#define LOAD_INSTANCE_FIELD(name, representation)       \
  asm_.Load(instance_node_, LoadOp::Kind::TaggedBase(), \
            MemoryRepresentation::representation(),     \
            WasmInstanceObject::k##name##Offset);

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

  TurboshaftGraphBuildingInterface(Graph& graph, Zone* zone,
                                   compiler::NodeOriginTable* node_origins)
      : asm_(graph, graph, zone, node_origins) {}

  void StartFunction(FullDecoder* decoder) {
    TSBlock* block = asm_.NewBlock();
    asm_.Bind(block);
    // Set 0 as the current source position (before locals declarations).
    asm_.SetCurrentOrigin(WasmPositionToOpIndex(0));
    instance_node_ = asm_.Parameter(0, RegisterRepresentation::PointerSized());
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

    StackCheck();  // TODO(14108): Remove for leaf functions.

    if (v8_flags.trace_wasm) {
      asm_.SetCurrentOrigin(WasmPositionToOpIndex(decoder->position()));
      CallRuntime(Runtime::kWasmTraceEnter, {});
    }
  }

  void StartFunctionBody(FullDecoder* decoder, Control* block) {}

  void FinishFunction(FullDecoder* decoder) {
    for (OpIndex index : asm_.output_graph().AllOperationIndices()) {
      WasmCodePosition position =
          OpIndexToWasmPosition(asm_.output_graph().operation_origins()[index]);
      asm_.output_graph().source_positions()[index] = SourcePosition(position);
    }
  }

  void OnFirstError(FullDecoder*) {}

  void NextInstruction(FullDecoder* decoder, WasmOpcode) {
    asm_.SetCurrentOrigin(WasmPositionToOpIndex(decoder->position()));
  }

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

    StackCheck();

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
    asm_.Branch(ConditionWithHint(cond.op), true_block, false_block);
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
      asm_.Branch(ConditionWithHint(cond.op), return_block, non_branching);
      EnterBlock(decoder, return_block, nullptr);
      DoReturn(decoder, 0);
      EnterBlock(decoder, non_branching, nullptr);
    } else {
      Control* target = decoder->control_at(depth);
      SetupControlFlowEdge(decoder, target->merge_block);
      TSBlock* non_branching = NewBlock(decoder, nullptr);
      SetupControlFlowEdge(decoder, non_branching);
      asm_.Branch(ConditionWithHint(cond.op), target->merge_block,
                  non_branching);
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
    base::SmallVector<OpIndex, 8> return_values(return_count);
    Value* stack_base = return_count == 0
                            ? nullptr
                            : decoder->stack_value(static_cast<uint32_t>(
                                  return_count + drop_values));
    for (size_t i = 0; i < return_count; i++) {
      return_values[i] = stack_base[i].op;
    }
    if (v8_flags.trace_wasm) {
      OpIndex info = asm_.IntPtrConstant(0);
      if (return_count == 1) {
        wasm::ValueType return_type = decoder->sig_->GetReturn(0);
        int size = return_type.value_kind_size();
        // TODO(14108): This won't fit everything.
        info = asm_.StackSlot(size, size);
        // TODO(14108): Write barrier might be needed.
        asm_.Store(
            info, return_values[0], StoreOp::Kind::RawAligned(),
            MemoryRepresentation::FromMachineType(return_type.machine_type()),
            compiler::kNoWriteBarrier);
      }
      CallRuntime(Runtime::kWasmTraceExit, base::VectorOf(&info, 1));
    }
    asm_.Return(asm_.Word32Constant(0), base::VectorOf(return_values));
  }

  void UnOp(FullDecoder* decoder, WasmOpcode opcode, const Value& value,
            Value* result) {
    result->op = UnOpImpl(decoder, opcode, value.op);
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
    using Implementation = compiler::turboshaft::SelectOp::Implementation;
    bool use_select = false;
    switch (tval.type.kind()) {
      case kI32:
        if (SupportedOperations::word32_select()) use_select = true;
        break;
      case kI64:
        if (SupportedOperations::word64_select()) use_select = true;
        break;
      case kF32:
        if (SupportedOperations::float32_select()) use_select = true;
        break;
      case kF64:
        if (SupportedOperations::float64_select()) use_select = true;
        break;
      case kRef:
      case kRefNull:
        break;
      case kS128:
        Bailout(decoder);
        return;
      case kI8:
      case kI16:
      case kRtt:
      case kVoid:
      case kBottom:
        UNREACHABLE();
    }

    if (use_select) {
      result->op = asm_.Select(cond.op, tval.op, fval.op,
                               RepresentationFor(decoder, tval.type),
                               BranchHint::kNone, Implementation::kCMove);
      return;
    } else {
      TSBlock* true_block = asm_.NewBlock();
      TSBlock* false_block = asm_.NewBlock();
      TSBlock* merge_block = asm_.NewBlock();
      asm_.Branch(ConditionWithHint(cond.op), true_block, false_block);
      asm_.Bind(true_block);
      asm_.Goto(merge_block);
      asm_.Bind(false_block);
      asm_.Goto(merge_block);
      asm_.Bind(merge_block);
      result->op =
          asm_.Phi({tval.op, fval.op}, RepresentationFor(decoder, tval.type));
    }
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
    if (imm.sig->contains(kWasmS128)) {
      Bailout(decoder);
      return;
    }

    if (imm.index < decoder->module_->num_imported_functions) {
      // Imported function.
      OpIndex func_index = asm_.IntPtrConstant(imm.index);
      OpIndex imported_function_refs =
          LOAD_INSTANCE_FIELD(ImportedFunctionRefs, TaggedPointer);
      OpIndex ref = asm_.Load(imported_function_refs, func_index,
                              LoadOp::Kind::TaggedBase(),
                              MemoryRepresentation::TaggedPointer(),
                              FixedArray::kHeaderSize, kTaggedSizeLog2);
      OpIndex imported_targets =
          LOAD_INSTANCE_FIELD(ImportedFunctionTargets, TaggedPointer);
      OpIndex target =
          asm_.Load(imported_targets, func_index, LoadOp::Kind::TaggedBase(),
                    MemoryRepresentation::PointerSized(),
                    FixedAddressArray::kHeaderSize, kSystemPointerSizeLog2);
      BuildWasmCall(decoder, imm.sig, target, ref, args, returns);
    } else {
      // Locally defined function.
      OpIndex callee =
          asm_.RelocatableConstant(imm.index, RelocInfo::WASM_CALL);
      BuildWasmCall(decoder, imm.sig, callee, instance_node_, args, returns);
    }
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

  void RefTestAbstract(FullDecoder* decoder, const Value& object, HeapType type,
                       Value* result, bool null_succeeds) {
    Bailout(decoder);
  }

  void RefCast(FullDecoder* decoder, uint32_t ref_index, const Value& object,
               Value* result, bool null_succeeds) {
    Bailout(decoder);
  }

  // TODO(jkummerow): {type} is redundant.
  void RefCastAbstract(FullDecoder* decoder, const Value& object, HeapType type,
                       Value* result, bool null_succeeds) {
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
    to->op = from.op;
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
      case kRefNull:
      case kRef:
        return RegisterRepresentation::Tagged();
      case kI8:
      case kI16:
      case kS128:
        BailoutWithoutOpcode(decoder, "unimplemented type");
        return RegisterRepresentation::Word32();
      case kVoid:
      case kRtt:
      case kBottom:
        UNREACHABLE();
    }
  }

  OpIndex ExtractTruncationProjections(OpIndex truncated) {
    OpIndex result =
        asm_.Projection(truncated, 0, RegisterRepresentation::Word64());
    OpIndex check =
        asm_.Projection(truncated, 1, RegisterRepresentation::Word32());
    asm_.TrapIf(asm_.Word32Equal(check, 0), OpIndex::Invalid(),
                compiler::TrapId::kTrapFloatUnrepresentable);
    return result;
  }

  // TODO(14108): Remove the decoder argument once we have no bailouts.
  OpIndex UnOpImpl(FullDecoder* decoder, WasmOpcode opcode, OpIndex arg) {
    switch (opcode) {
      case kExprI32Eqz:
        return asm_.Word32Equal(arg, 0);
      case kExprF32Abs:
        return asm_.Float32Abs(arg);
      case kExprF32Neg:
        return asm_.Float32Negate(arg);
      case kExprF32Sqrt:
        return asm_.Float32Sqrt(arg);
      case kExprF64Abs:
        return asm_.Float64Abs(arg);
      case kExprF64Neg:
        return asm_.Float64Negate(arg);
      case kExprF64Sqrt:
        return asm_.Float64Sqrt(arg);
      case kExprI32SConvertF32: {
        OpIndex truncated = UnOpImpl(decoder, kExprF32Trunc, arg);
        OpIndex result = asm_.TruncateFloat32ToInt32OverflowToMin(truncated);
        OpIndex converted_back = asm_.ChangeInt32ToFloat32(result);
        asm_.TrapIf(
            asm_.Word32Equal(asm_.Float32Equal(converted_back, truncated), 0),
            OpIndex::Invalid(), compiler::TrapId::kTrapFloatUnrepresentable);
        return result;
      }
      case kExprI32UConvertF32: {
        OpIndex truncated = UnOpImpl(decoder, kExprF32Trunc, arg);
        OpIndex result = asm_.TruncateFloat32ToUint32OverflowToMin(truncated);
        OpIndex converted_back = asm_.ChangeUint32ToFloat32(result);
        asm_.TrapIf(
            asm_.Word32Equal(asm_.Float32Equal(converted_back, truncated), 0),
            OpIndex::Invalid(), compiler::TrapId::kTrapFloatUnrepresentable);
        return result;
      }
      case kExprI32SConvertF64: {
        OpIndex truncated = UnOpImpl(decoder, kExprF64Trunc, arg);
        OpIndex result = asm_.TruncateFloat64ToInt64OverflowToMin(truncated);
        // Implicitly truncated to i32.
        OpIndex converted_back = asm_.ChangeInt32ToFloat64(result);
        asm_.TrapIf(
            asm_.Word32Equal(asm_.Float64Equal(converted_back, truncated), 0),
            OpIndex::Invalid(), compiler::TrapId::kTrapFloatUnrepresentable);
        return result;
      }
      case kExprI32UConvertF64: {
        OpIndex truncated = UnOpImpl(decoder, kExprF64Trunc, arg);
        OpIndex result = asm_.TruncateFloat64ToUint32OverflowToMin(truncated);
        OpIndex converted_back = asm_.ChangeUint32ToFloat64(result);
        asm_.TrapIf(
            asm_.Word32Equal(asm_.Float64Equal(converted_back, truncated), 0),
            OpIndex::Invalid(), compiler::TrapId::kTrapFloatUnrepresentable);
        return result;
      }
      case kExprI64SConvertF32:
        return ExtractTruncationProjections(
            asm_.TryTruncateFloat32ToInt64(arg));
      case kExprI64UConvertF32:
        return ExtractTruncationProjections(
            asm_.TryTruncateFloat32ToUint64(arg));
      case kExprI64SConvertF64:
        return ExtractTruncationProjections(
            asm_.TryTruncateFloat64ToInt64(arg));
      case kExprI64UConvertF64:
        return ExtractTruncationProjections(
            asm_.TryTruncateFloat64ToUint64(arg));
      case kExprF64SConvertI32:
        return asm_.ChangeInt32ToFloat64(arg);
      case kExprF64UConvertI32:
        return asm_.ChangeUint32ToFloat64(arg);
      case kExprF32SConvertI32:
        return asm_.ChangeInt32ToFloat32(arg);
      case kExprF32UConvertI32:
        return asm_.ChangeUint32ToFloat32(arg);
      case kExprI32SConvertSatF32: {
        OpIndex truncated = UnOpImpl(decoder, kExprF32Trunc, arg);
        OpIndex converted =
            asm_.TruncateFloat32ToInt32OverflowUndefined(truncated);
        OpIndex converted_back = asm_.ChangeInt32ToFloat32(converted);

        Label<compiler::turboshaft::Word32> done(&asm_);

        IF (LIKELY(asm_.Float32Equal(truncated, converted_back))) {
          GOTO(done, converted);
        }
        ELSE {
          // Overflow.
          IF (asm_.Float32Equal(arg, arg)) {
            // Not NaN.
            IF (asm_.Float32LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done,
                   asm_.Word32Constant(std::numeric_limits<int32_t>::min()));
            }
            ELSE {
              // Positive arg.
              GOTO(done,
                   asm_.Word32Constant(std::numeric_limits<int32_t>::max()));
            }
            END_IF
          }
          ELSE {
            // NaN.
            GOTO(done, asm_.Word32Constant(0));
          }
          END_IF
        }
        END_IF
        BIND(done, result);

        return result;
      }
      case kExprI32UConvertSatF32: {
        OpIndex truncated = UnOpImpl(decoder, kExprF32Trunc, arg);
        OpIndex converted =
            asm_.TruncateFloat32ToUint32OverflowUndefined(truncated);
        OpIndex converted_back = asm_.ChangeUint32ToFloat32(converted);

        Label<compiler::turboshaft::Word32> done(&asm_);

        IF (LIKELY(asm_.Float32Equal(truncated, converted_back))) {
          GOTO(done, converted);
        }
        ELSE {
          // Overflow.
          IF (asm_.Float32Equal(arg, arg)) {
            // Not NaN.
            IF (asm_.Float32LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done, asm_.Word32Constant(0));
            }
            ELSE {
              // Positive arg.
              GOTO(done,
                   asm_.Word32Constant(std::numeric_limits<uint32_t>::max()));
            }
            END_IF
          }
          ELSE {
            // NaN.
            GOTO(done, asm_.Word32Constant(0));
          }
          END_IF
        }
        END_IF
        BIND(done, result);

        return result;
      }
      case kExprI32SConvertSatF64: {
        OpIndex truncated = UnOpImpl(decoder, kExprF64Trunc, arg);
        OpIndex converted =
            asm_.TruncateFloat64ToInt32OverflowUndefined(truncated);
        OpIndex converted_back = asm_.ChangeInt32ToFloat64(converted);

        Label<compiler::turboshaft::Word32> done(&asm_);

        IF (LIKELY(asm_.Float64Equal(truncated, converted_back))) {
          GOTO(done, converted);
        }
        ELSE {
          // Overflow.
          IF (asm_.Float64Equal(arg, arg)) {
            // Not NaN.
            IF (asm_.Float64LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done,
                   asm_.Word32Constant(std::numeric_limits<int32_t>::min()));
            }
            ELSE {
              // Positive arg.
              GOTO(done,
                   asm_.Word32Constant(std::numeric_limits<int32_t>::max()));
            }
            END_IF
          }
          ELSE {
            // NaN.
            GOTO(done, asm_.Word32Constant(0));
          }
          END_IF
        }
        END_IF
        BIND(done, result);

        return result;
      }
      case kExprI32UConvertSatF64: {
        OpIndex truncated = UnOpImpl(decoder, kExprF64Trunc, arg);
        OpIndex converted =
            asm_.TruncateFloat64ToUint32OverflowUndefined(truncated);
        OpIndex converted_back = asm_.ChangeUint32ToFloat64(converted);

        Label<compiler::turboshaft::Word32> done(&asm_);

        IF (LIKELY(asm_.Float64Equal(truncated, converted_back))) {
          GOTO(done, converted);
        }
        ELSE {
          // Overflow.
          IF (asm_.Float64Equal(arg, arg)) {
            // Not NaN.
            IF (asm_.Float64LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done, asm_.Word32Constant(0));
            }
            ELSE {
              // Positive arg.
              GOTO(done,
                   asm_.Word32Constant(std::numeric_limits<uint32_t>::max()));
            }
            END_IF
          }
          ELSE {
            // NaN.
            GOTO(done, asm_.Word32Constant(0));
          }
          END_IF
        }
        END_IF
        BIND(done, result);

        return result;
      }
      case kExprI64SConvertSatF32: {
        OpIndex converted = asm_.TryTruncateFloat32ToInt64(arg);
        Label<compiler::turboshaft::Word64> done(&asm_);

        if (SupportedOperations::sat_conversion_is_safe()) {
          return asm_.Projection(converted, 0,
                                 RegisterRepresentation::Word64());
        }
        IF (LIKELY(asm_.Projection(converted, 1,
                                   RegisterRepresentation::Word32()))) {
          GOTO(done,
               asm_.Projection(converted, 0, RegisterRepresentation::Word64()));
        }
        ELSE {
          // Overflow.
          IF (asm_.Float32Equal(arg, arg)) {
            // Not NaN.
            IF (asm_.Float32LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done,
                   asm_.Word64Constant(std::numeric_limits<int64_t>::min()));
            }
            ELSE {
              // Positive arg.
              GOTO(done,
                   asm_.Word64Constant(std::numeric_limits<int64_t>::max()));
            }
            END_IF
          }
          ELSE {
            // NaN.
            GOTO(done, asm_.Word64Constant(int64_t{0}));
          }
          END_IF
        }
        END_IF
        BIND(done, result);

        return result;
      }
      case kExprI64UConvertSatF32: {
        OpIndex converted = asm_.TryTruncateFloat32ToUint64(arg);
        Label<compiler::turboshaft::Word64> done(&asm_);

        if (SupportedOperations::sat_conversion_is_safe()) {
          return asm_.Projection(converted, 0,
                                 RegisterRepresentation::Word64());
        }

        IF (LIKELY(asm_.Projection(converted, 1,
                                   RegisterRepresentation::Word32()))) {
          GOTO(done,
               asm_.Projection(converted, 0, RegisterRepresentation::Word64()));
        }
        ELSE {
          // Overflow.
          IF (asm_.Float32Equal(arg, arg)) {
            // Not NaN.
            IF (asm_.Float32LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done, asm_.Word64Constant(int64_t{0}));
            }
            ELSE {
              // Positive arg.
              GOTO(done,
                   asm_.Word64Constant(std::numeric_limits<uint64_t>::max()));
            }
            END_IF
          }
          ELSE {
            // NaN.
            GOTO(done, asm_.Word64Constant(int64_t{0}));
          }
          END_IF
        }
        END_IF
        BIND(done, result);

        return result;
      }
      case kExprI64SConvertSatF64: {
        OpIndex converted = asm_.TryTruncateFloat64ToInt64(arg);
        Label<compiler::turboshaft::Word64> done(&asm_);

        if (SupportedOperations::sat_conversion_is_safe()) {
          return asm_.Projection(converted, 0,
                                 RegisterRepresentation::Word64());
        }

        IF (LIKELY(asm_.Projection(converted, 1,
                                   RegisterRepresentation::Word32()))) {
          GOTO(done,
               asm_.Projection(converted, 0, RegisterRepresentation::Word64()));
        }
        ELSE {
          // Overflow.
          IF (asm_.Float64Equal(arg, arg)) {
            // Not NaN.
            IF (asm_.Float64LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done,
                   asm_.Word64Constant(std::numeric_limits<int64_t>::min()));
            }
            ELSE {
              // Positive arg.
              GOTO(done,
                   asm_.Word64Constant(std::numeric_limits<int64_t>::max()));
            }
            END_IF
          }
          ELSE {
            // NaN.
            GOTO(done, asm_.Word64Constant(int64_t{0}));
          }
          END_IF
        }
        END_IF
        BIND(done, result);

        return result;
      }
      case kExprI64UConvertSatF64: {
        OpIndex converted = asm_.TryTruncateFloat64ToUint64(arg);
        Label<compiler::turboshaft::Word64> done(&asm_);

        if (SupportedOperations::sat_conversion_is_safe()) {
          return asm_.Projection(converted, 0,
                                 RegisterRepresentation::Word64());
        }

        IF (LIKELY(asm_.Projection(converted, 1,
                                   RegisterRepresentation::Word32()))) {
          GOTO(done,
               asm_.Projection(converted, 0, RegisterRepresentation::Word64()));
        }
        ELSE {
          // Overflow.
          IF (asm_.Float64Equal(arg, arg)) {
            // Not NaN.
            IF (asm_.Float64LessThan(arg, 0)) {
              // Negative arg.
              GOTO(done, asm_.Word64Constant(int64_t{0}));
            }
            ELSE {
              // Positive arg.
              GOTO(done,
                   asm_.Word64Constant(std::numeric_limits<uint64_t>::max()));
            }
            END_IF
          }
          ELSE {
            // NaN.
            GOTO(done, asm_.Word64Constant(int64_t{0}));
          }
          END_IF
        }
        END_IF
        BIND(done, result);

        return result;
      }
      case kExprF32ConvertF64:
        return asm_.ChangeFloat64ToFloat32(arg);
      case kExprF64ConvertF32:
        return asm_.ChangeFloat32ToFloat64(arg);
      case kExprF32ReinterpretI32:
        return asm_.BitcastWord32ToFloat32(arg);
      case kExprI32ReinterpretF32:
        return asm_.BitcastFloat32ToWord32(arg);
      case kExprI32Clz:
        return asm_.Word32CountLeadingZeros(arg);
      case kExprI32Ctz:
        if (SupportedOperations::word32_ctz()) {
          return asm_.Word32CountTrailingZeros(arg);
        } else {
          // TODO(14108): Use reverse_bits if supported.
          return CallCStackSlotToInt32(arg,
                                       ExternalReference::wasm_word32_ctz(),
                                       MemoryRepresentation::Int32());
        }
      case kExprI32Popcnt:
        if (SupportedOperations::word32_popcnt()) {
          return asm_.Word32PopCount(arg);
        } else {
          return CallCStackSlotToInt32(arg,
                                       ExternalReference::wasm_word32_popcnt(),
                                       MemoryRepresentation::Int32());
        }
      case kExprF32Floor:
        if (SupportedOperations::float32_round_down()) {
          return asm_.Float32RoundDown(arg);
        } else {
          return CallCStackSlotToStackSlot(arg,
                                           ExternalReference::wasm_f32_floor(),
                                           MemoryRepresentation::Float32());
        }
      case kExprF32Ceil:
        if (SupportedOperations::float32_round_up()) {
          return asm_.Float32RoundUp(arg);
        } else {
          return CallCStackSlotToStackSlot(arg,
                                           ExternalReference::wasm_f32_ceil(),
                                           MemoryRepresentation::Float32());
        }
      case kExprF32Trunc:
        if (SupportedOperations::float32_round_to_zero()) {
          return asm_.Float32RoundToZero(arg);
        } else {
          return CallCStackSlotToStackSlot(arg,
                                           ExternalReference::wasm_f32_trunc(),
                                           MemoryRepresentation::Float32());
        }
      case kExprF32NearestInt:
        if (SupportedOperations::float32_round_ties_even()) {
          return asm_.Float32RoundTiesEven(arg);
        } else {
          return CallCStackSlotToStackSlot(
              arg, ExternalReference::wasm_f32_nearest_int(),
              MemoryRepresentation::Float32());
        }
      case kExprF64Floor:
        if (SupportedOperations::float64_round_down()) {
          return asm_.Float64RoundDown(arg);
        } else {
          return CallCStackSlotToStackSlot(arg,
                                           ExternalReference::wasm_f64_floor(),
                                           MemoryRepresentation::Float64());
        }
      case kExprF64Ceil:
        if (SupportedOperations::float64_round_up()) {
          return asm_.Float64RoundUp(arg);
        } else {
          return CallCStackSlotToStackSlot(arg,
                                           ExternalReference::wasm_f64_ceil(),
                                           MemoryRepresentation::Float64());
        }
      case kExprF64Trunc:
        if (SupportedOperations::float64_round_to_zero()) {
          return asm_.Float64RoundToZero(arg);
        } else {
          return CallCStackSlotToStackSlot(arg,
                                           ExternalReference::wasm_f64_trunc(),
                                           MemoryRepresentation::Float64());
        }
      case kExprF64NearestInt:
        if (SupportedOperations::float64_round_ties_even()) {
          return asm_.Float64RoundTiesEven(arg);
        } else {
          return CallCStackSlotToStackSlot(
              arg, ExternalReference::wasm_f64_nearest_int(),
              MemoryRepresentation::Float64());
        }
      case kExprF64Acos:
        return CallCStackSlotToStackSlot(
            arg, ExternalReference::f64_acos_wrapper_function(),
            MemoryRepresentation::Float64());
      case kExprF64Asin:
        return CallCStackSlotToStackSlot(
            arg, ExternalReference::f64_asin_wrapper_function(),
            MemoryRepresentation::Float64());
      case kExprF64Atan:
        return asm_.Float64Atan(arg);
      case kExprF64Cos:
        return asm_.Float64Cos(arg);
      case kExprF64Sin:
        return asm_.Float64Sin(arg);
      case kExprF64Tan:
        return asm_.Float64Tan(arg);
      case kExprF64Exp:
        return asm_.Float64Exp(arg);
      case kExprF64Log:
        return asm_.Float64Log(arg);
      case kExprI32ConvertI64:
        // Implicit in Turboshaft.
        return arg;
      case kExprI64SConvertI32:
        return asm_.ChangeInt32ToInt64(arg);
      case kExprI64UConvertI32:
        return asm_.ChangeUint32ToUint64(arg);
      case kExprF64ReinterpretI64:
        return asm_.BitcastWord64ToFloat64(arg);
      case kExprI64ReinterpretF64:
        return asm_.BitcastFloat64ToWord64(arg);
      case kExprI64Clz:
        return asm_.Word64CountLeadingZeros(arg);
      case kExprI64Ctz:
        if (SupportedOperations::word64_ctz()) {
          return asm_.Word64CountTrailingZeros(arg);
        } else {
          // TODO(14108): Use reverse_bits if supported.
          return asm_.ChangeUint32ToUint64(
              CallCStackSlotToInt32(arg, ExternalReference::wasm_word64_ctz(),
                                    MemoryRepresentation::Int64()));
        }
      case kExprI64Popcnt:
        if (SupportedOperations::word64_popcnt()) {
          return asm_.Word64PopCount(arg);
        } else {
          return asm_.ChangeUint32ToUint64(CallCStackSlotToInt32(
              arg, ExternalReference::wasm_word64_popcnt(),
              MemoryRepresentation::Int64()));
        }
      case kExprI64Eqz:
        return asm_.Word64Equal(arg, 0);
      case kExprF32SConvertI64:
        return asm_.ChangeInt64ToFloat32(arg);
      case kExprF32UConvertI64:
        return asm_.ChangeUint64ToFloat32(arg);
      case kExprF64SConvertI64:
        return asm_.ChangeInt64ToFloat64(arg);
      case kExprF64UConvertI64:
        return asm_.ChangeUint64ToFloat64(arg);
      case kExprI32SExtendI8:
        return asm_.Word32SignExtend8(arg);
      case kExprI32SExtendI16:
        return asm_.Word32SignExtend16(arg);
      case kExprI64SExtendI8:
        return asm_.Word64SignExtend8(arg);
      case kExprI64SExtendI16:
        return asm_.Word64SignExtend16(arg);
      case kExprI64SExtendI32:
        // TODO(14108): Is this correct?
        return asm_.ChangeInt32ToInt64(arg);
      case kExprI32AsmjsLoadMem8S:
      case kExprI32AsmjsLoadMem8U:
      case kExprI32AsmjsLoadMem16S:
      case kExprI32AsmjsLoadMem16U:
      case kExprI32AsmjsLoadMem:
      case kExprF32AsmjsLoadMem:
      case kExprF64AsmjsLoadMem:
      case kExprI32AsmjsSConvertF32:
      case kExprI32AsmjsUConvertF32:
      case kExprI32AsmjsSConvertF64:
      case kExprI32AsmjsUConvertF64:
      case kExprRefIsNull:
      case kExprRefAsNonNull:
      case kExprExternInternalize:
      case kExprExternExternalize:
        Bailout(decoder);
        return OpIndex::Invalid();
      default:
        UNREACHABLE();
    }
  }

  // TODO(14108): Implement 64-bit divisions on 32-bit platforms.
  OpIndex BinOpImpl(FullDecoder* decoder, WasmOpcode opcode, OpIndex lhs,
                    OpIndex rhs) {
    switch (opcode) {
      case kExprI32Add:
        return asm_.Word32Add(lhs, rhs);
      case kExprI32Sub:
        return asm_.Word32Sub(lhs, rhs);
      case kExprI32Mul:
        return asm_.Word32Mul(lhs, rhs);
      case kExprI32DivS: {
        asm_.TrapIf(asm_.Word32Equal(rhs, 0), OpIndex::Invalid(),
                    compiler::TrapId::kTrapDivByZero);
        V<Word32> unrepresentable_condition = asm_.Word32BitwiseAnd(
            asm_.Word32Equal(rhs, -1), asm_.Word32Equal(lhs, kMinInt));
        asm_.TrapIf(unrepresentable_condition, OpIndex::Invalid(),
                    compiler::TrapId::kTrapDivUnrepresentable);
        return asm_.Int32Div(lhs, rhs);
      }
      case kExprI32DivU:
        asm_.TrapIf(asm_.Word32Equal(rhs, 0), OpIndex::Invalid(),
                    compiler::TrapId::kTrapDivByZero);
        return asm_.Uint32Div(lhs, rhs);
      case kExprI32RemS: {
        asm_.TrapIf(asm_.Word32Equal(rhs, 0), OpIndex::Invalid(),
                    compiler::TrapId::kTrapRemByZero);
        TSBlock* denom_minus_one = asm_.NewBlock();
        TSBlock* otherwise = asm_.NewBlock();
        TSBlock* merge = asm_.NewBlock();
        ConditionWithHint condition(asm_.Word32Equal(rhs, -1),
                                    BranchHint::kFalse);
        asm_.Branch(condition, denom_minus_one, otherwise);
        asm_.Bind(denom_minus_one);
        OpIndex zero = asm_.Word32Constant(0);
        asm_.Goto(merge);
        asm_.Bind(otherwise);
        OpIndex mod = asm_.Int32Mod(lhs, rhs);
        asm_.Goto(merge);
        asm_.Bind(merge);
        return asm_.Phi({zero, mod}, RepresentationFor(decoder, kWasmI32));
      }
      case kExprI32RemU:
        asm_.TrapIf(asm_.Word32Equal(rhs, 0), OpIndex::Invalid(),
                    compiler::TrapId::kTrapRemByZero);
        return asm_.Uint32Mod(lhs, rhs);
      case kExprI32And:
        return asm_.Word32BitwiseAnd(lhs, rhs);
      case kExprI32Ior:
        return asm_.Word32BitwiseOr(lhs, rhs);
      case kExprI32Xor:
        return asm_.Word32BitwiseXor(lhs, rhs);
      case kExprI32Shl:
        // If possible, the bitwise-and gets optimized away later.
        return asm_.Word32ShiftLeft(lhs, asm_.Word32BitwiseAnd(rhs, 0x1f));
      case kExprI32ShrS:
        return asm_.Word32ShiftRightArithmetic(
            lhs, asm_.Word32BitwiseAnd(rhs, 0x1f));
      case kExprI32ShrU:
        return asm_.Word32ShiftRightLogical(lhs,
                                            asm_.Word32BitwiseAnd(rhs, 0x1f));
      case kExprI32Ror:
        return asm_.Word32RotateRight(lhs, asm_.Word32BitwiseAnd(rhs, 0x1f));
      case kExprI32Rol:
        if (SupportedOperations::word32_rol()) {
          return asm_.Word32RotateLeft(lhs, asm_.Word32BitwiseAnd(rhs, 0x1f));
        } else {
          return asm_.Word32RotateRight(
              lhs, asm_.Word32Sub(32, asm_.Word32BitwiseAnd(rhs, 0x1f)));
        }
      case kExprI32Eq:
        return asm_.Word32Equal(lhs, rhs);
      case kExprI32Ne:
        return asm_.Word32Equal(asm_.Word32Equal(lhs, rhs), 0);
      case kExprI32LtS:
        return asm_.Int32LessThan(lhs, rhs);
      case kExprI32LeS:
        return asm_.Int32LessThanOrEqual(lhs, rhs);
      case kExprI32LtU:
        return asm_.Uint32LessThan(lhs, rhs);
      case kExprI32LeU:
        return asm_.Uint32LessThanOrEqual(lhs, rhs);
      case kExprI32GtS:
        return asm_.Int32LessThan(rhs, lhs);
      case kExprI32GeS:
        return asm_.Int32LessThanOrEqual(rhs, lhs);
      case kExprI32GtU:
        return asm_.Uint32LessThan(rhs, lhs);
      case kExprI32GeU:
        return asm_.Uint32LessThanOrEqual(rhs, lhs);
      case kExprI64Add:
        return asm_.Word64Add(lhs, rhs);
      case kExprI64Sub:
        return asm_.Word64Sub(lhs, rhs);
      case kExprI64Mul:
        return asm_.Word64Mul(lhs, rhs);
      case kExprI64DivS: {
        asm_.TrapIf(asm_.Word64Equal(rhs, 0), OpIndex::Invalid(),
                    compiler::TrapId::kTrapDivByZero);
        OpIndex unrepresentable_condition = asm_.Word32BitwiseAnd(
            asm_.Word64Equal(rhs, -1),
            asm_.Word64Equal(lhs, std::numeric_limits<int64_t>::min()));
        asm_.TrapIf(unrepresentable_condition, OpIndex::Invalid(),
                    compiler::TrapId::kTrapDivUnrepresentable);
        return asm_.Int64Div(lhs, rhs);
      }
      case kExprI64DivU:
        asm_.TrapIf(asm_.Word64Equal(rhs, 0), OpIndex::Invalid(),
                    compiler::TrapId::kTrapDivByZero);
        return asm_.Uint64Div(lhs, rhs);
      case kExprI64RemS: {
        asm_.TrapIf(asm_.Word64Equal(rhs, 0), OpIndex::Invalid(),
                    compiler::TrapId::kTrapRemByZero);
        TSBlock* denom_minus_one = asm_.NewBlock();
        TSBlock* otherwise = asm_.NewBlock();
        TSBlock* merge = asm_.NewBlock();
        ConditionWithHint condition(asm_.Word64Equal(rhs, -1),
                                    BranchHint::kFalse);
        asm_.Branch(condition, denom_minus_one, otherwise);
        asm_.Bind(denom_minus_one);
        OpIndex zero = asm_.Word64Constant(int64_t{0});
        asm_.Goto(merge);
        asm_.Bind(otherwise);
        OpIndex mod = asm_.Int64Mod(lhs, rhs);
        asm_.Goto(merge);
        asm_.Bind(merge);
        return asm_.Phi({zero, mod}, RepresentationFor(decoder, kWasmI64));
      }
      case kExprI64RemU:
        asm_.TrapIf(asm_.Word64Equal(rhs, 0), OpIndex::Invalid(),
                    compiler::TrapId::kTrapRemByZero);
        return asm_.Uint64Mod(lhs, rhs);
      case kExprI64And:
        return asm_.Word64BitwiseAnd(lhs, rhs);
      case kExprI64Ior:
        return asm_.Word64BitwiseOr(lhs, rhs);
      case kExprI64Xor:
        return asm_.Word64BitwiseXor(lhs, rhs);
      case kExprI64Shl:
        // If possible, the bitwise-and gets optimized away later.
        return asm_.Word64ShiftLeft(lhs, asm_.Word64BitwiseAnd(rhs, 0x3f));
      case kExprI64ShrS:
        return asm_.Word64ShiftRightArithmetic(
            lhs, asm_.Word64BitwiseAnd(rhs, 0x3f));
      case kExprI64ShrU:
        return asm_.Word64ShiftRightLogical(lhs,
                                            asm_.Word64BitwiseAnd(rhs, 0x3f));
      case kExprI64Ror:
        return asm_.Word64RotateRight(lhs, asm_.Word64BitwiseAnd(rhs, 0x3f));
      case kExprI64Rol:
        if (SupportedOperations::word64_rol()) {
          return asm_.Word64RotateLeft(lhs, asm_.Word64BitwiseAnd(rhs, 0x3f));
        } else {
          return asm_.Word64RotateRight(
              lhs, asm_.Word64Sub(64, asm_.Word64BitwiseAnd(rhs, 0x3f)));
        }
      case kExprI64Eq:
        return asm_.Word64Equal(lhs, rhs);
      case kExprI64Ne:
        return asm_.Word32Equal(asm_.Word64Equal(lhs, rhs), 0);
      case kExprI64LtS:
        return asm_.Int64LessThan(lhs, rhs);
      case kExprI64LeS:
        return asm_.Int64LessThanOrEqual(lhs, rhs);
      case kExprI64LtU:
        return asm_.Uint64LessThan(lhs, rhs);
      case kExprI64LeU:
        return asm_.Uint64LessThanOrEqual(lhs, rhs);
      case kExprI64GtS:
        return asm_.Int64LessThan(rhs, lhs);
      case kExprI64GeS:
        return asm_.Int64LessThanOrEqual(rhs, lhs);
      case kExprI64GtU:
        return asm_.Uint64LessThan(rhs, lhs);
      case kExprI64GeU:
        return asm_.Uint64LessThanOrEqual(rhs, lhs);
      case kExprF32CopySign: {
        V<Word32> lhs_without_sign =
            asm_.Word32BitwiseAnd(asm_.BitcastFloat32ToWord32(lhs), 0x7fffffff);
        V<Word32> rhs_sign =
            asm_.Word32BitwiseAnd(asm_.BitcastFloat32ToWord32(rhs), 0x80000000);
        return asm_.BitcastWord32ToFloat32(
            asm_.Word32BitwiseOr(lhs_without_sign, rhs_sign));
      }
      case kExprF32Add:
        return asm_.Float32Add(lhs, rhs);
      case kExprF32Sub:
        return asm_.Float32Sub(lhs, rhs);
      case kExprF32Mul:
        return asm_.Float32Mul(lhs, rhs);
      case kExprF32Div:
        return asm_.Float32Div(lhs, rhs);
      case kExprF32Eq:
        return asm_.Float32Equal(lhs, rhs);
      case kExprF32Ne:
        return asm_.Word32Equal(asm_.Float32Equal(lhs, rhs), 0);
      case kExprF32Lt:
        return asm_.Float32LessThan(lhs, rhs);
      case kExprF32Le:
        return asm_.Float32LessThanOrEqual(lhs, rhs);
      case kExprF32Gt:
        return asm_.Float32LessThan(rhs, lhs);
      case kExprF32Ge:
        return asm_.Float32LessThanOrEqual(rhs, lhs);
      case kExprF32Min:
        return asm_.Float32Min(rhs, lhs);
      case kExprF32Max:
        return asm_.Float32Max(rhs, lhs);
      case kExprF64CopySign: {
        V<Word64> lhs_without_sign = asm_.Word64BitwiseAnd(
            asm_.BitcastFloat64ToWord64(lhs), 0x7fffffffffffffff);
        V<Word64> rhs_sign = asm_.Word64BitwiseAnd(
            asm_.BitcastFloat64ToWord64(rhs), 0x8000000000000000);
        return asm_.BitcastWord64ToFloat64(
            asm_.Word64BitwiseOr(lhs_without_sign, rhs_sign));
      }
      case kExprF64Add:
        return asm_.Float64Add(lhs, rhs);
      case kExprF64Sub:
        return asm_.Float64Sub(lhs, rhs);
      case kExprF64Mul:
        return asm_.Float64Mul(lhs, rhs);
      case kExprF64Div:
        return asm_.Float64Div(lhs, rhs);
      case kExprF64Eq:
        return asm_.Float64Equal(lhs, rhs);
      case kExprF64Ne:
        return asm_.Word32Equal(asm_.Float64Equal(lhs, rhs), 0);
      case kExprF64Lt:
        return asm_.Float64LessThan(lhs, rhs);
      case kExprF64Le:
        return asm_.Float64LessThanOrEqual(lhs, rhs);
      case kExprF64Gt:
        return asm_.Float64LessThan(rhs, lhs);
      case kExprF64Ge:
        return asm_.Float64LessThanOrEqual(rhs, lhs);
      case kExprF64Min:
        return asm_.Float64Min(lhs, rhs);
      case kExprF64Max:
        return asm_.Float64Max(lhs, rhs);
      case kExprF64Pow:
        return asm_.Float64Power(lhs, rhs);
      case kExprF64Atan2:
        return asm_.Float64Atan2(lhs, rhs);
      case kExprF64Mod:
        return CallCStackSlotToStackSlot(
            lhs, rhs, ExternalReference::f64_mod_wrapper_function(),
            MemoryRepresentation::Float64());
      case kExprRefEq:
        return asm_.TaggedEqual(lhs, rhs);
      case kExprI32AsmjsDivS:
      case kExprI32AsmjsDivU:
      case kExprI32AsmjsRemS:
      case kExprI32AsmjsRemU:
      case kExprI32AsmjsStoreMem8:
      case kExprI32AsmjsStoreMem16:
      case kExprI32AsmjsStoreMem:
      case kExprF32AsmjsStoreMem:
      case kExprF64AsmjsStoreMem:
        Bailout(decoder);
        return OpIndex::Invalid();
      default:
        UNREACHABLE();
    }
  }

  void StackCheck() {
    if (V8_UNLIKELY(!v8_flags.wasm_stack_checks)) return;
    OpIndex limit_address =
        LOAD_INSTANCE_FIELD(StackLimitAddress, PointerSized);
    OpIndex limit = asm_.Load(limit_address, LoadOp::Kind::RawAligned(),
                              MemoryRepresentation::PointerSized(), 0);
    OpIndex check =
        asm_.StackPointerGreaterThan(limit, compiler::StackCheckKind::kWasm);
    TSBlock* continuation = asm_.NewBlock();
    TSBlock* call_builtin = asm_.NewBlock();
    asm_.Branch(ConditionWithHint(check, BranchHint::kTrue), continuation,
                call_builtin);

    // TODO(14108): Cache descriptor.
    asm_.Bind(call_builtin);
    OpIndex builtin = asm_.RelocatableConstant(WasmCode::kWasmStackGuard,
                                               RelocInfo::WASM_STUB_CALL);
    const CallDescriptor* call_descriptor =
        compiler::Linkage::GetStubCallDescriptor(
            asm_.graph_zone(),                    // zone
            NoContextDescriptor{},                // descriptor
            0,                                    // stack parameter count
            CallDescriptor::kNoFlags,             // flags
            compiler::Operator::kNoProperties,    // properties
            StubCallMode::kCallWasmRuntimeStub);  // stub call mode
    const TSCallDescriptor* ts_call_descriptor =
        TSCallDescriptor::Create(call_descriptor, asm_.graph_zone());
    asm_.Call(builtin, {}, ts_call_descriptor);
    asm_.Goto(continuation);

    asm_.Bind(continuation);
  }

  void BuildWasmCall(FullDecoder* decoder, const FunctionSig* sig,
                     OpIndex callee, OpIndex ref, const Value args[],
                     Value returns[]) {
    const TSCallDescriptor* descriptor = TSCallDescriptor::Create(
        compiler::GetWasmCallDescriptor(asm_.graph_zone(), sig),
        asm_.graph_zone());

    std::vector<OpIndex> arg_indices(sig->parameter_count() + 1);
    arg_indices[0] = ref;
    for (uint32_t i = 0; i < sig->parameter_count(); i++) {
      arg_indices[i + 1] = args[i].op;
    }

    OpIndex call = asm_.Call(callee, OpIndex::Invalid(),
                             base::VectorOf(arg_indices), descriptor);

    if (sig->return_count() == 1) {
      returns[0].op = call;
    } else if (sig->return_count() > 1) {
      for (uint32_t i = 0; i < sig->return_count(); i++) {
        returns[i].op = asm_.Projection(
            call, i, RepresentationFor(decoder, sig->GetReturn(i)));
      }
    }
  }

  OpIndex CallRuntime(Runtime::FunctionId f, base::Vector<OpIndex> args) {
    const Runtime::Function* fun = Runtime::FunctionForId(f);
    OpIndex isolate_root = asm_.LoadRootRegister();
    DCHECK_EQ(1, fun->result_size);
    int builtin_slot_offset = IsolateData::BuiltinSlotOffset(
        Builtin::kCEntry_Return1_ArgvOnStack_NoBuiltinExit);
    OpIndex centry_stub =
        asm_.Load(isolate_root, LoadOp::Kind::RawAligned(),
                  MemoryRepresentation::PointerSized(), builtin_slot_offset);
    base::SmallVector<OpIndex, 8> centry_args;
    for (OpIndex arg : args) centry_args.emplace_back(arg);
    centry_args.emplace_back(
        asm_.ExternalConstant(ExternalReference::Create(f)));
    centry_args.emplace_back(asm_.Word32Constant(fun->nargs));
    centry_args.emplace_back(asm_.NoContextConstant());  // js_context
    const CallDescriptor* call_descriptor =
        compiler::Linkage::GetRuntimeCallDescriptor(
            asm_.graph_zone(), f, fun->nargs, compiler::Operator::kNoProperties,
            CallDescriptor::kNoFlags);
    const TSCallDescriptor* ts_call_descriptor =
        TSCallDescriptor::Create(call_descriptor, asm_.graph_zone());
    return asm_.Call(centry_stub, OpIndex::Invalid(),
                     base::VectorOf(centry_args), ts_call_descriptor);
  }

  OpIndex CallC(const MachineSignature* sig, ExternalReference ref,
                base::Vector<OpIndex> args) {
    DCHECK_LE(sig->return_count(), 1);
    const CallDescriptor* call_descriptor =
        compiler::Linkage::GetSimplifiedCDescriptor(asm_.graph_zone(), sig);
    const TSCallDescriptor* ts_call_descriptor =
        TSCallDescriptor::Create(call_descriptor, asm_.graph_zone());
    return asm_.Call(asm_.ExternalConstant(ref), OpIndex::Invalid(), args,
                     ts_call_descriptor);
  }

  OpIndex CallC(const MachineSignature* sig, ExternalReference ref,
                OpIndex* arg) {
    return CallC(sig, ref, base::VectorOf(arg, 1));
  }

  OpIndex CallCStackSlotToInt32(OpIndex arg, ExternalReference ref,
                                MemoryRepresentation arg_type) {
    OpIndex stack_slot_param =
        asm_.StackSlot(arg_type.SizeInBytes(), arg_type.SizeInBytes());
    asm_.Store(stack_slot_param, arg, StoreOp::Kind::RawAligned(), arg_type,
               compiler::WriteBarrierKind::kNoWriteBarrier);
    MachineType reps[]{MachineType::Int32(), MachineType::Pointer()};
    MachineSignature sig(1, 1, reps);
    return CallC(&sig, ref, &stack_slot_param);
  }

  OpIndex CallCStackSlotToStackSlot(OpIndex arg, ExternalReference ref,
                                    MemoryRepresentation arg_type) {
    OpIndex stack_slot =
        asm_.StackSlot(arg_type.SizeInBytes(), arg_type.SizeInBytes());
    asm_.Store(stack_slot, arg, StoreOp::Kind::RawAligned(), arg_type,
               compiler::WriteBarrierKind::kNoWriteBarrier);
    MachineType reps[]{MachineType::Pointer()};
    MachineSignature sig(0, 1, reps);
    CallC(&sig, ref, &stack_slot);
    return asm_.Load(stack_slot, LoadOp::Kind::RawAligned(), arg_type);
  }

  OpIndex CallCStackSlotToStackSlot(OpIndex arg0, OpIndex arg1,
                                    ExternalReference ref,
                                    MemoryRepresentation arg_type) {
    OpIndex stack_slot =
        asm_.StackSlot(2 * arg_type.SizeInBytes(), arg_type.SizeInBytes());
    asm_.Store(stack_slot, arg0, StoreOp::Kind::RawAligned(), arg_type,
               compiler::WriteBarrierKind::kNoWriteBarrier);
    asm_.Store(stack_slot, arg1, StoreOp::Kind::RawAligned(), arg_type,
               compiler::WriteBarrierKind::kNoWriteBarrier,
               arg_type.SizeInBytes());
    MachineType reps[]{MachineType::Pointer()};
    MachineSignature sig(0, 1, reps);
    CallC(&sig, ref, &stack_slot);
    return asm_.Load(stack_slot, LoadOp::Kind::RawAligned(), arg_type);
  }

  OpIndex WasmPositionToOpIndex(WasmCodePosition position) {
    return OpIndex(sizeof(compiler::turboshaft::OperationStorageSlot) *
                   static_cast<int>(position));
  }

  WasmCodePosition OpIndexToWasmPosition(OpIndex index) {
    return index.valid()
               ? static_cast<WasmCodePosition>(
                     index.offset() /
                     sizeof(compiler::turboshaft::OperationStorageSlot))
               : kNoCodePosition;
  }

  Assembler& Asm() { return asm_; }

  OpIndex instance_node_;
  std::unordered_map<TSBlock*, BlockPhis> block_phis_;
  Assembler asm_;
  std::vector<OpIndex> ssa_env_;
  bool did_bailout_ = false;
};

V8_EXPORT_PRIVATE bool BuildTSGraph(AccountingAllocator* allocator,
                                    const WasmFeatures& enabled,
                                    const WasmModule* module,
                                    WasmFeatures* detected,
                                    const FunctionBody& body, Graph& graph,
                                    compiler::NodeOriginTable* node_origins) {
  Zone zone(allocator, ZONE_NAME);
  WasmFullDecoder<Decoder::FullValidationTag, TurboshaftGraphBuildingInterface>
      decoder(&zone, module, enabled, detected, body, graph, &zone,
              node_origins);
  decoder.Decode();
  // Turboshaft runs with validation, but the function should already be
  // validated, so graph building must always succeed, unless we bailed out.
  DCHECK_IMPLIES(!decoder.ok(), decoder.interface().did_bailout());
  return decoder.ok();
}

#undef LOAD_INSTANCE_FIELD
#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::wasm
