// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/trusted-pointer-table.h"

#include "src/execution/isolate.h"
#include "src/heap/heap-layout-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/heap/read-only-spaces.h"
#include "src/logging/counters.h"
#include "src/objects/objects-inl.h"
#include "src/objects/trusted-object-inl.h"
#include "src/sandbox/trusted-pointer-table-inl.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8 {
namespace internal {

void TrustedPointerTable::SetUpFromReadOnlyArtifacts(
    Space* read_only_space, const ReadOnlyArtifacts* artifacts) {
  // Rebuild this isolate's read-only TPT segment by walking the exposed
  // trusted objects in the (shared) read-only heap. Everything an entry needs
  // is derivable from the object itself: the value is its address, the tag
  // comes from its instance type, and the handle is its self indirect pointer.
  // Iterating in object (address) order reproduces the same handle allocation
  // order as the initial deserialization, which the CHECK_EQ below enforces.
  UnsealReadOnlySegmentScope unseal_scope(this);
  ReadOnlyHeapObjectIterator it(artifacts->read_only_heap());
  for (Tagged<HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
    if (!IsExposedTrustedObject(o)) continue;
    SharedFlag shared = SharedFlag(HeapLayout::InAnySharedSpace(o));
    IndirectPointerTag tag =
        IndirectPointerTagFromInstanceType(o->map()->instance_type(), shared);
    Tagged<ExposedTrustedObject> exposed = TrustedCast<ExposedTrustedObject>(o);
    TrustedPointerHandle handle = AllocateAndInitializeEntry(
        read_only_space, exposed.ptr(), tag, nullptr);
    CHECK_EQ(handle, exposed->self_indirect_pointer_handle());
  }
}

uint32_t TrustedPointerTable::Sweep(Space* space, Counters* counters) {
  uint32_t num_live_entries = GenericSweep(space);
  counters->trusted_pointers_count()->AddSample(num_live_entries);
  return num_live_entries;
}

void TrustedPointerTable::Verify(Isolate* isolate, Space* space) {
  IterateEntriesIn(space, [&](uint32_t index) {
    auto& entry = at(index);
    IndirectPointerTag tag = entry.GetTag();
    if (tag == kIndirectPointerNullTag || tag == kIndirectPointerFreeEntryTag ||
        tag == kIndirectPointerZappedEntryTag ||
        tag == kUnpublishedIndirectPointerTag) {
      return;
    }

    Address pointer = entry.GetPointer(tag);

    // 1. The pointer must point outside of the sandbox or be write-protected in
    // Read-Only space.
    if (ReadOnlyHeap::Contains(pointer)) {
      CHECK(tag == kCodeIndirectPointerTag ||
            tag == kUncompiledDataIndirectPointerTag);
    } else {
      CHECK(OutsideSandbox(pointer) ||
            IsTrustedSpaceMigrationInProgressForObjectsWithTag(tag));
    }

    // 2. The object must be a valid HeapObject.
    Tagged<Object> obj_ptr(pointer);
    CHECK(Is<HeapObject>(obj_ptr));
    Tagged<HeapObject> obj = TrustedCast<HeapObject>(obj_ptr);
#ifdef VERIFY_HEAP
    Object::ObjectVerify(obj, isolate);
#endif

    // 3. The tag must match the object's instance type.
    SharedFlag is_shared =
        SharedFlag(this == &isolate->shared_trusted_pointer_table());
    IndirectPointerTag expected_tag = IndirectPointerTagFromInstanceType(
        obj->map()->instance_type(), is_shared);
    CHECK_EQ(tag, expected_tag);
  });
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX
