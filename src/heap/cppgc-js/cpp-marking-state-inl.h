// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_JS_CPP_MARKING_STATE_INL_H_
#define V8_HEAP_CPPGC_JS_CPP_MARKING_STATE_INL_H_

#include "src/heap/cppgc-js/cpp-heap.h"
#include "src/heap/cppgc-js/cpp-marking-state.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/page-memory.h"
#include "src/objects/embedder-data-slot-inl.h"
#include "src/objects/js-objects.h"

namespace v8 {
namespace internal {

bool CppMarkingState::ExtractEmbedderDataSnapshot(
    Map map, JSObject object, EmbedderDataSnapshot& snapshot) {
  if (JSObject::GetEmbedderFieldCount(map) < 2) return false;

  EmbedderDataSlot::EmbedderDataSlotSnapshot slot_snapshot;
  EmbedderDataSlot::PopulateEmbedderDataSnapshot(
      map, object, wrapper_descriptor_.wrappable_instance_index, slot_snapshot);
  // Check whether snapshot is valid.
  const EmbedderDataSlot instance_slot(slot_snapshot);
  bool valid = instance_slot.ToAlignedPointer(isolate_, &snapshot);
  if (!valid || !snapshot) return false;
#if defined(CPPGC_CAGED_HEAP)
  // On 64-bit builds we have to check whether the snapshot captured a valid
  // pointer. On such builds, the pointer is updated with 32-bit atomics which
  // means that the marker may observe the high and low parts in inconsistent
  // states. The high parts specifies the cage and the low part is always
  // non-null when it is interesting.
  return cppgc::internal::CagedHeapBase::IsWithinCage(snapshot) &&
         static_cast<uintptr_t>(
             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(snapshot))) != 0;
#elif V8_TARGET_ARCH_X64
  const cppgc::internal::BasePage* page =
      reinterpret_cast<const cppgc::internal::BasePage*>(
          CppHeap::From(isolate_->heap()->cpp_heap())
              ->page_backend()
              ->Lookup(const_cast<cppgc::internal::ConstAddress>(
                  reinterpret_cast<cppgc::internal::Address>(snapshot))));
  return page != nullptr;
#else
  // 32-bit configuration.
  return true;
#endif
}

void CppMarkingState::MarkAndPush(const EmbedderDataSnapshot& snapshot) {
  marking_state_.MarkAndPush(
      cppgc::internal::HeapObjectHeader::FromObject(snapshot));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CPPGC_JS_CPP_MARKING_STATE_INL_H_
