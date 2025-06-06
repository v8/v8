// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_GRAPH_H_
#define V8_MAGLEV_MAGLEV_GRAPH_H_

#include <vector>

#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/heap-refs.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

using BlockConstIterator = ZoneVector<BasicBlock*>::const_iterator;
using BlockConstReverseIterator =
    ZoneVector<BasicBlock*>::const_reverse_iterator;

struct MaglevCallSiteInfo;

class Graph final : public ZoneObject {
 public:
  static Graph* New(Zone* zone, bool is_osr) {
    return zone->New<Graph>(zone, is_osr);
  }

  // Shouldn't be used directly; public so that Zone::New can access it.
  Graph(Zone* zone, bool is_osr)
      : blocks_(zone),
        root_(zone),
        osr_values_(zone),
        smi_(zone),
        tagged_index_(zone),
        int32_(zone),
        uint32_(zone),
        intptr_(zone),
        float_(zone),
        external_references_(zone),
        parameters_(zone),
        inlineable_calls_(zone),
        allocations_escape_map_(zone),
        allocations_elide_map_(zone),
        register_inputs_(),
        constants_(zone),
        trusted_constants_(zone),
        inlined_functions_(zone),
        node_buffer_(zone),
        is_osr_(is_osr),
        scope_infos_(zone) {
    node_buffer_.reserve(32);
  }

  BasicBlock* operator[](int i) { return blocks_[i]; }
  const BasicBlock* operator[](int i) const { return blocks_[i]; }

  int num_blocks() const { return static_cast<int>(blocks_.size()); }
  ZoneVector<BasicBlock*>& blocks() { return blocks_; }

  BlockConstIterator begin() const { return blocks_.begin(); }
  BlockConstIterator end() const { return blocks_.end(); }
  BlockConstReverseIterator rbegin() const { return blocks_.rbegin(); }
  BlockConstReverseIterator rend() const { return blocks_.rend(); }

  BasicBlock* last_block() const { return blocks_.back(); }

  void Add(BasicBlock* block) {
    if (block->has_id()) {
      // The inliner adds blocks multiple times.
      DCHECK(v8_flags.maglev_non_eager_inlining ||
             v8_flags.turbolev_non_eager_inlining);
    } else {
      block->set_id(max_block_id_++);
    }
    blocks_.push_back(block);
  }

  void set_blocks(ZoneVector<BasicBlock*> blocks) { blocks_ = blocks; }

  inline void RemoveUnreachableBlocks() {
    DCHECK(may_have_unreachable_blocks());

    std::vector<bool> reachable_blocks(max_block_id(), false);
    std::vector<BasicBlock*> worklist;

    DCHECK(!blocks().empty());
    BasicBlock* initial_bb = blocks().front();
    worklist.push_back(initial_bb);
    reachable_blocks[initial_bb->id()] = true;
    DCHECK(!initial_bb->is_loop());

    while (!worklist.empty()) {
      BasicBlock* current = worklist.back();
      worklist.pop_back();

      for (auto handler : current->exception_handlers()) {
        if (!handler->HasExceptionHandler()) continue;
        if (handler->ShouldLazyDeopt()) continue;
        BasicBlock* catch_block = handler->catch_block();
        if (!reachable_blocks[catch_block->id()]) {
          reachable_blocks[catch_block->id()] = true;
          worklist.push_back(catch_block);
        }
      }
      current->ForEachSuccessor([&](BasicBlock* succ) {
        if (!reachable_blocks[succ->id()]) {
          reachable_blocks[succ->id()] = true;
          worklist.push_back(succ);
        }
      });
    }

    // Sweep dead blocks and remove unreachable predecessors.
    IterateGraphAndSweepDeadBlocks([&](BasicBlock* bb) {
      if (!reachable_blocks[bb->id()]) return true;
      // If block doesn't have a merge state, it has only one predecessor, so
      // it must be the reachable one.
      if (!bb->has_state()) return false;
      if (bb->is_loop() &&
          !reachable_blocks[bb->backedge_predecessor()->id()]) {
        // If the backedge predecessor is not reachable, we can turn the loop
        // into a regular block.
        bb->state()->TurnLoopIntoRegularBlock();
      }
      for (int i = bb->predecessor_count() - 1; i >= 0; i--) {
        if (!reachable_blocks[bb->predecessor_at(i)->id()]) {
          bb->state()->RemovePredecessorAt(i);
        }
      }
      return false;
    });

    set_may_have_unreachable_blocks(false);
  }

  template <typename Function>
  void IterateGraphAndSweepDeadBlocks(Function&& is_dead) {
    auto current = blocks_.begin();
    auto last_non_dead = current;
    while (current != blocks_.end()) {
      if (is_dead(*current)) {
        (*current)->mark_dead();
      } else {
        if (current != last_non_dead) {
          // Move current to last non dead position.
          *last_non_dead = *current;
        }
        ++last_non_dead;
      }
      ++current;
    }
    if (current != last_non_dead) {
      blocks_.resize(blocks_.size() - (current - last_non_dead));
    }
  }

