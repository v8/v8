// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/adapters.h"
#include "src/compiler/linkage.h"
#include "src/compiler/register-allocator.h"
#include "src/string-stream.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE(...)                             \
  do {                                         \
    if (FLAG_trace_alloc) PrintF(__VA_ARGS__); \
  } while (false)


namespace {

void RemoveElement(ZoneVector<LiveRange*>* v, LiveRange* range) {
  auto it = std::find(v->begin(), v->end(), range);
  DCHECK(it != v->end());
  v->erase(it);
}


int GetRegisterCount(const RegisterConfiguration* cfg, RegisterKind kind) {
  return kind == DOUBLE_REGISTERS ? cfg->num_aliased_double_registers()
                                  : cfg->num_general_registers();
}


const ZoneVector<LiveRange*>& GetFixedRegisters(
    const RegisterAllocationData* data, RegisterKind kind) {
  return kind == DOUBLE_REGISTERS ? data->fixed_double_live_ranges()
                                  : data->fixed_live_ranges();
}


const InstructionBlock* GetContainingLoop(const InstructionSequence* sequence,
                                          const InstructionBlock* block) {
  auto index = block->loop_header();
  if (!index.IsValid()) return nullptr;
  return sequence->InstructionBlockAt(index);
}


const InstructionBlock* GetInstructionBlock(const InstructionSequence* code,
                                            LifetimePosition pos) {
  return code->GetInstructionBlock(pos.ToInstructionIndex());
}


bool IsBlockBoundary(const InstructionSequence* code, LifetimePosition pos) {
  return pos.IsFullStart() &&
         code->GetInstructionBlock(pos.ToInstructionIndex())->code_start() ==
             pos.ToInstructionIndex();
}


Instruction* GetLastInstruction(InstructionSequence* code,
                                const InstructionBlock* block) {
  return code->InstructionAt(block->last_instruction_index());
}

}  // namespace


UsePosition::UsePosition(LifetimePosition pos, InstructionOperand* operand,
                         InstructionOperand* hint)
    : operand_(operand), hint_(hint), pos_(pos), next_(nullptr), flags_(0) {
  bool register_beneficial = true;
  UsePositionType type = UsePositionType::kAny;
  if (operand_ != nullptr && operand_->IsUnallocated()) {
    const UnallocatedOperand* unalloc = UnallocatedOperand::cast(operand_);
    if (unalloc->HasRegisterPolicy()) {
      type = UsePositionType::kRequiresRegister;
    } else if (unalloc->HasSlotPolicy()) {
      type = UsePositionType::kRequiresSlot;
      register_beneficial = false;
    } else {
      register_beneficial = !unalloc->HasAnyPolicy();
    }
  }
  flags_ = TypeField::encode(type) |
           RegisterBeneficialField::encode(register_beneficial);
  DCHECK(pos_.IsValid());
}


bool UsePosition::HasHint() const {
  return hint_ != nullptr && !hint_->IsUnallocated();
}


void UsePosition::set_type(UsePositionType type, bool register_beneficial) {
  DCHECK_IMPLIES(type == UsePositionType::kRequiresSlot, !register_beneficial);
  flags_ = TypeField::encode(type) |
           RegisterBeneficialField::encode(register_beneficial);
}


UseInterval* UseInterval::SplitAt(LifetimePosition pos, Zone* zone) {
  DCHECK(Contains(pos) && pos != start());
  auto after = new (zone) UseInterval(pos, end_);
  after->next_ = next_;
  next_ = nullptr;
  end_ = pos;
  return after;
}


struct LiveRange::SpillAtDefinitionList : ZoneObject {
  SpillAtDefinitionList(int gap_index, InstructionOperand* operand,
                        SpillAtDefinitionList* next)
      : gap_index(gap_index), operand(operand), next(next) {}
  const int gap_index;
  InstructionOperand* const operand;
  SpillAtDefinitionList* const next;
};


LiveRange::LiveRange(int id)
    : id_(id),
      spilled_(false),
      has_slot_use_(false),
      is_phi_(false),
      is_non_loop_phi_(false),
      kind_(UNALLOCATED_REGISTERS),
      assigned_register_(kInvalidAssignment),
      last_interval_(nullptr),
      first_interval_(nullptr),
      first_pos_(nullptr),
      parent_(nullptr),
      next_(nullptr),
      current_interval_(nullptr),
      last_processed_use_(nullptr),
      current_hint_operand_(nullptr),
      spill_start_index_(kMaxInt),
      spill_type_(SpillType::kNoSpillType),
      spill_operand_(nullptr),
      spills_at_definition_(nullptr) {}


void LiveRange::Verify() const {
  // Walk the positions, verifying that each is in an interval.
  auto interval = first_interval_;
  for (auto pos = first_pos_; pos != nullptr; pos = pos->next()) {
    CHECK(Start() <= pos->pos());
    CHECK(pos->pos() <= End());
    CHECK(interval != nullptr);
    while (!interval->Contains(pos->pos()) && interval->end() != pos->pos()) {
      interval = interval->next();
      CHECK(interval != nullptr);
    }
  }
}


void LiveRange::set_assigned_register(int reg) {
  DCHECK(!HasRegisterAssigned() && !IsSpilled());
  assigned_register_ = reg;
}


void LiveRange::MakeSpilled() {
  DCHECK(!IsSpilled());
  DCHECK(!TopLevel()->HasNoSpillType());
  spilled_ = true;
  assigned_register_ = kInvalidAssignment;
}


void LiveRange::SpillAtDefinition(Zone* zone, int gap_index,
                                  InstructionOperand* operand) {
  DCHECK(HasNoSpillType());
  spills_at_definition_ = new (zone)
      SpillAtDefinitionList(gap_index, operand, spills_at_definition_);
}


void LiveRange::CommitSpillsAtDefinition(InstructionSequence* sequence,
                                         InstructionOperand* op,
                                         bool might_be_duplicated) {
  DCHECK_IMPLIES(op->IsConstant(), spills_at_definition_ == nullptr);
  DCHECK(!IsChild());
  auto zone = sequence->zone();
  for (auto to_spill = spills_at_definition_; to_spill != nullptr;
       to_spill = to_spill->next) {
    auto instr = sequence->InstructionAt(to_spill->gap_index);
    auto move = instr->GetOrCreateParallelMove(Instruction::START, zone);
    // Skip insertion if it's possible that the move exists already as a
    // constraint move from a fixed output register to a slot.
    if (might_be_duplicated) {
      bool found = false;
      for (auto move_op : *move) {
        if (move_op->IsEliminated()) continue;
        if (move_op->source() == *to_spill->operand &&
            move_op->destination() == *op) {
          found = true;
          break;
        }
      }
      if (found) continue;
    }
    move->AddMove(*to_spill->operand, *op);
  }
}


void LiveRange::SetSpillOperand(InstructionOperand* operand) {
  DCHECK(HasNoSpillType());
  DCHECK(!operand->IsUnallocated() && !operand->IsImmediate());
  spill_type_ = SpillType::kSpillOperand;
  spill_operand_ = operand;
}


void LiveRange::SetSpillRange(SpillRange* spill_range) {
  DCHECK(HasNoSpillType() || HasSpillRange());
  DCHECK(spill_range);
  spill_type_ = SpillType::kSpillRange;
  spill_range_ = spill_range;
}


void LiveRange::CommitSpillOperand(AllocatedOperand* operand) {
  DCHECK(HasSpillRange());
  DCHECK(!IsChild());
  spill_type_ = SpillType::kSpillOperand;
  spill_operand_ = operand;
}


UsePosition* LiveRange::NextUsePosition(LifetimePosition start) const {
  UsePosition* use_pos = last_processed_use_;
  if (use_pos == nullptr || use_pos->pos() > start) {
    use_pos = first_pos();
  }
  while (use_pos != nullptr && use_pos->pos() < start) {
    use_pos = use_pos->next();
  }
  last_processed_use_ = use_pos;
  return use_pos;
}


UsePosition* LiveRange::NextUsePositionRegisterIsBeneficial(
    LifetimePosition start) const {
  UsePosition* pos = NextUsePosition(start);
  while (pos != nullptr && !pos->RegisterIsBeneficial()) {
    pos = pos->next();
  }
  return pos;
}


UsePosition* LiveRange::PreviousUsePositionRegisterIsBeneficial(
    LifetimePosition start) const {
  auto pos = first_pos();
  UsePosition* prev = nullptr;
  while (pos != nullptr && pos->pos() < start) {
    if (pos->RegisterIsBeneficial()) prev = pos;
    pos = pos->next();
  }
  return prev;
}


UsePosition* LiveRange::NextRegisterPosition(LifetimePosition start) const {
  UsePosition* pos = NextUsePosition(start);
  while (pos != nullptr && pos->type() != UsePositionType::kRequiresRegister) {
    pos = pos->next();
  }
  return pos;
}


bool LiveRange::CanBeSpilled(LifetimePosition pos) const {
  // We cannot spill a live range that has a use requiring a register
  // at the current or the immediate next position.
  auto use_pos = NextRegisterPosition(pos);
  if (use_pos == nullptr) return true;
  return use_pos->pos() > pos.NextStart().End();
}


InstructionOperand LiveRange::GetAssignedOperand() const {
  if (HasRegisterAssigned()) {
    DCHECK(!IsSpilled());
    switch (Kind()) {
      case GENERAL_REGISTERS:
        return RegisterOperand(assigned_register());
      case DOUBLE_REGISTERS:
        return DoubleRegisterOperand(assigned_register());
      default:
        UNREACHABLE();
    }
  }
  DCHECK(IsSpilled());
  DCHECK(!HasRegisterAssigned());
  auto op = TopLevel()->GetSpillOperand();
  DCHECK(!op->IsUnallocated());
  return *op;
}


UseInterval* LiveRange::FirstSearchIntervalForPosition(
    LifetimePosition position) const {
  if (current_interval_ == nullptr) return first_interval_;
  if (current_interval_->start() > position) {
    current_interval_ = nullptr;
    return first_interval_;
  }
  return current_interval_;
}


void LiveRange::AdvanceLastProcessedMarker(
    UseInterval* to_start_of, LifetimePosition but_not_past) const {
  if (to_start_of == nullptr) return;
  if (to_start_of->start() > but_not_past) return;
  auto start = current_interval_ == nullptr ? LifetimePosition::Invalid()
                                            : current_interval_->start();
  if (to_start_of->start() > start) {
    current_interval_ = to_start_of;
  }
}


