// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_MEMORY_OPTIMIZATION_H_
#define V8_COMPILER_TURBOSHAFT_MEMORY_OPTIMIZATION_H_

#include "src/base/template-utils.h"
#include "src/builtins/builtins.h"
#include "src/codegen/external-reference.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/utils.h"

namespace v8::internal::compiler::turboshaft {

const TSCallDescriptor* CreateAllocateBuiltinDescriptor(Zone* zone);

// The main purpose of memory optimization is folding multiple allocations into
// one. For this, the first allocation reserves additional space, that is
// consumed by subsequent allocations, which only move the allocation top
// pointer and are therefore guaranteed to succeed. Another nice side-effect of
// allocation folding is that more stores are performed on the most recent
// allocation, which allows us to eliminate the write barrier for the store.
//
// This analysis works by keeping track of the most recent non-folded
// allocation, as well as the number of bytes this allocation needs to reserve
// to satisfy all subsequent allocations.
// We can do write barrier elimination across loops if the loop does not contain
// any potentially allocating operations.
template <class Assembler>
struct MemoryAnalyzer {
  Zone* phase_zone;
  const Graph& input_graph;
  Assembler* assembler;
  MemoryAnalyzer(Zone* phase_zone, const Graph& input_graph,
                 Assembler* assembler)
      : phase_zone(phase_zone),
        input_graph(input_graph),
        assembler(assembler) {}

  struct BlockState {
    const AllocateOp* last_allocation = nullptr;
    base::Optional<uint32_t> reserved_size = base::nullopt;

    bool operator!=(const BlockState& other) {
      return last_allocation != other.last_allocation ||
             reserved_size != other.reserved_size;
    }
  };
  FixedSidetable<base::Optional<BlockState>, BlockIndex> block_states{
      input_graph.block_count(), phase_zone};
  ZoneUnorderedMap<const AllocateOp*, const AllocateOp*> folded_into{
      phase_zone};
  ZoneUnorderedSet<OpIndex> skipped_write_barriers{phase_zone};
  ZoneUnorderedMap<const AllocateOp*, uint32_t> reserved_size{phase_zone};
  BlockIndex current_block = BlockIndex(0);
  BlockState state;

  bool SkipWriteBarrier(const Operation& object) {
    if (ShouldSkipOptimizationStep()) return false;
    if (state.last_allocation == nullptr ||
        state.last_allocation->type != AllocationType::kYoung) {
      return false;
    }
    if (state.last_allocation == &object) {
      return true;
    }
    if (!object.Is<AllocateOp>()) return false;
    auto it = folded_into.find(&object.Cast<AllocateOp>());
    return it != folded_into.end() && it->second == state.last_allocation;
  }

  bool IsFoldedAllocation(OpIndex op) {
    return folded_into.count(
        input_graph.Get(op).template TryCast<AllocateOp>());
  }

  base::Optional<uint32_t> ReservedSize(OpIndex alloc) {
    if (auto it = reserved_size.find(
            input_graph.Get(alloc).template TryCast<AllocateOp>());
        it != reserved_size.end()) {
      return it->second;
    }
    return base::nullopt;
  }

  void Run();

  void Process(const Operation& op);
  void ProcessBlockTerminator(const Operation& op);
  void ProcessAllocation(const AllocateOp& alloc);
  void ProcessStore(OpIndex store, OpIndex object);
  void MergeCurrentStateIntoSuccessor(const Block* successor);
};

struct MemoryOptimizationReducerArgs {
  Isolate* isolate;
};

template <class Next>
class MemoryOptimizationReducer : public Next {
 public:
  using Next::Asm;
  using ArgT = base::append_tuple_type<typename Next::ArgT,
                                       MemoryOptimizationReducerArgs>;

  template <class... Args>
  explicit MemoryOptimizationReducer(const std::tuple<Args...>& args)
      : Next(args),
        isolate_(std::get<MemoryOptimizationReducerArgs>(args).isolate) {}

  void Analyze() {
    analyzer_.emplace(Asm().phase_zone(), Asm().input_graph(), &Asm());
    analyzer_->Run();
    Next::Analyze();
  }

