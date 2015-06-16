// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_GREEDY_ALLOCATOR_H_
#define V8_GREEDY_ALLOCATOR_H_

#include "src/compiler/register-allocator.h"
#include "src/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

class CoalescedLiveRanges;


// A variant of the LLVM Greedy Register Allocator. See
// http://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html
class GreedyAllocator final : public RegisterAllocator {
 public:
  explicit GreedyAllocator(RegisterAllocationData* data, RegisterKind kind,
                           Zone* local_zone);

  void AllocateRegisters();

 private:
  LifetimePosition GetSplittablePos(LifetimePosition pos);
  const RegisterConfiguration* config() const { return data()->config(); }
  Zone* local_zone() const { return local_zone_; }

  int GetHintedRegister(LiveRange* range);

  typedef ZonePriorityQueue<std::pair<unsigned, LiveRange*>> PQueue;

  unsigned GetLiveRangeSize(LiveRange* range);
  void Enqueue(LiveRange* range);

  void Evict(LiveRange* range);
  float CalculateSpillWeight(LiveRange* range);
  float CalculateMaxSpillWeight(const ZoneSet<LiveRange*>& ranges);


  bool TryAllocate(LiveRange* current, ZoneSet<LiveRange*>* conflicting);
  bool TryAllocatePhysicalRegister(unsigned reg_id, LiveRange* range,
                                   ZoneSet<LiveRange*>* conflicting);
  bool HandleSpillOperands(LiveRange* range);
  void AllocateBlockedRange(LiveRange* current, LifetimePosition pos,
                            bool spill);

  LiveRange* SpillBetweenUntil(LiveRange* range, LifetimePosition start,
                               LifetimePosition until, LifetimePosition end);
  void AssignRangeToRegister(int reg_id, LiveRange* range);

  LifetimePosition FindProgressingSplitPosition(LiveRange* range,
                                                bool* is_spill_pos);

  Zone* local_zone_;
  ZoneVector<CoalescedLiveRanges*> allocations_;
  PQueue queue_;
  DISALLOW_COPY_AND_ASSIGN(GreedyAllocator);
};
}  // namespace compiler
}  // namespace internal
}  // namespace v8
#endif  // V8_GREEDY_ALLOCATOR_H_