void LiveRange::SplitAt(LifetimePosition position, LiveRange* result,
                        Zone* zone) {
  DCHECK(Start() < position);
  DCHECK(result->IsEmpty());
  // Find the last interval that ends before the position. If the
  // position is contained in one of the intervals in the chain, we
  // split that interval and use the first part.
  auto current = FirstSearchIntervalForPosition(position);

  // If the split position coincides with the beginning of a use interval
  // we need to split use positons in a special way.
  bool split_at_start = false;

  if (current->start() == position) {
    // When splitting at start we need to locate the previous use interval.
    current = first_interval_;
  }

  UseInterval* after = nullptr;
  while (current != nullptr) {
    if (current->Contains(position)) {
      after = current->SplitAt(position, zone);
      break;
    }
    auto next = current->next();
    if (next->start() >= position) {
      split_at_start = (next->start() == position);
      break;
    }
    current = next;
  }

  // Partition original use intervals to the two live ranges.
  auto before = current;
  if (after == nullptr) after = before->next();
  result->last_interval_ =
      (last_interval_ == before)
          ? after            // Only interval in the range after split.
          : last_interval_;  // Last interval of the original range.
  result->first_interval_ = after;
  last_interval_ = before;

  // Find the last use position before the split and the first use
  // position after it.
  auto use_after = first_pos_;
  UsePosition* use_before = nullptr;
  if (split_at_start) {
    // The split position coincides with the beginning of a use interval (the
    // end of a lifetime hole). Use at this position should be attributed to
    // the split child because split child owns use interval covering it.
    while (use_after != nullptr && use_after->pos() < position) {
      use_before = use_after;
      use_after = use_after->next();
    }
  } else {
    while (use_after != nullptr && use_after->pos() <= position) {
      use_before = use_after;
      use_after = use_after->next();
    }
  }

  // Partition original use positions to the two live ranges.
  if (use_before != nullptr) {
    use_before->set_next(nullptr);
  } else {
    first_pos_ = nullptr;
  }
  result->first_pos_ = use_after;

  // Discard cached iteration state. It might be pointing
  // to the use that no longer belongs to this live range.
  last_processed_use_ = nullptr;
  current_interval_ = nullptr;

  // Link the new live range in the chain before any of the other
  // ranges linked from the range before the split.
  result->parent_ = (parent_ == nullptr) ? this : parent_;
  result->kind_ = result->parent_->kind_;
  result->next_ = next_;
  next_ = result;

#ifdef DEBUG
  Verify();
  result->Verify();
#endif
}


// This implements an ordering on live ranges so that they are ordered by their
// start positions.  This is needed for the correctness of the register
// allocation algorithm.  If two live ranges start at the same offset then there
// is a tie breaker based on where the value is first used.  This part of the
// ordering is merely a heuristic.
bool LiveRange::ShouldBeAllocatedBefore(const LiveRange* other) const {
  LifetimePosition start = Start();
  LifetimePosition other_start = other->Start();
  if (start == other_start) {
    UsePosition* pos = first_pos();
    if (pos == nullptr) return false;
    UsePosition* other_pos = other->first_pos();
    if (other_pos == nullptr) return true;
    return pos->pos() < other_pos->pos();
  }
  return start < other_start;
}


void LiveRange::ShortenTo(LifetimePosition start) {
  TRACE("Shorten live range %d to [%d\n", id_, start.value());
  DCHECK(first_interval_ != nullptr);
  DCHECK(first_interval_->start() <= start);
  DCHECK(start < first_interval_->end());
  first_interval_->set_start(start);
}


void LiveRange::EnsureInterval(LifetimePosition start, LifetimePosition end,
                               Zone* zone) {
  TRACE("Ensure live range %d in interval [%d %d[\n", id_, start.value(),
        end.value());
  auto new_end = end;
  while (first_interval_ != nullptr && first_interval_->start() <= end) {
    if (first_interval_->end() > end) {
      new_end = first_interval_->end();
    }
    first_interval_ = first_interval_->next();
  }

  auto new_interval = new (zone) UseInterval(start, new_end);
  new_interval->set_next(first_interval_);
  first_interval_ = new_interval;
  if (new_interval->next() == nullptr) {
    last_interval_ = new_interval;
  }
}


void LiveRange::AddUseInterval(LifetimePosition start, LifetimePosition end,
                               Zone* zone) {
  TRACE("Add to live range %d interval [%d %d[\n", id_, start.value(),
        end.value());
  if (first_interval_ == nullptr) {
    auto interval = new (zone) UseInterval(start, end);
    first_interval_ = interval;
    last_interval_ = interval;
  } else {
    if (end == first_interval_->start()) {
      first_interval_->set_start(start);
    } else if (end < first_interval_->start()) {
      auto interval = new (zone) UseInterval(start, end);
      interval->set_next(first_interval_);
      first_interval_ = interval;
    } else {
      // Order of instruction's processing (see ProcessInstructions) guarantees
      // that each new use interval either precedes or intersects with
      // last added interval.
      DCHECK(start < first_interval_->end());
      first_interval_->set_start(Min(start, first_interval_->start()));
      first_interval_->set_end(Max(end, first_interval_->end()));
    }
  }
}


void LiveRange::AddUsePosition(LifetimePosition pos,
                               InstructionOperand* operand,
                               InstructionOperand* hint, Zone* zone) {
  TRACE("Add to live range %d use position %d\n", id_, pos.value());
  auto use_pos = new (zone) UsePosition(pos, operand, hint);
  UsePosition* prev_hint = nullptr;
  UsePosition* prev = nullptr;
  auto current = first_pos_;
  while (current != nullptr && current->pos() < pos) {
    prev_hint = current->HasHint() ? current : prev_hint;
    prev = current;
    current = current->next();
  }

  if (prev == nullptr) {
    use_pos->set_next(first_pos_);
    first_pos_ = use_pos;
  } else {
    use_pos->set_next(prev->next());
    prev->set_next(use_pos);
  }

  if (prev_hint == nullptr && use_pos->HasHint()) {
    current_hint_operand_ = hint;
  }
}


void LiveRange::ConvertUsesToOperand(const InstructionOperand& op,
                                     InstructionOperand* spill_op) {
  for (auto pos = first_pos(); pos != nullptr; pos = pos->next()) {
    DCHECK(Start() <= pos->pos() && pos->pos() <= End());
    if (!pos->HasOperand()) {
      continue;
    }
    switch (pos->type()) {
      case UsePositionType::kRequiresSlot:
        if (spill_op != nullptr) {
          InstructionOperand::ReplaceWith(pos->operand(), spill_op);
        }
        break;
      case UsePositionType::kRequiresRegister:
        DCHECK(op.IsRegister() || op.IsDoubleRegister());
      // Fall through.
      case UsePositionType::kAny:
        InstructionOperand::ReplaceWith(pos->operand(), &op);
        break;
    }
  }
}


bool LiveRange::CanCover(LifetimePosition position) const {
  if (IsEmpty()) return false;
  return Start() <= position && position < End();
}


bool LiveRange::Covers(LifetimePosition position) const {
  if (!CanCover(position)) return false;
  auto start_search = FirstSearchIntervalForPosition(position);
  for (auto interval = start_search; interval != nullptr;
       interval = interval->next()) {
    DCHECK(interval->next() == nullptr ||
           interval->next()->start() >= interval->start());
    AdvanceLastProcessedMarker(interval, position);
    if (interval->Contains(position)) return true;
    if (interval->start() > position) return false;
  }
  return false;
}


LifetimePosition LiveRange::FirstIntersection(LiveRange* other) const {
  auto b = other->first_interval();
  if (b == nullptr) return LifetimePosition::Invalid();
  auto advance_last_processed_up_to = b->start();
  auto a = FirstSearchIntervalForPosition(b->start());
  while (a != nullptr && b != nullptr) {
    if (a->start() > other->End()) break;
    if (b->start() > End()) break;
    auto cur_intersection = a->Intersect(b);
    if (cur_intersection.IsValid()) {
      return cur_intersection;
    }
    if (a->start() < b->start()) {
      a = a->next();
      if (a == nullptr || a->start() > other->End()) break;
      AdvanceLastProcessedMarker(a, advance_last_processed_up_to);
    } else {
      b = b->next();
    }
  }
  return LifetimePosition::Invalid();
}


static bool AreUseIntervalsIntersecting(UseInterval* interval1,
                                        UseInterval* interval2) {
  while (interval1 != nullptr && interval2 != nullptr) {
    if (interval1->start() < interval2->start()) {
      if (interval1->end() > interval2->start()) {
        return true;
      }
      interval1 = interval1->next();
    } else {
      if (interval2->end() > interval1->start()) {
        return true;
      }
      interval2 = interval2->next();
    }
  }
  return false;
}


SpillRange::SpillRange(LiveRange* parent, Zone* zone) : live_ranges_(zone) {
  DCHECK(!parent->IsChild());
  UseInterval* result = nullptr;
  UseInterval* node = nullptr;
  // Copy the intervals for all ranges.
  for (auto range = parent; range != nullptr; range = range->next()) {
    auto src = range->first_interval();
    while (src != nullptr) {
      auto new_node = new (zone) UseInterval(src->start(), src->end());
      if (result == nullptr) {
        result = new_node;
      } else {
        node->set_next(new_node);
      }
      node = new_node;
      src = src->next();
    }
  }
  use_interval_ = result;
  live_ranges().push_back(parent);
  end_position_ = node->end();
  DCHECK(!parent->HasSpillRange());
  parent->SetSpillRange(this);
}


bool SpillRange::IsIntersectingWith(SpillRange* other) const {
  if (this->use_interval_ == nullptr || other->use_interval_ == nullptr ||
      this->End() <= other->use_interval_->start() ||
      other->End() <= this->use_interval_->start()) {
    return false;
  }
  return AreUseIntervalsIntersecting(use_interval_, other->use_interval_);
}


bool SpillRange::TryMerge(SpillRange* other) {
  if (Kind() != other->Kind() || IsIntersectingWith(other)) return false;

  auto max = LifetimePosition::MaxPosition();
  if (End() < other->End() && other->End() != max) {
    end_position_ = other->End();
  }
  other->end_position_ = max;

  MergeDisjointIntervals(other->use_interval_);
  other->use_interval_ = nullptr;

  for (auto range : other->live_ranges()) {
    DCHECK(range->GetSpillRange() == other);
    range->SetSpillRange(this);
  }

  live_ranges().insert(live_ranges().end(), other->live_ranges().begin(),
                       other->live_ranges().end());
  other->live_ranges().clear();

  return true;
}


void SpillRange::SetOperand(AllocatedOperand* op) {
  for (auto range : live_ranges()) {
    DCHECK(range->GetSpillRange() == this);
    range->CommitSpillOperand(op);
  }
}


void SpillRange::MergeDisjointIntervals(UseInterval* other) {
  UseInterval* tail = nullptr;
  auto current = use_interval_;
  while (other != nullptr) {
    // Make sure the 'current' list starts first
    if (current == nullptr || current->start() > other->start()) {
      std::swap(current, other);
    }
    // Check disjointness
    DCHECK(other == nullptr || current->end() <= other->start());
    // Append the 'current' node to the result accumulator and move forward
    if (tail == nullptr) {
      use_interval_ = current;
    } else {
      tail->set_next(current);
    }
    tail = current;
    current = current->next();
  }
  // Other list is empty => we are done
}


RegisterAllocationData::RegisterAllocationData(
    const RegisterConfiguration* config, Zone* zone, Frame* frame,
    InstructionSequence* code, const char* debug_name)
    : allocation_zone_(zone),
      frame_(frame),
      code_(code),
      debug_name_(debug_name),
      config_(config),
      phi_map_(allocation_zone()),
      live_in_sets_(code->InstructionBlockCount(), nullptr, allocation_zone()),
      live_ranges_(code->VirtualRegisterCount() * 2, nullptr,
                   allocation_zone()),
      fixed_live_ranges_(this->config()->num_general_registers(), nullptr,
                         allocation_zone()),
      fixed_double_live_ranges_(this->config()->num_double_registers(), nullptr,
                                allocation_zone()),
      spill_ranges_(allocation_zone()),
      assigned_registers_(nullptr),
      assigned_double_registers_(nullptr) {
  DCHECK(this->config()->num_general_registers() <=
         RegisterConfiguration::kMaxGeneralRegisters);
  DCHECK(this->config()->num_double_registers() <=
         RegisterConfiguration::kMaxDoubleRegisters);
  spill_ranges().reserve(8);
  assigned_registers_ = new (code_zone())
      BitVector(this->config()->num_general_registers(), code_zone());
  assigned_double_registers_ = new (code_zone())
      BitVector(this->config()->num_aliased_double_registers(), code_zone());
  this->frame()->SetAllocatedRegisters(assigned_registers_);
  this->frame()->SetAllocatedDoubleRegisters(assigned_double_registers_);
}


