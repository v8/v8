// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/explicit-management.h"

#include <tuple>

#include "src/heap/cppgc/heap-base.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"

namespace cppgc {
namespace internal {

namespace {

std::pair<bool, BasePage*> CanExplicitlyFree(void* object) {
  // object is guaranteed to be of type GarbageCollected, so getting the
  // BasePage is okay for regular and large objects.
  auto* base_page = BasePage::FromPayload(object);
  auto* heap = base_page->heap();
  // Whenever the GC is active, avoid modifying the object as it may mess with
  // state that the GC needs.
  const bool in_gc = heap->in_atomic_pause() || heap->marker() ||
                     heap->sweeper().IsSweepingInProgress();
  return {!in_gc, base_page};
}

}  // namespace

void FreeUnreferencedObject(void* object) {
  bool can_free;
  BasePage* base_page;
  std::tie(can_free, base_page) = CanExplicitlyFree(object);
  if (!can_free) {
    return;
  }

  auto& header = HeapObjectHeader::FromPayload(object);
  header.Finalize();

  if (base_page->is_large()) {  // Large object.
    base_page->space()->RemovePage(base_page);
    base_page->heap()->stats_collector()->NotifyExplicitFree(
        LargePage::From(base_page)->PayloadSize());
    LargePage::Destroy(LargePage::From(base_page));
  } else {  // Regular object.
    const size_t header_size = header.GetSize();
    auto* normal_page = NormalPage::From(base_page);
    auto& normal_space = *static_cast<NormalPageSpace*>(base_page->space());
    auto& lab = normal_space.linear_allocation_buffer();
    ConstAddress payload_end = header.PayloadEnd();
    SET_MEMORY_INACCESSIBLE(&header, header_size);
    if (payload_end == lab.start()) {  // Returning to LAB.
      lab.Set(reinterpret_cast<Address>(&header), lab.size() + header_size);
      normal_page->object_start_bitmap().ClearBit(lab.start());
    } else {  // Returning to free list.
      base_page->heap()->stats_collector()->NotifyExplicitFree(header_size);
      normal_space.free_list().Add({&header, header_size});
    }
  }
}

}  // namespace internal
}  // namespace cppgc