  OpIndex ReduceStore(OpIndex base, OpIndex index, OpIndex value,
                      StoreOp::Kind kind, MemoryRepresentation stored_rep,
                      WriteBarrierKind write_barrier, int32_t offset,
                      uint8_t element_scale) {
    if (!ShouldSkipOptimizationStep() &&
        analyzer_->skipped_write_barriers.count(
            Asm().current_operation_origin())) {
      write_barrier = WriteBarrierKind::kNoWriteBarrier;
    }
    return Next::ReduceStore(base, index, value, kind, stored_rep,
                             write_barrier, offset, element_scale);
  }

  OpIndex ReduceAllocate(OpIndex size, AllocationType type,
                         AllowLargeObjects allow_large_objects) {
    DCHECK_EQ(type, any_of(AllocationType::kYoung, AllocationType::kOld));

    if (v8_flags.single_generation && type == AllocationType::kYoung) {
      type = AllocationType::kOld;
    }

    OpIndex top_address = Asm().ExternalConstant(
        type == AllocationType::kYoung
            ? ExternalReference::new_space_allocation_top_address(isolate_)
            : ExternalReference::old_space_allocation_top_address(isolate_));
    Variable top =
        Asm().NewFreshVariable(RegisterRepresentation::PointerSized());
    Asm().Set(top, Asm().LoadOffHeap(top_address,
                                     MemoryRepresentation::PointerSized()));

    if (analyzer_->IsFoldedAllocation(Asm().current_operation_origin())) {
      Asm().StoreOffHeap(top_address, Asm().PointerAdd(Asm().Get(top), size),
                         MemoryRepresentation::PointerSized());
      return Asm().BitcastWordToTagged(Asm().PointerAdd(
          Asm().Get(top), Asm().IntPtrConstant(kHeapObjectTag)));
    }

    OpIndex allocate_builtin;
    if (type == AllocationType::kYoung) {
      if (allow_large_objects == AllowLargeObjects::kTrue) {
        allocate_builtin =
            Asm().BuiltinCode(Builtin::kAllocateInYoungGeneration, isolate_);
      } else {
        allocate_builtin = Asm().BuiltinCode(
            Builtin::kAllocateRegularInYoungGeneration, isolate_);
      }
    } else {
      if (allow_large_objects == AllowLargeObjects::kTrue) {
        allocate_builtin =
            Asm().BuiltinCode(Builtin::kAllocateInOldGeneration, isolate_);
      } else {
        allocate_builtin = Asm().BuiltinCode(
            Builtin::kAllocateRegularInOldGeneration, isolate_);
      }
    }

    Block* call_runtime = Asm().NewBlock();
    Block* done = Asm().NewBlock();

    OpIndex limit_address = Asm().ExternalConstant(
        type == AllocationType::kYoung
            ? ExternalReference::new_space_allocation_limit_address(isolate_)
            : ExternalReference::old_space_allocation_limit_address(isolate_));
    OpIndex limit =
        Asm().LoadOffHeap(limit_address, MemoryRepresentation::PointerSized());

    OpIndex reservation_size;
    if (auto c = analyzer_->ReservedSize(Asm().current_operation_origin())) {
      reservation_size = Asm().UintPtrConstant(*c);
    } else {
      reservation_size = size;
    }
    // Check if we can do bump pointer allocation here.
    bool reachable = true;
    if (allow_large_objects == AllowLargeObjects::kTrue) {
      reachable = Asm().GotoIfNot(
          Asm().UintPtrLessThan(
              size, Asm().IntPtrConstant(kMaxRegularHeapObjectSize)),
          call_runtime, BranchHint::kTrue);
    }
    if (reachable) {
      Asm().Branch(
          Asm().UintPtrLessThan(
              Asm().PointerAdd(Asm().Get(top), reservation_size), limit),
          done, call_runtime, BranchHint::kTrue);
    }

    // Call the runtime if bump pointer area exhausted.
    if (Asm().Bind(call_runtime)) {
      OpIndex allocated = Asm().Call(allocate_builtin, {reservation_size},
                                     AllocateBuiltinDescriptor());
      Asm().Set(top, Asm().PointerSub(Asm().BitcastTaggedToWord(allocated),
                                      Asm().IntPtrConstant(kHeapObjectTag)));
      Asm().Goto(done);
    }

    Asm().BindReachable(done);
    // Compute the new top and write it back.
    Asm().StoreOffHeap(top_address, Asm().PointerAdd(Asm().Get(top), size),
                       MemoryRepresentation::PointerSized());
    return Asm().BitcastWordToTagged(
        Asm().PointerAdd(Asm().Get(top), Asm().IntPtrConstant(kHeapObjectTag)));
  }