LiveRange* RegisterAllocationData::LiveRangeFor(int index) {
  if (index >= static_cast<int>(live_ranges().size())) {
    live_ranges().resize(index + 1, nullptr);
  }
  auto result = live_ranges()[index];
  if (result == nullptr) {
    result = NewLiveRange(index);
    live_ranges()[index] = result;
  }
  return result;
}


MoveOperands* RegisterAllocationData::AddGapMove(
    int index, Instruction::GapPosition position,
    const InstructionOperand& from, const InstructionOperand& to) {
  auto instr = code()->InstructionAt(index);
  auto moves = instr->GetOrCreateParallelMove(position, code_zone());
  return moves->AddMove(from, to);
}


void RegisterAllocationData::AssignPhiInput(
    LiveRange* range, const InstructionOperand& assignment) {
  DCHECK(range->is_phi());
  auto it = phi_map().find(range->id());
  DCHECK(it != phi_map().end());
  for (auto move : it->second->incoming_moves) {
    move->set_destination(assignment);
  }
}


LiveRange* RegisterAllocationData::NewLiveRange(int index) {
  return new (allocation_zone()) LiveRange(index);
}


bool RegisterAllocationData::ExistsUseWithoutDefinition() {
  bool found = false;
  BitVector::Iterator iterator(live_in_sets()[0]);
  while (!iterator.Done()) {
    found = true;
    int operand_index = iterator.Current();
    PrintF("Register allocator error: live v%d reached first block.\n",
           operand_index);
    LiveRange* range = LiveRangeFor(operand_index);
    PrintF("  (first use is at %d)\n", range->first_pos()->pos().value());
    if (debug_name() == nullptr) {
      PrintF("\n");
    } else {
      PrintF("  (function: %s)\n", debug_name());
    }
    iterator.Advance();
  }
  return found;
}


SpillRange* RegisterAllocationData::AssignSpillRangeToLiveRange(
    LiveRange* range) {
  auto spill_range =
      new (allocation_zone()) SpillRange(range, allocation_zone());
  spill_ranges().push_back(spill_range);
  return spill_range;
}


void RegisterAllocationData::SetLiveRangeAssignedRegister(LiveRange* range,
                                                          int reg) {
  if (range->Kind() == DOUBLE_REGISTERS) {
    assigned_double_registers_->Add(reg);
  } else {
    DCHECK(range->Kind() == GENERAL_REGISTERS);
    assigned_registers_->Add(reg);
  }
  range->set_assigned_register(reg);
  auto assignment = range->GetAssignedOperand();
  // TODO(dcarney): stop aliasing hint operands.
  range->ConvertUsesToOperand(assignment, nullptr);
  if (range->is_phi()) AssignPhiInput(range, assignment);
}


ConstraintBuilder::ConstraintBuilder(RegisterAllocationData* data)
    : data_(data) {}


InstructionOperand* ConstraintBuilder::AllocateFixed(
    UnallocatedOperand* operand, int pos, bool is_tagged) {
  TRACE("Allocating fixed reg for op %d\n", operand->virtual_register());
  DCHECK(operand->HasFixedPolicy());
  InstructionOperand allocated;
  if (operand->HasFixedSlotPolicy()) {
    allocated = AllocatedOperand(AllocatedOperand::STACK_SLOT,
                                 operand->fixed_slot_index());
  } else if (operand->HasFixedRegisterPolicy()) {
    allocated = AllocatedOperand(AllocatedOperand::REGISTER,
                                 operand->fixed_register_index());
  } else if (operand->HasFixedDoubleRegisterPolicy()) {
    allocated = AllocatedOperand(AllocatedOperand::DOUBLE_REGISTER,
                                 operand->fixed_register_index());
  } else {
    UNREACHABLE();
  }
  InstructionOperand::ReplaceWith(operand, &allocated);
  if (is_tagged) {
    TRACE("Fixed reg is tagged at %d\n", pos);
    auto instr = InstructionAt(pos);
    if (instr->HasReferenceMap()) {
      instr->reference_map()->RecordReference(*operand);
    }
  }
  return operand;
}


void ConstraintBuilder::MeetRegisterConstraints() {
  for (auto block : code()->instruction_blocks()) {
    MeetRegisterConstraints(block);
  }
}


void ConstraintBuilder::MeetRegisterConstraints(const InstructionBlock* block) {
  int start = block->first_instruction_index();
  int end = block->last_instruction_index();
  DCHECK_NE(-1, start);
  for (int i = start; i <= end; ++i) {
    MeetConstraintsBefore(i);
    if (i != end) MeetConstraintsAfter(i);
  }
  // Meet register constraints for the instruction in the end.
  MeetRegisterConstraintsForLastInstructionInBlock(block);
}


void ConstraintBuilder::MeetRegisterConstraintsForLastInstructionInBlock(
    const InstructionBlock* block) {
  int end = block->last_instruction_index();
  auto last_instruction = InstructionAt(end);
  for (size_t i = 0; i < last_instruction->OutputCount(); i++) {
    auto output_operand = last_instruction->OutputAt(i);
    DCHECK(!output_operand->IsConstant());
    auto output = UnallocatedOperand::cast(output_operand);
    int output_vreg = output->virtual_register();
    auto range = LiveRangeFor(output_vreg);
    bool assigned = false;
    if (output->HasFixedPolicy()) {
      AllocateFixed(output, -1, false);
      // This value is produced on the stack, we never need to spill it.
      if (output->IsStackSlot()) {
        DCHECK(StackSlotOperand::cast(output)->index() <
               data()->frame()->GetSpillSlotCount());
        range->SetSpillOperand(StackSlotOperand::cast(output));
        range->SetSpillStartIndex(end);
        assigned = true;
      }

      for (auto succ : block->successors()) {
        const InstructionBlock* successor = code()->InstructionBlockAt(succ);
        DCHECK(successor->PredecessorCount() == 1);
        int gap_index = successor->first_instruction_index();
        // Create an unconstrained operand for the same virtual register
        // and insert a gap move from the fixed output to the operand.
        UnallocatedOperand output_copy(UnallocatedOperand::ANY, output_vreg);
        data()->AddGapMove(gap_index, Instruction::START, *output, output_copy);
      }
    }

    if (!assigned) {
      for (auto succ : block->successors()) {
        const InstructionBlock* successor = code()->InstructionBlockAt(succ);
        DCHECK(successor->PredecessorCount() == 1);
        int gap_index = successor->first_instruction_index();
        range->SpillAtDefinition(allocation_zone(), gap_index, output);
        range->SetSpillStartIndex(gap_index);
      }
    }
  }
}


void ConstraintBuilder::MeetConstraintsAfter(int instr_index) {
  auto first = InstructionAt(instr_index);
  // Handle fixed temporaries.
  for (size_t i = 0; i < first->TempCount(); i++) {
    auto temp = UnallocatedOperand::cast(first->TempAt(i));
    if (temp->HasFixedPolicy()) AllocateFixed(temp, instr_index, false);
  }
  // Handle constant/fixed output operands.
  for (size_t i = 0; i < first->OutputCount(); i++) {
    InstructionOperand* output = first->OutputAt(i);
    if (output->IsConstant()) {
      int output_vreg = ConstantOperand::cast(output)->virtual_register();
      auto range = LiveRangeFor(output_vreg);
      range->SetSpillStartIndex(instr_index + 1);
      range->SetSpillOperand(output);
      continue;
    }
    auto first_output = UnallocatedOperand::cast(output);
    auto range = LiveRangeFor(first_output->virtual_register());
    bool assigned = false;
    if (first_output->HasFixedPolicy()) {
      int output_vreg = first_output->virtual_register();
      UnallocatedOperand output_copy(UnallocatedOperand::ANY, output_vreg);
      bool is_tagged = IsReference(output_vreg);
      AllocateFixed(first_output, instr_index, is_tagged);

      // This value is produced on the stack, we never need to spill it.
      if (first_output->IsStackSlot()) {
        DCHECK(StackSlotOperand::cast(first_output)->index() <
               data()->frame()->GetSpillSlotCount());
        range->SetSpillOperand(StackSlotOperand::cast(first_output));
        range->SetSpillStartIndex(instr_index + 1);
        assigned = true;
      }
      data()->AddGapMove(instr_index + 1, Instruction::START, *first_output,
                         output_copy);
    }
    // Make sure we add a gap move for spilling (if we have not done
    // so already).
    if (!assigned) {
      range->SpillAtDefinition(allocation_zone(), instr_index + 1,
                               first_output);
      range->SetSpillStartIndex(instr_index + 1);
    }
  }
}


void ConstraintBuilder::MeetConstraintsBefore(int instr_index) {
  auto second = InstructionAt(instr_index);
  // Handle fixed input operands of second instruction.
  for (size_t i = 0; i < second->InputCount(); i++) {
    auto input = second->InputAt(i);
    if (input->IsImmediate()) continue;  // Ignore immediates.
    auto cur_input = UnallocatedOperand::cast(input);
    if (cur_input->HasFixedPolicy()) {
      int input_vreg = cur_input->virtual_register();
      UnallocatedOperand input_copy(UnallocatedOperand::ANY, input_vreg);
      bool is_tagged = IsReference(input_vreg);
      AllocateFixed(cur_input, instr_index, is_tagged);
      data()->AddGapMove(instr_index, Instruction::END, input_copy, *cur_input);
    }
  }
  // Handle "output same as input" for second instruction.
  for (size_t i = 0; i < second->OutputCount(); i++) {
    auto output = second->OutputAt(i);
    if (!output->IsUnallocated()) continue;
    auto second_output = UnallocatedOperand::cast(output);
    if (!second_output->HasSameAsInputPolicy()) continue;
    DCHECK(i == 0);  // Only valid for first output.
    UnallocatedOperand* cur_input =
        UnallocatedOperand::cast(second->InputAt(0));
    int output_vreg = second_output->virtual_register();
    int input_vreg = cur_input->virtual_register();
    UnallocatedOperand input_copy(UnallocatedOperand::ANY, input_vreg);
    cur_input->set_virtual_register(second_output->virtual_register());
    data()->AddGapMove(instr_index, Instruction::END, input_copy, *cur_input);
    if (IsReference(input_vreg) && !IsReference(output_vreg)) {
      if (second->HasReferenceMap()) {
        second->reference_map()->RecordReference(input_copy);
      }
    } else if (!IsReference(input_vreg) && IsReference(output_vreg)) {
      // The input is assumed to immediately have a tagged representation,
      // before the pointer map can be used. I.e. the pointer map at the
      // instruction will include the output operand (whose value at the
      // beginning of the instruction is equal to the input operand). If
      // this is not desired, then the pointer map at this instruction needs
      // to be adjusted manually.
    }
  }
}


void ConstraintBuilder::ResolvePhis() {
  // Process the blocks in reverse order.
  for (InstructionBlock* block : base::Reversed(code()->instruction_blocks())) {
    ResolvePhis(block);
  }
}


