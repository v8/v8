// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/internal/write-barrier.h"

#include "include/cppgc/internal/pointer-policies.h"
#include "src/heap/cppgc/heap-object-header-inl.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page-inl.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/marker.h"
#include "src/heap/cppgc/marking-visitor.h"

namespace cppgc {
namespace internal {

namespace {

void MarkValue(const BasePage* page, Marker* marker, const void* value) {
  auto& header =
      const_cast<HeapObjectHeader&>(page->ObjectHeaderFromInnerAddress(value));
  if (!header.TryMarkAtomic()) return;

  DCHECK(marker);

  if (V8_UNLIKELY(MutatorThreadMarkingVisitor::IsInConstruction(header))) {
    // It is assumed that objects on not_fully_constructed_worklist_ are not
    // marked.
    header.Unmark();
    Marker::NotFullyConstructedWorklist::View not_fully_constructed_worklist(
        marker->not_fully_constructed_worklist(), Marker::kMutatorThreadId);
    not_fully_constructed_worklist.Push(header.Payload());
    return;
  }

  Marker::WriteBarrierWorklist::View write_barrier_worklist(
      marker->write_barrier_worklist(), Marker::kMutatorThreadId);
  write_barrier_worklist.Push(&header);
}

}  // namespace

void WriteBarrier::MarkingBarrierSlow(const void*, const void* value) {
  if (!value || value == kSentinelPointer) return;

  const BasePage* page = BasePage::FromPayload(value);
  const auto* heap = page->heap();

  // Marker being not set up means that no incremental/concurrent marking is in
  // progress.
  if (!heap->marker()) return;

  MarkValue(page, heap->marker(), value);
}

}  // namespace internal
}  // namespace cppgc
