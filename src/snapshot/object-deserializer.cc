// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/object-deserializer.h"

#include "src/assembler-inl.h"
#include "src/isolate.h"
#include "src/objects.h"
#include "src/snapshot/code-serializer.h"

namespace v8 {
namespace internal {

MaybeHandle<HeapObject> ObjectDeserializer::Deserialize(Isolate* isolate) {
  Initialize(isolate);
  if (!ReserveSpace()) return MaybeHandle<HeapObject>();

  DCHECK(deserializing_user_code());
  HandleScope scope(isolate);
  Handle<HeapObject> result;
  {
    DisallowHeapAllocation no_gc;
    Object* root;
    VisitRootPointer(Root::kPartialSnapshotCache, &root);
    DeserializeDeferredObjects();
    FlushICacheForNewCodeObjectsAndRecordEmbeddedObjects();
    result = Handle<HeapObject>(HeapObject::cast(root));
    RegisterDeserializedObjectsForBlackAllocation();
  }
  CommitPostProcessedObjects();
  return scope.CloseAndEscape(result);
}

void ObjectDeserializer::
    FlushICacheForNewCodeObjectsAndRecordEmbeddedObjects() {
  DCHECK(deserializing_user_code());
  for (Code* code : new_code_objects()) {
    // Record all references to embedded objects in the new code object.
    isolate()->heap()->RecordWritesIntoCode(code);

    if (FLAG_serialize_age_code) code->PreAge(isolate());
    Assembler::FlushICache(isolate(), code->instruction_start(),
                           code->instruction_size());
  }
}

void ObjectDeserializer::CommitPostProcessedObjects() {
  StringTable::EnsureCapacityForDeserialization(
      isolate(), new_internalized_strings().length());
  for (Handle<String> string : new_internalized_strings()) {
    StringTableInsertionKey key(*string);
    DCHECK_NULL(StringTable::LookupKeyIfExists(isolate(), &key));
    StringTable::LookupKey(isolate(), &key);
  }

  Heap* heap = isolate()->heap();
  Factory* factory = isolate()->factory();
  for (Handle<Script> script : new_scripts()) {
    // Assign a new script id to avoid collision.
    script->set_id(isolate()->heap()->NextScriptId());
    // Add script to list.
    Handle<Object> list = WeakFixedArray::Add(factory->script_list(), script);
    heap->SetRootScriptList(*list);
  }
}

}  // namespace internal
}  // namespace v8