void ConstraintBuilder::ResolvePhis(const InstructionBlock* block) {
  for (auto phi : block->phis()) {
    int phi_vreg = phi->virtual_register();
    auto map_value = new (allocation_zone())
        RegisterAllocationData::PhiMapValue(phi, block, allocation_zone());
    auto res = data()->phi_map().insert(std::make_pair(phi_vreg, map_value));
    DCHECK(res.second);
    USE(res);
    auto& output = phi->output();
    for (size_t i = 0; i < phi->operands().size(); ++i) {
      InstructionBlock* cur_block =
          code()->InstructionBlockAt(block->predecessors()[i]);
      UnallocatedOperand input(UnallocatedOperand::ANY, phi->operands()[i]);
      auto move = data()->AddGapMove(cur_block->last_instruction_index(),
                                     Instruction::END, input, output);
      map_value->incoming_moves.push_back(move);
      DCHECK(!InstructionAt(cur_block->last_instruction_index())
                  ->HasReferenceMap());
    }
    auto live_range = LiveRangeFor(phi_vreg);
    int gap_index = block->first_instruction_index();
    live_range->SpillAtDefinition(allocation_zone(), gap_index, &output);
    live_range->SetSpillStartIndex(gap_index);
    // We use the phi-ness of some nodes in some later heuristics.
    live_range->set_is_phi(true);
    live_range->set_is_non_loop_phi(!block->IsLoopHeader());
  }
}


LiveRangeBuilder::LiveRangeBuilder(RegisterAllocationData* data)
    : data_(data) {}


BitVector* LiveRangeBuilder::ComputeLiveOut(const InstructionBlock* block) {
  // Compute live out for the given block, except not including backward
  // successor edges.
  auto live_out = new (allocation_zone())
      BitVector(code()->VirtualRegisterCount(), allocation_zone());

  // Process all successor blocks.
  for (auto succ : block->successors()) {
    // Add values live on entry to the successor. Note the successor's
    // live_in will not be computed yet for backwards edges.
    auto live_in = live_in_sets()[succ.ToSize()];
    if (live_in != nullptr) live_out->Union(*live_in);

    // All phi input operands corresponding to this successor edge are live
    // out from this block.
    auto successor = code()->InstructionBlockAt(succ);
    size_t index = successor->PredecessorIndexOf(block->rpo_number());
    DCHECK(index < successor->PredecessorCount());
    for (auto phi : successor->phis()) {
      live_out->Add(phi->operands()[index]);
    }
  }
  return live_out;
}


void LiveRangeBuilder::AddInitialIntervals(const InstructionBlock* block,
                                           BitVector* live_out) {
  // Add an interval that includes the entire block to the live range for
  // each live_out value.
  auto start = LifetimePosition::GapFromInstructionIndex(
      block->first_instruction_index());
  auto end = LifetimePosition::InstructionFromInstructionIndex(
                 block->last_instruction_index()).NextStart();
  BitVector::Iterator iterator(live_out);
  while (!iterator.Done()) {
    int operand_index = iterator.Current();
    auto range = LiveRangeFor(operand_index);
    range->AddUseInterval(start, end, allocation_zone());
    iterator.Advance();
  }
}


int LiveRangeBuilder::FixedDoubleLiveRangeID(int index) {
  return -index - 1 - config()->num_general_registers();
}


LiveRange* LiveRangeBuilder::FixedLiveRangeFor(int index) {
  DCHECK(index < config()->num_general_registers());
  auto result = data()->fixed_live_ranges()[index];
  if (result == nullptr) {
    result = data()->NewLiveRange(FixedLiveRangeID(index));
    DCHECK(result->IsFixed());
    result->set_kind(GENERAL_REGISTERS);
    data()->SetLiveRangeAssignedRegister(result, index);
    data()->fixed_live_ranges()[index] = result;
  }
  return result;
}


LiveRange* LiveRangeBuilder::FixedDoubleLiveRangeFor(int index) {
  DCHECK(index < config()->num_aliased_double_registers());
  auto result = data()->fixed_double_live_ranges()[index];
  if (result == nullptr) {
    result = data()->NewLiveRange(FixedDoubleLiveRangeID(index));
    DCHECK(result->IsFixed());
    result->set_kind(DOUBLE_REGISTERS);
    data()->SetLiveRangeAssignedRegister(result, index);
    data()->fixed_double_live_ranges()[index] = result;
  }
  return result;
}


LiveRange* LiveRangeBuilder::LiveRangeFor(InstructionOperand* operand) {
  if (operand->IsUnallocated()) {
    return LiveRangeFor(UnallocatedOperand::cast(operand)->virtual_register());
  } else if (operand->IsConstant()) {
    return LiveRangeFor(ConstantOperand::cast(operand)->virtual_register());
  } else if (operand->IsRegister()) {
    return FixedLiveRangeFor(RegisterOperand::cast(operand)->index());
  } else if (operand->IsDoubleRegister()) {
    return FixedDoubleLiveRangeFor(
        DoubleRegisterOperand::cast(operand)->index());
  } else {
    return nullptr;
  }
}


void LiveRangeBuilder::Define(LifetimePosition position,
                              InstructionOperand* operand,
                              InstructionOperand* hint) {
  auto range = LiveRangeFor(operand);
  if (range == nullptr) return;

  if (range->IsEmpty() || range->Start() > position) {
    // Can happen if there is a definition without use.
    range->AddUseInterval(position, position.NextStart(), allocation_zone());
    range->AddUsePosition(position.NextStart(), nullptr, nullptr,
                          allocation_zone());
  } else {
    range->ShortenTo(position);
  }

  if (operand->IsUnallocated()) {
    auto unalloc_operand = UnallocatedOperand::cast(operand);
    range->AddUsePosition(position, unalloc_operand, hint, allocation_zone());
  }
}


void LiveRangeBuilder::Use(LifetimePosition block_start,
                           LifetimePosition position,
                           InstructionOperand* operand,
                           InstructionOperand* hint) {
  auto range = LiveRangeFor(operand);
  if (range == nullptr) return;
  if (operand->IsUnallocated()) {
    UnallocatedOperand* unalloc_operand = UnallocatedOperand::cast(operand);
    range->AddUsePosition(position, unalloc_operand, hint, allocation_zone());
  }
  range->AddUseInterval(block_start, position, allocation_zone());
}


bool LiveRangeBuilder::IsOutputRegisterOf(Instruction* instr, int index) {
  for (size_t i = 0; i < instr->OutputCount(); i++) {
    auto output = instr->OutputAt(i);
    if (output->IsRegister() && RegisterOperand::cast(output)->index() == index)
      return true;
  }
  return false;
}


bool LiveRangeBuilder::IsOutputDoubleRegisterOf(Instruction* instr, int index) {
  for (size_t i = 0; i < instr->OutputCount(); i++) {
    auto output = instr->OutputAt(i);
    if (output->IsDoubleRegister() &&
        DoubleRegisterOperand::cast(output)->index() == index)
      return true;
  }
  return false;
}


void LiveRangeBuilder::ProcessInstructions(const InstructionBlock* block,
                                           BitVector* live) {
  int block_start = block->first_instruction_index();
  auto block_start_position =
      LifetimePosition::GapFromInstructionIndex(block_start);

  for (int index = block->last_instruction_index(); index >= block_start;
       index--) {
    auto curr_position =
        LifetimePosition::InstructionFromInstructionIndex(index);
    auto instr = code()->InstructionAt(index);
    DCHECK(instr != nullptr);
    DCHECK(curr_position.IsInstructionPosition());
    // Process output, inputs, and temps of this instruction.
    for (size_t i = 0; i < instr->OutputCount(); i++) {
      auto output = instr->OutputAt(i);
      if (output->IsUnallocated()) {
        // Unsupported.
        DCHECK(!UnallocatedOperand::cast(output)->HasSlotPolicy());
        int out_vreg = UnallocatedOperand::cast(output)->virtual_register();
        live->Remove(out_vreg);
      } else if (output->IsConstant()) {
        int out_vreg = ConstantOperand::cast(output)->virtual_register();
        live->Remove(out_vreg);
      }
      Define(curr_position, output, nullptr);
    }

    if (instr->ClobbersRegisters()) {
      for (int i = 0; i < config()->num_general_registers(); ++i) {
        if (!IsOutputRegisterOf(instr, i)) {
          auto range = FixedLiveRangeFor(i);
          range->AddUseInterval(curr_position, curr_position.End(),
                                allocation_zone());
        }
      }
    }

    if (instr->ClobbersDoubleRegisters()) {
      for (int i = 0; i < config()->num_aliased_double_registers(); ++i) {
        if (!IsOutputDoubleRegisterOf(instr, i)) {
          auto range = FixedDoubleLiveRangeFor(i);
          range->AddUseInterval(curr_position, curr_position.End(),
                                allocation_zone());
        }
      }
    }

    for (size_t i = 0; i < instr->InputCount(); i++) {
      auto input = instr->InputAt(i);
      if (input->IsImmediate()) continue;  // Ignore immediates.
      LifetimePosition use_pos;
      if (input->IsUnallocated() &&
          UnallocatedOperand::cast(input)->IsUsedAtStart()) {
        use_pos = curr_position;
      } else {
        use_pos = curr_position.End();
      }

      if (input->IsUnallocated()) {
        UnallocatedOperand* unalloc = UnallocatedOperand::cast(input);
        int vreg = unalloc->virtual_register();
        live->Add(vreg);
        if (unalloc->HasSlotPolicy()) {
          LiveRangeFor(vreg)->set_has_slot_use(true);
        }
      }
      Use(block_start_position, use_pos, input, nullptr);
    }

    for (size_t i = 0; i < instr->TempCount(); i++) {
      auto temp = instr->TempAt(i);
      // Unsupported.
      DCHECK_IMPLIES(temp->IsUnallocated(),
                     !UnallocatedOperand::cast(temp)->HasSlotPolicy());
      if (instr->ClobbersTemps()) {
        if (temp->IsRegister()) continue;
        if (temp->IsUnallocated()) {
          UnallocatedOperand* temp_unalloc = UnallocatedOperand::cast(temp);
          if (temp_unalloc->HasFixedPolicy()) {
            continue;
          }
        }
      }
      Use(block_start_position, curr_position.End(), temp, nullptr);
      Define(curr_position, temp, nullptr);
    }

    // Process the moves of the instruction's gaps, making their sources live.
    const Instruction::GapPosition kPositions[] = {Instruction::END,
                                                   Instruction::START};
    curr_position = curr_position.PrevStart();
    DCHECK(curr_position.IsGapPosition());
    for (auto position : kPositions) {
      auto move = instr->GetParallelMove(position);
      if (move == nullptr) continue;
      if (position == Instruction::END) {
        curr_position = curr_position.End();
      } else {
        curr_position = curr_position.Start();
      }
      for (auto cur : *move) {
        auto& from = cur->source();
        auto& to = cur->destination();
        auto hint = &to;
        if (to.IsUnallocated()) {
          int to_vreg = UnallocatedOperand::cast(to).virtual_register();
          auto to_range = LiveRangeFor(to_vreg);
          if (to_range->is_phi()) {
            if (to_range->is_non_loop_phi()) {
              hint = to_range->current_hint_operand();
            }
          } else {
            if (live->Contains(to_vreg)) {
              Define(curr_position, &to, &from);
              live->Remove(to_vreg);
            } else {
              cur->Eliminate();
              continue;
            }
          }
        } else {
          Define(curr_position, &to, &from);
        }
        Use(block_start_position, curr_position, &from, hint);
        if (from.IsUnallocated()) {
          live->Add(UnallocatedOperand::cast(from).virtual_register());
        }
      }
    }
  }
}


