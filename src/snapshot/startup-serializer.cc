// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/startup-serializer.h"

#include "src/objects-inl.h"
#include "src/v8threads.h"

namespace v8 {
namespace internal {

StartupSerializer::StartupSerializer(Isolate* isolate, SnapshotByteSink* sink)
    : Serializer(isolate, sink),
      root_index_wave_front_(0),
      serializing_builtins_(false) {
  // Clear the cache of objects used by the partial snapshot.  After the
  // strong roots have been serialized we can create a partial snapshot
  // which will repopulate the cache with objects needed by that partial
  // snapshot.
  isolate->partial_snapshot_cache()->Clear();
  InitializeCodeAddressMap();
}

StartupSerializer::~StartupSerializer() {
  OutputStatistics("StartupSerializer");
}

void StartupSerializer::SerializeObject(HeapObject* obj, HowToCode how_to_code,
                                        WhereToPoint where_to_point, int skip) {
  DCHECK(!obj->IsJSFunction());

  if (obj->IsCode()) {
    Code* code = Code::cast(obj);
    // If the function code is compiled (either as native code or bytecode),
    // replace it with lazy-compile builtin. Only exception is when we are
    // serializing the canonical interpreter-entry-trampoline builtin.
    if (code->kind() == Code::FUNCTION ||
        (!serializing_builtins_ && code->is_interpreter_entry_trampoline())) {
      obj = isolate()->builtins()->builtin(Builtins::kCompileLazy);
    }
  } else if (obj->IsBytecodeArray()) {
    obj = isolate()->heap()->undefined_value();
  }

  int root_index = root_index_map_.Lookup(obj);
  bool is_immortal_immovable_root = false;
  // We can only encode roots as such if it has already been serialized.
  // That applies to root indices below the wave front.
  if (root_index != RootIndexMap::kInvalidRootIndex) {
    if (root_index < root_index_wave_front_) {
      PutRoot(root_index, obj, how_to_code, where_to_point, skip);
      return;
    } else {
      is_immortal_immovable_root = Heap::RootIsImmortalImmovable(root_index);
    }
  }

  if (SerializeKnownObject(obj, how_to_code, where_to_point, skip)) return;

  FlushSkip(skip);

  // Object has not yet been serialized.  Serialize it here.
  ObjectSerializer object_serializer(this, obj, sink_, how_to_code,
                                     where_to_point);
  object_serializer.Serialize();

  if (is_immortal_immovable_root) {
    // Make sure that the immortal immovable root has been included in the first
    // chunk of its reserved space , so that it is deserialized onto the first
    // page of its space and stays immortal immovable.
    BackReference ref = back_reference_map_.Lookup(obj);
    CHECK(ref.is_valid() && ref.chunk_index() == 0);
  }
}

void StartupSerializer::SerializeWeakReferencesAndDeferred() {
  // This phase comes right after the serialization (of the snapshot).
  // After we have done the partial serialization the partial snapshot cache
  // will contain some references needed to decode the partial snapshot.  We
  // add one entry with 'undefined' which is the sentinel that the deserializer
  // uses to know it is done deserializing the array.
  Object* undefined = isolate()->heap()->undefined_value();
  VisitPointer(&undefined);
  isolate()->heap()->IterateWeakRoots(this, VISIT_ALL);
  SerializeDeferredObjects();
  Pad();
}

void StartupSerializer::Synchronize(VisitorSynchronization::SyncTag tag) {
  // We expect the builtins tag after builtins have been serialized.
  DCHECK(!serializing_builtins_ || tag == VisitorSynchronization::kBuiltins);
  serializing_builtins_ = (tag == VisitorSynchronization::kHandleScope);
  sink_->Put(kSynchronize, "Synchronize");
}

void StartupSerializer::SerializeStrongReferences() {
  Isolate* isolate = this->isolate();
  // No active threads.
  CHECK_NULL(isolate->thread_manager()->FirstThreadStateInUse());
  // No active or weak handles.
  CHECK(isolate->handle_scope_implementer()->blocks()->is_empty());
  CHECK_EQ(0, isolate->global_handles()->NumberOfWeakHandles());
  CHECK_EQ(0, isolate->eternal_handles()->NumberOfHandles());
  // We don't support serializing installed extensions.
  CHECK(!isolate->has_installed_extensions());
  isolate->heap()->IterateSmiRoots(this);
  isolate->heap()->IterateStrongRoots(this, VISIT_ONLY_STRONG);
}

void StartupSerializer::VisitPointers(Object** start, Object** end) {
  for (Object** current = start; current < end; current++) {
    if (start == isolate()->heap()->roots_array_start()) {
      root_index_wave_front_ =
          Max(root_index_wave_front_, static_cast<intptr_t>(current - start));
    }
    if (ShouldBeSkipped(current)) {
      sink_->Put(kSkip, "Skip");
      sink_->PutInt(kPointerSize, "SkipOneWord");
    } else if ((*current)->IsSmi()) {
      sink_->Put(kOnePointerRawData, "Smi");
      for (int i = 0; i < kPointerSize; i++) {
        sink_->Put(reinterpret_cast<byte*>(current)[i], "Byte");
      }
    } else {
      SerializeObject(HeapObject::cast(*current), kPlain, kStartOfObject, 0);
    }
  }
}

}  // namespace internal
}  // namespace v8
