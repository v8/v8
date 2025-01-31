// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/platform/mutex.h"

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/time.h"

#if V8_OS_WIN
#include <windows.h>
#endif

#if DEBUG
#include <unordered_set>
#endif  // DEBUG

namespace v8 {
namespace base {

#if V8_OS_POSIX

static V8_INLINE void InitializeRecursiveNativeHandle(pthread_mutex_t* mutex) {
  pthread_mutexattr_t attr;
  int result = pthread_mutexattr_init(&attr);
  DCHECK_EQ(0, result);
  result = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  DCHECK_EQ(0, result);
  result = pthread_mutex_init(mutex, &attr);
  DCHECK_EQ(0, result);
  result = pthread_mutexattr_destroy(&attr);
  DCHECK_EQ(0, result);
  USE(result);
}


static V8_INLINE void DestroyNativeHandle(pthread_mutex_t* mutex) {
  int result = pthread_mutex_destroy(mutex);
  DCHECK_EQ(0, result);
  USE(result);
}


static V8_INLINE void LockNativeHandle(pthread_mutex_t* mutex) {
  int result = pthread_mutex_lock(mutex);
  DCHECK_EQ(0, result);
  USE(result);
}


static V8_INLINE void UnlockNativeHandle(pthread_mutex_t* mutex) {
  int result = pthread_mutex_unlock(mutex);
  DCHECK_EQ(0, result);
  USE(result);
}


static V8_INLINE bool TryLockNativeHandle(pthread_mutex_t* mutex) {
  int result = pthread_mutex_trylock(mutex);
  if (result == EBUSY) {
    return false;
  }
  DCHECK_EQ(0, result);
  return true;
}

RecursiveMutex::RecursiveMutex() {
  InitializeRecursiveNativeHandle(&native_handle_);
#ifdef DEBUG
  level_ = 0;
#endif
}


RecursiveMutex::~RecursiveMutex() {
  DestroyNativeHandle(&native_handle_);
  DCHECK_EQ(0, level_);
}


void RecursiveMutex::Lock() {
  LockNativeHandle(&native_handle_);
#ifdef DEBUG
  DCHECK_LE(0, level_);
  level_++;
#endif
}


void RecursiveMutex::Unlock() {
#ifdef DEBUG
  DCHECK_LT(0, level_);
  level_--;
#endif
  UnlockNativeHandle(&native_handle_);
}


bool RecursiveMutex::TryLock() {
  if (!TryLockNativeHandle(&native_handle_)) {
    return false;
  }
#ifdef DEBUG
  DCHECK_LE(0, level_);
  level_++;
#endif
  return true;
}

#elif V8_OS_WIN

RecursiveMutex::RecursiveMutex() {
  InitializeCriticalSection(V8ToWindowsType(&native_handle_));
#ifdef DEBUG
  level_ = 0;
#endif
}


RecursiveMutex::~RecursiveMutex() {
  DeleteCriticalSection(V8ToWindowsType(&native_handle_));
  DCHECK_EQ(0, level_);
}


void RecursiveMutex::Lock() {
  EnterCriticalSection(V8ToWindowsType(&native_handle_));
#ifdef DEBUG
  DCHECK_LE(0, level_);
  level_++;
#endif
}


void RecursiveMutex::Unlock() {
#ifdef DEBUG
  DCHECK_LT(0, level_);
  level_--;
#endif
  LeaveCriticalSection(V8ToWindowsType(&native_handle_));
}


bool RecursiveMutex::TryLock() {
  if (!TryEnterCriticalSection(V8ToWindowsType(&native_handle_))) {
    return false;
  }
#ifdef DEBUG
  DCHECK_LE(0, level_);
  level_++;
#endif
  return true;
}

#elif V8_OS_STARBOARD

RecursiveMutex::RecursiveMutex() {}

RecursiveMutex::~RecursiveMutex() {}

void RecursiveMutex::Lock() { native_handle_.Acquire(); }

void RecursiveMutex::Unlock() { native_handle_.Release(); }

bool RecursiveMutex::TryLock() { return native_handle_.AcquireTry(); }

#endif  // V8_OS_STARBOARD

Mutex::Mutex() {
#ifdef DEBUG
  level_ = 0;
#endif
}

Mutex::~Mutex() { DCHECK_EQ(0, level_); }

void Mutex::Lock() ABSL_EXCLUSIVE_LOCK_FUNCTION(native_handle_) {
  native_handle_.Lock();
  AssertUnheldAndMark();
}

void Mutex::Unlock() ABSL_UNLOCK_FUNCTION(native_handle_) {
  AssertHeldAndUnmark();
  native_handle_.Unlock();
}

bool Mutex::TryLock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true, native_handle_) {
  if (!native_handle_.TryLock()) return false;
  AssertUnheldAndMark();
  return true;
}

SpinningMutex::SpinningMutex() = default;
void SpinningMutex::Lock() ABSL_NO_THREAD_SAFETY_ANALYSIS { lock_.Lock(); }

}  // namespace base
}  // namespace v8