void LiveRangeBuilder::BuildLiveRanges() {
  // Process the blocks in reverse order.
  for (int block_id = code()->InstructionBlockCount() - 1; block_id >= 0;
       --block_id) {
    auto block = code()->InstructionBlockAt(RpoNumber::FromInt(block_id));
    auto live = ComputeLiveOut(block);
    // Initially consider all live_out values live for the entire block. We
    // will shorten these intervals if necessary.
    AddInitialIntervals(block, live);

    // Process the instructions in reverse order, generating and killing
    // live values.
    ProcessInstructions(block, live);
    // All phi output operands are killed by this block.
    for (auto phi : block->phis()) {
      // The live range interval already ends at the first instruction of the
      // block.
      int phi_vreg = phi->virtual_register();
      live->Remove(phi_vreg);
      InstructionOperand* hint = nullptr;
      auto instr = GetLastInstruction(
          code(), code()->InstructionBlockAt(block->predecessors()[0]));
      for (auto move : *instr->GetParallelMove(Instruction::END)) {
        auto& to = move->destination();
        if (to.IsUnallocated() &&
            UnallocatedOperand::cast(to).virtual_register() == phi_vreg) {
          hint = &move->source();
          break;
        }
      }
      DCHECK(hint != nullptr);
      auto block_start = LifetimePosition::GapFromInstructionIndex(
          block->first_instruction_index());
      Define(block_start, &phi->output(), hint);
    }

    // Now live is live_in for this block except not including values live
    // out on backward successor edges.
    live_in_sets()[block_id] = live;

    if (block->IsLoopHeader()) {
      // Add a live range stretching from the first loop instruction to the last
      // for each value live on entry to the header.
      BitVector::Iterator iterator(live);
      auto start = LifetimePosition::GapFromInstructionIndex(
          block->first_instruction_index());
      auto end = LifetimePosition::GapFromInstructionIndex(
                     code()->LastLoopInstructionIndex(block)).NextFullStart();
      while (!iterator.Done()) {
        int operand_index = iterator.Current();
        auto range = LiveRangeFor(operand_index);
        range->EnsureInterval(start, end, allocation_zone());
        iterator.Advance();
      }
      // Insert all values into the live in sets of all blocks in the loop.
      for (int i = block->rpo_number().ToInt() + 1;
           i < block->loop_end().ToInt(); ++i) {
        live_in_sets()[i]->Union(*live);
      }
    }
  }

  for (auto range : data()->live_ranges()) {
    if (range == nullptr) continue;
    range->set_kind(RequiredRegisterKind(range->id()));
    // Give slots to all ranges with a non fixed slot use.
    if (range->has_slot_use() && range->HasNoSpillType()) {
      data()->AssignSpillRangeToLiveRange(range);
    }
    // TODO(bmeurer): This is a horrible hack to make sure that for constant
    // live ranges, every use requires the constant to be in a register.
    // Without this hack, all uses with "any" policy would get the constant
    // operand assigned.
    if (range->HasSpillOperand() && range->GetSpillOperand()->IsConstant()) {
      for (auto pos = range->first_pos(); pos != nullptr; pos = pos->next()) {
        if (pos->type() == UsePositionType::kRequiresSlot) continue;
        UsePositionType new_type = UsePositionType::kAny;
        // Can't mark phis as needing a register.
        if (!pos->pos().IsGapPosition()) {
          new_type = UsePositionType::kRequiresRegister;
        }
        pos->set_type(new_type, true);
      }
    }
  }

#ifdef DEBUG
  Verify();
#endif
}


RegisterKind LiveRangeBuilder::RequiredRegisterKind(
    int virtual_register) const {
  return (code()->IsDouble(virtual_register)) ? DOUBLE_REGISTERS
                                              : GENERAL_REGISTERS;
}


void LiveRangeBuilder::Verify() const {
  for (auto current : data()->live_ranges()) {
    if (current != nullptr) current->Verify();
  }
}


RegisterAllocator::RegisterAllocator(RegisterAllocationData* data,
                                     RegisterKind kind)
    : data_(data),
      mode_(kind),
      num_registers_(GetRegisterCount(data->config(), kind)) {}


LiveRange* RegisterAllocator::SplitRangeAt(LiveRange* range,
                                           LifetimePosition pos) {
  DCHECK(!range->IsFixed());
  TRACE("Splitting live range %d at %d\n", range->id(), pos.value());

  if (pos <= range->Start()) return range;

  // We can't properly connect liveranges if splitting occurred at the end
  // a block.
  DCHECK(pos.IsStart() || pos.IsGapPosition() ||
         (GetInstructionBlock(code(), pos)->last_instruction_index() !=
          pos.ToInstructionIndex()));

  int vreg = code()->NextVirtualRegister();
  auto result = LiveRangeFor(vreg);
  range->SplitAt(pos, result, allocation_zone());
  return result;
}


LiveRange* RegisterAllocator::SplitBetween(LiveRange* range,
                                           LifetimePosition start,
                                           LifetimePosition end) {
  DCHECK(!range->IsFixed());
  TRACE("Splitting live range %d in position between [%d, %d]\n", range->id(),
        start.value(), end.value());

  auto split_pos = FindOptimalSplitPos(start, end);
  DCHECK(split_pos >= start);
  return SplitRangeAt(range, split_pos);
}


LifetimePosition RegisterAllocator::FindOptimalSplitPos(LifetimePosition start,
                                                        LifetimePosition end) {
  int start_instr = start.ToInstructionIndex();
  int end_instr = end.ToInstructionIndex();
  DCHECK(start_instr <= end_instr);

  // We have no choice
  if (start_instr == end_instr) return end;

  auto start_block = GetInstructionBlock(code(), start);
  auto end_block = GetInstructionBlock(code(), end);

  if (end_block == start_block) {
    // The interval is split in the same basic block. Split at the latest
    // possible position.
    return end;
  }

  auto block = end_block;
  // Find header of outermost loop.
  // TODO(titzer): fix redundancy below.
  while (GetContainingLoop(code(), block) != nullptr &&
         GetContainingLoop(code(), block)->rpo_number().ToInt() >
             start_block->rpo_number().ToInt()) {
    block = GetContainingLoop(code(), block);
  }

  // We did not find any suitable outer loop. Split at the latest possible
  // position unless end_block is a loop header itself.
  if (block == end_block && !end_block->IsLoopHeader()) return end;

  return LifetimePosition::GapFromInstructionIndex(
      block->first_instruction_index());
}


LifetimePosition RegisterAllocator::FindOptimalSpillingPos(
    LiveRange* range, LifetimePosition pos) {
  auto block = GetInstructionBlock(code(), pos.Start());
  auto loop_header =
      block->IsLoopHeader() ? block : GetContainingLoop(code(), block);

  if (loop_header == nullptr) return pos;

  auto prev_use = range->PreviousUsePositionRegisterIsBeneficial(pos);

  while (loop_header != nullptr) {
    // We are going to spill live range inside the loop.
    // If possible try to move spilling position backwards to loop header.
    // This will reduce number of memory moves on the back edge.
    auto loop_start = LifetimePosition::GapFromInstructionIndex(
        loop_header->first_instruction_index());

    if (range->Covers(loop_start)) {
      if (prev_use == nullptr || prev_use->pos() < loop_start) {
        // No register beneficial use inside the loop before the pos.
        pos = loop_start;
      }
    }

    // Try hoisting out to an outer loop.
    loop_header = GetContainingLoop(code(), loop_header);
  }

  return pos;
}


void RegisterAllocator::Spill(LiveRange* range) {
  DCHECK(!range->IsSpilled());
  TRACE("Spilling live range %d\n", range->id());
  auto first = range->TopLevel();
  if (first->HasNoSpillType()) {
    data()->AssignSpillRangeToLiveRange(first);
  }
  range->MakeSpilled();
}


LinearScanAllocator::LinearScanAllocator(RegisterAllocationData* data,
                                         RegisterKind kind, Zone* local_zone)
    : RegisterAllocator(data, kind),
      unhandled_live_ranges_(local_zone),
      active_live_ranges_(local_zone),
      inactive_live_ranges_(local_zone) {
  unhandled_live_ranges().reserve(
      static_cast<size_t>(code()->VirtualRegisterCount() * 2));
  active_live_ranges().reserve(8);
  inactive_live_ranges().reserve(8);
  // TryAllocateFreeReg and AllocateBlockedReg assume this
  // when allocating local arrays.
  DCHECK(RegisterConfiguration::kMaxDoubleRegisters >=
         this->data()->config()->num_general_registers());
}


void LinearScanAllocator::AllocateRegisters() {
  DCHECK(unhandled_live_ranges().empty());
  DCHECK(active_live_ranges().empty());
  DCHECK(inactive_live_ranges().empty());

  for (auto range : data()->live_ranges()) {
    if (range == nullptr) continue;
    if (range->Kind() == mode()) {
      AddToUnhandledUnsorted(range);
    }
  }
  SortUnhandled();
  DCHECK(UnhandledIsSorted());

  auto& fixed_ranges = GetFixedRegisters(data(), mode());
  for (auto current : fixed_ranges) {
    if (current != nullptr) {
      DCHECK_EQ(mode(), current->Kind());
      AddToInactive(current);
    }
  }

  while (!unhandled_live_ranges().empty()) {
    DCHECK(UnhandledIsSorted());
    auto current = unhandled_live_ranges().back();
    unhandled_live_ranges().pop_back();
    DCHECK(UnhandledIsSorted());
    auto position = current->Start();
#ifdef DEBUG
    allocation_finger_ = position;
#endif
    TRACE("Processing interval %d start=%d\n", current->id(), position.value());

    if (!current->HasNoSpillType()) {
      TRACE("Live range %d already has a spill operand\n", current->id());
      auto next_pos = position;
      if (next_pos.IsGapPosition()) {
        next_pos = next_pos.NextStart();
      }
      auto pos = current->NextUsePositionRegisterIsBeneficial(next_pos);
      // If the range already has a spill operand and it doesn't need a
      // register immediately, split it and spill the first part of the range.
      if (pos == nullptr) {
        Spill(current);
        continue;
      } else if (pos->pos() > current->Start().NextStart()) {
        // Do not spill live range eagerly if use position that can benefit from
        // the register is too close to the start of live range.
        SpillBetween(current, current->Start(), pos->pos());
        DCHECK(UnhandledIsSorted());
        continue;
      }
    }

    if (TryReuseSpillForPhi(current)) continue;

    for (size_t i = 0; i < active_live_ranges().size(); ++i) {
      auto cur_active = active_live_ranges()[i];
      if (cur_active->End() <= position) {
        ActiveToHandled(cur_active);
        --i;  // The live range was removed from the list of active live ranges.
      } else if (!cur_active->Covers(position)) {
        ActiveToInactive(cur_active);
        --i;  // The live range was removed from the list of active live ranges.
      }
    }

    for (size_t i = 0; i < inactive_live_ranges().size(); ++i) {
      auto cur_inactive = inactive_live_ranges()[i];
      if (cur_inactive->End() <= position) {
        InactiveToHandled(cur_inactive);
        --i;  // Live range was removed from the list of inactive live ranges.
      } else if (cur_inactive->Covers(position)) {
        InactiveToActive(cur_inactive);
        --i;  // Live range was removed from the list of inactive live ranges.
      }
    }

    DCHECK(!current->HasRegisterAssigned() && !current->IsSpilled());

    bool result = TryAllocateFreeReg(current);
    if (!result) AllocateBlockedReg(current);
    if (current->HasRegisterAssigned()) {
      AddToActive(current);
    }
  }
}