  uint32_t tagged_stack_slots() const { return tagged_stack_slots_; }
  uint32_t untagged_stack_slots() const { return untagged_stack_slots_; }
  uint32_t max_call_stack_args() const { return max_call_stack_args_; }
  uint32_t max_deopted_stack_size() const { return max_deopted_stack_size_; }
  void set_tagged_stack_slots(uint32_t stack_slots) {
    DCHECK_EQ(kMaxUInt32, tagged_stack_slots_);
    DCHECK_NE(kMaxUInt32, stack_slots);
    tagged_stack_slots_ = stack_slots;
  }
  void set_untagged_stack_slots(uint32_t stack_slots) {
    DCHECK_EQ(kMaxUInt32, untagged_stack_slots_);
    DCHECK_NE(kMaxUInt32, stack_slots);
    untagged_stack_slots_ = stack_slots;
  }
  void set_max_call_stack_args(uint32_t stack_slots) {
    DCHECK_EQ(kMaxUInt32, max_call_stack_args_);
    DCHECK_NE(kMaxUInt32, stack_slots);
    max_call_stack_args_ = stack_slots;
  }
  void set_max_deopted_stack_size(uint32_t size) {
    DCHECK_EQ(kMaxUInt32, max_deopted_stack_size_);
    DCHECK_NE(kMaxUInt32, size);
    max_deopted_stack_size_ = size;
  }

  int total_inlined_bytecode_size() const {
    return total_inlined_bytecode_size_;
  }
  void add_inlined_bytecode_size(int size) {
    total_inlined_bytecode_size_ += size;
  }

  int total_peeled_bytecode_size() const { return total_peeled_bytecode_size_; }
  void add_peeled_bytecode_size(int size) {
    total_peeled_bytecode_size_ += size;
  }

  ZoneMap<RootIndex, RootConstant*>& root() { return root_; }
  ZoneVector<InitialValue*>& osr_values() { return osr_values_; }
  ZoneMap<int, SmiConstant*>& smi() { return smi_; }
  ZoneMap<int, TaggedIndexConstant*>& tagged_index() { return tagged_index_; }
  ZoneMap<int32_t, Int32Constant*>& int32() { return int32_; }
  ZoneMap<uint32_t, Uint32Constant*>& uint32() { return uint32_; }
  ZoneMap<intptr_t, IntPtrConstant*>& intptr() { return intptr_; }
  ZoneMap<uint64_t, Float64Constant*>& float64() { return float_; }
  ZoneMap<Address, ExternalConstant*>& external_references() {
    return external_references_;
  }
  ZoneVector<InitialValue*>& parameters() { return parameters_; }

  ZoneVector<MaglevCallSiteInfo*>& inlineable_calls() {
    return inlineable_calls_;
  }

  ZoneVector<Node*>& node_buffer() { return node_buffer_; }

  // Running JS2, 99.99% of the cases, we have less than 2 dependencies.
  using SmallAllocationVector = SmallZoneVector<InlinedAllocation*, 2>;

  // If the key K of the map escape, all the set allocations_escape_map[K] must
  // also escape.
  ZoneMap<InlinedAllocation*, SmallAllocationVector>& allocations_escape_map() {
    return allocations_escape_map_;
  }
  // The K of the map can be elided if it hasn't escaped and all the set
  // allocations_elide_map[K] can also be elided.
  ZoneMap<InlinedAllocation*, SmallAllocationVector>& allocations_elide_map() {
    return allocations_elide_map_;
  }

  RegList& register_inputs() { return register_inputs_; }
  compiler::ZoneRefMap<compiler::ObjectRef, Constant*>& constants() {
    return constants_;
  }

  compiler::ZoneRefMap<compiler::HeapObjectRef, TrustedConstant*>&
  trusted_constants() {
    return trusted_constants_;
  }

  ZoneVector<OptimizedCompilationInfo::InlinedFunctionHolder>&
  inlined_functions() {
    return inlined_functions_;
  }
  bool has_recursive_calls() const { return has_recursive_calls_; }
  void set_has_recursive_calls(bool value) { has_recursive_calls_ = value; }

  bool is_osr() const { return is_osr_; }
  uint32_t min_maglev_stackslots_for_unoptimized_frame_size() {
    DCHECK(is_osr());
    if (osr_values().size() == 0) {
      return InitialValue::stack_slot(0);
    }
    return osr_values().back()->stack_slot() + 1;
  }

  uint32_t NewObjectId() { return object_ids_++; }

  void set_has_resumable_generator() { has_resumable_generator_ = true; }
  bool has_resumable_generator() const { return has_resumable_generator_; }
  void set_may_have_unreachable_blocks(bool value = true) {
    may_have_unreachable_blocks_ = value;
  }
  bool may_have_unreachable_blocks() const {
    return may_have_unreachable_blocks_;
  }

