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

#include "mutex.h"

#include <errno.h>

#include "checks.h"

namespace v8 {
namespace internal {

#if V8_OS_UNIX

Mutex::Mutex() {
  pthread_mutexattr_t attr;
  int result = pthread_mutexattr_init(&attr);
  ASSERT_EQ(0, result);
  result = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  ASSERT_EQ(0, result);
  result = pthread_mutex_init(&mutex_, &attr);
  ASSERT_EQ(0, result);
  result = pthread_mutexattr_destroy(&attr);
  ASSERT_EQ(0, result);
  USE(result);
}


Mutex::~Mutex() {
  pthread_mutex_destroy(&mutex_);
}


void Mutex::Lock() {
  int result = pthread_mutex_lock(&mutex_);
  ASSERT_EQ(0, result);
  USE(result);
}


void Mutex::Unlock() {
  int result = pthread_mutex_unlock(&mutex_);
  ASSERT_EQ(0, result);
  USE(result);
}


bool Mutex::TryLock() {
  int result = pthread_mutex_trylock(&mutex_);
  // Return false if the lock is busy and locking failed.
  if (result == EBUSY) {
    return false;
  }
  ASSERT_EQ(0, result);
  return true;
}

#elif V8_OS_WIN32

Mutex::Mutex() {
  InitializeCriticalSection(&cs_);
}


Mutex::~Mutex() {
  DeleteCriticalSection(&cs_);
}


void Mutex::Lock() {
  EnterCriticalSection(&cs_);
}


void Mutex::Unlock() {
  LeaveCriticalSection(&cs_);
}


bool Mutex::TryLock() {
  return TryEnterCriticalSection(&cs_);
}

#endif  // V8_OS_WIN32

} }  // namespace v8::internal
