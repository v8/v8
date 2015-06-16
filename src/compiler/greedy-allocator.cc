// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/greedy-allocator.h"
#include "src/compiler/register-allocator.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE(...)                             \
  do {                                         \
    if (FLAG_trace_alloc) PrintF(__VA_ARGS__); \
  } while (false)


class CoalescedLiveRanges : public ZoneObject {
 public:
  explicit CoalescedLiveRanges(Zone* zone) : storage_(zone) {}

  LiveRange* Find(UseInterval* query) {
    ZoneSplayTree<Config>::Locator locator;

    if (storage_.Find(GetKey(query), &locator)) {
      return locator.value();
    }
    return nullptr;
  }

  // TODO(mtrofin): Change to void returning if we do not care if the interval
  // was previously added.
  bool Insert(LiveRange* range) {
    auto* interval = range->first_interval();
    while (interval != nullptr) {
      if (!Insert(interval, range)) return false;
      interval = interval->next();
    }
    return true;
  }

  bool Remove(LiveRange* range) {
    bool ret = false;
    auto* segment = range->first_interval();
    while (segment != nullptr) {
      ret |= Remove(segment);
      segment = segment->next();
    }
    return ret;
  }

  bool IsEmpty() { return storage_.is_empty(); }

 private:
  struct Config {
    typedef std::pair<int, int> Key;
    typedef LiveRange* Value;
    static const Key kNoKey;
    static Value NoValue() { return nullptr; }
    static int Compare(const Key& a, const Key& b) {
      if (a.second <= b.first) return -1;
      if (a.first >= b.second) return 1;
      return 0;
    }
  };

  Config::Key GetKey(UseInterval* interval) {
    if (interval == nullptr) return std::make_pair(0, 0);
    return std::make_pair(interval->start().value(), interval->end().value());
  }

  // TODO(mtrofin): Change to void returning if we do not care if the interval
  // was previously added.
  bool Insert(UseInterval* interval, LiveRange* range) {
    ZoneSplayTree<Config>::Locator locator;
    bool ret = storage_.Insert(GetKey(interval), &locator);
    if (ret) locator.set_value(range);
    return ret;
  }

  bool Remove(UseInterval* key) { return storage_.Remove(GetKey(key)); }

  ZoneSplayTree<Config> storage_;
  DISALLOW_COPY_AND_ASSIGN(CoalescedLiveRanges);
};


const std::pair<int, int> CoalescedLiveRanges::Config::kNoKey = {0, 0};

GreedyAllocator::GreedyAllocator(RegisterAllocationData* data,
                                 RegisterKind kind, Zone* local_zone)
    : RegisterAllocator(data, kind),
      local_zone_(local_zone),
      allocations_(local_zone),
      queue_(local_zone) {}


unsigned GreedyAllocator::GetLiveRangeSize(LiveRange* range) {
  auto interval = range->first_interval();
  if (interval == nullptr) return 0;

  unsigned size = 0;
  while (interval != nullptr) {
    size += (interval->end().value() - interval->start().value());
    interval = interval->next();
  }

  return size;
}


void GreedyAllocator::AssignRangeToRegister(int reg_id, LiveRange* range) {
  allocations_[reg_id]->Insert(range);
  if (range->HasRegisterAssigned()) {
    DCHECK_EQ(reg_id, range->assigned_register());
    return;
  }
  range->set_assigned_register(reg_id);
  range->SetUseHints(reg_id);
  if (range->is_phi()) {
    data()->GetPhiMapValueFor(range->id())->set_assigned_register(reg_id);
  }
}


float GreedyAllocator::CalculateSpillWeight(LiveRange* range) {
  InstructionOperand* first_hint = nullptr;
  if (range->FirstHintPosition() != nullptr) {
    first_hint = range->FirstHintPosition()->operand();
  }

  if (range->IsFixed()) return std::numeric_limits<float>::max();
  bool spill;
  if (!FindProgressingSplitPosition(range, &spill).IsValid())
    return std::numeric_limits<float>::max();

  float multiplier = 1.0;
  if (first_hint != nullptr && first_hint->IsRegister()) {
    multiplier = 3.0;
  }

  unsigned use_count = 0;
  auto* pos = range->first_pos();
  while (pos != nullptr) {
    use_count++;
    pos = pos->next();
  }

  unsigned range_size = GetLiveRangeSize(range);
  DCHECK_NE(0U, range_size);

  return multiplier * static_cast<float>(use_count) /
         static_cast<float>(range_size);
}


float GreedyAllocator::CalculateMaxSpillWeight(
    const ZoneSet<LiveRange*>& ranges) {
  float max = 0.0;
  for (auto* r : ranges) {
    max = std::max(max, CalculateSpillWeight(r));
  }
  return max;
}