  OpIndex ReduceDecodeExternalPointer(OpIndex handle, ExternalPointerTag tag) {
#ifdef V8_ENABLE_SANDBOX
    // Decode loaded external pointer.
    //
    // Here we access the external pointer table through an ExternalReference.
    // Alternatively, we could also hardcode the address of the table since it
    // is never reallocated. However, in that case we must be able to guarantee
    // that the generated code is never executed under a different Isolate, as
    // that would allow access to external objects from different Isolates. It
    // also would break if the code is serialized/deserialized at some point.
    OpIndex table_address =
        IsSharedExternalPointerType(tag)
            ? Asm().LoadOffHeap(
                  Asm().ExternalConstant(
                      ExternalReference::
                          shared_external_pointer_table_address_address(
                              isolate_)),
                  MemoryRepresentation::PointerSized())
            : Asm().ExternalConstant(
                  ExternalReference::external_pointer_table_address(isolate_));
    OpIndex table = Asm().LoadOffHeap(
        table_address, Internals::kExternalPointerTableBufferOffset,
        MemoryRepresentation::PointerSized());
    OpIndex index = Asm().ShiftRightLogical(handle, kExternalPointerIndexShift,
                                            WordRepresentation::Word32());
    OpIndex pointer =
        Asm().LoadOffHeap(table, Asm().ChangeUint32ToUint64(index), 0,
                          MemoryRepresentation::PointerSized());
    pointer = Asm().Word64BitwiseAnd(pointer, Asm().Word64Constant(~tag));
    return pointer;
#else   // V8_ENABLE_SANDBOX
    UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
  }

 private:
  base::Optional<MemoryAnalyzer<typename Next::AssemblerType>> analyzer_;
  Isolate* isolate_;
  const TSCallDescriptor* allocate_builtin_descriptor_ = nullptr;

