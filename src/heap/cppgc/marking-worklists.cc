
// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/marking-worklists.h"

namespace cppgc {
namespace internal {

constexpr int MarkingWorklists::kMutatorThreadId;

void MarkingWorklists::ClearForTesting() {
  marking_worklist_.Clear();
  not_fully_constructed_worklist_.Clear();
  previously_not_fully_constructed_worklist_.Clear();
  write_barrier_worklist_.Clear();
  weak_callback_worklist_.Clear();
  concurrent_marking_bailout_worklist_.Clear();
  discovered_ephemeron_pairs_worklist_.Clear();
  ephemeron_pairs_for_processing_worklist_.Clear();
}

void MarkingWorklists::NotFullyConstructedWorklist::Push(
    HeapObjectHeader* object) {
  DCHECK_NOT_NULL(object);
  v8::base::MutexGuard guard(&lock_);
  objects_.insert(object);
}

std::unordered_set<HeapObjectHeader*>
MarkingWorklists::NotFullyConstructedWorklist::Extract() {
  v8::base::MutexGuard guard(&lock_);
  std::unordered_set<HeapObjectHeader*> extracted;
  std::swap(extracted, objects_);
  DCHECK(objects_.empty());
  return extracted;
}

void MarkingWorklists::NotFullyConstructedWorklist::Clear() {
  v8::base::MutexGuard guard(&lock_);
  objects_.clear();
}

bool MarkingWorklists::NotFullyConstructedWorklist::IsEmpty() {
  v8::base::MutexGuard guard(&lock_);
  return objects_.empty();
}

MarkingWorklists::NotFullyConstructedWorklist::~NotFullyConstructedWorklist() {
  DCHECK(IsEmpty());
}

bool MarkingWorklists::NotFullyConstructedWorklist::ContainsForTesting(
    HeapObjectHeader* object) {
  v8::base::MutexGuard guard(&lock_);
  return objects_.find(object) != objects_.end();
}

}  // namespace internal
}  // namespace cppgc
