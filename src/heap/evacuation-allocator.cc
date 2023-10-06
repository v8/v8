// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/evacuation-allocator.h"

#include "src/heap/main-allocator-inl.h"

namespace v8 {
namespace internal {

EvacuationAllocator::EvacuationAllocator(
    Heap* heap, CompactionSpaceKind compaction_space_kind)
    : heap_(heap),
      new_space_(heap->new_space()),
      compaction_spaces_(heap, compaction_space_kind),
      new_space_lab_(LocalAllocationBuffer::InvalidBuffer()),
      lab_allocation_will_fail_(false) {
  old_space_allocator_.emplace(heap, compaction_spaces_.Get(OLD_SPACE),
                               compaction_space_kind,
                               MainAllocator::SupportsExtendingLAB::kNo);
  compaction_spaces_.Get(OLD_SPACE)->set_main_allocator(old_space_allocator());

  code_space_allocator_.emplace(heap, compaction_spaces_.Get(CODE_SPACE),
                                compaction_space_kind,
                                MainAllocator::SupportsExtendingLAB::kNo);
  compaction_spaces_.Get(CODE_SPACE)
      ->set_main_allocator(code_space_allocator());

  shared_space_allocator_.emplace(heap, compaction_spaces_.Get(SHARED_SPACE),
                                  compaction_space_kind,
                                  MainAllocator::SupportsExtendingLAB::kNo);
  compaction_spaces_.Get(SHARED_SPACE)
      ->set_main_allocator(shared_space_allocator());

  trusted_space_allocator_.emplace(heap, compaction_spaces_.Get(TRUSTED_SPACE),
                                   compaction_space_kind,
                                   MainAllocator::SupportsExtendingLAB::kNo);
  compaction_spaces_.Get(TRUSTED_SPACE)
      ->set_main_allocator(trusted_space_allocator());
}

void EvacuationAllocator::Finalize() {
  heap_->old_space()->MergeCompactionSpace(compaction_spaces_.Get(OLD_SPACE));
  heap_->code_space()->MergeCompactionSpace(compaction_spaces_.Get(CODE_SPACE));
  if (heap_->shared_space()) {
    heap_->shared_space()->MergeCompactionSpace(
        compaction_spaces_.Get(SHARED_SPACE));
  }
  heap_->trusted_space()->MergeCompactionSpace(
      compaction_spaces_.Get(TRUSTED_SPACE));

  // Give back remaining LAB space if this EvacuationAllocator's new space LAB
  // sits right next to new space allocation top.
  const LinearAllocationArea info = new_space_lab_.CloseAndMakeIterable();
  if (new_space_) new_space_->main_allocator()->MaybeFreeUnusedLab(info);
}

}  // namespace internal
}  // namespace v8
