// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/platform/mutex.h"

#include <errno.h>

#include <atomic>

#include "src/base/platform/condition-variable.h"

#if DEBUG
#include <unordered_set>
#endif  // DEBUG

#if V8_OS_WIN
#include <windows.h>
#endif

namespace v8 {
namespace base {

#if DEBUG
namespace {
// Used for asserts to guarantee we are not re-locking a mutex on the same
// thread. If this thread has only one held shared mutex (common case), we use
// {single_held_shared_mutex}. If it has more than one we allocate a set for it.
// Said set has to manually be constructed and destroyed.
thread_local base::SharedMutex* single_held_shared_mutex = nullptr;
using TSet = std::unordered_set<base::SharedMutex*>;
thread_local TSet* held_shared_mutexes = nullptr;

// Returns true iff {shared_mutex} is not a held mutex.
bool SharedMutexNotHeld(SharedMutex* shared_mutex) {
  DCHECK_NOT_NULL(shared_mutex);
  return single_held_shared_mutex != shared_mutex &&
         (!held_shared_mutexes ||
          held_shared_mutexes->count(shared_mutex) == 0);
}

// Tries to hold {shared_mutex}. Returns true iff it hadn't been held prior to
// this function call.
bool TryHoldSharedMutex(SharedMutex* shared_mutex) {
  DCHECK_NOT_NULL(shared_mutex);
  if (single_held_shared_mutex) {
    if (shared_mutex == single_held_shared_mutex) {
      return false;
    }
    DCHECK_NULL(held_shared_mutexes);
    held_shared_mutexes = new TSet({single_held_shared_mutex, shared_mutex});
    single_held_shared_mutex = nullptr;
    return true;
  } else if (held_shared_mutexes) {
    return held_shared_mutexes->insert(shared_mutex).second;
  } else {
    DCHECK_NULL(single_held_shared_mutex);
    single_held_shared_mutex = shared_mutex;
    return true;
  }
}

// Tries to release {shared_mutex}. Returns true iff it had been held prior to
// this function call.
bool TryReleaseSharedMutex(SharedMutex* shared_mutex) {
  DCHECK_NOT_NULL(shared_mutex);
  if (single_held_shared_mutex == shared_mutex) {
    single_held_shared_mutex = nullptr;
    return true;
  }
  if (held_shared_mutexes && held_shared_mutexes->erase(shared_mutex)) {
    if (held_shared_mutexes->empty()) {
      delete held_shared_mutexes;
      held_shared_mutexes = nullptr;
    }
    return true;
  }
  return false;
}
}  // namespace
#endif  // DEBUG

#if V8_OS_POSIX

static V8_INLINE void InitializeNativeHandle(pthread_mutex_t* mutex) {
  int result;
#if defined(DEBUG)
  // Use an error checking mutex in debug mode.
  pthread_mutexattr_t attr;
  result = pthread_mutexattr_init(&attr);
  DCHECK_EQ(0, result);
  result = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
  DCHECK_EQ(0, result);
  result = pthread_mutex_init(mutex, &attr);
  DCHECK_EQ(0, result);
  result = pthread_mutexattr_destroy(&attr);
#else
  // Use a fast mutex (default attributes).
  result = pthread_mutex_init(mutex, nullptr);
#endif  // defined(DEBUG)
  DCHECK_EQ(0, result);
  USE(result);
}


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


Mutex::Mutex() {
  InitializeNativeHandle(&native_handle_);
#ifdef DEBUG
  level_ = 0;
#endif
}


Mutex::~Mutex() {
  DestroyNativeHandle(&native_handle_);
  DCHECK_EQ(0, level_);
}


void Mutex::Lock() {
  LockNativeHandle(&native_handle_);
  AssertUnheldAndMark();
}


void Mutex::Unlock() {
  AssertHeldAndUnmark();
  UnlockNativeHandle(&native_handle_);
}


bool Mutex::TryLock() {
  if (!TryLockNativeHandle(&native_handle_)) {
    return false;
  }
  AssertUnheldAndMark();
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

#if V8_OS_DARWIN

// On Mac OS X, we have a custom implementation of SharedMutex, which uses
// exclusive locks, since shared locks are broken. Here is how it works.
//
// It uses two exclusive mutexes, {ex_lock} and {ex_cv_lock}, a condition
// variable {ex_cv} and a counted {shared_count}.
//
// To lock the SharedMutex, readers and writers need to first lock {ex_lock}.
// Readers then simply increment {shared_count} and release {ex_lock} (so that
// other readers can take the shared lock as well). On the other hand, writers
// keep {ex_lock} locked until they release the SharedLock. This means that
// while a writer has the SharedLock, no other readers or writers can aquire it,
// since {ex_lock} is locked.
//
// Additionally, after having locked {ex_lock}, writers wait (using {ex_cv_lock}
// and {ex_cv}) for {shared_count} to become 0. Once that's the case, it means
// that no reader has the lock anymore (and no reader or writer can lock it
// again until the current writer unlocked it, since {ex_lock} is locked).
//
// To release the lock:
//   * readers decrement {shared_count}, and NotifyOne on {ex_cv} to wake up any
//     potential waiting writer.
//   * writers simply unlock {ex_lock}
//
// Why {ex_cv_lock} is needed: condition variables always need a mutex: the
// "sleeper" (writer waiting for the SharedMutex here) locks the mutex, test the
// condition and then "wait" (which releases the mutex), while the "awaiter"
// (readers releasing the SharedMutex here) needs to take the mutex and notify.
// Without the mutex, this could happen:
//   * writer sees that `native_handle_.shared_count != 0` and decides to wait.
//   * but writer actually gets interrupted before calling `wait`.
//   * meanwhile, reader decrements `shared_count` which reaches 0 and calls
//     `NotifyOne`.
//   * writer is resumed and calls `wait`
// In this situation, "writer" missed the NotifyOne, and there is no other
// NotifyOne coming (and writer has the exclusive lock on {ex_lock}, and won't
// release it until it gets Notified, which means that no-one can ever take this
// lock again). Thanks to the lock on {ex_cv_lock}, this cannot happen: writer
// takes the lock before checking `native_handle_.shared_count != 0` and only
// releases it during the call to `wait`, and reader acquires it before calling
// NotifyOne.
//
//
// This SharedMutex implementation prevents both readers and writers starvation
// if the underlying implementation of Mutex::Lock is fair (and it should be!).
// This is because both LockShared and LockExclusive try to lock {ex_lock} in
// order to lock the SharedLock, which means that:
//   - when the 1st writer wants the lock, he'll lock {ex_lock}.
//   - all readers and writers that want the lock after that will try to lock
//     {ex_lock} as well, and will hang until it's released.
//   - once {ex_lock} is released by the writer, any of the reader or writer
//     waiting for {ex_lock} could get it (and thus get the shared/exclusive
//     lock on this SharedMutex).

// The default constructors of Mutex will initialize and destruct
// native_handle_.ex_lock and native_handle_.ex_cv_lock automatically. So, we
// just have to take care of native_handle_.ex_cv manually, because it's a
// pointer (and it's a pointer because condition-variable.h includes mutex.h,
// which means that it couldn't be included in mutex.h).
// TODO(v8:12037): Consider moving SharedMutex to a separate file to solve this.
SharedMutex::SharedMutex() { native_handle_.ex_cv = new ConditionVariable(); }
SharedMutex::~SharedMutex() { delete native_handle_.ex_cv; }

void SharedMutex::LockShared() {
  // We need to lock {ex_lock} when taking a shared_lock, in order to prevent
  // taking a shared lock while a thread has the exclusive lock or is waiting
  // for it. If a thread is already waiting for an exclusive lock, then this
  // ex_lock.Lock() will hang until the exclusive lock is released. Once we've
  // incremented {shared_count}, this shared lock is externally visible, and
  // {ex_lock} is released, so that other threads can take the shared lock (or
  // can wait for the exclusive lock).
  MutexGuard guard(&native_handle_.ex_lock);
  native_handle_.shared_count.fetch_add(1, std::memory_order_relaxed);
}

void SharedMutex::LockExclusive() {
  DCHECK(TryHoldSharedMutex(this));
  native_handle_.ex_lock.Lock();
  MutexGuard guard(&native_handle_.ex_cv_lock);
  while (native_handle_.shared_count.load(std::memory_order_relaxed) != 0) {
    // If {shared_count} is not 0, then some threads still have the shared lock.
    // Once the last of them releases its lock, {shared_count} will fall to 0,
    // and this other thread will call ex_cv->NotifyOne().
    native_handle_.ex_cv->Wait(&native_handle_.ex_cv_lock);
  }
  // Once {shared_count} reaches 0, we are guaranteed that there are no more
  // threads with the shared lock, and because we hold the lock for {ex_lock},
  // no thread can take the shared (or exclusive) lock after we've woken from
  // Wait or after we've checked that "shared_count != 0".
  DCHECK_EQ(native_handle_.shared_count, 0u);
}

void SharedMutex::UnlockShared() {
  MutexGuard guard(&native_handle_.ex_cv_lock);
  if (native_handle_.shared_count.fetch_sub(1, std::memory_order_relaxed) ==
      1) {
    // {shared_count} was 1 before the subtraction (`x.fetch_sub(1)` is similar
    // to `x--`), so it is now 0. We wake up any potential writer that was
    // waiting for readers to let go of the lock.
    native_handle_.ex_cv->NotifyOne();
  }
}

void SharedMutex::UnlockExclusive() {
  DCHECK(TryReleaseSharedMutex(this));
  native_handle_.ex_lock.Unlock();
}

bool SharedMutex::TryLockShared() {
  if (!native_handle_.ex_lock.TryLock()) return false;
  native_handle_.shared_count.fetch_add(1, std::memory_order_relaxed);
  native_handle_.ex_lock.Unlock();
  return true;
}

bool SharedMutex::TryLockExclusive() {
  DCHECK(SharedMutexNotHeld(this));
  if (!native_handle_.ex_lock.TryLock()) return false;
  if (native_handle_.shared_count.load(std::memory_order_relaxed) == 0) {
    // Is {shared_count} is 0, then all of the shared locks have been released,
    // and there is no need to use the condition variable.
    DCHECK(TryHoldSharedMutex(this));
    return true;
  } else {
    // Note that there is a chance that {shared_count} became 0 after we've
    // checked if it's 0, since UnlockShared doesn't lock {ex_lock}.
    // Nevertheless, the specification of TryLockExclusive allows to return
    // false even though the mutex isn't already locked.
    native_handle_.ex_lock.Unlock();
    return false;
  }
}

#else  // !V8_OS_DARWIN

SharedMutex::SharedMutex() { pthread_rwlock_init(&native_handle_, nullptr); }

SharedMutex::~SharedMutex() {
  int result = pthread_rwlock_destroy(&native_handle_);
  DCHECK_EQ(0, result);
  USE(result);
}

void SharedMutex::LockShared() {
  DCHECK(TryHoldSharedMutex(this));
  int result = pthread_rwlock_rdlock(&native_handle_);
  DCHECK_EQ(0, result);
  USE(result);
}

void SharedMutex::LockExclusive() {
  DCHECK(TryHoldSharedMutex(this));
  int result = pthread_rwlock_wrlock(&native_handle_);
  DCHECK_EQ(0, result);
  USE(result);
}

void SharedMutex::UnlockShared() {
  DCHECK(TryReleaseSharedMutex(this));
  int result = pthread_rwlock_unlock(&native_handle_);
  DCHECK_EQ(0, result);
  USE(result);
}

void SharedMutex::UnlockExclusive() {
  // Same code as {UnlockShared} on POSIX.
  UnlockShared();
}

bool SharedMutex::TryLockShared() {
  DCHECK(SharedMutexNotHeld(this));
  bool result = pthread_rwlock_tryrdlock(&native_handle_) == 0;
  if (result) DCHECK(TryHoldSharedMutex(this));
  return result;
}

bool SharedMutex::TryLockExclusive() {
  DCHECK(SharedMutexNotHeld(this));
  bool result = pthread_rwlock_trywrlock(&native_handle_) == 0;
  if (result) DCHECK(TryHoldSharedMutex(this));
  return result;
}

#endif  // !V8_OS_DARWIN

#elif V8_OS_WIN

Mutex::Mutex() : native_handle_(SRWLOCK_INIT) {
#ifdef DEBUG
  level_ = 0;
#endif
}


Mutex::~Mutex() {
  DCHECK_EQ(0, level_);
}


void Mutex::Lock() {
  AcquireSRWLockExclusive(V8ToWindowsType(&native_handle_));
  AssertUnheldAndMark();
}


void Mutex::Unlock() {
  AssertHeldAndUnmark();
  ReleaseSRWLockExclusive(V8ToWindowsType(&native_handle_));
}


bool Mutex::TryLock() {
  if (!TryAcquireSRWLockExclusive(V8ToWindowsType(&native_handle_))) {
    return false;
  }
  AssertUnheldAndMark();
  return true;
}


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

SharedMutex::SharedMutex() : native_handle_(SRWLOCK_INIT) {}

SharedMutex::~SharedMutex() {}

void SharedMutex::LockShared() {
  DCHECK(TryHoldSharedMutex(this));
  AcquireSRWLockShared(V8ToWindowsType(&native_handle_));
}

void SharedMutex::LockExclusive() {
  DCHECK(TryHoldSharedMutex(this));
  AcquireSRWLockExclusive(V8ToWindowsType(&native_handle_));
}

void SharedMutex::UnlockShared() {
  DCHECK(TryReleaseSharedMutex(this));
  ReleaseSRWLockShared(V8ToWindowsType(&native_handle_));
}

void SharedMutex::UnlockExclusive() {
  DCHECK(TryReleaseSharedMutex(this));
  ReleaseSRWLockExclusive(V8ToWindowsType(&native_handle_));
}

bool SharedMutex::TryLockShared() {
  DCHECK(SharedMutexNotHeld(this));
  bool result = TryAcquireSRWLockShared(V8ToWindowsType(&native_handle_));
  if (result) DCHECK(TryHoldSharedMutex(this));
  return result;
}

bool SharedMutex::TryLockExclusive() {
  DCHECK(SharedMutexNotHeld(this));
  bool result = TryAcquireSRWLockExclusive(V8ToWindowsType(&native_handle_));
  if (result) DCHECK(TryHoldSharedMutex(this));
  return result;
}

#elif V8_OS_STARBOARD

Mutex::Mutex() { SbMutexCreate(&native_handle_); }

Mutex::~Mutex() { SbMutexDestroy(&native_handle_); }

void Mutex::Lock() { SbMutexAcquire(&native_handle_); }

void Mutex::Unlock() { SbMutexRelease(&native_handle_); }

RecursiveMutex::RecursiveMutex() {}

RecursiveMutex::~RecursiveMutex() {}

void RecursiveMutex::Lock() { native_handle_.Acquire(); }

void RecursiveMutex::Unlock() { native_handle_.Release(); }

bool RecursiveMutex::TryLock() { return native_handle_.AcquireTry(); }

SharedMutex::SharedMutex() = default;

SharedMutex::~SharedMutex() = default;

void SharedMutex::LockShared() {
  DCHECK(TryHoldSharedMutex(this));
  native_handle_.AcquireReadLock();
}

void SharedMutex::LockExclusive() {
  DCHECK(TryHoldSharedMutex(this));
  native_handle_.AcquireWriteLock();
}

void SharedMutex::UnlockShared() {
  DCHECK(TryReleaseSharedMutex(this));
  native_handle_.ReleaseReadLock();
}

void SharedMutex::UnlockExclusive() {
  DCHECK(TryReleaseSharedMutex(this));
  native_handle_.ReleaseWriteLock();
}

bool SharedMutex::TryLockShared() {
  DCHECK(SharedMutexNotHeld(this));
  return false;
}

bool SharedMutex::TryLockExclusive() {
  DCHECK(SharedMutexNotHeld(this));
  return false;
}
#endif  // V8_OS_STARBOARD

}  // namespace base
}  // namespace v8