  compiler::OptionalScopeInfoRef TryGetScopeInfoForContextLoad(
      ValueNode* context, int offset, compiler::JSHeapBroker* broker) {
    compiler::OptionalScopeInfoRef cur = TryGetScopeInfo(context, broker);
    if (offset == Context::OffsetOfElementAt(Context::EXTENSION_INDEX)) {
      return cur;
    }
    CHECK_EQ(offset, Context::OffsetOfElementAt(Context::PREVIOUS_INDEX));
    if (cur.has_value()) {
      cur = (*cur).OuterScopeInfo(broker);
      while (!cur->HasContext() && cur->HasOuterScopeInfo()) {
        cur = cur->OuterScopeInfo(broker);
      }
      if (cur->HasContext()) {
        return cur;
      }
    }
    return {};
  }

  // Resolve the scope info of a context value.
  // An empty result means we don't statically know the context's scope.
  compiler::OptionalScopeInfoRef TryGetScopeInfo(
      ValueNode* context, compiler::JSHeapBroker* broker) {
    auto it = scope_infos_.find(context);
    if (it != scope_infos_.end()) {
      return it->second;
    }
    compiler::OptionalScopeInfoRef res;
    if (auto context_const = context->TryCast<Constant>()) {
      res = context_const->object().AsContext().scope_info(broker);
      DCHECK(res->HasContext());
    } else if (auto load =
                   context->TryCast<LoadTaggedFieldForContextSlotNoCells>()) {
      compiler::OptionalScopeInfoRef cur = TryGetScopeInfoForContextLoad(
          load->input(0).node(), load->offset(), broker);
      if (cur.has_value()) res = cur;
    } else if (auto load_script =
                   context->TryCast<LoadTaggedFieldForContextSlot>()) {
      compiler::OptionalScopeInfoRef cur = TryGetScopeInfoForContextLoad(
          load_script->input(0).node(), load_script->offset(), broker);
      if (cur.has_value()) res = cur;
    } else if (context->Is<InitialValue>()) {
      // We should only fail to keep track of initial contexts originating from
      // the OSR prequel.
      // TODO(olivf): Keep track of contexts when analyzing OSR Prequel.
      DCHECK(is_osr());
    } else {
      // Any context created within a function must be registered in
      // graph()->scope_infos(). Initial contexts must be registered before
      // BuildBody. We don't track context in generators (yet) and around eval
      // the bytecode compiler creates contexts by calling
      // Runtime::kNewFunctionInfo directly.
      DCHECK(context->Is<Phi>() || context->Is<GeneratorRestoreRegister>() ||
             context->Is<RegisterInput>() || context->Is<CallRuntime>());
    }
    return scope_infos_[context] = res;
  }

  void record_scope_info(ValueNode* context,
                         compiler::OptionalScopeInfoRef scope_info) {
    scope_infos_[context] = scope_info;
  }

  Zone* zone() const { return blocks_.zone(); }

  BasicBlock::Id max_block_id() const { return max_block_id_; }

 private:
  uint32_t tagged_stack_slots_ = kMaxUInt32;
  uint32_t untagged_stack_slots_ = kMaxUInt32;
  uint32_t max_call_stack_args_ = kMaxUInt32;
  uint32_t max_deopted_stack_size_ = kMaxUInt32;
  ZoneVector<BasicBlock*> blocks_;
  ZoneMap<RootIndex, RootConstant*> root_;
  ZoneVector<InitialValue*> osr_values_;
  ZoneMap<int, SmiConstant*> smi_;
  ZoneMap<int, TaggedIndexConstant*> tagged_index_;
  ZoneMap<int32_t, Int32Constant*> int32_;
  ZoneMap<uint32_t, Uint32Constant*> uint32_;
  ZoneMap<intptr_t, IntPtrConstant*> intptr_;
  // Use the bits of the float as the key.
  ZoneMap<uint64_t, Float64Constant*> float_;
  ZoneMap<Address, ExternalConstant*> external_references_;
  ZoneVector<InitialValue*> parameters_;
  ZoneVector<MaglevCallSiteInfo*> inlineable_calls_;
  ZoneMap<InlinedAllocation*, SmallAllocationVector> allocations_escape_map_;
  ZoneMap<InlinedAllocation*, SmallAllocationVector> allocations_elide_map_;
  RegList register_inputs_;
  compiler::ZoneRefMap<compiler::ObjectRef, Constant*> constants_;
  compiler::ZoneRefMap<compiler::HeapObjectRef, TrustedConstant*>
      trusted_constants_;
  ZoneVector<OptimizedCompilationInfo::InlinedFunctionHolder>
      inlined_functions_;
  ZoneVector<Node*> node_buffer_;
  bool has_recursive_calls_ = false;
  int total_inlined_bytecode_size_ = 0;
  int total_peeled_bytecode_size_ = 0;
  bool is_osr_ = false;
  uint32_t object_ids_ = 0;
  bool has_resumable_generator_ = false;
  bool may_have_unreachable_blocks_ = false;
  ZoneUnorderedMap<ValueNode*, compiler::OptionalScopeInfoRef> scope_infos_;
  BasicBlock::Id max_block_id_ = 0;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_GRAPH_H_
