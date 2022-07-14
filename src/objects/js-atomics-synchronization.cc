// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-atomics-synchronization.h"

#include "src/base/macros.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/base/platform/yield-processor.h"
#include "src/execution/isolate-inl.h"
#include "src/heap/parked-scope.h"
#include "src/objects/js-atomics-synchronization-inl.h"
#include "src/sandbox/external-pointer-inl.h"

namespace v8 {
namespace internal {

namespace detail {

// To manage waiting threads, there is a process-wide doubly-linked intrusive
// list per waiter (i.e. mutex or condition variable). There is a per-thread
// node allocated on the stack when the thread goes to sleep during waiting. In
// the case of sandboxed pointers, the access to the on-stack node is indirected
// through the shared external pointer table.
class V8_NODISCARD WaiterQueueNode final {
 public:
  template <typename T>
  static typename T::StateT EncodeHead(Isolate* requester,
                                       WaiterQueueNode* head) {
#ifdef V8_SANDBOXED_EXTERNAL_POINTERS
    if (head == nullptr) return 0;
    auto state = static_cast<typename T::StateT>(
        requester->InsertWaiterQueueNodeIntoSharedExternalPointerTable(
            reinterpret_cast<Address>(head)));
#else
    auto state = base::bit_cast<typename T::StateT>(head);
#endif  // V8_SANDBOXED_EXTERNAL_POINTERS

    DCHECK_EQ(0, state & T::kLockBitsMask);
    return state;
  }

  // Decode a WaiterQueueNode from the state. This is a destructive operation
  // when sandboxing external pointers to prevent reuse.
  template <typename T>
  static WaiterQueueNode* DestructivelyDecodeHead(Isolate* requester,
                                                  typename T::StateT state) {
#ifdef V8_SANDBOXED_EXTERNAL_POINTERS
    ExternalPointer_t ptr =
        static_cast<ExternalPointer_t>(state & T::kWaiterQueueHeadMask);
    if (ptr == 0) return nullptr;
    ExternalPointerHandle handle =
        static_cast<ExternalPointerHandle>(state & T::kWaiterQueueHeadMask);
    if (handle == 0) return nullptr;
    // The external pointer is cleared after decoding to prevent reuse by
    // multiple synchronization primitives in case of heap corruption.
    return reinterpret_cast<WaiterQueueNode*>(
        requester->shared_external_pointer_table().Exchange(
            handle, kNullAddress, kWaiterQueueNodeTag));
#else
    return base::bit_cast<WaiterQueueNode*>(state & T::kWaiterQueueHeadMask);
#endif  // V8_SANDBOXED_EXTERNAL_POINTERS
  }

