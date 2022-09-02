// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/handles/shared-object-conveyors.h"

#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

SharedObjectConveyorHandles::SharedObjectConveyorHandles(Isolate* isolate,
                                                         uint32_t id)
    : id(id), persistent_handles_(isolate->NewPersistentHandles()) {}

uint32_t SharedObjectConveyorHandles::Persist(HeapObject shared_object) {
  DCHECK(shared_object.IsShared());
  uint32_t id = static_cast<uint32_t>(shared_objects_.size());
  shared_objects_.push_back(persistent_handles_->NewHandle(shared_object));
  return id;
}

HeapObject SharedObjectConveyorHandles::GetPersisted(uint32_t object_id) {
  DCHECK(HasPersisted(object_id));
  return *shared_objects_[object_id];
}

void SharedObjectConveyorHandles::Delete() {
  persistent_handles_->isolate()->GetSharedObjectConveyors()->DeleteConveyor(
      id);
}

SharedObjectConveyorHandles* SharedObjectConveyors::NewConveyor() {
  base::MutexGuard guard(&conveyors_mutex_);

  uint32_t id;
  if (conveyors_.empty()) {
    id = 0;
  } else {
    size_t i;
    for (i = 0; i < conveyors_.size(); i++) {
      if (conveyors_[i] == nullptr) break;
    }
    id = static_cast<uint32_t>(i);
  }

  auto handles = std::make_unique<SharedObjectConveyorHandles>(isolate_, id);
  if (id < conveyors_.size()) {
    conveyors_[id] = std::move(handles);
  } else {
    DCHECK_EQ(id, conveyors_.size());
    conveyors_.push_back(std::move(handles));
  }

  return conveyors_[id].get();
}

SharedObjectConveyorHandles* SharedObjectConveyors::MaybeGetConveyor(
    uint32_t conveyor_id) {
  base::MutexGuard guard(&conveyors_mutex_);
  if (HasConveyor(conveyor_id)) return conveyors_[conveyor_id].get();
  return nullptr;
}

void SharedObjectConveyors::DeleteConveyor(uint32_t conveyor_id) {
  base::MutexGuard guard(&conveyors_mutex_);
  DCHECK(HasConveyor(conveyor_id));
  conveyors_[conveyor_id].reset(nullptr);
}

bool SharedObjectConveyors::HasConveyor(uint32_t conveyor_id) const {
  return conveyor_id < conveyors_.size() &&
         conveyors_[conveyor_id] != nullptr &&
         conveyors_[conveyor_id]->id == conveyor_id;
}

}  // namespace internal
}  // namespace v8
