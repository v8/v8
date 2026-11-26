// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_SYNCHRONIZATION_POINT_SUPPORT_H_
#define V8_BASE_SYNCHRONIZATION_POINT_SUPPORT_H_

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "include/v8config.h"
#include "src/base/base-export.h"
#include "src/base/macros.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/base/strong-alias.h"
#include "src/base/thread-id.h"

namespace v8 {
namespace base {

// Singleton that backs up the synchronization point functionality, which allows
// blocking the V8 execution at potentially interesting places in tests/fuzzers
// to simulate specific timings.
//
// The C++ code should use the `SYNCHRONIZATION_POINT` macro in order to let the
// execution there be controlled via the %BlockAt/%Resume/%WaitUntilBlocked
// native APIs.
class V8_BASE_EXPORT SynchronizationPointSupport {
 public:
  using AllowSafepoint = StrongAlias<struct AllowSafepointTag, bool>;
  using WaitWrapper = void (*)(void (*callback)(void*), void* data);
  using DeferToSafepointCallback =
      void (*)(std::string_view synchronization_point);

  static void Initialize();
  static V8_INLINE SynchronizationPointSupport* Get() { return instance_; }

  // Registers an optional wrapper invoked around safepoint-capable blocking
  // waits (e.g. to park a running V8 LocalHeap while blocked at a
  // synchronization point where GC safepoints are allowed).
  void SetWaitWrapper(WaitWrapper wrapper) {
    wait_wrapper_.store(wrapper, std::memory_order_release);
  }

  // Registers a callback invoked when a synchronization point armed with
  // RequestBlockAtNextSafepoint is reached, so the caller's LocalHeap can
  // defer the actual block until its next Safepoint() call. The passed
  // string_view remains valid for the lifetime of the process.
  void SetDeferToSafepointCallback(DeferToSafepointCallback callback) {
    defer_to_safepoint_callback_.store(callback, std::memory_order_release);
  }

  // Requests a block at the given synchronization point. When a thread reaches
  // it, it will block for the specified timeout (or less, if it is resumed).
  void RequestBlockAt(std::string synchronization_point, TimeDelta timeout);

  // Like RequestBlockAt, but when a thread reaches a synchronization point that
  // does not allow safepoints, it keeps running and instead blocks (parked) at
  // its next LocalHeap::Safepoint() call. If the synchronization point already
  // allows safepoints (e.g. SYNCHRONIZATION_POINT_WITH_SAFEPOINT), it blocks
  // there directly. Requires V8_ENABLE_TEST_ONLY_SYNC_POINTS so
  // LocalHeap::Safepoint() checks for deferred sync points.
  void RequestBlockAtNextSafepoint(std::string synchronization_point,
                                   TimeDelta timeout);

  // Resumes a thread currently blocked at the given synchronization point.
  // Returns false if a block wasn't requested or already timed out.
  bool Resume(std::string_view synchronization_point);

  // Waits until the given synchronization point is reached by some thread.
  // Returns false if a block wasn't requested or on timeout. Note: this does
  // not request a block; it must be requested first. Always invokes the
  // registered wait wrapper (parking a running LocalHeap for GC safepoints),
  // so callers must not hold live raw Tagged<> pointers across this call.
  bool WaitUntilBlocked(std::string_view synchronization_point,
                        TimeDelta timeout);

  // Called when the synchronization point is reached; blocks if requested.
  // Only invokes the registered wait wrapper (e.g. parking LocalHeap for GC)
  // when `allow_safepoint` is true.
  V8_INLINE void BlockIfRequested(
      std::string_view synchronization_point,
      AllowSafepoint allow_safepoint = AllowSafepoint{false}) {
    if (!any_requested_.load(std::memory_order_relaxed)) [[likely]] {
      return;
    }
    BlockIfRequestedSlow(synchronization_point, allow_safepoint);
  }

 private:
  using DeferToSafepoint = StrongAlias<struct DeferToSafepointTag, bool>;

  struct SyncPointState {
    ConditionVariable cv;
    // Set to true to signal that any thread that reaches this point should
    // block.
    bool block_requested = false;
    // If true, reaching a non-safepoint sync point defers blocking to the
    // thread's next LocalHeap::Safepoint() call via
    // `defer_to_safepoint_callback_`.
    bool defer_to_safepoint = false;
    // Number of threads that have reached the point and are currently blocked.
    int blocked_threads = 0;
    // The identity of the thread that set the `block_requested` flag.
    ThreadId block_requester_thread = ThreadId::Invalid();
    // How long a thread should remain blocked, unless resumed.
    TimeDelta block_timeout;
  };

  void RequestBlockImpl(std::string synchronization_point, TimeDelta timeout,
                        DeferToSafepoint defer_to_safepoint);
  void ClearBlockRequestLocked(SyncPointState* state);
  V8_NOINLINE V8_PRESERVE_MOST void BlockIfRequestedSlow(
      std::string_view synchronization_point, AllowSafepoint allow_safepoint);

  static SynchronizationPointSupport* instance_;

  std::atomic<bool> any_requested_{false};
  std::atomic<WaitWrapper> wait_wrapper_{nullptr};
  std::atomic<DeferToSafepointCallback> defer_to_safepoint_callback_{nullptr};
  Mutex mutex_;
  int active_requests_ = 0;
  std::map<std::string, std::unique_ptr<SyncPointState>, std::less<>> states_;
};

// A synchronization point that allows a thread reaching it to be predictably
// blocked and resumed by JS testing intrinsics (%BlockAt and %Resume).
// Does not park the LocalHeap, so it is safe inside DisallowGarbageCollection.
#define SYNCHRONIZATION_POINT(sync_point_name)                    \
  v8::base::SynchronizationPointSupport::Get()->BlockIfRequested( \
      sync_point_name)

// A synchronization point at a safepoint-safe location (outside any
// DisallowGarbageCollection scope and with no live raw Tagged<> pointers).
// Parks a running LocalHeap while blocked so concurrent GCs do not deadlock.
#define SYNCHRONIZATION_POINT_WITH_SAFEPOINT(sync_point_name)     \
  v8::base::SynchronizationPointSupport::Get()->BlockIfRequested( \
      sync_point_name,                                            \
      v8::base::SynchronizationPointSupport::AllowSafepoint{true})

// Similar to `SYNCHRONIZATION_POINT`, but is intended for being used on hot
// paths. Unlike the former, it compiles to nop unless the
// `v8_enable_test_only_sync_points` GN flag is true.
#ifdef V8_ENABLE_TEST_ONLY_SYNC_POINTS
#define SYNCHRONIZATION_POINT_TEST_ONLY(sync_point_name) \
  SYNCHRONIZATION_POINT(sync_point_name)
#define SYNCHRONIZATION_POINT_WITH_SAFEPOINT_TEST_ONLY(sync_point_name) \
  SYNCHRONIZATION_POINT_WITH_SAFEPOINT(sync_point_name)
#else
#define SYNCHRONIZATION_POINT_TEST_ONLY(sync_point_name) ((void)0)
#define SYNCHRONIZATION_POINT_WITH_SAFEPOINT_TEST_ONLY(sync_point_name) \
  ((void)0)
#endif

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_SYNCHRONIZATION_POINT_SUPPORT_H_
