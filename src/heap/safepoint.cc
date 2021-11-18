// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/safepoint.h"

#include <atomic>

#include "src/base/logging.h"
#include "src/common/assert-scope.h"
#include "src/handles/handles.h"
#include "src/handles/local-handles.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap.h"
#include "src/heap/parked-scope.h"
#include "src/logging/counters-scopes.h"

namespace v8 {
namespace internal {

IsolateSafepoint::IsolateSafepoint(Heap* heap)
    : heap_(heap), local_heaps_head_(nullptr), active_safepoint_scopes_(0) {}

void IsolateSafepoint::EnterSafepointScope(StopMainThread stop_main_thread) {
  // Safepoints need to be initiated on the main thread.
  DCHECK_EQ(ThreadId::Current(), heap_->isolate()->thread_id());
  DCHECK_NULL(LocalHeap::Current());
  DCHECK(AllowGarbageCollection::IsAllowed());

  if (++active_safepoint_scopes_ > 1) return;

  TimedHistogramScope timer(
      heap_->isolate()->counters()->gc_time_to_safepoint());
  TRACE_GC(heap_->tracer(), GCTracer::Scope::TIME_TO_SAFEPOINT);

  local_heaps_mutex_.Lock();

  barrier_.Arm();

  int running = 0;

  // There needs to be at least one LocalHeap for the main thread.
  DCHECK_NOT_NULL(local_heaps_head_);

  for (LocalHeap* local_heap = local_heaps_head_; local_heap;
       local_heap = local_heap->next_) {
    if (local_heap->is_main_thread() &&
        stop_main_thread == StopMainThread::kNo) {
      continue;
    }

    const LocalHeap::ThreadState old_state =
        local_heap->state_.SetSafepointRequested();

    if (old_state.IsRunning()) running++;
    CHECK_IMPLIES(old_state.IsCollectionRequested(),
                  local_heap->is_main_thread());
    CHECK(!old_state.IsSafepointRequested());
  }

  barrier_.WaitUntilRunningThreadsInSafepoint(running);
}

void IsolateSafepoint::LeaveSafepointScope(StopMainThread stop_main_thread) {
  // Safepoints need to be initiated on the main thread.
  DCHECK_EQ(ThreadId::Current(), heap_->isolate()->thread_id());
  DCHECK_NULL(LocalHeap::Current());

  DCHECK_GT(active_safepoint_scopes_, 0);
  if (--active_safepoint_scopes_ > 0) return;

  for (LocalHeap* local_heap = local_heaps_head_; local_heap;
       local_heap = local_heap->next_) {
    if (local_heap->is_main_thread() &&
        stop_main_thread == StopMainThread::kNo) {
      continue;
    }

    const LocalHeap::ThreadState old_state =
        local_heap->state_.ClearSafepointRequested();

    CHECK(old_state.IsParked());
    CHECK(old_state.IsSafepointRequested());
    CHECK_IMPLIES(old_state.IsCollectionRequested(),
                  local_heap->is_main_thread());
  }

  barrier_.Disarm();

  local_heaps_mutex_.Unlock();
}

void IsolateSafepoint::WaitInSafepoint() { barrier_.WaitInSafepoint(); }

void IsolateSafepoint::WaitInUnpark() { barrier_.WaitInUnpark(); }

void IsolateSafepoint::NotifyPark() { barrier_.NotifyPark(); }

void IsolateSafepoint::Barrier::Arm() {
  base::MutexGuard guard(&mutex_);
  DCHECK(!IsArmed());
  armed_ = true;
  stopped_ = 0;
}

void IsolateSafepoint::Barrier::Disarm() {
  base::MutexGuard guard(&mutex_);
  DCHECK(IsArmed());
  armed_ = false;
  stopped_ = 0;
  cv_resume_.NotifyAll();
}

void IsolateSafepoint::Barrier::WaitUntilRunningThreadsInSafepoint(
    int running) {
  base::MutexGuard guard(&mutex_);
  DCHECK(IsArmed());
  while (stopped_ < running) {
    cv_stopped_.Wait(&mutex_);
  }
  DCHECK_EQ(stopped_, running);
}

void IsolateSafepoint::Barrier::NotifyPark() {
  base::MutexGuard guard(&mutex_);
  CHECK(IsArmed());
  stopped_++;
  cv_stopped_.NotifyOne();
}

void IsolateSafepoint::Barrier::WaitInSafepoint() {
  base::MutexGuard guard(&mutex_);
  CHECK(IsArmed());
  stopped_++;
  cv_stopped_.NotifyOne();

  while (IsArmed()) {
    cv_resume_.Wait(&mutex_);
  }
}

void IsolateSafepoint::Barrier::WaitInUnpark() {
  base::MutexGuard guard(&mutex_);

  while (IsArmed()) {
    cv_resume_.Wait(&mutex_);
  }
}

SafepointScope::SafepointScope(Heap* heap) : safepoint_(heap->safepoint()) {
  safepoint_->EnterSafepointScope(IsolateSafepoint::StopMainThread::kNo);
}

SafepointScope::~SafepointScope() {
  safepoint_->LeaveSafepointScope(IsolateSafepoint::StopMainThread::kNo);
}

bool IsolateSafepoint::ContainsLocalHeap(LocalHeap* local_heap) {
  base::MutexGuard guard(&local_heaps_mutex_);
  LocalHeap* current = local_heaps_head_;

  while (current) {
    if (current == local_heap) return true;
    current = current->next_;
  }

  return false;
}

bool IsolateSafepoint::ContainsAnyLocalHeap() {
  base::MutexGuard guard(&local_heaps_mutex_);
  return local_heaps_head_ != nullptr;
}

void IsolateSafepoint::Iterate(RootVisitor* visitor) {
  AssertActive();
  for (LocalHeap* current = local_heaps_head_; current;
       current = current->next_) {
    current->handles()->Iterate(visitor);
  }
}

GlobalSafepoint::GlobalSafepoint(Isolate* isolate)
    : shared_isolate_(isolate), shared_heap_(isolate->heap()) {}

void GlobalSafepoint::AppendClient(Isolate* client) {
  base::MutexGuard guard(&clients_mutex_);

  DCHECK_NULL(client->global_safepoint_prev_client_isolate_);
  DCHECK_NULL(client->global_safepoint_next_client_isolate_);
  DCHECK_NE(clients_head_, client);

  if (clients_head_) {
    clients_head_->global_safepoint_prev_client_isolate_ = client;
  }

  client->global_safepoint_prev_client_isolate_ = nullptr;
  client->global_safepoint_next_client_isolate_ = clients_head_;

  clients_head_ = client;
  client->shared_isolate_ = shared_isolate_;
}

void GlobalSafepoint::RemoveClient(Isolate* client) {
  DCHECK_EQ(client->heap()->gc_state(), Heap::TEAR_DOWN);
  base::MutexGuard guard(&clients_mutex_);

  if (client->global_safepoint_next_client_isolate_) {
    client->global_safepoint_next_client_isolate_
        ->global_safepoint_prev_client_isolate_ =
        client->global_safepoint_prev_client_isolate_;
  }

  if (client->global_safepoint_prev_client_isolate_) {
    client->global_safepoint_prev_client_isolate_
        ->global_safepoint_next_client_isolate_ =
        client->global_safepoint_next_client_isolate_;
  } else {
    DCHECK_EQ(clients_head_, client);
    clients_head_ = client->global_safepoint_next_client_isolate_;
  }

  client->shared_isolate_ = nullptr;
}

void GlobalSafepoint::AssertNoClients() { DCHECK_NULL(clients_head_); }

void GlobalSafepoint::EnterGlobalSafepointScope(Isolate* initiator) {
  if (!clients_mutex_.TryLock()) {
    ParkedScope parked_scope(initiator->main_thread_local_heap());
    clients_mutex_.Lock();
  }

  TimedHistogramScope timer(
      initiator->counters()->gc_time_to_global_safepoint());
  TRACE_GC(initiator->heap()->tracer(),
           GCTracer::Scope::TIME_TO_GLOBAL_SAFEPOINT);

  IterateClientIsolates([this, initiator](Isolate* client) {
    Heap* client_heap = client->heap();
    CHECK_EQ(initiator, client);
    client_heap->safepoint()->EnterSafepointScope(
        IsolateSafepoint::StopMainThread::kNo);

    USE(this);
    DCHECK_EQ(client->shared_isolate(), shared_isolate_);
    DCHECK(client_heap->deserialization_complete());
  });
}

void GlobalSafepoint::LeaveGlobalSafepointScope(Isolate* initiator) {
  IterateClientIsolates([](Isolate* client) {
    Heap* client_heap = client->heap();
    client_heap->safepoint()->LeaveSafepointScope(
        IsolateSafepoint::StopMainThread::kNo);
  });

  clients_mutex_.Unlock();
}

GlobalSafepointScope::GlobalSafepointScope(Isolate* initiator)
    : initiator_(initiator), shared_isolate_(initiator->shared_isolate()) {
  if (shared_isolate_) {
    shared_isolate_->global_safepoint()->EnterGlobalSafepointScope(initiator_);
  } else {
    initiator_->heap()->safepoint()->EnterSafepointScope(
        IsolateSafepoint::StopMainThread::kNo);
  }
}

GlobalSafepointScope::~GlobalSafepointScope() {
  if (shared_isolate_) {
    shared_isolate_->global_safepoint()->LeaveGlobalSafepointScope(initiator_);
  } else {
    initiator_->heap()->safepoint()->LeaveSafepointScope(
        IsolateSafepoint::StopMainThread::kNo);
  }
}

}  // namespace internal
}  // namespace v8