const char* LinearScanAllocator::RegisterName(int allocation_index) const {
  if (mode() == GENERAL_REGISTERS) {
    return data()->config()->general_register_name(allocation_index);
  } else {
    return data()->config()->double_register_name(allocation_index);
  }
}


void LinearScanAllocator::AddToActive(LiveRange* range) {
  TRACE("Add live range %d to active\n", range->id());
  active_live_ranges().push_back(range);
}


void LinearScanAllocator::AddToInactive(LiveRange* range) {
  TRACE("Add live range %d to inactive\n", range->id());
  inactive_live_ranges().push_back(range);
}


void LinearScanAllocator::AddToUnhandledSorted(LiveRange* range) {
  if (range == nullptr || range->IsEmpty()) return;
  DCHECK(!range->HasRegisterAssigned() && !range->IsSpilled());
  DCHECK(allocation_finger_ <= range->Start());
  for (int i = static_cast<int>(unhandled_live_ranges().size() - 1); i >= 0;
       --i) {
    auto cur_range = unhandled_live_ranges().at(i);
    if (!range->ShouldBeAllocatedBefore(cur_range)) continue;
    TRACE("Add live range %d to unhandled at %d\n", range->id(), i + 1);
    auto it = unhandled_live_ranges().begin() + (i + 1);
    unhandled_live_ranges().insert(it, range);
    DCHECK(UnhandledIsSorted());
    return;
  }
  TRACE("Add live range %d to unhandled at start\n", range->id());
  unhandled_live_ranges().insert(unhandled_live_ranges().begin(), range);
  DCHECK(UnhandledIsSorted());
}


void LinearScanAllocator::AddToUnhandledUnsorted(LiveRange* range) {
  if (range == nullptr || range->IsEmpty()) return;
  DCHECK(!range->HasRegisterAssigned() && !range->IsSpilled());
  TRACE("Add live range %d to unhandled unsorted at end\n", range->id());
  unhandled_live_ranges().push_back(range);
}


static bool UnhandledSortHelper(LiveRange* a, LiveRange* b) {
  DCHECK(!a->ShouldBeAllocatedBefore(b) || !b->ShouldBeAllocatedBefore(a));
  if (a->ShouldBeAllocatedBefore(b)) return false;
  if (b->ShouldBeAllocatedBefore(a)) return true;
  return a->id() < b->id();
}


// Sort the unhandled live ranges so that the ranges to be processed first are
// at the end of the array list.  This is convenient for the register allocation
// algorithm because it is efficient to remove elements from the end.
void LinearScanAllocator::SortUnhandled() {
  TRACE("Sort unhandled\n");
  std::sort(unhandled_live_ranges().begin(), unhandled_live_ranges().end(),
            &UnhandledSortHelper);
}


bool LinearScanAllocator::UnhandledIsSorted() {
  size_t len = unhandled_live_ranges().size();
  for (size_t i = 1; i < len; i++) {
    auto a = unhandled_live_ranges().at(i - 1);
    auto b = unhandled_live_ranges().at(i);
    if (a->Start() < b->Start()) return false;
  }
  return true;
}


void LinearScanAllocator::ActiveToHandled(LiveRange* range) {
  RemoveElement(&active_live_ranges(), range);
  TRACE("Moving live range %d from active to handled\n", range->id());
}


void LinearScanAllocator::ActiveToInactive(LiveRange* range) {
  RemoveElement(&active_live_ranges(), range);
  inactive_live_ranges().push_back(range);
  TRACE("Moving live range %d from active to inactive\n", range->id());
}


void LinearScanAllocator::InactiveToHandled(LiveRange* range) {
  RemoveElement(&inactive_live_ranges(), range);
  TRACE("Moving live range %d from inactive to handled\n", range->id());
}


void LinearScanAllocator::InactiveToActive(LiveRange* range) {
  RemoveElement(&inactive_live_ranges(), range);
  active_live_ranges().push_back(range);
  TRACE("Moving live range %d from inactive to active\n", range->id());
}


bool LinearScanAllocator::TryAllocateFreeReg(LiveRange* current) {
  LifetimePosition free_until_pos[RegisterConfiguration::kMaxDoubleRegisters];

  for (int i = 0; i < num_registers(); i++) {
    free_until_pos[i] = LifetimePosition::MaxPosition();
  }

  for (auto cur_active : active_live_ranges()) {
    free_until_pos[cur_active->assigned_register()] =
        LifetimePosition::GapFromInstructionIndex(0);
  }

  for (auto cur_inactive : inactive_live_ranges()) {
    DCHECK(cur_inactive->End() > current->Start());
    auto next_intersection = cur_inactive->FirstIntersection(current);
    if (!next_intersection.IsValid()) continue;
    int cur_reg = cur_inactive->assigned_register();
    free_until_pos[cur_reg] = Min(free_until_pos[cur_reg], next_intersection);
  }

  auto hint = current->FirstHint();
  if (hint != nullptr && (hint->IsRegister() || hint->IsDoubleRegister())) {
    int register_index = AllocatedOperand::cast(hint)->index();
    TRACE("Found reg hint %s (free until [%d) for live range %d (end %d[).\n",
          RegisterName(register_index), free_until_pos[register_index].value(),
          current->id(), current->End().value());

    // The desired register is free until the end of the current live range.
    if (free_until_pos[register_index] >= current->End()) {
      TRACE("Assigning preferred reg %s to live range %d\n",
            RegisterName(register_index), current->id());
      data()->SetLiveRangeAssignedRegister(current, register_index);
      return true;
    }
  }

  // Find the register which stays free for the longest time.
  int reg = 0;
  for (int i = 1; i < num_registers(); ++i) {
    if (free_until_pos[i] > free_until_pos[reg]) {
      reg = i;
    }
  }

  auto pos = free_until_pos[reg];

  if (pos <= current->Start()) {
    // All registers are blocked.
    return false;
  }

  if (pos < current->End()) {
    // Register reg is available at the range start but becomes blocked before
    // the range end. Split current at position where it becomes blocked.
    auto tail = SplitRangeAt(current, pos);
    AddToUnhandledSorted(tail);
  }

  // Register reg is available at the range start and is free until
  // the range end.
  DCHECK(pos >= current->End());
  TRACE("Assigning free reg %s to live range %d\n", RegisterName(reg),
        current->id());
  data()->SetLiveRangeAssignedRegister(current, reg);

  return true;
}


void LinearScanAllocator::AllocateBlockedReg(LiveRange* current) {
  auto register_use = current->NextRegisterPosition(current->Start());
  if (register_use == nullptr) {
    // There is no use in the current live range that requires a register.
    // We can just spill it.
    Spill(current);
    return;
  }

  LifetimePosition use_pos[RegisterConfiguration::kMaxDoubleRegisters];
  LifetimePosition block_pos[RegisterConfiguration::kMaxDoubleRegisters];

  for (int i = 0; i < num_registers(); i++) {
    use_pos[i] = block_pos[i] = LifetimePosition::MaxPosition();
  }

  for (auto range : active_live_ranges()) {
    int cur_reg = range->assigned_register();
    if (range->IsFixed() || !range->CanBeSpilled(current->Start())) {
      block_pos[cur_reg] = use_pos[cur_reg] =
          LifetimePosition::GapFromInstructionIndex(0);
    } else {
      auto next_use =
          range->NextUsePositionRegisterIsBeneficial(current->Start());
      if (next_use == nullptr) {
        use_pos[cur_reg] = range->End();
      } else {
        use_pos[cur_reg] = next_use->pos();
      }
    }
  }

  for (auto range : inactive_live_ranges()) {
    DCHECK(range->End() > current->Start());
    auto next_intersection = range->FirstIntersection(current);
    if (!next_intersection.IsValid()) continue;
    int cur_reg = range->assigned_register();
    if (range->IsFixed()) {
      block_pos[cur_reg] = Min(block_pos[cur_reg], next_intersection);
      use_pos[cur_reg] = Min(block_pos[cur_reg], use_pos[cur_reg]);
    } else {
      use_pos[cur_reg] = Min(use_pos[cur_reg], next_intersection);
    }
  }

  int reg = 0;
  for (int i = 1; i < num_registers(); ++i) {
    if (use_pos[i] > use_pos[reg]) {
      reg = i;
    }
  }

  auto pos = use_pos[reg];

  if (pos < register_use->pos()) {
    // All registers are blocked before the first use that requires a register.
    // Spill starting part of live range up to that use.
    SpillBetween(current, current->Start(), register_use->pos());
    return;
  }

  if (block_pos[reg] < current->End()) {
    // Register becomes blocked before the current range end. Split before that
    // position.
    LiveRange* tail =
        SplitBetween(current, current->Start(), block_pos[reg].Start());
    AddToUnhandledSorted(tail);
  }

  // Register reg is not blocked for the whole range.
  DCHECK(block_pos[reg] >= current->End());
  TRACE("Assigning blocked reg %s to live range %d\n", RegisterName(reg),
        current->id());
  data()->SetLiveRangeAssignedRegister(current, reg);

  // This register was not free. Thus we need to find and spill
  // parts of active and inactive live regions that use the same register
  // at the same lifetime positions as current.
  SplitAndSpillIntersecting(current);
}


void LinearScanAllocator::SplitAndSpillIntersecting(LiveRange* current) {
  DCHECK(current->HasRegisterAssigned());
  int reg = current->assigned_register();
  auto split_pos = current->Start();
  for (size_t i = 0; i < active_live_ranges().size(); ++i) {
    auto range = active_live_ranges()[i];
    if (range->assigned_register() == reg) {
      auto next_pos = range->NextRegisterPosition(current->Start());
      auto spill_pos = FindOptimalSpillingPos(range, split_pos);
      if (next_pos == nullptr) {
        SpillAfter(range, spill_pos);
      } else {
        // When spilling between spill_pos and next_pos ensure that the range
        // remains spilled at least until the start of the current live range.
        // This guarantees that we will not introduce new unhandled ranges that
        // start before the current range as this violates allocation invariant
        // and will lead to an inconsistent state of active and inactive
        // live-ranges: ranges are allocated in order of their start positions,
        // ranges are retired from active/inactive when the start of the
        // current live-range is larger than their end.
        SpillBetweenUntil(range, spill_pos, current->Start(), next_pos->pos());
      }
      ActiveToHandled(range);
      --i;
    }
  }

  for (size_t i = 0; i < inactive_live_ranges().size(); ++i) {
    auto range = inactive_live_ranges()[i];
    DCHECK(range->End() > current->Start());
    if (range->assigned_register() == reg && !range->IsFixed()) {
      LifetimePosition next_intersection = range->FirstIntersection(current);
      if (next_intersection.IsValid()) {
        UsePosition* next_pos = range->NextRegisterPosition(current->Start());
        if (next_pos == nullptr) {
          SpillAfter(range, split_pos);
        } else {
          next_intersection = Min(next_intersection, next_pos->pos());
          SpillBetween(range, split_pos, next_intersection);
        }
        InactiveToHandled(range);
        --i;
      }
    }
  }
}


