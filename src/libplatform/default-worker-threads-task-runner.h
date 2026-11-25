// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_DEFAULT_WORKER_THREADS_TASK_RUNNER_H_
#define V8_LIBPLATFORM_DEFAULT_WORKER_THREADS_TASK_RUNNER_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "include/libplatform/libplatform-export.h"
#include "include/v8-platform.h"
#include "src/base/build_config.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/memory-protection-key.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "src/libplatform/delayed-task-queue.h"

namespace v8 {
namespace platform {

class V8_PLATFORM_EXPORT DefaultWorkerThreadsTaskRunner
    : public NON_EXPORTED_BASE(TaskRunner) {
 public:
  using TimeFunction = double (*)();

  DefaultWorkerThreadsTaskRunner(
      uint32_t thread_pool_size, TimeFunction time_function,
      base::Thread::Priority priority = base::Thread::Priority::kDefault);

  ~DefaultWorkerThreadsTaskRunner() override;

  // Shuts down the task runner and joins all worker threads. Idempotent for
  // sequential callers and called automatically by the destructor.
  void Terminate();

  double MonotonicallyIncreasingTime();

  // v8::TaskRunner implementation.
  bool IdleTasksEnabled() override;

  void SetFailWorkerStartForTesting(bool fail) {
    fail_worker_start_for_testing_.store(fail, std::memory_order_relaxed);
  }

 private:
  // v8::TaskRunner implementation.
  void PostTaskImpl(std::unique_ptr<Task> task,
                    const SourceLocation& location) override;

  void PostDelayedTaskImpl(std::unique_ptr<Task> task, double delay_in_seconds,
                           const SourceLocation& location) override;

  void PostIdleTaskImpl(std::unique_ptr<IdleTask> task,
                        const SourceLocation& location) override;

  class WorkerThread final : public base::Thread {
   public:
    // Creates and starts the thread. If the OS thread could not be created,
    // started() returns false and the object is inert: Run() never executes
    // and the destructor does not join.
    WorkerThread(DefaultWorkerThreadsTaskRunner* runner,
                 base::Thread::Priority priority, bool is_eager,
                 bool is_fallback, bool fail_start_for_testing);
    ~WorkerThread() override;

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;

    bool started() const { return started_; }

    // This thread attempts to get tasks in a loop from |runner_| and run them.
    void Run() override;

    void Notify();

   private:
    friend class DefaultWorkerThreadsTaskRunner;

    DefaultWorkerThreadsTaskRunner* runner_;
    base::ConditionVariable condition_var_;
    bool started_ = false;
    bool idle_ = false;
    bool waiting_delayed_ = false;
#if V8_OS_LINUX
    const bool is_eager_ = false;
    bool is_fallback_ = false;
    bool retired_ = false;
#endif
  };

  struct SpawnReservation {
    uint32_t count = 0;
    bool is_fallback = false;
  };

  // All *Locked helpers require |lock_| to be held.
  //
  // Worker accounting invariant under |lock_|:
  // - |searching_workers_| counts workers that are awake and on their way to
  //   TryGetNext() (including |in_flight_spawns_| slots reserved to be started
  //   outside |lock_|).
  // - |idle_threads_| holds workers sleeping in Wait()/WaitFor() (each with
  //   |idle_ == true|), of which at most one (|delayed_waiters_ <= 1|) is in
  //   WaitFor() on the earliest delayed deadline (|waiting_delayed_ == true|).
  // - Workers currently executing Task::Run() unlocked are in neither set.
  size_t NeededWorkersLocked() const;
  bool HasInFlightWorkLocked() const;
  template <typename Pred>
  bool WakeMatchingIdleWorkerLocked(Pred pred);
  void WakeWorkerLocked(WorkerThread* worker);
  void ParkWorkerLocked(WorkerThread* worker,
                        std::optional<base::TimeDelta> wait_time);
  void NotifyIdleWorkerLocked();
  void WakeDelayedWaiterLocked();
  SpawnReservation NotifyOrReserveSpawnsLocked(bool is_worker_thread,
                                               bool caller_is_suboptimal);
  void SpawnReservedWorkers(SpawnReservation reservation);

  bool terminated_ = false;
  base::Mutex lock_;
  // Notified when in-flight spawns (and retired-worker joins on Linux) drop to
  // zero.
  base::ConditionVariable in_flight_work_cv_;
  // Vector of idle threads -- these are pushed in LIFO order, so that the most
  // recently active thread is the first to be reactivated.
  std::vector<WorkerThread*> idle_threads_;
  std::vector<std::unique_ptr<WorkerThread>> thread_pool_;
  uint32_t searching_workers_ = 0;
  uint32_t in_flight_spawns_ = 0;
  uint32_t delayed_waiters_ = 0;
  base::TimeTicks next_spawn_retry_;
  // Worker threads access this queue, so we can only destroy it after all
  // workers stopped.
  DelayedTaskQueue queue_;
  TimeFunction time_function_;
  const uint32_t thread_pool_size_;
  const base::Thread::Priority priority_;
  std::atomic<bool> fail_worker_start_for_testing_{false};
#if V8_OS_LINUX
  void RetireWorkerLocked(WorkerThread* worker);
  bool WakeFallbackDelayedWaiterLocked();

  // Workers retired due to suboptimal nice values; joined outside |lock_| on
  // the next spawn or in Terminate().
  std::vector<std::unique_ptr<WorkerThread>> retired_threads_;
  // Active and in-flight-spawned workers with |is_fallback_ == true| (guarded
  // by |lock_|).
  uint32_t fallback_workers_ = 0;
  // Number of SpawnReservedWorkers() calls currently joining |retired_threads_|
  // outside |lock_| (guarded by |lock_|).
  uint32_t in_flight_joins_ = 0;
  // Number of workers in |in_flight_spawns_| that already called
  // RetireWorkerLocked() before SpawnReservedWorkers() reacquired |lock_|
  // (guarded by |lock_|). Subtracted from |in_flight_spawns_| when computing
  // occupied pool slots so an already-exited in-flight worker cannot block
  // spawning a replacement.
  uint32_t retired_in_flight_spawns_ = 0;
  const int target_nice_;
#endif
#if V8_HAS_PKU_SUPPORT
  // Baseline protection-key register captured from the constructing thread when
  // hardware/OS PKU support is active; restored on WorkerThread::Run() entry so
  // lazy spawns do not inherit caller PKU scopes (e.g. DisallowSandboxAccess or
  // RwxMemoryWriteScope).
  const std::optional<base::MemoryProtectionKey::ThreadKeyRegister>
      baseline_pku_register_;
#endif
};

}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_DEFAULT_WORKER_THREADS_TASK_RUNNER_H_
