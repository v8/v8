// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_SHARED_OBJECT_CONVEYORS_H_
#define V8_HANDLES_SHARED_OBJECT_CONVEYORS_H_

#include <memory>
#include <vector>

#include "src/handles/persistent-handles.h"

namespace v8 {
namespace internal {

class PersistentHandles;

// Wrapper around PersistentHandles that is used to convey shared objects
// (i.e. keep them alive) from a ValueSerializer to a ValueDeserializer for APIs
// like postMessage.
//
// The conveyor must be allocated in an isolate that remains alive until the
// ValueDeserializer in the receiving isolate finishes processing the message.
//
// Each conveyor has an id that is stable across GCs. Each shared object that is
// conveyed is gets an id pair (conveyor_id, object_id). Once all objects in a
// conveyor are received, the conveyor is deleted and its id may be reused for
// future conveyance.
//
// TODO(v8:12547): Currently the shared isolate owns all the conveyors. Change
// the owner to the main isolate once the shared isolate is removed.
class SharedObjectConveyorHandles {
 public:
  SharedObjectConveyorHandles(Isolate* isolate, uint32_t id);

  SharedObjectConveyorHandles(const SharedObjectConveyorHandles&) = delete;
  SharedObjectConveyorHandles& operator=(const SharedObjectConveyorHandles&) =
      delete;

  // Persist and GetPersisted are not threadsafe. A particular conveyor is used
  // by a single thread at a time, either during sending a message or receiving
  // a message.
  uint32_t Persist(HeapObject shared_object);
  HeapObject GetPersisted(uint32_t object_id);

  // Deleting conveyors is threadsafe and may be called from multiple threads.
  void Delete();

  const uint32_t id;

 private:
  std::unique_ptr<PersistentHandles> persistent_handles_;
  std::vector<Handle<HeapObject>> shared_objects_;
};

// A class to own and manage conveyors. All methods are threadsafe and may be
// called from multiple threads.
class SharedObjectConveyors {
 public:
  explicit SharedObjectConveyors(Isolate* isolate) : isolate_(isolate) {}

  SharedObjectConveyorHandles* NewConveyor();
  SharedObjectConveyorHandles* GetConveyor(uint32_t conveyor_id);

 private:
  friend class SharedObjectConveyorHandles;

  void DeleteConveyor(uint32_t conveyor_id);

  void DcheckIsValidConveyorId(uint32_t conveyor_id);

  Isolate* isolate_;
  base::Mutex conveyors_mutex_;
  std::vector<std::unique_ptr<SharedObjectConveyorHandles>> conveyors_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HANDLES_SHARED_OBJECT_CONVEYORS_H_
