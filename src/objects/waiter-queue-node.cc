// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/waiter-queue-node.h"

#include "src/base/macros.h"
#include "src/base/platform/time.h"
#include "src/heap/local-heap-inl.h"

namespace v8 {
namespace internal {

namespace {

bool WasInterruptedWithoutException(Isolate* requester, bool interrupted) {
  if (interrupted) {
    return !IsException(requester->stack_guard()->HandleInterrupts(),
                        requester);
  }
  return false;
}

}  // namespace

namespace detail {

WaiterQueueNode::WaiterQueueNode(Isolate* requester) : requester_(requester) {}

WaiterQueueNode::~WaiterQueueNode() {
  // Since waiter queue nodes are allocated on the stack, they must be removed
  // from the intrusive linked list once they go out of scope, otherwise there
  // will be dangling pointers.
  VerifyNotInList();
}

// static
void WaiterQueueNode::Enqueue(WaiterQueueNode** head,
                              WaiterQueueNode* new_tail) {
  DCHECK_NOT_NULL(head);
  new_tail->VerifyNotInList();
  WaiterQueueNode* current_head = *head;
  if (current_head == nullptr) {
    new_tail->next_ = new_tail;
    new_tail->prev_ = new_tail;
    *head = new_tail;
  } else {
    WaiterQueueNode* current_tail = current_head->prev_;
    current_tail->next_ = new_tail;
    current_head->prev_ = new_tail;
    new_tail->next_ = current_head;
    new_tail->prev_ = current_tail;
  }
}

void WaiterQueueNode::DequeueUnchecked(WaiterQueueNode** head) {
  if (next_ == this) {
    // The queue contains exactly 1 node.
    *head = nullptr;
  } else {
    // The queue contains >1 nodes.
    if (this == *head) {
      WaiterQueueNode* tail = (*head)->prev_;
      // The matched node is the head, so next is the new head.
      next_->prev_ = tail;
      tail->next_ = next_;
      *head = next_;
    } else {
      // The matched node is in the middle of the queue, so the head does
      // not need to be updated.
      prev_->next_ = next_;
      next_->prev_ = prev_;
    }
  }
  SetNotInListForVerification();
}

WaiterQueueNode* WaiterQueueNode::DequeueMatching(
    WaiterQueueNode** head, const DequeueMatcher& matcher) {
  DCHECK_NOT_NULL(head);
  DCHECK_NOT_NULL(*head);
  WaiterQueueNode* original_head = *head;
  WaiterQueueNode* cur = *head;
  do {
    if (matcher(cur)) {
      cur->DequeueUnchecked(head);
      return cur;
    }
    cur = cur->next_;
  } while (cur != original_head);
  return nullptr;
}

void WaiterQueueNode::DequeueAllMatchingForAsyncCleanup(
    WaiterQueueNode** head, const DequeueMatcher& matcher) {
  DCHECK_NOT_NULL(head);
  DCHECK_NOT_NULL(*head);
  WaiterQueueNode* original_tail = (*head)->prev_;
  WaiterQueueNode* cur = *head;
  for (;;) {
    DCHECK_NOT_NULL(cur);
    WaiterQueueNode* next = cur->next_;
    if (matcher(cur)) {
      cur->DequeueUnchecked(head);
      cur->SetReadyForAsyncCleanup();
    }
    if (cur == original_tail) break;
    cur = next;
  }
}

// static
WaiterQueueNode* WaiterQueueNode::Dequeue(WaiterQueueNode** head) {
  return DequeueMatching(head, [](WaiterQueueNode* node) { return true; });
}

// static
WaiterQueueNode* WaiterQueueNode::Split(WaiterQueueNode** head,
                                        uint32_t count) {
  DCHECK_GT(count, 0);
  DCHECK_NOT_NULL(head);
  DCHECK_NOT_NULL(*head);
  WaiterQueueNode* front_head = *head;
  WaiterQueueNode* back_head = front_head;
  uint32_t actual_count = 0;
  while (actual_count < count) {
    back_head = back_head->next_;
    // The queue is shorter than the requested count, return the whole queue.
    if (back_head == front_head) {
      *head = nullptr;
      return front_head;
    }
    actual_count++;
  }
  WaiterQueueNode* front_tail = back_head->prev_;
  WaiterQueueNode* back_tail = front_head->prev_;

  // Fix up the back list (i.e. remainder of the list).
  back_head->prev_ = back_tail;
  back_tail->next_ = back_head;
  *head = back_head;

  // Fix up and return the front list (i.e. the dequeued list).
  front_head->prev_ = front_tail;
  front_tail->next_ = front_head;
  return front_head;
}

// static
int WaiterQueueNode::LengthFromHead(WaiterQueueNode* head) {
  WaiterQueueNode* cur = head;
  int len = 0;
  do {
    len++;
    cur = cur->next_;
  } while (cur != head);
  return len;
}

uint32_t WaiterQueueNode::NotifyAllInList() {
  WaiterQueueNode* cur = this;
  uint32_t count = 0;
  do {
    WaiterQueueNode* next = cur->next_;
    cur->Notify();
    cur = next;
    count++;
  } while (cur != this);
  return count;
}

void WaiterQueueNode::VerifyNotInList() {
  DCHECK_NULL(next_);
  DCHECK_NULL(prev_);
}

void WaiterQueueNode::SetNotInListForVerification() {
#ifdef DEBUG
  next_ = prev_ = nullptr;
#endif
}

SyncWaiterQueueNode::WaitResult SyncWaiterQueueNode::Wait() {
  AllowGarbageCollection allow_before_parking;
  should_wait_ = true;
  // Outer loop checks for interruptions.
  for (;;) {
    WaitResult result;
    requester_->main_thread_local_heap()->ExecuteWhileParked([this, &result]() {
      base::MutexGuard guard(&wait_lock_);
      // Check for interruptions first so that we don't drop any
      // interrupts that came while the lock wasn't held.
      while (should_wait_ && !thread_interrupted_) {
        wait_cond_var_.Wait(&wait_lock_);
      }
      if (thread_interrupted_) {
        result = WaitResult::kThreadTerminated;
        thread_interrupted_ = false;
        return;
      }
      result = WaitResult::kNotified;
    });
    // This must be outside of the critical section to prevent deadlock from
    // lock ordering between `wait_lock` and mutexes locked by HandleInterrupts.
    if (WasInterruptedWithoutException(
            requester_, result == WaitResult::kThreadTerminated)) {
      // An interrupt signal was received but no exception was thrown. Likely
      // due to GC.
      continue;
    }
    return result;
  }
}

SyncWaiterQueueNode::WaitResult SyncWaiterQueueNode::WaitFor(
    const base::TimeDelta& rel_time) {
  AllowGarbageCollection allow_before_parking;
  should_wait_ = true;
  // Outer loop checks for interruptions.
  for (;;) {
    WaitResult result;
    requester_->main_thread_local_heap()->ExecuteWhileParked([this, rel_time,
                                                              &result]() {
      base::MutexGuard guard(&wait_lock_);
      base::TimeTicks current_time = base::TimeTicks::Now();
      base::TimeTicks timeout_time = current_time + rel_time;
      for (;;) {
        // Check for interruptions first so that we don't drop any
        // interrupts that came while the lock wasn't held.
        if (thread_interrupted_) {
          result = WaitResult::kThreadTerminated;
          thread_interrupted_ = false;
          return;
        }
        if (!should_wait_) {
          result = WaitResult::kNotified;
          return;
        }
        current_time = base::TimeTicks::Now();
        if (current_time >= timeout_time) {
          result = WaitResult::kTimedOut;
          return;
        }
        base::TimeDelta time_until_timeout = timeout_time - current_time;
        bool wait_res = wait_cond_var_.WaitFor(&wait_lock_, time_until_timeout);
        USE(wait_res);
        // The wake up may have been spurious, so loop again.
      }
    });
    // This must be outside of the critical section to prevent deadlock from
    // lock ordering between `wait_lock` and mutexes locked by HandleInterrupts.
    if (WasInterruptedWithoutException(
            requester_, result == WaitResult::kThreadTerminated)) {
      // An interrupt signal was received but no exception was thrown. Likely
      // due to GC.
      continue;
    }
    return result;
  }
}

void SyncWaiterQueueNode::Notify() {
  base::MutexGuard guard(&wait_lock_);
  should_wait_ = false;
  wait_cond_var_.NotifyOne();
  SetNotInListForVerification();
}

void SyncWaiterQueueNode::NotifyInterrupted() {
  base::MutexGuard guard(&wait_lock_);
  thread_interrupted_ = true;
  wait_cond_var_.NotifyOne();
}

bool SyncWaiterQueueNode::IsSameIsolateForAsyncCleanup(Isolate* isolate) {
  // Sync waiters are only queued while the thread is sleeping, so there
  // should not be sync nodes while cleaning up the isolate.
  DCHECK_NE(requester_, isolate);
  return false;
}

}  // namespace detail
}  // namespace internal
}  // namespace v8
