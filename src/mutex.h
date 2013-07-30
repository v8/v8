// Copyright 2013 the V8 project authors. All rights reserved.
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

#ifndef V8_MUTEX_H_
#define V8_MUTEX_H_

#include "globals.h"

#if V8_OS_UNIX
#include <pthread.h>  // NOLINT
#elif V8_OS_WIN32
#include "win32-headers.h"
#endif

#include "checks.h"
#include "lazy-instance.h"

namespace v8 {
namespace internal {

// ----------------------------------------------------------------------------
// Mutex
//
// Mutexes are used for serializing access to non-reentrant sections of code.
// The implementations of mutex should allow for nested/recursive locking.
//
class Mutex {
 public:
  Mutex();
  ~Mutex();

  // Locks the given mutex. If the mutex is currently unlocked, it becomes
  // locked and owned by the calling thread. If the mutex is already locked
  // by another thread, suspends the calling thread until the mutex is
  // unlocked.
  void Lock();

  // Unlocks the given mutex. The mutex is assumed to be locked and owned
  // by the calling thread on entrance.
  void Unlock();

  // Tries to lock the given mutex. Returns true if the mutex was locked
  // successfully.
  bool TryLock();

 private:
#if V8_OS_UNIX
  pthread_mutex_t mutex_;
#elif V8_OS_WIN32
  CRITICAL_SECTION cs_;
#endif

  DISALLOW_COPY_AND_ASSIGN(Mutex);
};


// ----------------------------------------------------------------------------
// LazyMutex
//
// POD Mutex initialized lazily (i.e. the first time Pointer() is called).
// Usage:
//   static LazyMutex my_mutex = LAZY_MUTEX_INITIALIZER;
//
//   void my_function() {
//     ScopedLock my_lock(&my_mutex);
//     // Do something.
//   }
//
typedef LazyDynamicInstance<Mutex,
                            DefaultCreateTrait<Mutex>,
                            ThreadSafeInitOnceTrait>::type LazyMutex;

#define LAZY_MUTEX_INITIALIZER LAZY_DYNAMIC_INSTANCE_INITIALIZER


// ----------------------------------------------------------------------------
// ScopedLock
//
// Stack-allocated ScopedLocks provide block-scoped locking and
// unlocking of a mutex.
//
class ScopedLock {
 public:
  explicit ScopedLock(Mutex* mutex): mutex_(mutex) {
    ASSERT(mutex_ != NULL);
    mutex_->Lock();
  }

  explicit ScopedLock(LazyMutex* lazy_mutex) : mutex_(lazy_mutex->Pointer()) {
    ASSERT(mutex_ != NULL);
    mutex_->Lock();
  }

  ~ScopedLock() {
    mutex_->Unlock();
  }

 private:
  Mutex* mutex_;

  DISALLOW_COPY_AND_ASSIGN(ScopedLock);
};

} }  // namespace v8::internal

#endif  // V8_MUTEX_H_
