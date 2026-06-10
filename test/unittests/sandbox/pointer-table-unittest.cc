// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/allocation.h"
#include "include/cppgc/garbage-collected.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/handles/global-handles-inl.h"
#include "src/handles/handles-inl.h"
#include "src/objects/js-objects.h"
#include "src/sandbox/cppheap-pointer-table-inl.h"
#include "src/sandbox/external-pointer-table.h"
#include "test/unittests/heap/cppgc-js/unified-heap-utils.h"
#include "test/unittests/heap/heap-utils.h"  // For ManualGCScope
#include "test/unittests/test-utils.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8 {
namespace internal {

using PointerTableTest = WithHeapInternals<TestWithContext>;

DirectHandle<FixedArray> FillExternalPointerTableSpace(
    Isolate* isolate, ExternalPointerTable::Space* space, void* external_value,
    ExternalPointerTag tag) {
  uint32_t initial_segments = space->NumSegmentsForTesting();
  uint32_t num_entries = space->freelist_length();
  DirectHandle<FixedArray> array =
      isolate->factory()->NewFixedArray(num_entries);
  {
    v8::HandleScope inner_scope(reinterpret_cast<v8::Isolate*>(isolate));
    for (uint32_t i = 0; i < num_entries; i++) {
      DirectHandle<JSObject> obj = isolate->factory()->NewExternal(
          external_value, tag, AllocationType::kOld);
      array->set(i, *obj);
    }
    CHECK_EQ(0, space->freelist_length());
    CHECK_EQ(initial_segments, space->NumSegmentsForTesting());
  }
  return array;
}

TEST_F(PointerTableTest, ExternalPointerTableCompaction) {
  // This tests ensures that pointer table compaction works as expected and
  // that --stress-compaction causes us to compact the table whenever possible.

  auto* iso = i_isolate();
  auto* heap = iso->heap();
  auto* space = heap->old_external_pointer_space();

  ManualGCScope manual_gc_scope(iso);

  v8_flags.stress_compaction = true;

  int* external_1 = new int;
  int* external_2 = new int;

  {
    v8::HandleScope scope(reinterpret_cast<v8::Isolate*>(iso));

    // Allocate one segment worth of external pointer table entries and keep the
    // host objects in a FixedArray so they and their entries are kept alive.
    DirectHandle<FixedArray> array = FillExternalPointerTableSpace(
        iso, space, external_1, kLastExternalTypeTag);

    {
      v8::HandleScope inner_scope(reinterpret_cast<v8::Isolate*>(iso));

      // Allocate target object which goes to the newly allocated segment
      // (evacuation area).
      CHECK_EQ(1, space->NumSegmentsForTesting());
      DirectHandle<JSExternalObject> obj =
          Cast<JSExternalObject>(iso->factory()->NewExternal(
              external_2, kLastExternalTypeTag, AllocationType::kOld));
      CHECK_EQ(2, space->NumSegmentsForTesting());

      // TODO(saelo): maybe it'd be nice to also automatically generate
      // accessors for the underlying table handles.
      ExternalPointerHandle original_handle = obj->value_.load_encoded();

      // Free the first entry in the first segment to create a free slot below
      // the evacuation area.
      array->set(0, *iso->factory()->undefined_value());

      // There should be no free entries in the table yet, so nothing can be
      // compacted during the first GC.
      InvokeMajorGC();
      CHECK_EQ(2, space->NumSegmentsForTesting());
      ExternalPointerHandle current_handle = obj->value_.load_encoded();
      CHECK_EQ(original_handle, current_handle);
      CHECK_EQ(obj->value(kLastExternalTypeTag), external_2);

      // Now at least one entry in the first segment must be free, so compaction
      // should be possible. This should leave the 2nd segment empty, causing it
      // to be deallocated.
      InvokeMajorGC();
      CHECK_EQ(1, space->NumSegmentsForTesting());
      current_handle = obj->value_.load_encoded();
      CHECK_NE(original_handle, current_handle);
      CHECK_EQ(obj->value(kLastExternalTypeTag), external_2);
    }
  }

  delete external_1;
  delete external_2;
}

class TestWrappable : public cppgc::GarbageCollected<TestWrappable> {
 public:
  void Trace(cppgc::Visitor* visitor) const {}
};

TEST_F(PointerTableTest, CppHeapPointerTableDoubleMarkingCrash) {
  if (!v8_flags.incremental_marking) return;

  v8_flags.stress_compaction = true;

  auto* iso = i_isolate();
  v8::Isolate* isolate = reinterpret_cast<v8::Isolate*>(iso);
  v8::HandleScope scope(isolate);
  v8::Local<v8::Context> context =
      v8::Local<v8::Context>::New(isolate, this->context());

  ManualGCScope manual_gc_scope(iso);

  cppgc::AllocationHandle& alloc_handle =
      iso->heap()->cpp_heap()->GetAllocationHandle();
  TestWrappable* dummy_wrappable =
      cppgc::MakeGarbageCollected<TestWrappable>(alloc_handle);

  auto* space = iso->heap()->cpp_heap_pointer_space();

  // Ensure we have at least one segment.
  v8::Local<v8::Object> dummy =
      v8::internal::WrapperHelper::CreateWrapper(context, dummy_wrappable);

  uint32_t num_entries = space->freelist_length();
  // We need to keep dummy and target_obj alive, so size is num_entries + 2.
  DirectHandle<FixedArray> array =
      iso->factory()->NewFixedArray(num_entries + 2);
  array->set(0, *Cast<JSObject>(Utils::OpenDirectHandle(*dummy)));

  {
    v8::HandleScope local_scope(isolate);
    for (uint32_t i = 0; i < num_entries; i++) {
      v8::Local<v8::Object> wrapper =
          v8::internal::WrapperHelper::CreateWrapper(context, dummy_wrappable);
      DirectHandle<JSObject> obj =
          Cast<JSObject>(Utils::OpenDirectHandle(*wrapper));
      array->set(i + 1, *obj);
    }
    CHECK_EQ(0, space->freelist_length());
  }

  uint32_t segments_after_fill = space->NumSegmentsForTesting();

  // Allocate target object which goes to the newly allocated segment
  // (evacuation area).
  v8::Local<v8::Object> target_obj =
      v8::internal::WrapperHelper::CreateWrapper(context, dummy_wrappable);
  CHECK_EQ(segments_after_fill + 1, space->NumSegmentsForTesting());

  array->set(num_entries + 1,
             *Cast<JSObject>(Utils::OpenDirectHandle(*target_obj)));

  // Keep the array alive using a global handle so it is scanned during
  // incremental marking.
  Handle<FixedArray> global_array = iso->global_handles()->Create(*array);

  // Free the first entry in the first segment to create a free slot below the
  // evacuation area.
  array->set(0, *iso->factory()->undefined_value());

  // Run a Major GC to sweep and free the slot.
  // Compaction will abort during this GC because there are no free slots
  // initially, but it will successfully free the slots during sweeping.
  InvokeMajorGC();
  iso->heap()->EnsureSweepingCompleted(
      Heap::SweepingForcedFinalizationMode::kUnifiedHeap,
      CompleteSweepingReason::kTesting);

  // Start incremental marking and advance it to completion.
  // This will start compaction and ensure target_js_obj is marked.
  SimulateIncrementalMarking();

  DirectHandle<JSObject> target_js_obj =
      Cast<JSObject>(Utils::OpenDirectHandle(*target_obj));
  CHECK(iso->heap()->marking_state()->IsMarked(*target_js_obj));

  // Get slot and handle for target_js_obj.
  CppHeapPointerSlot target_slot = target_js_obj->RawCppHeapPointerField(
      offsetof(JSAPIObjectWithEmbedderSlots, cpp_heap_wrappable_));
  CppHeapPointerHandle target_handle = target_slot.Relaxed_LoadHandle();
  CppHeapPointerTable& table = iso->cpp_heap_pointer_table();

  // Mutator write.
  TestWrappable* new_wrappable =
      cppgc::MakeGarbageCollected<TestWrappable>(alloc_handle);
  v8::internal::WrapperHelper::SetWrappableConnection(isolate, target_obj,
                                                      new_wrappable);

  // Manually call Mark to simulate the concurrent marking race where
  // the marker marks the object after the mutator write.
  table.Mark(space, target_handle, target_slot.address());

  // Complete the GC. This will run sweeping and resolve evacuation entries
  // which will crash in case of duplicate entries.
  InvokeMajorGC();

  iso->global_handles()->Destroy(global_array.location());
}

TEST_F(PointerTableTest, ExternalPointerTableExchangeCompactionRace) {
  if (!v8_flags.incremental_marking) return;

  v8_flags.stress_compaction = true;

  auto* iso = i_isolate();
  v8::Isolate* isolate = reinterpret_cast<v8::Isolate*>(iso);
  v8::HandleScope scope(isolate);

  ManualGCScope manual_gc_scope(iso);

  auto* space = iso->heap()->old_external_pointer_space();
  auto& table = iso->external_pointer_table();

  int* external_1 = new int;
  int* external_2 = new int;
  int* external_3 = new int;

  // Allocate one segment worth of external pointer table entries and keep the
  // host objects in a FixedArray so they and their entries are kept alive.
  DirectHandle<FixedArray> array = FillExternalPointerTableSpace(
      iso, space, external_1, kLastExternalTypeTag);

  // Allocate target object which goes to the newly allocated segment
  // (evacuation area).
  DirectHandle<JSExternalObject> target_obj =
      Cast<JSExternalObject>(iso->factory()->NewExternal(
          external_2, kLastExternalTypeTag, AllocationType::kOld));
  CHECK_EQ(2, space->NumSegmentsForTesting());

  ExternalPointerHandle target_handle = target_obj->value_.load_encoded();
  Address target_slot_address = target_obj->value_.storage_address();

  // Free the first entry in the first segment to create a free slot below the
  // evacuation area.
  array->set(0, *iso->factory()->undefined_value());

  // Run a Major GC to sweep and free the slot.
  // Compaction will abort during this GC because there are no free slots
  // initially, but it will successfully free the slots during sweeping.
  InvokeMajorGC();
  iso->heap()->EnsureSweepingCompleted(
      Heap::SweepingForcedFinalizationMode::kUnifiedHeap,
      CompleteSweepingReason::kTesting);

  // Verify that the target object was not evacuated yet, and we still have 2
  // segments.
  CHECK_EQ(2, space->NumSegmentsForTesting());
  CHECK_EQ(target_handle, target_obj->value_.load_encoded());

  // Start incremental marking. This will start compaction.
  SimulateIncrementalMarking(false);

  // Simulating the mutator write.
  table.Exchange(target_handle, reinterpret_cast<Address>(external_3),
                 kLastExternalTypeTag);
  // Manually call Mark to simulate the concurrent marking race where
  // the marker marks the object after the mutator write.
  table.Mark(space, target_handle, target_slot_address);

  // Complete GC. This will run sweeping and deallocate the evacuation segment.
  // Since the entry was not evacuated, trying to read it after GC will crash
  // or return garbage/zapped value because the segment was freed.
  InvokeMajorGC();

  // Try to read the value from the table.
  ExternalPointerHandle post_gc_handle = target_obj->value_.load_encoded();
  CHECK_EQ(1, space->NumSegmentsForTesting());
  CHECK_NE(target_handle, post_gc_handle);

  Address final_val = table.Get(post_gc_handle, kLastExternalTypeTag);
  CHECK(final_val == reinterpret_cast<Address>(external_3));

  delete external_1;
  delete external_2;
  delete external_3;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX
