// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/default-worker-threads-task-runner.h"

#include <algorithm>
#include <iterator>

#if V8_OS_LINUX
#include <sys/resource.h>
#endif

#include "src/base/platform/time.h"
#include "src/base/small-vector.h"
#include "src/libplatform/delayed-task-queue.h"

namespace v8 {
namespace platform {

namespace {

// Cooldown after a failed OS thread creation before the next task post or
// worker dequeue attempts to spawn again.
constexpr base::TimeDelta kSpawnRetryCooldown =
    base::TimeDelta::FromMilliseconds(50);

#if V8_OS_LINUX
int GetCurrentThreadNice() { return getpriority(PRIO_PROCESS, 0); }

// For Priority::kDefault (PriorityMode::kDontApply), preserve the creator
// thread's initial_nice (including negative values) so the pool's eager worker
// and descendants keep the creator's priority unchanged. For explicit
// priorities, clamp with initial_nice because unprivileged Linux cannot lower
// nice below it; without clamping, workers running at initial_nice would be
// misclassified as suboptimal_priority.
int TargetNiceForPriority(base::Thread::Priority priority, int initial_nice) {
  if (std::optional<int> nice = base::Thread::PriorityToNiceValue(priority)) {
    return std::max(*nice, initial_nice);
  }
  return initial_nice;
}
#endif  // V8_OS_LINUX

}  // namespace

DefaultWorkerThreadsTaskRunner::DefaultWorkerThreadsTaskRunner(
    uint32_t thread_pool_size, TimeFunction time_function,
    base::Thread::Priority priority)
    : queue_(time_function),
      time_function_(time_function),
      thread_pool_size_(thread_pool_size),
      priority_(priority)
#if V8_OS_LINUX
      ,
      target_nice_(TargetNiceForPriority(priority, GetCurrentThreadNice()))
#endif
#if V8_HAS_PKU_SUPPORT
      ,
      baseline_pku_register_(base::MemoryProtectionKey::ReadThreadKeyRegister())
#endif
{
  idle_threads_.reserve(thread_pool_size_);
  thread_pool_.reserve(thread_pool_size_);
  // Start one worker eagerly so that posted tasks always get executed, even if
  // the OS refuses to create further threads later on (e.g. because the
  // address space is exhausted). The remaining workers are started on demand.
  if (thread_pool_size_ > 0) {
    base::MutexGuard guard(&lock_);
    searching_workers_ = 1;
    auto worker = std::make_unique<WorkerThread>(
        this, priority_, /*is_eager=*/true, /*is_fallback=*/false,
        /*fail_start_for_testing=*/false);
    CHECK(worker->started());
    thread_pool_.push_back(std::move(worker));
  }
}

DefaultWorkerThreadsTaskRunner::~DefaultWorkerThreadsTaskRunner() {
  Terminate();
  DCHECK_EQ(0u, searching_workers_);
  DCHECK_EQ(0u, in_flight_spawns_);
  DCHECK_EQ(0u, delayed_waiters_);
#if V8_OS_LINUX
  DCHECK_EQ(0u, fallback_workers_);
  DCHECK_EQ(0u, in_flight_joins_);
  DCHECK_EQ(0u, retired_in_flight_spawns_);
#endif
}

double DefaultWorkerThreadsTaskRunner::MonotonicallyIncreasingTime() {
  return time_function_();
}

bool DefaultWorkerThreadsTaskRunner::HasInFlightWorkLocked() const {
  lock_.AssertHeld();
#if V8_OS_LINUX
  return in_flight_spawns_ > 0 || in_flight_joins_ > 0;
#else
  return in_flight_spawns_ > 0;
#endif
}

void DefaultWorkerThreadsTaskRunner::Terminate() {
  std::vector<std::unique_ptr<WorkerThread>> threads_to_join;
  {
    base::MutexGuard guard(&lock_);
    if (!terminated_) {
      terminated_ = true;
      queue_.Terminate();
      for (WorkerThread* worker : idle_threads_) {
        WakeWorkerLocked(worker);
      }
      idle_threads_.clear();
    }
    while (HasInFlightWorkLocked()) {
      in_flight_work_cv_.Wait(&lock_);
    }
    threads_to_join.swap(thread_pool_);
#if V8_OS_LINUX
    for (auto& retired : retired_threads_) {
      threads_to_join.push_back(std::move(retired));
    }
    retired_threads_.clear();
#endif
  }
  // Joining outside |lock_| lets running workers drain queued tasks and exit.
  threads_to_join.clear();
}

size_t DefaultWorkerThreadsTaskRunner::NeededWorkersLocked() const {
  lock_.AssertHeld();
  return queue_.immediate_size() +
         ((queue_.has_delayed_tasks() && delayed_waiters_ == 0) ? 1 : 0);
}

template <typename Pred>
bool DefaultWorkerThreadsTaskRunner::WakeMatchingIdleWorkerLocked(Pred pred) {
  lock_.AssertHeld();
  for (auto it = idle_threads_.rbegin(); it != idle_threads_.rend(); ++it) {
    if (pred(*it)) {
      WorkerThread* worker = *it;
      idle_threads_.erase(std::next(it).base());
      WakeWorkerLocked(worker);
      return true;
    }
  }
  return false;
}

void DefaultWorkerThreadsTaskRunner::WakeWorkerLocked(WorkerThread* worker) {
  lock_.AssertHeld();
  DCHECK(worker->idle_);
  worker->idle_ = false;
  if (worker->waiting_delayed_) {
    worker->waiting_delayed_ = false;
    DCHECK_GT(delayed_waiters_, 0u);
    --delayed_waiters_;
  }
  ++searching_workers_;
  worker->Notify();
}

void DefaultWorkerThreadsTaskRunner::ParkWorkerLocked(
    WorkerThread* worker, std::optional<base::TimeDelta> wait_time) {
  lock_.AssertHeld();
  DCHECK_GT(searching_workers_, 0u);
  DCHECK(!worker->idle_);
  DCHECK(!worker->waiting_delayed_);
  --searching_workers_;
  worker->idle_ = true;
  idle_threads_.push_back(worker);
  if (wait_time.has_value()) {
    DCHECK_EQ(0u, delayed_waiters_);
    worker->waiting_delayed_ = true;
    ++delayed_waiters_;
    // WaitFor unfortunately doesn't care about our fake time and will wait
    // the 'real' amount of time, based on whatever clock the system call uses.
    bool notified = worker->condition_var_.WaitFor(&lock_, *wait_time);
    USE(notified);
  } else {
    worker->condition_var_.Wait(&lock_);
  }
  // Clean up if woken by timeout or spuriously rather than by
  // WakeWorkerLocked().
  if (worker->idle_) {
    if (worker->waiting_delayed_) {
      worker->waiting_delayed_ = false;
      DCHECK_GT(delayed_waiters_, 0u);
      --delayed_waiters_;
    }
    std::erase(idle_threads_, worker);
    worker->idle_ = false;
    ++searching_workers_;
  }
}

void DefaultWorkerThreadsTaskRunner::NotifyIdleWorkerLocked() {
  lock_.AssertHeld();
  DCHECK(!idle_threads_.empty());
  // Prefer waking an indefinite waiter in LIFO order so an active
  // kWaitDelayed timer thread is not unnecessarily interrupted.
  if (WakeMatchingIdleWorkerLocked(
          [](const WorkerThread* w) { return !w->waiting_delayed_; })) {
    return;
  }
  WorkerThread* worker = idle_threads_.back();
  idle_threads_.pop_back();
  WakeWorkerLocked(worker);
}

void DefaultWorkerThreadsTaskRunner::WakeDelayedWaiterLocked() {
  WakeMatchingIdleWorkerLocked(
      [](const WorkerThread* w) { return w->waiting_delayed_; });
}

#if V8_OS_LINUX
bool DefaultWorkerThreadsTaskRunner::WakeFallbackDelayedWaiterLocked() {
  return WakeMatchingIdleWorkerLocked([](const WorkerThread* w) {
    return w->waiting_delayed_ && w->is_fallback_;
  });
}
#endif

DefaultWorkerThreadsTaskRunner::SpawnReservation
DefaultWorkerThreadsTaskRunner::NotifyOrReserveSpawnsLocked(
    bool is_worker_thread, bool caller_is_suboptimal) {
  lock_.AssertHeld();
  if (terminated_) return {};

  while (searching_workers_ < NeededWorkersLocked() && !idle_threads_.empty()) {
    NotifyIdleWorkerLocked();
  }

  const size_t needed = NeededWorkersLocked();
  if (searching_workers_ >= needed) return {};

  // Defer multi-worker fan-out to an already-waking normal worker so posting
  // threads (e.g. main thread during GC) do not run serial pthread_create
  // loops.
  if (!is_worker_thread &&
#if V8_OS_LINUX
      searching_workers_ > fallback_workers_
#else
      searching_workers_ > 0
#endif
  ) {
    return {};
  }

  // Retired workers in |retired_threads_| have already exited |thread_pool_|
  // and will be joined on the next spawn; |total_workers| counts active and
  // non-retired in-flight pool members.
#if V8_OS_LINUX
  DCHECK_GE(in_flight_spawns_, retired_in_flight_spawns_);
  size_t total_workers =
      thread_pool_.size() + (in_flight_spawns_ - retired_in_flight_spawns_);
#else
  size_t total_workers = thread_pool_.size() + in_flight_spawns_;
#endif
  if (total_workers >= thread_pool_size_) return {};

  if (!next_spawn_retry_.IsNull() &&
      base::TimeTicks::Now() < next_spawn_retry_) {
    return {};
  }

  bool is_fallback = false;
  size_t to_spawn =
      std::min(needed - searching_workers_,
               static_cast<size_t>(thread_pool_size_) - total_workers);
  if (!is_worker_thread) {
    to_spawn = 1;
  }

#if V8_OS_LINUX
  // On unprivileged Linux, setpriority() cannot lower a thread's nice value
  // once raised (e.g. from a kBestEffort nice=10 worker back to nice=0).
  // Cap immediate-task fallback workers at 1 (and never daisy-chain immediate
  // spawns from a suboptimal worker) so a burst from a lower-priority thread
  // cannot fill the pool with nice=10 threads. When a delayed task is pending
  // with no active waiter or searching worker, intentionally allow spawning
  // additional fallback workers (up to thread_pool_size_) even if an immediate
  // burst drains ahead of the timer: this liveness trade-off guarantees a
  // delayed task cannot starve behind multiple blocking immediate tasks, and
  // all fallback workers still retire once idle or once an optimal worker
  // takes over the delayed wait.
  const bool needs_delayed_waiter = queue_.has_pending_delayed_tasks() &&
                                    delayed_waiters_ == 0 &&
                                    searching_workers_ == 0;
  if (caller_is_suboptimal) {
    if (!needs_delayed_waiter) return {};
    is_fallback = true;
    to_spawn = 1;
    ++fallback_workers_;
  } else if (!is_worker_thread && GetCurrentThreadNice() > target_nice_) {
    if (searching_workers_ > 0 ||
        (fallback_workers_ > 0 && !needs_delayed_waiter)) {
      return {};
    }
    is_fallback = true;
    to_spawn = 1;
    ++fallback_workers_;
  }
#else
  USE(caller_is_suboptimal);
#endif

  uint32_t count = static_cast<uint32_t>(to_spawn);
  in_flight_spawns_ += count;
  searching_workers_ += count;
  return {count, is_fallback};
}

void DefaultWorkerThreadsTaskRunner::SpawnReservedWorkers(
    SpawnReservation reservation) {
  DCHECK_GT(reservation.count, 0u);
#if V8_OS_LINUX
  std::vector<std::unique_ptr<WorkerThread>> to_join;
#endif
  const bool fail_for_testing =
      fail_worker_start_for_testing_.load(std::memory_order_relaxed);
  base::SmallVector<std::unique_ptr<WorkerThread>, 16> spawned;
  for (uint32_t i = 0; i < reservation.count; ++i) {
    // Creating a thread can fail, e.g. when the address space is exhausted.
    // This is not fatal: queued tasks are picked up by already running workers.
    auto worker = std::make_unique<WorkerThread>(
        this, priority_, /*is_eager=*/false, reservation.is_fallback,
        fail_for_testing);
    if (!worker->started()) break;
    spawned.push_back(std::move(worker));
  }

  {
    base::MutexGuard guard(&lock_);
#if V8_OS_LINUX
    if (!terminated_) {
      to_join.swap(retired_threads_);
    }
#endif
    const uint32_t started_count = static_cast<uint32_t>(spawned.size());
    for (auto& worker : spawned) {
#if V8_OS_LINUX
      if (worker->retired_) {
        DCHECK_GT(retired_in_flight_spawns_, 0u);
        --retired_in_flight_spawns_;
        if (terminated_) {
          retired_threads_.push_back(std::move(worker));
        } else {
          to_join.push_back(std::move(worker));
        }
        continue;
      }
#endif
      thread_pool_.push_back(std::move(worker));
    }
    const uint32_t failed_count = reservation.count - started_count;
    DCHECK_GE(in_flight_spawns_, reservation.count);
    in_flight_spawns_ -= reservation.count;
    if (failed_count > 0) {
#if V8_OS_LINUX
      if (reservation.is_fallback) {
        DCHECK_GT(fallback_workers_, 0u);
        --fallback_workers_;
      }
#endif
      DCHECK_GE(searching_workers_, failed_count);
      searching_workers_ -= failed_count;
      next_spawn_retry_ = base::TimeTicks::Now() + kSpawnRetryCooldown;
      if (!terminated_) {
        while (searching_workers_ < NeededWorkersLocked() &&
               !idle_threads_.empty()) {
          NotifyIdleWorkerLocked();
        }
      }
    }
#if V8_OS_LINUX
    if (!to_join.empty()) {
      ++in_flight_joins_;
    }
#endif
    if (!HasInFlightWorkLocked() && terminated_) {
      in_flight_work_cv_.NotifyAll();
    }
  }

#if V8_OS_LINUX
  if (!to_join.empty()) {
    to_join.clear();
    base::MutexGuard guard(&lock_);
    DCHECK_GT(in_flight_joins_, 0u);
    --in_flight_joins_;
    if (!HasInFlightWorkLocked() && terminated_) {
      in_flight_work_cv_.NotifyAll();
    }
  }
#endif
}

#if V8_OS_LINUX
void DefaultWorkerThreadsTaskRunner::RetireWorkerLocked(WorkerThread* worker) {
  lock_.AssertHeld();
  DCHECK(!terminated_);
  DCHECK(!worker->retired_);
  worker->retired_ = true;
  if (worker->is_fallback_) {
    worker->is_fallback_ = false;
    DCHECK_GT(fallback_workers_, 0u);
    --fallback_workers_;
  }
  auto it =
      std::find_if(thread_pool_.begin(), thread_pool_.end(),
                   [worker](const std::unique_ptr<WorkerThread>& candidate) {
                     return candidate.get() == worker;
                   });
  if (it != thread_pool_.end()) {
    retired_threads_.push_back(std::move(*it));
    thread_pool_.erase(it);
  } else {
    // Worker retired before SpawnReservedWorkers() reacquired |lock_| to
    // insert it into |thread_pool_|.
    ++retired_in_flight_spawns_;
  }
}
#endif

void DefaultWorkerThreadsTaskRunner::PostTaskImpl(
    std::unique_ptr<Task> task, const SourceLocation& location) {
  SpawnReservation reservation;
  {
    base::MutexGuard guard(&lock_);
    if (terminated_) return;
    queue_.Append(std::move(task));
    reservation = NotifyOrReserveSpawnsLocked(/*is_worker_thread=*/false,
                                              /*caller_is_suboptimal=*/false);
  }
  if (reservation.count > 0) {
    SpawnReservedWorkers(reservation);
  }
}

void DefaultWorkerThreadsTaskRunner::PostDelayedTaskImpl(
    std::unique_ptr<Task> task, double delay_in_seconds,
    const SourceLocation& location) {
  SpawnReservation reservation;
  {
    base::MutexGuard guard(&lock_);
    if (terminated_) return;
    bool became_earliest =
        queue_.AppendDelayed(std::move(task), delay_in_seconds);
    if (became_earliest && delayed_waiters_ > 0) {
      WakeDelayedWaiterLocked();
    }
    reservation = NotifyOrReserveSpawnsLocked(/*is_worker_thread=*/false,
                                              /*caller_is_suboptimal=*/false);
  }
  if (reservation.count > 0) {
    SpawnReservedWorkers(reservation);
  }
}

void DefaultWorkerThreadsTaskRunner::PostIdleTaskImpl(
    std::unique_ptr<IdleTask> task, const SourceLocation& location) {
  // There are no idle worker tasks.
  UNREACHABLE();
}

bool DefaultWorkerThreadsTaskRunner::IdleTasksEnabled() {
  // There are no idle worker tasks.
  return false;
}

DefaultWorkerThreadsTaskRunner::WorkerThread::WorkerThread(
    DefaultWorkerThreadsTaskRunner* runner, base::Thread::Priority priority,
    bool is_eager, bool is_fallback, bool fail_start_for_testing)
    : Thread(
          Options("V8 DefaultWorkerThreadsTaskRunner WorkerThread", priority)),
      runner_(runner)
#if V8_OS_LINUX
      ,
      is_eager_(is_eager),
      is_fallback_(is_fallback)
#endif
{
#if !V8_OS_LINUX
  USE(is_eager);
  USE(is_fallback);
#endif
  // Starting the OS thread can fail under resource exhaustion; callers check
  // started() and drop the WorkerThread if it could not be started.
  started_ = !fail_start_for_testing && Start();
}

DefaultWorkerThreadsTaskRunner::WorkerThread::~WorkerThread() {
  // A thread that could not be started never runs and must not be joined.
  if (!started_) return;
  condition_var_.NotifyAll();
  Join();
}

void DefaultWorkerThreadsTaskRunner::WorkerThread::Run() {
#if V8_HAS_PKU_SUPPORT
  if (runner_->baseline_pku_register_.has_value()) {
    base::MemoryProtectionKey::RestoreThreadKeyRegister(
        *runner_->baseline_pku_register_);
  }
#endif
#if V8_OS_LINUX
  if (priority() == Priority::kDefault &&
      GetCurrentThreadNice() != runner_->target_nice_) {
    setpriority(PRIO_PROCESS, 0, runner_->target_nice_);
  }
  const bool suboptimal_priority =
      GetCurrentThreadNice() > runner_->target_nice_;
#else
  constexpr bool suboptimal_priority = false;
#endif

  base::MutexGuard guard(&runner_->lock_);
#if V8_OS_LINUX
  if (is_fallback_ && !suboptimal_priority) {
    // E.g. CAP_SYS_NICE allowed setpriority() to restore target_nice_; promote
    // this worker to a normal pool member.
    is_fallback_ = false;
    DCHECK_GT(runner_->fallback_workers_, 0u);
    --runner_->fallback_workers_;
  }
#endif

  while (true) {
    DCHECK(!idle_);
    DCHECK(!waiting_delayed_);
    DCHECK_GT(runner_->searching_workers_, 0u);
#if V8_OS_LINUX
    if (suboptimal_priority &&
        runner_->idle_threads_.size() > runner_->delayed_waiters_ &&
        runner_->NeededWorkersLocked() > 0 && !is_eager_) {
      runner_->NotifyIdleWorkerLocked();
      --runner_->searching_workers_;
      runner_->RetireWorkerLocked(this);
      return;
    }
#endif
    DelayedTaskQueue::MaybeNextTask next_task = runner_->queue_.TryGetNext();
    switch (next_task.state) {
      case DelayedTaskQueue::MaybeNextTask::kTask: {
        --runner_->searching_workers_;
        SpawnReservation reservation = runner_->NotifyOrReserveSpawnsLocked(
            /*is_worker_thread=*/true, suboptimal_priority);
        runner_->lock_.Unlock();
        if (reservation.count > 0) {
          runner_->SpawnReservedWorkers(reservation);
        }
        next_task.task->Run();
        // Destroy the task outside lock_ in case its destructor posts tasks.
        next_task.task.reset();
        runner_->lock_.Lock();
        ++runner_->searching_workers_;
        continue;
      }
      case DelayedTaskQueue::MaybeNextTask::kTerminated:
#if V8_OS_LINUX
        if (is_fallback_) {
          is_fallback_ = false;
          DCHECK_GT(runner_->fallback_workers_, 0u);
          --runner_->fallback_workers_;
        }
#endif
        --runner_->searching_workers_;
        return;
      case DelayedTaskQueue::MaybeNextTask::kWaitIndefinite:
#if V8_OS_LINUX
        if (suboptimal_priority && !is_eager_) {
          --runner_->searching_workers_;
          runner_->RetireWorkerLocked(this);
          return;
        }
#endif
        runner_->ParkWorkerLocked(this, std::nullopt);
        continue;
      case DelayedTaskQueue::MaybeNextTask::kWaitDelayed: {
#if V8_OS_LINUX
        if (!suboptimal_priority && runner_->delayed_waiters_ > 0 &&
            runner_->fallback_workers_ > 0) {
          runner_->WakeFallbackDelayedWaiterLocked();
        }
        if (suboptimal_priority && runner_->delayed_waiters_ > 0 &&
            !is_eager_) {
          --runner_->searching_workers_;
          runner_->RetireWorkerLocked(this);
          return;
        }
#endif
        std::optional<base::TimeDelta> wait_time;
        if (runner_->delayed_waiters_ == 0) {
          wait_time = next_task.wait_time;
        }
        runner_->ParkWorkerLocked(this, wait_time);
        continue;
      }
    }
  }
}

void DefaultWorkerThreadsTaskRunner::WorkerThread::Notify() {
  condition_var_.NotifyAll();
}

}  // namespace platform
}  // namespace v8