bool LinearScanAllocator::TryReuseSpillForPhi(LiveRange* range) {
  if (range->IsChild() || !range->is_phi()) return false;
  DCHECK(!range->HasSpillOperand());

  auto lookup = data()->phi_map().find(range->id());
  DCHECK(lookup != data()->phi_map().end());
  auto phi = lookup->second->phi;
  auto block = lookup->second->block;
  // Count the number of spilled operands.
  size_t spilled_count = 0;
  LiveRange* first_op = nullptr;
  for (size_t i = 0; i < phi->operands().size(); i++) {
    int op = phi->operands()[i];
    LiveRange* op_range = LiveRangeFor(op);
    if (!op_range->HasSpillRange()) continue;
    auto pred = code()->InstructionBlockAt(block->predecessors()[i]);
    auto pred_end = LifetimePosition::InstructionFromInstructionIndex(
        pred->last_instruction_index());
    while (op_range != nullptr && !op_range->CanCover(pred_end)) {
      op_range = op_range->next();
    }
    if (op_range != nullptr && op_range->IsSpilled()) {
      spilled_count++;
      if (first_op == nullptr) {
        first_op = op_range->TopLevel();
      }
    }
  }

  // Only continue if more than half of the operands are spilled.
  if (spilled_count * 2 <= phi->operands().size()) {
    return false;
  }

  // Try to merge the spilled operands and count the number of merged spilled
  // operands.
  DCHECK(first_op != nullptr);
  auto first_op_spill = first_op->GetSpillRange();
  size_t num_merged = 1;
  for (size_t i = 1; i < phi->operands().size(); i++) {
    int op = phi->operands()[i];
    auto op_range = LiveRangeFor(op);
    if (!op_range->HasSpillRange()) continue;
    auto op_spill = op_range->GetSpillRange();
    if (op_spill == first_op_spill || first_op_spill->TryMerge(op_spill)) {
      num_merged++;
    }
  }

  // Only continue if enough operands could be merged to the
  // same spill slot.
  if (num_merged * 2 <= phi->operands().size() ||
      AreUseIntervalsIntersecting(first_op_spill->interval(),
                                  range->first_interval())) {
    return false;
  }

  // If the range does not need register soon, spill it to the merged
  // spill range.
  auto next_pos = range->Start();
  if (next_pos.IsGapPosition()) next_pos = next_pos.NextStart();
  auto pos = range->NextUsePositionRegisterIsBeneficial(next_pos);
  if (pos == nullptr) {
    auto spill_range =
        range->TopLevel()->HasSpillRange()
            ? range->TopLevel()->GetSpillRange()
            : data()->AssignSpillRangeToLiveRange(range->TopLevel());
    bool merged = first_op_spill->TryMerge(spill_range);
    CHECK(merged);
    Spill(range);
    return true;
  } else if (pos->pos() > range->Start().NextStart()) {
    auto spill_range =
        range->TopLevel()->HasSpillRange()
            ? range->TopLevel()->GetSpillRange()
            : data()->AssignSpillRangeToLiveRange(range->TopLevel());
    bool merged = first_op_spill->TryMerge(spill_range);
    CHECK(merged);
    SpillBetween(range, range->Start(), pos->pos());
    DCHECK(UnhandledIsSorted());
    return true;
  }
  return false;
}


void LinearScanAllocator::SpillAfter(LiveRange* range, LifetimePosition pos) {
  auto second_part = SplitRangeAt(range, pos);
  Spill(second_part);
}


void LinearScanAllocator::SpillBetween(LiveRange* range, LifetimePosition start,
                                       LifetimePosition end) {
  SpillBetweenUntil(range, start, start, end);
}


void LinearScanAllocator::SpillBetweenUntil(LiveRange* range,
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
    if (IsBlockBoundary(code(), end.Start())) {
      third_part_end = end.Start();
    }
    auto third_part = SplitBetween(
        second_part, Max(second_part->Start().End(), until), third_part_end);

    DCHECK(third_part != second_part);

    Spill(second_part);
    AddToUnhandledSorted(third_part);
  } else {
    // The split result does not intersect with [start, end[.
    // Nothing to spill. Just put it to unhandled as whole.
    AddToUnhandledSorted(second_part);
  }
}


class CoallescedLiveRanges : public ZoneObject {
 public:
  explicit CoallescedLiveRanges(Zone* zone) : storage_(zone) {}

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
  DISALLOW_COPY_AND_ASSIGN(CoallescedLiveRanges);
};


const std::pair<int, int> CoallescedLiveRanges::Config::kNoKey = {0, 0};

GreedyAllocator::GreedyAllocator(RegisterAllocationData* data,
                                 RegisterKind kind, Zone* local_zone)
    : RegisterAllocator(data, kind),
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
}


