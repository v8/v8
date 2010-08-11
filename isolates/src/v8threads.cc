// Copyright 2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "api.h"
#include "bootstrapper.h"
#include "debug.h"
#include "execution.h"
#include "v8threads.h"
#include "regexp-stack.h"

namespace v8 {


// Track whether this V8 instance has ever called v8::Locker. This allows the
// API code to verify that the lock is always held when V8 is being entered.
bool Locker::active_ = false;


// Constructor for the Locker object.  Once the Locker is constructed the
// current thread will be guaranteed to have the big V8 lock.
Locker::Locker() : has_lock_(false), top_level_(true) {
  // TODO(isolates): When Locker has Isolate parameter and it is provided, grab
  // that one instead of using the current one.
  // We pull default isolate for Locker constructor w/o p[arameter.
  // A thread should not enter an isolate before acquiring a lock,
  // in cases which mandate using Lockers.
  // So getting a lock is the first thing threads do in a scenario where
  // multple threads share an isolate. Hence, we need to access
  // 'locking isolate' before we can actually enter into default isolate.
  internal::Isolate* isolate = internal::Isolate::GetDefaultIsolateForLocking();
  ASSERT(isolate != NULL);

  // Record that the Locker has been used at least once.
  active_ = true;
  // Get the big lock if necessary.
  if (!isolate->thread_manager()->IsLockedByCurrentThread()) {
    isolate->thread_manager()->Lock();
    has_lock_ = true;

    if (isolate->IsDefaultIsolate()) {
      // This only enters if not yet entered.
      internal::Isolate::EnterDefaultIsolate();
    }

    ASSERT(internal::Thread::HasThreadLocal(
        internal::Isolate::thread_id_key()));

    // Make sure that V8 is initialized.  Archiving of threads interferes
    // with deserialization by adding additional root pointers, so we must
    // initialize here, before anyone can call ~Locker() or Unlocker().
    if (!internal::V8::IsRunning()) {
      V8::Initialize();
    }
    // This may be a locker within an unlocker in which case we have to
    // get the saved state for this thread and restore it.
    if (isolate->thread_manager()->RestoreThread()) {
      top_level_ = false;
    } else {
      internal::ExecutionAccess access;
      isolate->stack_guard()->ClearThread(access);
      isolate->stack_guard()->InitThread(access);
    }
  }
  ASSERT(isolate->thread_manager()->IsLockedByCurrentThread());
}


bool Locker::IsLocked() {
  return internal::Isolate::Current()->thread_manager()->
      IsLockedByCurrentThread();
}


Locker::~Locker() {
  internal::Isolate* isolate = internal::Isolate::Current();
  ASSERT(isolate->thread_manager()->IsLockedByCurrentThread());
  if (has_lock_) {
    if (top_level_) {
      isolate->thread_manager()->FreeThreadResources();
    } else {
      isolate->thread_manager()->ArchiveThread();
    }
    isolate->thread_manager()->Unlock();
  }
}


Unlocker::Unlocker() {
  internal::Isolate* isolate = internal::Isolate::Current();
  ASSERT(isolate->thread_manager()->IsLockedByCurrentThread());
  isolate->thread_manager()->ArchiveThread();
  isolate->thread_manager()->Unlock();
}


Unlocker::~Unlocker() {
  internal::Isolate* isolate = internal::Isolate::Current();
  ASSERT(!isolate->thread_manager()->IsLockedByCurrentThread());
  isolate->thread_manager()->Lock();
  isolate->thread_manager()->RestoreThread();
}


void Locker::StartPreemption(int every_n_ms) {
  v8::internal::ContextSwitcher::StartPreemption(every_n_ms);
}


void Locker::StopPreemption() {
  v8::internal::ContextSwitcher::StopPreemption();
}


namespace internal {


bool ThreadManager::RestoreThread() {
  // First check whether the current thread has been 'lazily archived', ie
  // not archived at all.  If that is the case we put the state storage we
  // had prepared back in the free list, since we didn't need it after all.
  if (lazily_archived_thread_.IsSelf()) {
    lazily_archived_thread_.Initialize(ThreadHandle::INVALID);
    ASSERT(Isolate::CurrentPerIsolateThreadData()->thread_state() ==
           lazily_archived_thread_state_);
    lazily_archived_thread_state_->set_id(kInvalidId);
    lazily_archived_thread_state_->LinkInto(ThreadState::FREE_LIST);
    lazily_archived_thread_state_ = NULL;
    Isolate::CurrentPerIsolateThreadData()->set_thread_state(NULL);
    return true;
  }

  // Make sure that the preemption thread cannot modify the thread state while
  // it is being archived or restored.
  ExecutionAccess access;

  // If there is another thread that was lazily archived then we have to really
  // archive it now.
  if (lazily_archived_thread_.IsValid()) {
    EagerlyArchiveThread();
  }
  Isolate::PerIsolateThreadData* per_thread =
      Isolate::CurrentPerIsolateThreadData();
  if (per_thread == NULL || per_thread->thread_state() == NULL) {
    // This is a new thread.
    isolate_->stack_guard()->InitThread(access);
    return false;
  }
  ThreadState* state = per_thread->thread_state();
  char* from = state->data();
  from = isolate_->handle_scope_implementer()->RestoreThread(from);
  from = isolate_->RestoreThread(from);
  from = Relocatable::RestoreState(from);
#ifdef ENABLE_DEBUGGER_SUPPORT
  from = isolate_->debug()->RestoreDebug(from);
#endif
  from = isolate_->stack_guard()->RestoreStackGuard(from);
  from = isolate_->regexp_stack()->RestoreStack(from);
  from = isolate_->bootstrapper()->RestoreState(from);
  per_thread->set_thread_state(NULL);
  if (state->terminate_on_restore()) {
    isolate_->stack_guard()->TerminateExecution();
    state->set_terminate_on_restore(false);
  }
  state->set_id(kInvalidId);
  state->Unlink();
  state->LinkInto(ThreadState::FREE_LIST);
  return true;
}


void ThreadManager::Lock() {
  mutex_->Lock();
  mutex_owner_.Initialize(ThreadHandle::SELF);
  ASSERT(IsLockedByCurrentThread());
}


void ThreadManager::Unlock() {
  mutex_owner_.Initialize(ThreadHandle::INVALID);
  mutex_->Unlock();
}


static int ArchiveSpacePerThread() {
  return HandleScopeImplementer::ArchiveSpacePerThread() +
                        Isolate::ArchiveSpacePerThread() +
#ifdef ENABLE_DEBUGGER_SUPPORT
                          Debug::ArchiveSpacePerThread() +
#endif
                     StackGuard::ArchiveSpacePerThread() +
                    RegExpStack::ArchiveSpacePerThread() +
                   Bootstrapper::ArchiveSpacePerThread() +
                    Relocatable::ArchiveSpacePerThread();
}


ThreadState::ThreadState(ThreadManager* thread_manager)
    : id_(ThreadManager::kInvalidId),
      terminate_on_restore_(false),
      next_(this),
      previous_(this),
      thread_manager_(thread_manager) {
}


void ThreadState::AllocateSpace() {
  data_ = NewArray<char>(ArchiveSpacePerThread());
}


void ThreadState::Unlink() {
  next_->previous_ = previous_;
  previous_->next_ = next_;
}


void ThreadState::LinkInto(List list) {
  ThreadState* flying_anchor =
      list == FREE_LIST ? thread_manager_->free_anchor_
                        : thread_manager_->in_use_anchor_;
  next_ = flying_anchor->next_;
  previous_ = flying_anchor;
  flying_anchor->next_ = this;
  next_->previous_ = this;
}


ThreadState* ThreadManager::GetFreeThreadState() {
  ThreadState* gotten = free_anchor_->next_;
  if (gotten == free_anchor_) {
    ThreadState* new_thread_state = new ThreadState(this);
    new_thread_state->AllocateSpace();
    return new_thread_state;
  }
  return gotten;
}


// Gets the first in the list of archived threads.
ThreadState* ThreadManager::FirstThreadStateInUse() {
  return in_use_anchor_->Next();
}


ThreadState* ThreadState::Next() {
  if (next_ == thread_manager_->in_use_anchor_) return NULL;
  return next_;
}


// Thread ids must start with 1, because in TLS having thread id 0 can't
// be distinguished from not having a thread id at all (since NULL is
// defined as 0.)
ThreadManager::ThreadManager()
    : mutex_(OS::CreateMutex()),
      mutex_owner_(ThreadHandle::INVALID),
      lazily_archived_thread_(ThreadHandle::INVALID),
      lazily_archived_thread_state_(NULL),
      free_anchor_(NULL),
      in_use_anchor_(NULL) {
  free_anchor_ = new ThreadState(this);
  in_use_anchor_ = new ThreadState(this);
}


ThreadManager::~ThreadManager() {
  // TODO(isolates): Destroy TLS keys and mutexes.
}


void ThreadManager::ArchiveThread() {
  ASSERT(!lazily_archived_thread_.IsValid());
  ASSERT(!IsArchived());
  ThreadState* state = GetFreeThreadState();
  state->Unlink();
  Isolate::CurrentPerIsolateThreadData()->set_thread_state(state);
  lazily_archived_thread_.Initialize(ThreadHandle::SELF);
  lazily_archived_thread_state_ = state;
  ASSERT(state->id() == kInvalidId);
  state->set_id(CurrentId());
  ASSERT(state->id() != kInvalidId);
}


void ThreadManager::EagerlyArchiveThread() {
  ThreadState* state = lazily_archived_thread_state_;
  state->LinkInto(ThreadState::IN_USE_LIST);
  char* to = state->data();
  // Ensure that data containing GC roots are archived first, and handle them
  // in ThreadManager::Iterate(ObjectVisitor*).
  to = isolate_->handle_scope_implementer()->ArchiveThread(to);
  to = isolate_->ArchiveThread(to);
  to = Relocatable::ArchiveState(to);
#ifdef ENABLE_DEBUGGER_SUPPORT
  to = isolate_->debug()->ArchiveDebug(to);
#endif
  to = isolate_->stack_guard()->ArchiveStackGuard(to);
  to = isolate_->regexp_stack()->ArchiveStack(to);
  to = isolate_->bootstrapper()->ArchiveState(to);
  lazily_archived_thread_.Initialize(ThreadHandle::INVALID);
  lazily_archived_thread_state_ = NULL;
}


void ThreadManager::FreeThreadResources() {
  isolate_->handle_scope_implementer()->FreeThreadResources();
  isolate_->FreeThreadResources();
#ifdef ENABLE_DEBUGGER_SUPPORT
  isolate_->debug()->FreeThreadResources();
#endif
  isolate_->stack_guard()->FreeThreadResources();
  isolate_->regexp_stack()->FreeThreadResources();
  isolate_->bootstrapper()->FreeThreadResources();
}


bool ThreadManager::IsArchived() {
  Isolate::PerIsolateThreadData* data = Isolate::CurrentPerIsolateThreadData();
  return data != NULL && data->thread_state() != NULL;
}


void ThreadManager::Iterate(ObjectVisitor* v) {
  // Expecting no threads during serialization/deserialization
  for (ThreadState* state = FirstThreadStateInUse();
       state != NULL;
       state = state->Next()) {
    char* data = state->data();
    data = HandleScopeImplementer::Iterate(v, data);
    data = isolate_->Iterate(v, data);
    data = Relocatable::Iterate(v, data);
  }
}


void ThreadManager::IterateArchivedThreads(ThreadVisitor* v) {
  for (ThreadState* state = FirstThreadStateInUse();
       state != NULL;
       state = state->Next()) {
    char* data = state->data();
    data += HandleScopeImplementer::ArchiveSpacePerThread();
    isolate_->IterateThread(v, data);
  }
}


void ThreadManager::MarkCompactPrologue(bool is_compacting) {
  for (ThreadState* state = FirstThreadStateInUse();
       state != NULL;
       state = state->Next()) {
    char* data = state->data();
    data += HandleScopeImplementer::ArchiveSpacePerThread();
    isolate_->MarkCompactPrologue(is_compacting, data);
  }
}


void ThreadManager::MarkCompactEpilogue(bool is_compacting) {
  for (ThreadState* state = FirstThreadStateInUse();
       state != NULL;
       state = state->Next()) {
    char* data = state->data();
    data += HandleScopeImplementer::ArchiveSpacePerThread();
    isolate_->MarkCompactEpilogue(is_compacting, data);
  }
}


int ThreadManager::CurrentId() {
  return Thread::GetThreadLocalInt(Isolate::thread_id_key());
}


void ThreadManager::TerminateExecution(int thread_id) {
  for (ThreadState* state = FirstThreadStateInUse();
       state != NULL;
       state = state->Next()) {
    if (thread_id == state->id()) {
      state->set_terminate_on_restore(true);
    }
  }
}


ContextSwitcher::ContextSwitcher(Isolate* isolate, int every_n_ms)
  : Thread(isolate),
    keep_going_(true),
    sleep_ms_(every_n_ms) {
}


// Set the scheduling interval of V8 threads. This function starts the
// ContextSwitcher thread if needed.
void ContextSwitcher::StartPreemption(int every_n_ms) {
  Isolate* isolate = Isolate::Current();
  ASSERT(Locker::IsLocked());
  if (isolate->context_switcher() == NULL) {
    // If the ContextSwitcher thread is not running at the moment start it now.
    isolate->set_context_switcher(new ContextSwitcher(isolate, every_n_ms));
    isolate->context_switcher()->Start();
  } else {
    // ContextSwitcher thread is already running, so we just change the
    // scheduling interval.
    isolate->context_switcher()->sleep_ms_ = every_n_ms;
  }
}


// Disable preemption of V8 threads. If multiple threads want to use V8 they
// must cooperatively schedule amongst them from this point on.
void ContextSwitcher::StopPreemption() {
  Isolate* isolate = Isolate::Current();
  ASSERT(Locker::IsLocked());
  if (isolate->context_switcher() != NULL) {
    // The ContextSwitcher thread is running. We need to stop it and release
    // its resources.
    isolate->context_switcher()->keep_going_ = false;
    // Wait for the ContextSwitcher thread to exit.
    isolate->context_switcher()->Join();
    // Thread has exited, now we can delete it.
    delete(isolate->context_switcher());
    isolate->set_context_switcher(NULL);
  }
}


// Main loop of the ContextSwitcher thread: Preempt the currently running V8
// thread at regular intervals.
void ContextSwitcher::Run() {
  while (keep_going_) {
    OS::Sleep(sleep_ms_);
    isolate()->stack_guard()->Preempt();
  }
}


// Acknowledge the preemption by the receiving thread.
void ContextSwitcher::PreemptionReceived() {
  ASSERT(Locker::IsLocked());
  // There is currently no accounting being done for this. But could be in the
  // future, which is why we leave this in.
}


}  // namespace internal
}  // namespace v8
