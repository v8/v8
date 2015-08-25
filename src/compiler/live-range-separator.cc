// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/live-range-separator.h"
#include "src/compiler/register-allocator.h"

namespace v8 {
namespace internal {
namespace compiler {


#define TRACE(...)                             \
  do {                                         \
    if (FLAG_trace_alloc) PrintF(__VA_ARGS__); \
  } while (false)


namespace {

// Starting from a deferred block, find the last consecutive deferred block.
RpoNumber GetLastDeferredBlock(const InstructionBlock *block,
                               const InstructionSequence *code) {
  DCHECK(block->IsDeferred());
  RpoNumber first = block->rpo_number();

  RpoNumber last = first;
  for (int i = first.ToInt(); i < code->InstructionBlockCount(); ++i) {
    RpoNumber at_i = RpoNumber::FromInt(i);
    const InstructionBlock *block_at_i = code->InstructionBlockAt(at_i);
    if (!block_at_i->IsDeferred()) break;
    last = at_i;
  }

  return last;
}


// Delimits consecutive deferred block sequences.
void AssociateDeferredBlockSequences(InstructionSequence *code) {
  for (int blk_id = 0; blk_id < code->InstructionBlockCount(); ++blk_id) {
    InstructionBlock *block =
        code->InstructionBlockAt(RpoNumber::FromInt(blk_id));
    if (!block->IsDeferred()) continue;
    RpoNumber last = GetLastDeferredBlock(block, code);
    block->set_last_deferred(last);
    // We know last is still deferred, and that last + 1, is not (or is an
    // invalid index). So skip over last + 1 and continue from last + 2. This
    // way, we visit each block exactly once, and the total complexity of this
    // function is O(n), n being jthe number of blocks.
    blk_id = last.ToInt() + 1;
  }
}


// If the live range has a liveness hole right between start and end,
// we don't need to splinter it.
bool IsIntervalAlreadyExcluded(const LiveRange *range, LifetimePosition start,
                               LifetimePosition end) {
  for (UseInterval *interval = range->first_interval(); interval != nullptr;
       interval = interval->next()) {
    if (interval->start() <= start && start < interval->end()) return false;
    if (interval->start() < end && end <= interval->end()) return false;
  }
  return true;
}


void CreateSplinter(LiveRange *range, RegisterAllocationData *data,
                    LifetimePosition first_cut, LifetimePosition last_cut) {
  DCHECK(!range->IsChild());
  DCHECK(!range->IsSplinter());
  // We can ignore ranges that live solely in deferred blocks.
  // If a range ends right at the end of a deferred block, it is marked by
  // the range builder as ending at gap start of the next block - since the
  // end is a position where the variable isn't live. We need to take that
  // into consideration.
  LifetimePosition max_allowed_end = last_cut.NextFullStart();

  if (first_cut <= range->Start() && max_allowed_end >= range->End()) {
    return;
  }

  LifetimePosition start = Max(first_cut, range->Start());
  LifetimePosition end = Min(last_cut, range->End());
  // Skip ranges that have a hole where the deferred block(s) are.
  if (IsIntervalAlreadyExcluded(range, start, end)) return;

  if (start < end) {
    // Ensure the original range has a spill range associated, before it gets
    // splintered. Splinters will point to it. This way, when attempting to
    // reuse spill slots of splinters, during allocation, we avoid clobbering
    // such slots.
    if (range->MayRequireSpillRange()) {
      data->CreateSpillRangeForLiveRange(range);
    }
    LiveRange *result = data->NewChildRangeFor(range);
    Zone *zone = data->allocation_zone();
    range->Splinter(start, end, result, zone);
  }
}


// Splinter all ranges live inside successive deferred blocks.
// No control flow analysis is performed. After the register allocation, we will
// merge the splinters back into the original ranges, and then rely on the
// range connector to properly connect them.
void SplinterRangesInDeferredBlocks(RegisterAllocationData *data) {
  InstructionSequence *code = data->code();
  int code_block_count = code->InstructionBlockCount();
  Zone *zone = data->allocation_zone();
  ZoneVector<BitVector *> &in_sets = data->live_in_sets();

  for (int i = 0; i < code_block_count; ++i) {
    InstructionBlock *block = code->InstructionBlockAt(RpoNumber::FromInt(i));
    if (!block->IsDeferred()) continue;

    RpoNumber last_deferred = block->last_deferred();
    i = last_deferred.ToInt();

    LifetimePosition first_cut = LifetimePosition::GapFromInstructionIndex(
        block->first_instruction_index());

    LifetimePosition last_cut = LifetimePosition::GapFromInstructionIndex(
        static_cast<int>(code->instructions().size()));

    const BitVector *in_set = in_sets[i];
    InstructionBlock *last = code->InstructionBlockAt(last_deferred);
    const BitVector *out_set = LiveRangeBuilder::ComputeLiveOut(last, data);
    last_cut = LifetimePosition::GapFromInstructionIndex(
        last->last_instruction_index());

    BitVector ranges_to_splinter(*in_set, zone);
    ranges_to_splinter.Union(*out_set);
    BitVector::Iterator iterator(&ranges_to_splinter);

    while (!iterator.Done()) {
      int range_id = iterator.Current();
      iterator.Advance();

      LiveRange *range = data->live_ranges()[range_id];
      CreateSplinter(range, data, first_cut, last_cut);
    }
  }
}
}  // namespace


void LiveRangeSeparator::Splinter() {
  AssociateDeferredBlockSequences(data()->code());
  SplinterRangesInDeferredBlocks(data());
}


void LiveRangeMerger::Merge() {
  int live_range_count = static_cast<int>(data()->live_ranges().size());
  for (int i = 0; i < live_range_count; ++i) {
    LiveRange *range = data()->live_ranges()[i];
    if (range == nullptr || range->IsEmpty() || range->IsChild() ||
        !range->IsSplinter()) {
      continue;
    }
    LiveRange *splinter_parent = range->splintered_from();

    splinter_parent->Merge(range, data());
  }
}


}  // namespace compiler
}  // namespace internal
}  // namespace v8