float GreedyAllocator::CalculateSpillWeight(LiveRange* range) {
  if (range->IsFixed()) return std::numeric_limits<float>::max();

  if (range->FirstHint() != nullptr && range->FirstHint()->IsRegister()) {
    return std::numeric_limits<float>::max();
  }

  unsigned use_count = 0;
  auto* pos = range->first_pos();
  while (pos != nullptr) {
    use_count++;
    pos = pos->next();
  }

  // GetLiveRangeSize is DCHECK-ed to not be 0
  unsigned range_size = GetLiveRangeSize(range);
  DCHECK_NE(0U, range_size);

  return static_cast<float>(use_count) / static_cast<float>(range_size);
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

bool GreedyAllocator::TryAllocate(LiveRange* current,
                                  ZoneSet<LiveRange*>* conflicting) {
  if (current->HasSpillOperand()) {
    Spill(current);
    return true;
  }
  if (current->IsFixed()) {
    return TryAllocatePhysicalRegister(current->assigned_register(), current,
                                       conflicting);
  }

  if (current->HasRegisterAssigned()) {
    int reg_id = current->assigned_register();
    return TryAllocatePhysicalRegister(reg_id, current, conflicting);
  }

  for (unsigned candidate_reg = 0; candidate_reg < allocations_.size();
       candidate_reg++) {
    if (TryAllocatePhysicalRegister(candidate_reg, current, conflicting)) {
      conflicting->clear();
      return true;
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
    if (IsBlockBoundary(code(), end.Start())) {
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
  queue_.push(std::make_pair(size, range));
}


// TODO(mtrofin): consolidate with identical code segment in
// LinearScanAllocator::AllocateRegisters
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
  // TODO(mtrofin): Do we need this?
  // return (TryReuseSpillForPhi(range));
}


void GreedyAllocator::AllocateRegisters() {
  for (auto range : data()->live_ranges()) {
    if (range == nullptr) continue;
    if (range->Kind() == mode()) {
      DCHECK(!range->HasRegisterAssigned() && !range->IsSpilled());
      TRACE("Enqueueing live range %d to priority queue \n", range->id());
      Enqueue(range);
    }
  }

  allocations_.resize(num_registers());
  auto* zone = data()->allocation_zone();
  for (int i = 0; i < num_registers(); i++) {
    allocations_[i] = new (zone) CoallescedLiveRanges(zone);
  }

  for (auto* current : GetFixedRegisters(data(), mode())) {
    if (current != nullptr) {
      DCHECK_EQ(mode(), current->Kind());
      int reg_nr = current->assigned_register();
      bool inserted = allocations_[reg_nr]->Insert(current);
      CHECK(inserted);
    }
  }

  while (!queue_.empty()) {
    auto current_pair = queue_.top();
    queue_.pop();
    auto current = current_pair.second;
    if (HandleSpillOperands(current)) continue;
    ZoneSet<LiveRange*> conflicting(zone);
    if (!TryAllocate(current, &conflicting)) {
      DCHECK(!conflicting.empty());
      float this_max = CalculateSpillWeight(current);
      float max_conflicting = CalculateMaxSpillWeight(conflicting);
      if (max_conflicting < this_max) {
        for (auto* conflict : conflicting) {
          Evict(conflict);
          Enqueue(conflict);
        }
        conflicting.clear();
        bool allocated = TryAllocate(current, &conflicting);
        CHECK(allocated);
        DCHECK(conflicting.empty());
      } else {
        DCHECK(!current->IsFixed() || current->CanBeSpilled(current->Start()));
        bool allocated = AllocateBlockedRange(current, conflicting);
        CHECK(allocated);
      }
    }
  }

  BitVector* assigned_registers = new (zone) BitVector(num_registers(), zone);
  for (size_t i = 0; i < allocations_.size(); ++i) {
    if (!allocations_[i]->IsEmpty()) {
      assigned_registers->Add(static_cast<int>(i));
    }
  }
  data()->frame()->SetAllocatedRegisters(assigned_registers);
}


bool GreedyAllocator::AllocateBlockedRange(
    LiveRange* current, const ZoneSet<LiveRange*>& conflicts) {
  auto register_use = current->NextRegisterPosition(current->Start());
  if (register_use == nullptr) {
    // There is no use in the current live range that requires a register.
    // We can just spill it.
    Spill(current);
    return true;
  }

  auto second_part = SplitRangeAt(current, register_use->pos());
  Spill(second_part);

  return true;
}


OperandAssigner::OperandAssigner(RegisterAllocationData* data) : data_(data) {}


void OperandAssigner::AssignSpillSlots() {
  auto& spill_ranges = data()->spill_ranges();
  // Merge disjoint spill ranges
  for (size_t i = 0; i < spill_ranges.size(); i++) {
    auto range = spill_ranges[i];
    if (range->IsEmpty()) continue;
    for (size_t j = i + 1; j < spill_ranges.size(); j++) {
      auto other = spill_ranges[j];
      if (!other->IsEmpty()) {
        range->TryMerge(other);
      }
    }
  }
  // Allocate slots for the merged spill ranges.
  for (auto range : spill_ranges) {
    if (range->IsEmpty()) continue;
    // Allocate a new operand referring to the spill slot.
    auto kind = range->Kind();
    int index = data()->frame()->AllocateSpillSlot(kind == DOUBLE_REGISTERS);
    auto op_kind = kind == DOUBLE_REGISTERS
                       ? AllocatedOperand::DOUBLE_STACK_SLOT
                       : AllocatedOperand::STACK_SLOT;
    auto op = AllocatedOperand::New(data()->code_zone(), op_kind, index);
    range->SetOperand(op);
  }
}


void OperandAssigner::CommitAssignment() {
  for (auto range : data()->live_ranges()) {
    if (range == nullptr || range->IsEmpty()) continue;
    InstructionOperand* spill_operand = nullptr;
    if (!range->TopLevel()->HasNoSpillType()) {
      spill_operand = range->TopLevel()->GetSpillOperand();
    }
    auto assigned = range->GetAssignedOperand();
    range->ConvertUsesToOperand(assigned, spill_operand);
    if (range->is_phi()) data()->AssignPhiInput(range, assigned);
    if (!range->IsChild() && spill_operand != nullptr) {
      range->CommitSpillsAtDefinition(data()->code(), spill_operand,
                                      range->has_slot_use());
    }
  }
}


ReferenceMapPopulator::ReferenceMapPopulator(RegisterAllocationData* data)
    : data_(data) {}


bool ReferenceMapPopulator::SafePointsAreInOrder() const {
  int safe_point = 0;
  for (auto map : *data()->code()->reference_maps()) {
    if (safe_point > map->instruction_position()) return false;
    safe_point = map->instruction_position();
  }
  return true;
}


void ReferenceMapPopulator::PopulateReferenceMaps() {
  DCHECK(SafePointsAreInOrder());

  // Iterate over all safe point positions and record a pointer
  // for all spilled live ranges at this point.
  int last_range_start = 0;
  auto reference_maps = data()->code()->reference_maps();
  ReferenceMapDeque::const_iterator first_it = reference_maps->begin();
  for (LiveRange* range : data()->live_ranges()) {
    if (range == nullptr) continue;
    // Iterate over the first parts of multi-part live ranges.
    if (range->IsChild()) continue;
    // Skip non-reference values.
    if (!data()->IsReference(range->id())) continue;
    // Skip empty live ranges.
    if (range->IsEmpty()) continue;

    // Find the extent of the range and its children.
    int start = range->Start().ToInstructionIndex();
    int end = 0;
    for (auto cur = range; cur != nullptr; cur = cur->next()) {
      auto this_end = cur->End();
      if (this_end.ToInstructionIndex() > end)
        end = this_end.ToInstructionIndex();
      DCHECK(cur->Start().ToInstructionIndex() >= start);
    }

    // Most of the ranges are in order, but not all.  Keep an eye on when they
    // step backwards and reset the first_it so we don't miss any safe points.
    if (start < last_range_start) first_it = reference_maps->begin();
    last_range_start = start;

    // Step across all the safe points that are before the start of this range,
    // recording how far we step in order to save doing this for the next range.
    for (; first_it != reference_maps->end(); ++first_it) {
      auto map = *first_it;
      if (map->instruction_position() >= start) break;
    }

    // Step through the safe points to see whether they are in the range.
    for (auto it = first_it; it != reference_maps->end(); ++it) {
      auto map = *it;
      int safe_point = map->instruction_position();

      // The safe points are sorted so we can stop searching here.
      if (safe_point - 1 > end) break;

      // Advance to the next active range that covers the current
      // safe point position.
      auto safe_point_pos =
          LifetimePosition::InstructionFromInstructionIndex(safe_point);
      auto cur = range;
      while (cur != nullptr && !cur->Covers(safe_point_pos)) {
        cur = cur->next();
      }
      if (cur == nullptr) continue;

      // Check if the live range is spilled and the safe point is after
      // the spill position.
      if (range->HasSpillOperand() &&
          safe_point >= range->spill_start_index() &&
          !range->GetSpillOperand()->IsConstant()) {
        TRACE("Pointer for range %d (spilled at %d) at safe point %d\n",
              range->id(), range->spill_start_index(), safe_point);
        map->RecordReference(*range->GetSpillOperand());
      }

      if (!cur->IsSpilled()) {
        TRACE(
            "Pointer in register for range %d (start at %d) "
            "at safe point %d\n",
            cur->id(), cur->Start().value(), safe_point);
        auto operand = cur->GetAssignedOperand();
        DCHECK(!operand.IsStackSlot());
        map->RecordReference(operand);
      }
    }
  }
}


namespace {

class LiveRangeBound {
 public:
  explicit LiveRangeBound(const LiveRange* range)
      : range_(range), start_(range->Start()), end_(range->End()) {
    DCHECK(!range->IsEmpty());
  }

  bool CanCover(LifetimePosition position) {
    return start_ <= position && position < end_;
  }

  const LiveRange* const range_;
  const LifetimePosition start_;
  const LifetimePosition end_;

 private:
  DISALLOW_COPY_AND_ASSIGN(LiveRangeBound);
};


struct FindResult {
  const LiveRange* cur_cover_;
  const LiveRange* pred_cover_;
};


class LiveRangeBoundArray {
 public:
  LiveRangeBoundArray() : length_(0), start_(nullptr) {}

  bool ShouldInitialize() { return start_ == nullptr; }

  void Initialize(Zone* zone, const LiveRange* const range) {
    size_t length = 0;
    for (auto i = range; i != nullptr; i = i->next()) length++;
    start_ = zone->NewArray<LiveRangeBound>(length);
    length_ = length;
    auto curr = start_;
    for (auto i = range; i != nullptr; i = i->next(), ++curr) {
      new (curr) LiveRangeBound(i);
    }
  }

  LiveRangeBound* Find(const LifetimePosition position) const {
    size_t left_index = 0;
    size_t right_index = length_;
    while (true) {
      size_t current_index = left_index + (right_index - left_index) / 2;
      DCHECK(right_index > current_index);
      auto bound = &start_[current_index];
      if (bound->start_ <= position) {
        if (position < bound->end_) return bound;
        DCHECK(left_index < current_index);
        left_index = current_index;
      } else {
        right_index = current_index;
      }
    }
  }

  LiveRangeBound* FindPred(const InstructionBlock* pred) {
    auto pred_end = LifetimePosition::InstructionFromInstructionIndex(
        pred->last_instruction_index());
    return Find(pred_end);
  }

  LiveRangeBound* FindSucc(const InstructionBlock* succ) {
    auto succ_start = LifetimePosition::GapFromInstructionIndex(
        succ->first_instruction_index());
    return Find(succ_start);
  }

  void Find(const InstructionBlock* block, const InstructionBlock* pred,
            FindResult* result) const {
    auto pred_end = LifetimePosition::InstructionFromInstructionIndex(
        pred->last_instruction_index());
    auto bound = Find(pred_end);
    result->pred_cover_ = bound->range_;
    auto cur_start = LifetimePosition::GapFromInstructionIndex(
        block->first_instruction_index());
    // Common case.
    if (bound->CanCover(cur_start)) {
      result->cur_cover_ = bound->range_;
      return;
    }
    result->cur_cover_ = Find(cur_start)->range_;
    DCHECK(result->pred_cover_ != nullptr && result->cur_cover_ != nullptr);
  }

 private:
  size_t length_;
  LiveRangeBound* start_;

  DISALLOW_COPY_AND_ASSIGN(LiveRangeBoundArray);
};


class LiveRangeFinder {
 public:
  explicit LiveRangeFinder(const RegisterAllocationData* data, Zone* zone)
      : data_(data),
        bounds_length_(static_cast<int>(data_->live_ranges().size())),
        bounds_(zone->NewArray<LiveRangeBoundArray>(bounds_length_)),
        zone_(zone) {
    for (int i = 0; i < bounds_length_; ++i) {
      new (&bounds_[i]) LiveRangeBoundArray();
    }
  }

  LiveRangeBoundArray* ArrayFor(int operand_index) {
    DCHECK(operand_index < bounds_length_);
    auto range = data_->live_ranges()[operand_index];
    DCHECK(range != nullptr && !range->IsEmpty());
    auto array = &bounds_[operand_index];
    if (array->ShouldInitialize()) {
      array->Initialize(zone_, range);
    }
    return array;
  }

 private:
  const RegisterAllocationData* const data_;
  const int bounds_length_;
  LiveRangeBoundArray* const bounds_;
  Zone* const zone_;

  DISALLOW_COPY_AND_ASSIGN(LiveRangeFinder);
};

}  // namespace


LiveRangeConnector::LiveRangeConnector(RegisterAllocationData* data)
    : data_(data) {}


bool LiveRangeConnector::CanEagerlyResolveControlFlow(
    const InstructionBlock* block) const {
  if (block->PredecessorCount() != 1) return false;
  return block->predecessors()[0].IsNext(block->rpo_number());
}


void LiveRangeConnector::ResolveControlFlow(Zone* local_zone) {
  // Lazily linearize live ranges in memory for fast lookup.
  LiveRangeFinder finder(data(), local_zone);
  auto& live_in_sets = data()->live_in_sets();
  for (auto block : code()->instruction_blocks()) {
    if (CanEagerlyResolveControlFlow(block)) continue;
    auto live = live_in_sets[block->rpo_number().ToInt()];
    BitVector::Iterator iterator(live);
    while (!iterator.Done()) {
      auto* array = finder.ArrayFor(iterator.Current());
      for (auto pred : block->predecessors()) {
        FindResult result;
        const auto* pred_block = code()->InstructionBlockAt(pred);
        array->Find(block, pred_block, &result);
        if (result.cur_cover_ == result.pred_cover_ ||
            result.cur_cover_->IsSpilled())
          continue;
        auto pred_op = result.pred_cover_->GetAssignedOperand();
        auto cur_op = result.cur_cover_->GetAssignedOperand();
        if (pred_op == cur_op) continue;
        ResolveControlFlow(block, cur_op, pred_block, pred_op);
      }
      iterator.Advance();
    }
  }
}


void LiveRangeConnector::ResolveControlFlow(const InstructionBlock* block,
                                            const InstructionOperand& cur_op,
                                            const InstructionBlock* pred,
                                            const InstructionOperand& pred_op) {
  DCHECK(pred_op != cur_op);
  int gap_index;
  Instruction::GapPosition position;
  if (block->PredecessorCount() == 1) {
    gap_index = block->first_instruction_index();
    position = Instruction::START;
  } else {
    DCHECK(pred->SuccessorCount() == 1);
    DCHECK(!code()
                ->InstructionAt(pred->last_instruction_index())
                ->HasReferenceMap());
    gap_index = pred->last_instruction_index();
    position = Instruction::END;
  }
  data()->AddGapMove(gap_index, position, pred_op, cur_op);
}


void LiveRangeConnector::ConnectRanges(Zone* local_zone) {
  ZoneMap<std::pair<ParallelMove*, InstructionOperand>, InstructionOperand>
      delayed_insertion_map(local_zone);
  for (auto first_range : data()->live_ranges()) {
    if (first_range == nullptr || first_range->IsChild()) continue;
    for (auto second_range = first_range->next(); second_range != nullptr;
         first_range = second_range, second_range = second_range->next()) {
      auto pos = second_range->Start();
      // Add gap move if the two live ranges touch and there is no block
      // boundary.
      if (second_range->IsSpilled()) continue;
      if (first_range->End() != pos) continue;
      if (IsBlockBoundary(code(), pos) &&
          !CanEagerlyResolveControlFlow(GetInstructionBlock(code(), pos))) {
        continue;
      }
      auto prev_operand = first_range->GetAssignedOperand();
      auto cur_operand = second_range->GetAssignedOperand();
      if (prev_operand == cur_operand) continue;
      bool delay_insertion = false;
      Instruction::GapPosition gap_pos;
      int gap_index = pos.ToInstructionIndex();
      if (pos.IsGapPosition()) {
        gap_pos = pos.IsStart() ? Instruction::START : Instruction::END;
      } else {
        if (pos.IsStart()) {
          delay_insertion = true;
        } else {
          gap_index++;
        }
        gap_pos = delay_insertion ? Instruction::END : Instruction::START;
      }
      auto move = code()->InstructionAt(gap_index)->GetOrCreateParallelMove(
          gap_pos, code_zone());
      if (!delay_insertion) {
        move->AddMove(prev_operand, cur_operand);
      } else {
        delayed_insertion_map.insert(
            std::make_pair(std::make_pair(move, prev_operand), cur_operand));
      }
    }
  }
  if (delayed_insertion_map.empty()) return;
  // Insert all the moves which should occur after the stored move.
  ZoneVector<MoveOperands*> to_insert(local_zone);
  ZoneVector<MoveOperands*> to_eliminate(local_zone);
  to_insert.reserve(4);
  to_eliminate.reserve(4);
  auto moves = delayed_insertion_map.begin()->first.first;
  for (auto it = delayed_insertion_map.begin();; ++it) {
    bool done = it == delayed_insertion_map.end();
    if (done || it->first.first != moves) {
      // Commit the MoveOperands for current ParallelMove.
      for (auto move : to_eliminate) {
        move->Eliminate();
      }
      for (auto move : to_insert) {
        moves->push_back(move);
      }
      if (done) break;
      // Reset state.
      to_eliminate.clear();
      to_insert.clear();
      moves = it->first.first;
    }
    // Gather all MoveOperands for a single ParallelMove.
    auto move = new (code_zone()) MoveOperands(it->first.second, it->second);
    auto eliminate = moves->PrepareInsertAfter(move);
    to_insert.push_back(move);
    if (eliminate != nullptr) to_eliminate.push_back(eliminate);
  }
}


}  // namespace compiler
}  // namespace internal
}  // namespace v8