void GreedyAllocator::Evict(LiveRange* range) {
  bool removed = allocations_[range->assigned_register()]->Remove(range);
  CHECK(removed);
  range->UnsetUseHints();
  range->UnsetAssignedRegister();
  if (range->is_phi()) {
    data()->GetPhiMapValueFor(range->id())->UnsetAssignedRegister();
  }
}


bool GreedyAllocator::TryAllocatePhysicalRegister(
    unsigned reg_id, LiveRange* range, ZoneSet<LiveRange*>* conflicting) {
  auto* segment = range->first_interval();

  auto* alloc_info = allocations_[reg_id];
  while (segment != nullptr) {
    if (auto* existing = alloc_info->Find(segment)) {
      DCHECK(existing->HasRegisterAssigned());
      conflicting->insert(existing);
    }
    segment = segment->next();
  }
  if (!conflicting->empty()) return false;
  // No conflicts means we can safely allocate this register to this range.
  AssignRangeToRegister(reg_id, range);
  return true;
}


int GreedyAllocator::GetHintedRegister(LiveRange* range) {
  int reg;
  if (range->FirstHintPosition(&reg) != nullptr) {
    return reg;
  }
  return -1;
}


bool GreedyAllocator::TryAllocate(LiveRange* current,
                                  ZoneSet<LiveRange*>* conflicting) {
  if (current->IsFixed()) {
    return TryAllocatePhysicalRegister(current->assigned_register(), current,
                                       conflicting);
  }

  int hinted_reg_id = GetHintedRegister(current);
  if (hinted_reg_id >= 0) {
    if (TryAllocatePhysicalRegister(hinted_reg_id, current, conflicting)) {
      return true;
    }
  }

  ZoneSet<LiveRange*> local_conflicts(local_zone());
  for (unsigned candidate_reg = 0; candidate_reg < allocations_.size();
       candidate_reg++) {
    if (hinted_reg_id >= 0 &&
        candidate_reg == static_cast<size_t>(hinted_reg_id))
      continue;
    local_conflicts.clear();
    if (TryAllocatePhysicalRegister(candidate_reg, current, &local_conflicts)) {
      conflicting->clear();
      return true;
    } else {
      conflicting->insert(local_conflicts.begin(), local_conflicts.end());
    }
  }
  return false;
}


LiveRange* GreedyAllocator::SpillBetweenUntil(LiveRange* range,
                                              LifetimePosition start,
                                              LifetimePosition until,
                                              LifetimePosition end) {
  CHECK(start < end);
  auto second_part = SplitRangeAt(range, start);

  if (second_part->Start() < end) {
    // The split result intersects with [start, end[.
    // Split it at position between ]start+1, end[, spill the middle part
    // and put the rest to unhandled.
    auto third_part_end = end.PrevStart().End();
    if (data()->IsBlockBoundary(end.Start())) {
      third_part_end = end.Start();
    }
    auto third_part = SplitBetween(
        second_part, Max(second_part->Start().End(), until), third_part_end);

    DCHECK(third_part != second_part);

    Spill(second_part);
    return third_part;
  } else {
    // The split result does not intersect with [start, end[.
    // Nothing to spill. Just return it for re-processing.
    return second_part;
  }
}


void GreedyAllocator::Enqueue(LiveRange* range) {
  if (range == nullptr || range->IsEmpty()) return;
  unsigned size = GetLiveRangeSize(range);
  TRACE("Enqueuing range %d\n", range->id());
  queue_.push(std::make_pair(size, range));
}


bool GreedyAllocator::HandleSpillOperands(LiveRange* range) {
  auto position = range->Start();
  TRACE("Processing interval %d start=%d\n", range->id(), position.value());

  if (!range->HasNoSpillType()) {
    TRACE("Live range %d already has a spill operand\n", range->id());
    auto next_pos = position;
    if (next_pos.IsGapPosition()) {
      next_pos = next_pos.NextStart();
    }
    auto pos = range->NextUsePositionRegisterIsBeneficial(next_pos);
    // If the range already has a spill operand and it doesn't need a
    // register immediately, split it and spill the first part of the range.
    if (pos == nullptr) {
      Spill(range);
      return true;
    } else if (pos->pos() > range->Start().NextStart()) {
      // Do not spill live range eagerly if use position that can benefit from
      // the register is too close to the start of live range.
      auto* reminder = SpillBetweenUntil(range, position, position, pos->pos());
      Enqueue(reminder);
      return true;
    }
  }
  return false;
}


