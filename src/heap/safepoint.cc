// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/safepoint.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap.h"

namespace v8 {
namespace internal {

Safepoint::Safepoint(Heap* heap) : heap_(heap) {}

void Safepoint::StopThreads() {
  heap_->local_heaps_mutex_.Lock();

  barrier_.Arm();

  for (LocalHeap* current = heap_->local_heaps_head_; current;
       current = current->next_) {
    current->RequestSafepoint();
  }

  for (LocalHeap* current = heap_->local_heaps_head_; current;
       current = current->next_) {
    current->state_mutex_.Lock();

    while (current->state_ == LocalHeap::ThreadState::Running) {
      current->state_change_.Wait(&current->state_mutex_);
    }
  }
}

void Safepoint::ResumeThreads() {
  for (LocalHeap* current = heap_->local_heaps_head_; current;
       current = current->next_) {
    current->state_mutex_.Unlock();
  }

  barrier_.Disarm();

  heap_->local_heaps_mutex_.Unlock();
}

void Safepoint::EnterFromThread(LocalHeap* local_heap) {
  {
    base::MutexGuard guard(&local_heap->state_mutex_);
    local_heap->state_ = LocalHeap::ThreadState::Safepoint;
    local_heap->state_change_.NotifyAll();
  }

  barrier_.Wait();

  {
    base::MutexGuard guard(&local_heap->state_mutex_);
    local_heap->state_ = LocalHeap::ThreadState::Running;
  }
}

void Safepoint::Barrier::Arm() {
  base::MutexGuard guard(&mutex_);
  CHECK(!armed_);
  armed_ = true;
}

void Safepoint::Barrier::Disarm() {
  base::MutexGuard guard(&mutex_);
  CHECK(armed_);
  armed_ = false;
  cond_.NotifyAll();
}

void Safepoint::Barrier::Wait() {
  base::MutexGuard guard(&mutex_);
  while (armed_) {
    cond_.Wait(&mutex_);
  }
}

SafepointScope::SafepointScope(Heap* heap) : safepoint_(heap->safepoint()) {
  safepoint_->StopThreads();
}

SafepointScope::~SafepointScope() { safepoint_->ResumeThreads(); }

}  // namespace internal
}  // namespace v8