  // Enqueues {new_tail}, mutating {head} to be the new head.
  static void Enqueue(WaiterQueueNode** head, WaiterQueueNode* new_tail) {
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

  // Dequeues a waiter and returns it; mutating {head} to be the new head.
  static WaiterQueueNode* Dequeue(WaiterQueueNode** head) {
    DCHECK_NOT_NULL(head);
    DCHECK_NOT_NULL(*head);
    WaiterQueueNode* current_head = *head;
    WaiterQueueNode* new_head = current_head->next_;
    if (new_head == current_head) {
      *head = nullptr;
    } else {
      WaiterQueueNode* tail = current_head->prev_;
      new_head->prev_ = tail;
      tail->next_ = new_head;
      *head = new_head;
    }
    current_head->SetNotInListForVerification();
    return current_head;
  }

  // Splits at most {count} nodes of the waiter list of into its own list and
  // returns it, mutating {head} to be the head of the back list.
  static WaiterQueueNode* Split(WaiterQueueNode** head, uint32_t count) {
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

  // This method must be called from a known waiter queue head. Incorrectly
  // encoded lists can cause this method to infinitely loop.
  static int LengthFromHead(WaiterQueueNode* head) {
    WaiterQueueNode* cur = head;
    int len = 0;
    do {
      len++;
      cur = cur->next_;
    } while (cur != head);
    return len;
  }

  void Wait(Isolate* requester) {
    AllowGarbageCollection allow_before_parking;
    ParkedScope parked_scope(requester->main_thread_local_heap());
    base::MutexGuard guard(&wait_lock_);
    while (should_wait) {
      wait_cond_var_.Wait(&wait_lock_);
    }
  }

  // Returns false if timed out, true otherwise.
  bool WaitFor(Isolate* requester, const base::TimeDelta& rel_time) {
    AllowGarbageCollection allow_before_parking;
    ParkedScope parked_scope(requester->main_thread_local_heap());
    base::MutexGuard guard(&wait_lock_);
    base::TimeTicks current_time = base::TimeTicks::Now();
    base::TimeTicks timeout_time = current_time + rel_time;
    for (;;) {
      if (!should_wait) return true;
      current_time = base::TimeTicks::Now();
      if (current_time >= timeout_time) return false;
      base::TimeDelta time_until_timeout = timeout_time - current_time;
      bool wait_res = wait_cond_var_.WaitFor(&wait_lock_, time_until_timeout);
      USE(wait_res);
      // The wake up may have been spurious, so loop again.
    }
  }

  void Notify() {
    base::MutexGuard guard(&wait_lock_);
    should_wait = false;
    wait_cond_var_.NotifyOne();
    SetNotInListForVerification();
  }

  uint32_t NotifyAllInList() {
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

  bool should_wait = false;

 private:
  void VerifyNotInList() {
    DCHECK_NULL(next_);
    DCHECK_NULL(prev_);
  }

  void SetNotInListForVerification() {
#ifdef DEBUG
    next_ = prev_ = nullptr;
#endif
  }

  // The queue wraps around, e.g. the head's prev is the tail, and the tail's
  // next is the head.
  WaiterQueueNode* next_ = nullptr;
  WaiterQueueNode* prev_ = nullptr;

  base::Mutex wait_lock_;
  base::ConditionVariable wait_cond_var_;
};

}  // namespace detail

using detail::WaiterQueueNode;

// static
Handle<JSAtomicsMutex> JSAtomicsMutex::Create(Isolate* isolate) {
  auto* factory = isolate->factory();
  Handle<Map> map = isolate->js_atomics_mutex_map();
  Handle<JSAtomicsMutex> mutex = Handle<JSAtomicsMutex>::cast(
      factory->NewSystemPointerAlignedJSObjectFromMap(
          map, AllocationType::kSharedOld));
  mutex->set_state(kUnlocked);
  mutex->set_owner_thread_id(ThreadId::Invalid().ToInteger());
  return mutex;
}

bool JSAtomicsMutex::TryLockExplicit(std::atomic<StateT>* state,
                                     StateT& expected) {
  // Try to lock a possibly contended mutex.
  expected &= ~kIsLockedBit;
  return state->compare_exchange_weak(expected, expected | kIsLockedBit,
                                      std::memory_order_acquire,
                                      std::memory_order_relaxed);
}

bool JSAtomicsMutex::TryLockWaiterQueueExplicit(std::atomic<StateT>* state,
                                                StateT& expected) {
  // The queue lock can only be acquired on a locked mutex.
  DCHECK(expected & kIsLockedBit);
  // Try to acquire the queue lock.
  expected &= ~kIsWaiterQueueLockedBit;
  return state->compare_exchange_weak(
      expected, expected | kIsWaiterQueueLockedBit, std::memory_order_acquire,
      std::memory_order_relaxed);
}

// static
void JSAtomicsMutex::LockSlowPath(Isolate* requester,
                                  Handle<JSAtomicsMutex> mutex,
                                  std::atomic<StateT>* state) {
  for (;;) {
    // Spin for a little bit to try to acquire the lock, so as to be fast under
    // microcontention.
    //
    // The backoff algorithm is copied from PartitionAlloc's SpinningMutex.
    constexpr int kSpinCount = 64;
    constexpr int kMaxBackoff = 16;

    int tries = 0;
    int backoff = 1;
    StateT current_state = state->load(std::memory_order_relaxed);
    do {
      if (mutex->TryLockExplicit(state, current_state)) return;

      for (int yields = 0; yields < backoff; yields++) {
        YIELD_PROCESSOR;
        tries++;
      }

      backoff = std::min(kMaxBackoff, backoff << 1);
    } while (tries < kSpinCount);

    // At this point the lock is considered contended, so try to go to sleep and
    // put the requester thread on the waiter queue.

    // Allocate a waiter queue node on-stack, since this thread is going to
    // sleep and will be blocked anyway.
    WaiterQueueNode this_waiter;

    {
      // Try to acquire the queue lock, which is itself a spinlock.
      current_state = state->load(std::memory_order_relaxed);
      for (;;) {
        if ((current_state & kIsLockedBit) &&
            mutex->TryLockWaiterQueueExplicit(state, current_state)) {
          break;
        }
        // Also check for the lock having been released by another thread during
        // attempts to acquire the queue lock.
        if (mutex->TryLockExplicit(state, current_state)) return;
        YIELD_PROCESSOR;
      }

      // With the queue lock held, enqueue the requester onto the waiter queue.
      this_waiter.should_wait = true;
      WaiterQueueNode* waiter_head =
          WaiterQueueNode::DestructivelyDecodeHead<JSAtomicsMutex>(
              requester, current_state);
      WaiterQueueNode::Enqueue(&waiter_head, &this_waiter);

      // Release the queue lock and install the new waiter queue head by
      // creating a new state.
      DCHECK_EQ(state->load(), current_state | kIsWaiterQueueLockedBit);
      StateT new_state =
          WaiterQueueNode::EncodeHead<JSAtomicsMutex>(requester, waiter_head);
      // The lock is held, just not by us, so don't set the current thread id as
      // the owner.
      DCHECK(current_state & kIsLockedBit);
      DCHECK(!mutex->IsCurrentThreadOwner());
      new_state |= kIsLockedBit;
      state->store(new_state, std::memory_order_release);
    }

    // Wait for another thread to release the lock and wake us up.
    this_waiter.Wait(requester);

    // Reload the state pointer after wake up in case of shared GC while
    // blocked.
    state = mutex->AtomicStatePtr();

    // After wake up we try to acquire the lock again by spinning, as the
    // contention at the point of going to sleep should not be correlated with
    // contention at the point of waking up.
  }
}

void JSAtomicsMutex::UnlockSlowPath(Isolate* requester,
                                    std::atomic<StateT>* state) {
  // The fast path unconditionally cleared the owner thread.
  DCHECK_EQ(ThreadId::Invalid().ToInteger(),
            AtomicOwnerThreadIdPtr()->load(std::memory_order_relaxed));

  // To wake a sleeping thread, first acquire the queue lock, which is itself
  // a spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);
  while (!TryLockWaiterQueueExplicit(state, current_state)) {
    YIELD_PROCESSOR;
  }

  // Get the waiter queue head, which is guaranteed to be non-null because the
  // unlock fast path uses a strong CAS which does not allow spurious
  // failure. This is unlike the lock fast path, which uses a weak CAS.
  WaiterQueueNode* waiter_head =
      WaiterQueueNode::DestructivelyDecodeHead<JSAtomicsMutex>(requester,
                                                               current_state);
  WaiterQueueNode* old_head = WaiterQueueNode::Dequeue(&waiter_head);

  // Release both the lock and the queue lock and also install the new waiter
  // queue head by creating a new state.
  StateT new_state =
      WaiterQueueNode::EncodeHead<JSAtomicsMutex>(requester, waiter_head);
  state->store(new_state, std::memory_order_release);

  old_head->Notify();
}

// static
Handle<JSAtomicsCondition> JSAtomicsCondition::Create(Isolate* isolate) {
  auto* factory = isolate->factory();
  Handle<Map> map = isolate->js_atomics_condition_map();
  Handle<JSAtomicsCondition> cond = Handle<JSAtomicsCondition>::cast(
      factory->NewJSObjectFromMap(map, AllocationType::kSharedOld));
  cond->set_state(kEmptyState);
  return cond;
}

bool JSAtomicsCondition::TryLockWaiterQueueExplicit(std::atomic<StateT>* state,
                                                    StateT& expected) {
  // Try to acquire the queue lock.
  expected &= ~kIsWaiterQueueLockedBit;
  return state->compare_exchange_weak(
      expected, expected | kIsWaiterQueueLockedBit, std::memory_order_acquire,
      std::memory_order_relaxed);
}

// static
bool JSAtomicsCondition::WaitFor(Isolate* requester,
                                 Handle<JSAtomicsCondition> cv,
                                 Handle<JSAtomicsMutex> mutex,
                                 base::Optional<base::TimeDelta> timeout) {
  DisallowGarbageCollection no_gc;

  // Allocate a waiter queue node on-stack, since this thread is going to sleep
  // and will be blocked anyway.
  WaiterQueueNode this_waiter;

  {
    // The state pointer should not be used outside of this block as a shared GC
    // may reallocate it after waiting.
    std::atomic<StateT>* state = cv->AtomicStatePtr();

    // Try to acquire the queue lock, which is itself a spinlock.
    StateT current_state = state->load(std::memory_order_relaxed);
    while (!cv->TryLockWaiterQueueExplicit(state, current_state)) {
      YIELD_PROCESSOR;
    }

    // With the queue lock held, enqueue the requester onto the waiter queue.
    this_waiter.should_wait = true;
    WaiterQueueNode* waiter_head =
        WaiterQueueNode::DestructivelyDecodeHead<JSAtomicsCondition>(
            requester, current_state);
    WaiterQueueNode::Enqueue(&waiter_head, &this_waiter);

    // Release the queue lock and install the new waiter queue head by creating
    // a new state.
    DCHECK_EQ(state->load(), current_state | kIsWaiterQueueLockedBit);
    StateT new_state =
        WaiterQueueNode::EncodeHead<JSAtomicsCondition>(requester, waiter_head);
    state->store(new_state, std::memory_order_release);
  }

  // Release the mutex and wait for another thread to wake us up, reacquiring
  // the mutex upon wakeup.
  mutex->Unlock(requester);
  bool rv;
  if (timeout) {
    rv = this_waiter.WaitFor(requester, *timeout);
  } else {
    this_waiter.Wait(requester);
    rv = true;
  }
  JSAtomicsMutex::Lock(requester, mutex);
  return rv;
}

uint32_t JSAtomicsCondition::Notify(Isolate* requester, uint32_t count) {
  std::atomic<StateT>* state = AtomicStatePtr();

  // To wake a sleeping thread, first acquire the queue lock, which is itself a
  // spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);
  // There are no waiters.
  if (current_state == kEmptyState) return 0;
  while (!TryLockWaiterQueueExplicit(state, current_state)) {
    YIELD_PROCESSOR;
  }

  // Get the waiter queue head.
  WaiterQueueNode* waiter_head =
      WaiterQueueNode::DestructivelyDecodeHead<JSAtomicsCondition>(
          requester, current_state);

  // There's no waiter to wake up, release the queue lock by setting it to the
  // empty state.
  if (waiter_head == nullptr) {
    DCHECK_EQ(state->load(), current_state | kIsWaiterQueueLockedBit);
    state->store(kEmptyState, std::memory_order_release);
    return 0;
  }

  WaiterQueueNode* old_head;
  if (count == 1) {
    old_head = WaiterQueueNode::Dequeue(&waiter_head);
  } else if (count == kAllWaiters) {
    old_head = waiter_head;
    waiter_head = nullptr;
  } else {
    old_head = WaiterQueueNode::Split(&waiter_head, count);
  }

  // Release the queue lock and install the new waiter queue head by creating a
  // new state.
  DCHECK_EQ(state->load(), current_state | kIsWaiterQueueLockedBit);
  StateT new_state =
      WaiterQueueNode::EncodeHead<JSAtomicsCondition>(requester, waiter_head);
  state->store(new_state, std::memory_order_release);

  // Notify the waiters.
  if (count == 1) {
    old_head->Notify();
    return 1;
  }
  return old_head->NotifyAllInList();
}

Object JSAtomicsCondition::NumWaitersForTesting(Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  std::atomic<StateT>* state = AtomicStatePtr();
  StateT current_state = state->load(std::memory_order_relaxed);

  // There are no waiters.
  if (current_state == kEmptyState) return Smi::FromInt(0);

  int num_waiters;
  {
    // Take the queue lock.
    while (!TryLockWaiterQueueExplicit(state, current_state)) {
      YIELD_PROCESSOR;
    }

    // Get the waiter queue head.
    WaiterQueueNode* waiter_head =
        WaiterQueueNode::DestructivelyDecodeHead<JSAtomicsCondition>(
            isolate, current_state);
    num_waiters = WaiterQueueNode::LengthFromHead(waiter_head);

    // Release the queue lock and reinstall the same queue head by creating a
    // new state.
    DCHECK_EQ(state->load(), current_state | kIsWaiterQueueLockedBit);
    StateT new_state =
        WaiterQueueNode::EncodeHead<JSAtomicsCondition>(isolate, waiter_head);
    state->store(new_state, std::memory_order_release);
  }

  return Smi::FromInt(num_waiters);
}

}  // namespace internal
}  // namespace v8