void GreedyAllocator::AllocateRegisters() {
  for (auto range : data()->live_ranges()) {
    if (range == nullptr) continue;
    if (range->kind() == mode()) {
      DCHECK(!range->HasRegisterAssigned() && !range->spilled());
      TRACE("Enqueueing live range %d to priority queue \n", range->id());
      Enqueue(range);
    }
  }

  allocations_.resize(num_registers());
  for (int i = 0; i < num_registers(); i++) {
    allocations_[i] = new (local_zone()) CoalescedLiveRanges(local_zone());
  }

  for (auto* current : GetFixedRegisters()) {
    if (current != nullptr) {
      DCHECK_EQ(mode(), current->kind());
      int reg_nr = current->assigned_register();
      bool inserted = allocations_[reg_nr]->Insert(current);
      CHECK(inserted);
    }
  }

  while (!queue_.empty()) {
    auto current_pair = queue_.top();
    queue_.pop();
    auto current = current_pair.second;
    if (HandleSpillOperands(current)) {
      continue;
    }
    bool spill = false;
    ZoneSet<LiveRange*> conflicting(local_zone());
    if (!TryAllocate(current, &conflicting)) {
      DCHECK(!conflicting.empty());
      float this_weight = std::numeric_limits<float>::max();
      LifetimePosition split_pos =
          FindProgressingSplitPosition(current, &spill);
      if (split_pos.IsValid()) {
        this_weight = CalculateSpillWeight(current);
      }

      bool evicted = false;
      for (auto* conflict : conflicting) {
        if (CalculateSpillWeight(conflict) < this_weight) {
          Evict(conflict);
          Enqueue(conflict);
          evicted = true;
        }
      }
      if (evicted) {
        conflicting.clear();
        TryAllocate(current, &conflicting);
      }
      if (!conflicting.empty()) {
        DCHECK(!current->IsFixed() || current->CanBeSpilled(current->Start()));
        DCHECK(split_pos.IsValid());
        AllocateBlockedRange(current, split_pos, spill);
      }
    }
  }

  for (size_t i = 0; i < allocations_.size(); ++i) {
    if (!allocations_[i]->IsEmpty()) {
      data()->MarkAllocated(mode(), static_cast<int>(i));
    }
  }
}


LifetimePosition GreedyAllocator::GetSplittablePos(LifetimePosition pos) {
  auto ret = pos.PrevStart().End();
  if (data()->IsBlockBoundary(pos.Start())) {
    ret = pos.Start();
  }
  DCHECK(ret <= pos);
  return ret;
}

LifetimePosition GreedyAllocator::FindProgressingSplitPosition(
    LiveRange* range, bool* is_spill_pos) {
  auto start = range->Start();
  auto end = range->End();

  UsePosition* next_reg_use = range->first_pos();
  while (next_reg_use != nullptr &&
         next_reg_use->type() != UsePositionType::kRequiresRegister) {
    next_reg_use = next_reg_use->next();
  }

  if (next_reg_use == nullptr) {
    *is_spill_pos = true;
    auto ret = FindOptimalSpillingPos(range, start);
    DCHECK(ret.IsValid());
    return ret;
  }

  *is_spill_pos = false;
  auto reg_pos = next_reg_use->pos();
  auto correct_pos = GetSplittablePos(reg_pos);
  if (start < correct_pos && correct_pos < end) {
    return correct_pos;
  }

  if (correct_pos >= end) {
    return LifetimePosition::Invalid();
  }

  // Correct_pos must be at or before start. Find the next use position.
  next_reg_use = next_reg_use->next();
  auto reference = reg_pos;
  while (next_reg_use != nullptr) {
    auto pos = next_reg_use->pos();
    // Skip over tight successive uses.
    if (reference.NextStart() < pos) {
      break;
    }
    reference = pos;
    next_reg_use = next_reg_use->next();
  }

  if (next_reg_use == nullptr) {
    // While there may not be another use, we may still have space in the range
    // to clip off.
    correct_pos = reference.NextStart();
    if (start < correct_pos && correct_pos < end) {
      return correct_pos;
    }
    return LifetimePosition::Invalid();
  }

  correct_pos = GetSplittablePos(next_reg_use->pos());
  if (start < correct_pos && correct_pos < end) {
    DCHECK(reference < correct_pos);
    return correct_pos;
  }
  return LifetimePosition::Invalid();
}


void GreedyAllocator::AllocateBlockedRange(LiveRange* current,
                                           LifetimePosition pos, bool spill) {
  auto tail = SplitRangeAt(current, pos);
  if (spill) {
    Spill(tail);
  } else {
    Enqueue(tail);
  }
  if (tail != current) {
    Enqueue(current);
  }
}


}  // namespace compiler
}  // namespace internal
}  // namespace v8