  const TSCallDescriptor* AllocateBuiltinDescriptor() {
    if (allocate_builtin_descriptor_ == nullptr) {
      allocate_builtin_descriptor_ =
          CreateAllocateBuiltinDescriptor(Asm().graph_zone());
    }
    return allocate_builtin_descriptor_;
  }
};

template <class Assembler>
inline void MemoryAnalyzer<Assembler>::Run() {
  block_states[current_block] = BlockState{};
  BlockIndex end = BlockIndex(input_graph.block_count());
  while (current_block < end) {
    state = *block_states[current_block];
    auto operations_range =
        input_graph.operations(input_graph.Get(current_block));
    // Set the next block index here already, to allow it to be changed if
    // needed.
    current_block = BlockIndex(current_block.id() + 1);
    for (const Operation& op : operations_range) {
      Process(op);
    }
  }
}

template <class Assembler>
inline void MemoryAnalyzer<Assembler>::Process(const Operation& op) {
  if (assembler->ShouldSkipOperation(op, input_graph.Index(op))) {
    return;
  }

  if (auto* alloc = op.TryCast<AllocateOp>()) {
    ProcessAllocation(*alloc);
    return;
  }
  if (auto* store = op.TryCast<StoreOp>()) {
    ProcessStore(input_graph.Index(op), store->base());
    return;
  }
  OpProperties properties = op.Properties();
  if (properties.can_allocate) {
    state = BlockState();
  }
  if (properties.is_block_terminator) {
    ProcessBlockTerminator(op);
  }
}

// Update the successor block states based on the state of the current block.
// For loop backedges, we need to re-start the analysis from the loop header
// unless the backedge state is unchanged.
template <class Assembler>
inline void MemoryAnalyzer<Assembler>::ProcessBlockTerminator(
    const Operation& op) {
  if (auto* goto_op = op.TryCast<GotoOp>()) {
    if (input_graph.IsLoopBackedge(*goto_op)) {
      base::Optional<BlockState>& target_state =
          block_states[goto_op->destination->index()];
      BlockState old_state = *target_state;
      MergeCurrentStateIntoSuccessor(goto_op->destination);
      if (old_state != *target_state) {
        // We can never fold allocations inside of the loop into an
        // allocation before the loop, since this leads to unbounded
        // allocation size. An unknown `reserved_size` will prevent adding
        // allocations inside of the loop.
        target_state->reserved_size = base::nullopt;
        // Redo the analysis from the beginning of the loop.
        current_block = goto_op->destination->index();
      }
      return;
    } else if (goto_op->destination->IsLoop()) {
      // Look ahead to detect allocating loops earlier, avoiding a wrong
      // speculation resulting in processing the loop twice.
      for (const Operation& op :
           input_graph.operations(*goto_op->destination)) {
        if (op.Properties().can_allocate &&
            !assembler->ShouldSkipOperation(op, input_graph.Index(op))) {
          state = BlockState();
          break;
        }
      }
    }
  }
  for (Block* successor : SuccessorBlocks(op)) {
    MergeCurrentStateIntoSuccessor(successor);
  }
}

// We try to merge the new allocation into a previous dominating allocation.
// We also allow folding allocations across blocks, as long as there is a
// dominating relationship.
template <class Assembler>
inline void MemoryAnalyzer<Assembler>::ProcessAllocation(
    const AllocateOp& alloc) {
  if (ShouldSkipOptimizationStep()) return;
  base::Optional<uint64_t> new_size;
  if (auto* size =
          input_graph.Get(alloc.size()).template TryCast<ConstantOp>()) {
    new_size = size->integral();
  }
  // If the new allocation has a static size and is of the same type, then we
  // can fold it into the previous allocation unless the folded allocation would
  // exceed `kMaxRegularHeapObjectSize`.
  if (state.last_allocation && new_size.has_value() &&
      state.reserved_size.has_value() &&
      alloc.type == state.last_allocation->type &&
      *new_size <= kMaxRegularHeapObjectSize - *state.reserved_size) {
    state.reserved_size =
        static_cast<uint32_t>(*state.reserved_size + *new_size);
    folded_into[&alloc] = state.last_allocation;
    uint32_t& max_reserved_size = reserved_size[state.last_allocation];
    max_reserved_size = std::max(max_reserved_size, *state.reserved_size);
    return;
  }
  state.last_allocation = &alloc;
  state.reserved_size = base::nullopt;
  if (new_size.has_value() && *new_size <= kMaxRegularHeapObjectSize) {
    state.reserved_size = static_cast<uint32_t>(*new_size);
  }
  // We might be re-visiting the current block. In this case, we need to remove
  // an allocation that can no longer be folded.
  reserved_size.erase(&alloc);
  folded_into.erase(&alloc);
}

template <class Assembler>
inline void MemoryAnalyzer<Assembler>::ProcessStore(OpIndex store,
                                                    OpIndex object) {
  if (SkipWriteBarrier(input_graph.Get(object))) {
    skipped_write_barriers.insert(store);
  } else {
    // We might be re-visiting the current block. In this case, we need to
    // still update the information.
    skipped_write_barriers.erase(store);
  }
}

template <class Assembler>
inline void MemoryAnalyzer<Assembler>::MergeCurrentStateIntoSuccessor(
    const Block* successor) {
  base::Optional<BlockState>& target_state = block_states[successor->index()];
  if (!target_state.has_value()) {
    target_state = state;
    return;
  }
  // All predecessors need to have the same last allocation for us to continue
  // folding into it.
  if (target_state->last_allocation != state.last_allocation) {
    target_state = BlockState();
    return;
  }
  // We take the maximum allocation size of all predecessors. If the size is
  // unknown because it is dynamic, we remember the allocation to eliminate
  // write barriers.
  if (target_state->reserved_size.has_value() &&
      state.reserved_size.has_value()) {
    target_state->reserved_size =
        std::max(*target_state->reserved_size, *state.reserved_size);
  } else {
    target_state->reserved_size = base::nullopt;
  }
}

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_MEMORY_OPTIMIZATION_H_
