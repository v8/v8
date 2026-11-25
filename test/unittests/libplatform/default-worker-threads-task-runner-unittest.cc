// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/default-worker-threads-task-runner.h"

#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <vector>

#if V8_OS_LINUX
#include <sys/resource.h>
#endif

#include "include/v8-platform.h"
#include "src/base/build_config.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/memory-protection-key.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/semaphore.h"
#include "src/base/platform/time.h"
#include "testing/gtest-support.h"

namespace v8 {
namespace platform {

class TestTask : public v8::Task {
 public:
  explicit TestTask(std::function<void()> f) : f_(std::move(f)) {}

  void Run() override { f_(); }

 private:
  std::function<void()> f_;
};

double RealTime() {
  return base::TimeTicks::Now().ToInternalValue() /
         static_cast<double>(base::Time::kMicrosecondsPerSecond);
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, PostTaskOrder) {
  std::vector<int> order;
  base::Semaphore semaphore(0);
  DefaultWorkerThreadsTaskRunner runner(1, RealTime);

  std::unique_ptr<TestTask> task1 =
      std::make_unique<TestTask>([&] { order.push_back(1); });
  std::unique_ptr<TestTask> task2 =
      std::make_unique<TestTask>([&] { order.push_back(2); });
  std::unique_ptr<TestTask> task3 = std::make_unique<TestTask>([&] {
    order.push_back(3);
    semaphore.Signal();
  });

  runner.PostTask(std::move(task1));
  runner.PostTask(std::move(task2));
  runner.PostTask(std::move(task3));

  semaphore.Wait();

  runner.Terminate();
  ASSERT_EQ(3UL, order.size());
  ASSERT_EQ(1, order[0]);
  ASSERT_EQ(2, order[1]);
  ASSERT_EQ(3, order[2]);
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, PostTaskOrderMultipleWorkers) {
  base::Mutex vector_lock;
  std::vector<int> order;
  base::Semaphore all_done(0);
  DefaultWorkerThreadsTaskRunner runner(4, RealTime);

  auto make_task = [&](int id) {
    return std::make_unique<TestTask>([&, id] {
      bool done = false;
      {
        base::MutexGuard guard(&vector_lock);
        order.push_back(id);
        done = (order.size() == 5);
      }
      if (done) all_done.Signal();
    });
  };

  runner.PostTask(make_task(1));
  runner.PostTask(make_task(2));
  runner.PostTask(make_task(3));
  runner.PostTask(make_task(4));
  runner.PostTask(make_task(5));

  // We can't observe any ordering when there are multiple worker threads. The
  // tasks are guaranteed to be dispatched to workers in the input order, but
  // the workers are different threads and can be scheduled arbitrarily. Just
  // check that all of the tasks were run once.
  all_done.Wait();

  runner.Terminate();
  ASSERT_EQ(5UL, order.size());
  ASSERT_EQ(1, std::count(order.begin(), order.end(), 1));
  ASSERT_EQ(1, std::count(order.begin(), order.end(), 2));
  ASSERT_EQ(1, std::count(order.begin(), order.end(), 3));
  ASSERT_EQ(1, std::count(order.begin(), order.end(), 4));
  ASSERT_EQ(1, std::count(order.begin(), order.end(), 5));
}

class FakeClock {
 public:
  static double time() { return time_.load(); }
  static void set_time(double time) { time_.store(time); }
  static void set_time_and_wake_up_runner(
      double time, DefaultWorkerThreadsTaskRunner* runner) {
    time_.store(time);
    // PostTask will cause the condition variable WaitFor() call to be notified
    // early, rather than waiting for the real amount of time. WaitFor() listens
    // to the system clock and not our FakeClock.
    runner->PostTask(std::make_unique<TestTask>([] {}));
  }

 private:
  static std::atomic<double> time_;
};

std::atomic<double> FakeClock::time_{0.0};

TEST(DefaultWorkerThreadsTaskRunnerUnittest, PostDelayedTaskOrder) {
  FakeClock::set_time(0.0);
  std::vector<int> order;
  base::Semaphore task1_semaphore(0);
  base::Semaphore task3_semaphore(0);
  DefaultWorkerThreadsTaskRunner runner(1, FakeClock::time);

  std::unique_ptr<TestTask> task1 = std::make_unique<TestTask>([&] {
    order.push_back(1);
    task1_semaphore.Signal();
  });
  std::unique_ptr<TestTask> task2 =
      std::make_unique<TestTask>([&] { order.push_back(2); });
  std::unique_ptr<TestTask> task3 = std::make_unique<TestTask>([&] {
    order.push_back(3);
    task3_semaphore.Signal();
  });

  runner.PostDelayedTask(std::move(task1), 100);
  runner.PostTask(std::move(task2));
  runner.PostTask(std::move(task3));

  FakeClock::set_time_and_wake_up_runner(99, &runner);

  task3_semaphore.Wait();
  ASSERT_EQ(2UL, order.size());
  ASSERT_EQ(2, order[0]);
  ASSERT_EQ(3, order[1]);

  FakeClock::set_time_and_wake_up_runner(101, &runner);
  task1_semaphore.Wait();

  runner.Terminate();
  ASSERT_EQ(3UL, order.size());
  ASSERT_EQ(2, order[0]);
  ASSERT_EQ(3, order[1]);
  ASSERT_EQ(1, order[2]);
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, PostDelayedTaskOrder2) {
  FakeClock::set_time(0.0);
  std::vector<int> order;
  base::Semaphore task1_semaphore(0);
  base::Semaphore task2_semaphore(0);
  base::Semaphore task3_semaphore(0);
  DefaultWorkerThreadsTaskRunner runner(1, FakeClock::time);

  std::unique_ptr<TestTask> task1 = std::make_unique<TestTask>([&] {
    order.push_back(1);
    task1_semaphore.Signal();
  });
  std::unique_ptr<TestTask> task2 = std::make_unique<TestTask>([&] {
    order.push_back(2);
    task2_semaphore.Signal();
  });
  std::unique_ptr<TestTask> task3 = std::make_unique<TestTask>([&] {
    order.push_back(3);
    task3_semaphore.Signal();
  });

  runner.PostDelayedTask(std::move(task1), 500);
  runner.PostDelayedTask(std::move(task2), 100);
  runner.PostDelayedTask(std::move(task3), 200);

  FakeClock::set_time_and_wake_up_runner(101, &runner);

  task2_semaphore.Wait();
  ASSERT_EQ(1UL, order.size());
  ASSERT_EQ(2, order[0]);

  FakeClock::set_time_and_wake_up_runner(201, &runner);

  task3_semaphore.Wait();
  ASSERT_EQ(2UL, order.size());
  ASSERT_EQ(2, order[0]);
  ASSERT_EQ(3, order[1]);

  FakeClock::set_time_and_wake_up_runner(501, &runner);

  task1_semaphore.Wait();
  runner.Terminate();
  ASSERT_EQ(3UL, order.size());
  ASSERT_EQ(2, order[0]);
  ASSERT_EQ(3, order[1]);
  ASSERT_EQ(1, order[2]);
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, PostAfterTerminate) {
  FakeClock::set_time(0.0);
  std::vector<int> order;
  base::Semaphore task1_semaphore(0);
  base::Semaphore task2_semaphore(0);
  base::Semaphore task3_semaphore(0);
  DefaultWorkerThreadsTaskRunner runner(1, FakeClock::time);

  std::unique_ptr<TestTask> task1 = std::make_unique<TestTask>([&] {
    order.push_back(1);
    task1_semaphore.Signal();
  });
  std::unique_ptr<TestTask> task2 = std::make_unique<TestTask>([&] {
    order.push_back(2);
    task2_semaphore.Signal();
  });
  std::unique_ptr<TestTask> task3 = std::make_unique<TestTask>([&] {
    order.push_back(3);
    task3_semaphore.Signal();
  });

  runner.PostTask(std::move(task1));
  runner.PostDelayedTask(std::move(task2), 100);

  task1_semaphore.Wait();
  ASSERT_EQ(1UL, order.size());
  ASSERT_EQ(1, order[0]);

  runner.Terminate();
  FakeClock::set_time_and_wake_up_runner(201, &runner);
  // OK, we can't actually prove that this never executes. But wait a bit at
  // least.
  bool signalled =
      task2_semaphore.WaitFor(base::TimeDelta::FromMilliseconds(100));
  ASSERT_FALSE(signalled);
  ASSERT_EQ(1UL, order.size());
  ASSERT_EQ(1, order[0]);

  runner.PostTask(std::move(task3));
  signalled = task3_semaphore.WaitFor(base::TimeDelta::FromMilliseconds(100));
  ASSERT_FALSE(signalled);
  ASSERT_EQ(1UL, order.size());
  ASSERT_EQ(1, order[0]);
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, NoIdleTasks) {
  DefaultWorkerThreadsTaskRunner runner(1, FakeClock::time);

  ASSERT_FALSE(runner.IdleTasksEnabled());
  runner.Terminate();
}

// A controllable fake clock that counts distinct worker threads calling time()
// (every started WorkerThread calls TryGetNext() -> time() before Terminate()
// returns) and supports blocking waits on worker poll counts without spinning.
class ControlledClock {
 public:
  ControlledClock() : id_(next_id_.fetch_add(1) + 1) {
    ignored_tids_.push_back(base::OS::GetCurrentThreadId());
    DCHECK_NULL(current_.load());
    current_.store(this);
  }
  ~ControlledClock() { current_.store(nullptr); }

  static double time() { return current_.load()->TimeImpl(); }

  void IgnoreCurrentThread() {
    base::MutexGuard guard(&mutex_);
    ignored_tids_.push_back(base::OS::GetCurrentThreadId());
  }

  void Advance(double new_time = 1.0) {
    base::MutexGuard guard(&mutex_);
    now_ = new_time;
  }

  int worker_calls() {
    base::MutexGuard guard(&mutex_);
    return worker_calls_;
  }

  int worker_calls_for_tid(int tid) {
    base::MutexGuard guard(&mutex_);
    auto it = worker_call_seqs_by_tid_.find(tid);
    return it == worker_call_seqs_by_tid_.end()
               ? 0
               : static_cast<int>(it->second.size());
  }

  bool WaitForWorkerCalls(int target) {
    return WaitUntil([&] { return worker_calls_ >= target; });
  }

  bool WaitForWorkerCallsForTid(int tid, int target) {
    return WaitUntil([&] {
      auto it = worker_call_seqs_by_tid_.find(tid);
      return it != worker_call_seqs_by_tid_.end() &&
             static_cast<int>(it->second.size()) >= target;
    });
  }

  bool WaitForWorkerThreads(uint32_t target) {
    return WaitUntil([&] { return worker_threads_ >= target; });
  }

  int FirstWorkerTidOtherThan(int excluded_tid) {
    base::MutexGuard guard(&mutex_);
    for (const auto& [tid, seqs] : worker_call_seqs_by_tid_) {
      if (tid != excluded_tid) return tid;
    }
    return 0;
  }

  // Waits until |after_tid| has made at least |after_call_count| polls AND
  // |tid| has polled strictly after |after_tid|'s |after_call_count|-th poll.
  bool WaitForTidCallAfter(int tid, int after_tid, int after_call_count) {
    return WaitUntil([&] {
      auto after_it = worker_call_seqs_by_tid_.find(after_tid);
      if (after_it == worker_call_seqs_by_tid_.end() ||
          static_cast<int>(after_it->second.size()) < after_call_count) {
        return false;
      }
      int threshold_seq = after_it->second[after_call_count - 1];
      auto it = worker_call_seqs_by_tid_.find(tid);
      return it != worker_call_seqs_by_tid_.end() && !it->second.empty() &&
             it->second.back() > threshold_seq;
    });
  }

  uint32_t worker_threads() {
    base::MutexGuard guard(&mutex_);
    return worker_threads_;
  }

 private:
  template <typename Pred>
  bool WaitUntil(Pred pred) {
    const base::TimeTicks deadline =
        base::TimeTicks::Now() + base::TimeDelta::FromSeconds(10);
    base::MutexGuard guard(&mutex_);
    while (!pred()) {
      base::TimeDelta remaining = deadline - base::TimeTicks::Now();
      if (remaining <= base::TimeDelta()) return false;
      if (!cv_.WaitFor(&mutex_, remaining)) {
        if (!pred()) return false;
      }
    }
    return true;
  }

  double TimeImpl() {
    thread_local uint64_t seen_clock_id = 0;
    const int tid = base::OS::GetCurrentThreadId();
    base::MutexGuard guard(&mutex_);
    const bool is_worker = std::find(ignored_tids_.begin(), ignored_tids_.end(),
                                     tid) == ignored_tids_.end();
    if (is_worker) {
      if (seen_clock_id != id_) {
        seen_clock_id = id_;
        ++worker_threads_;
      }
      ++worker_calls_;
      worker_call_seqs_by_tid_[tid].push_back(worker_calls_);
      cv_.NotifyAll();
    }
    return now_;
  }

  static std::atomic<uint64_t> next_id_;
  static std::atomic<ControlledClock*> current_;

  const uint64_t id_;
  base::Mutex mutex_;
  base::ConditionVariable cv_;
  std::vector<int> ignored_tids_;
  std::unordered_map<int, std::vector<int>> worker_call_seqs_by_tid_;
  double now_ = 0.0;
  int worker_calls_ = 0;
  uint32_t worker_threads_ = 0;
};

std::atomic<uint64_t> ControlledClock::next_id_{0};
std::atomic<ControlledClock*> ControlledClock::current_{nullptr};

TEST(DefaultWorkerThreadsTaskRunnerUnittest, ZeroPoolSizeSpawnsNoWorkers) {
  ControlledClock clock;
  DefaultWorkerThreadsTaskRunner runner(0, ControlledClock::time);
  runner.Terminate();
  ASSERT_EQ(0u, clock.worker_threads());
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, TerminateWithoutTasks) {
  ControlledClock clock;
  DefaultWorkerThreadsTaskRunner runner(4, ControlledClock::time);
  runner.Terminate();
  ASSERT_EQ(1u, clock.worker_threads());
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, SequentialTasksReuseSingleWorker) {
  ControlledClock clock;
  base::Semaphore done(0);
  std::atomic<int> calls_in_task{0};
  DefaultWorkerThreadsTaskRunner runner(4, ControlledClock::time);
  ASSERT_TRUE(clock.WaitForWorkerCalls(1));

  for (int i = 0; i < 4; ++i) {
    runner.PostTask(std::make_unique<TestTask>([&] {
      // Sample worker_calls while Worker 1 is actively running this task so no
      // spurious Wait() wakeup can inflate the target before ~Task() finishes.
      calls_in_task.store(clock.worker_calls());
      done.Signal();
    }));
    ASSERT_TRUE(done.WaitFor(base::TimeDelta::FromSeconds(10)));
    // Wait until Worker 1 has finished ~Task() and re-polled TryGetNext().
    ASSERT_TRUE(clock.WaitForWorkerCalls(calls_in_task.load() + 1));
  }

  runner.Terminate();
  ASSERT_EQ(1u, clock.worker_threads());
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest,
     PartialConcurrencySpawnsExactWorkers) {
  ControlledClock clock;
  std::atomic<uint32_t> started{0};
  base::Semaphore both_started(0);
  base::Semaphore release_workers(0);
  DefaultWorkerThreadsTaskRunner runner(4, ControlledClock::time);

  for (int i = 0; i < 2; ++i) {
    runner.PostTask(std::make_unique<TestTask>([&] {
      if (started.fetch_add(1) + 1 == 2) {
        both_started.Signal();
      }
      release_workers.Wait();
    }));
  }

  bool both_running = both_started.WaitFor(base::TimeDelta::FromSeconds(10));
  release_workers.Signal();
  release_workers.Signal();
  runner.Terminate();

  ASSERT_TRUE(both_running);
  ASSERT_EQ(2u, clock.worker_threads());
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, SpawnsAllWorkersOnDemand) {
  constexpr uint32_t kPoolSize = 4;
  ControlledClock clock;
  std::atomic<uint32_t> started{0};
  std::atomic<uint32_t> completed{0};
  base::Semaphore all_started(0);
  base::Semaphore release_workers(0);
  DefaultWorkerThreadsTaskRunner runner(kPoolSize, ControlledClock::time);

  for (uint32_t i = 0; i < kPoolSize; ++i) {
    runner.PostTask(std::make_unique<TestTask>([&] {
      if (started.fetch_add(1) + 1 == kPoolSize) {
        all_started.Signal();
      }
      release_workers.Wait();
      completed.fetch_add(1);
    }));
  }

  // Only one worker is started eagerly; the other kPoolSize - 1 must be
  // spawned on demand, since all kPoolSize tasks have to run concurrently for
  // all_started to be signaled. Bounded so that a regression fails fast
  // instead of hanging the test; the workers are released before asserting so
  // that Terminate() can still join them.
  bool all_running = all_started.WaitFor(base::TimeDelta::FromSeconds(10));

  for (uint32_t i = 0; i < kPoolSize; ++i) {
    release_workers.Signal();
  }

  runner.Terminate();
  ASSERT_TRUE(all_running);
  ASSERT_EQ(kPoolSize, completed.load());
  ASSERT_EQ(kPoolSize, clock.worker_threads());
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, FutureDelayedTasksDoNotOverSpawn) {
  ControlledClock clock;
  DefaultWorkerThreadsTaskRunner runner(4, ControlledClock::time);

  for (int i = 0; i < 4; ++i) {
    runner.PostDelayedTask(std::make_unique<TestTask>([] {}), 100.0);
  }

  runner.Terminate();
  ASSERT_EQ(1u, clock.worker_threads());
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest,
     DelayedTaskTimeoutRemovesFromIdleThreads) {
  ControlledClock clock;
  base::Semaphore second_task_started(0);
  base::Semaphore first_task_done(0);
  std::atomic<bool> second_task_ran_concurrently{false};
  DefaultWorkerThreadsTaskRunner runner(2, ControlledClock::time);

  // Wait until Worker 1 has polled the empty queue once and entered
  // idle_threads_.
  ASSERT_TRUE(clock.WaitForWorkerCalls(1));

  int calls_before = clock.worker_calls();
  // Worker 1 is notified, enters kWaitDelayed (1 ms), and wakes via WaitFor
  // timeout (not Notify()). While Worker 1 is busy running this task, posting
  // another task must spawn Worker 2 rather than notifying a stale Worker 1
  // entry in idle_threads_.
  runner.PostDelayedTask(
      std::make_unique<TestTask>([&] {
        runner.PostTask(
            std::make_unique<TestTask>([&] { second_task_started.Signal(); }));
        second_task_ran_concurrently =
            second_task_started.WaitFor(base::TimeDelta::FromSeconds(10));
        first_task_done.Signal();
      }),
      0.001);

  ASSERT_TRUE(clock.WaitForWorkerCalls(calls_before + 1));
  clock.Advance();

  first_task_done.Wait();
  runner.Terminate();
  ASSERT_TRUE(second_task_ran_concurrently);
  ASSERT_EQ(2u, clock.worker_threads());
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, DelayedTasksSpawnWorkersOnDemand) {
  constexpr uint32_t kPoolSize = 4;
  ControlledClock clock;
  std::atomic<uint32_t> started{0};
  std::atomic<uint32_t> completed{0};
  base::Semaphore all_started(0);
  base::Semaphore release_workers(0);
  DefaultWorkerThreadsTaskRunner runner(kPoolSize, ControlledClock::time);

  ASSERT_TRUE(clock.WaitForWorkerCalls(1));
  int calls_before = clock.worker_calls();

  for (uint32_t i = 0; i < kPoolSize; ++i) {
    runner.PostDelayedTask(std::make_unique<TestTask>([&] {
                             if (started.fetch_add(1) + 1 == kPoolSize) {
                               all_started.Signal();
                             }
                             release_workers.Wait();
                             completed.fetch_add(1);
                           }),
                           0.001);
  }

  // Wait until Worker 1 has entered kWaitDelayed for the 1 ms deadline; no
  // extra workers should have been spawned before the deadline expires.
  ASSERT_TRUE(clock.WaitForWorkerCalls(calls_before + 1));
  uint32_t workers_before_advance = clock.worker_threads();

  clock.Advance();
  bool all_running = all_started.WaitFor(base::TimeDelta::FromSeconds(10));

  for (uint32_t i = 0; i < kPoolSize; ++i) {
    release_workers.Signal();
  }

  runner.Terminate();
  ASSERT_EQ(1u, workers_before_advance);
  ASSERT_TRUE(all_running);
  ASSERT_EQ(kPoolSize, completed.load());
  ASSERT_EQ(kPoolSize, clock.worker_threads());
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest,
     StaggeredDelayedTasksSpawnWorkersOnDemand) {
  ControlledClock clock;
  base::Semaphore second_task_started(0);
  base::Semaphore first_task_done(0);
  std::atomic<bool> second_task_ran_concurrently{false};
  DefaultWorkerThreadsTaskRunner runner(2, ControlledClock::time);

  ASSERT_TRUE(clock.WaitForWorkerCalls(1));

  // Two delayed tasks with distinct deadlines (1 ms vs 5 ms). When the 1 ms
  // task expires at t = 0.002, the 5 ms task is still in delayed_task_queue_;
  // Worker 1 must hand off the timed wait by spawning Worker 2 rather than
  // starving task 2.
  runner.PostDelayedTask(
      std::make_unique<TestTask>([&] {
        // Wait until Worker 2 has spawned and polled TryGetNext() for the
        // remaining 5 ms delayed task, then advance the clock so it becomes
        // due.
        if (clock.WaitForWorkerCalls(4)) {
          clock.Advance(1.0);
        }
        second_task_ran_concurrently =
            second_task_started.WaitFor(base::TimeDelta::FromSeconds(10));
        first_task_done.Signal();
      }),
      0.001);
  runner.PostDelayedTask(
      std::make_unique<TestTask>([&] { second_task_started.Signal(); }), 0.005);

  ASSERT_TRUE(clock.WaitForWorkerCalls(2));
  clock.Advance(0.002);

  ASSERT_TRUE(first_task_done.WaitFor(base::TimeDelta::FromSeconds(10)));
  runner.Terminate();
  ASSERT_TRUE(second_task_ran_concurrently);
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest,
     ImmediateTaskDoesNotStarveDelayedTask) {
  ControlledClock clock;
  base::Semaphore delayed_task_ran(0);
  base::Semaphore immediate_task_done(0);
  std::atomic<bool> ran_concurrently{false};
  DefaultWorkerThreadsTaskRunner runner(2, ControlledClock::time);

  ASSERT_TRUE(clock.WaitForWorkerCalls(1));

  // Put Worker 1 into kWaitDelayed, then post an immediate task that blocks on
  // the delayed task; another worker must be spawned to wait on the deadline.
  runner.PostDelayedTask(
      std::make_unique<TestTask>([&] { delayed_task_ran.Signal(); }), 0.001);
  ASSERT_TRUE(clock.WaitForWorkerCalls(2));

  runner.PostTask(std::make_unique<TestTask>([&] {
    if (clock.WaitForWorkerCalls(4)) {
      clock.Advance(1.0);
    }
    ran_concurrently =
        delayed_task_ran.WaitFor(base::TimeDelta::FromSeconds(10));
    immediate_task_done.Signal();
  }));

  ASSERT_TRUE(immediate_task_done.WaitFor(base::TimeDelta::FromSeconds(10)));
  runner.Terminate();
  ASSERT_TRUE(ran_concurrently);
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest,
     EarliestDelayedTaskWakesWaiterWhileOtherWorkerSearches) {
  ControlledClock clock;
  base::Semaphore both_idle_warmed(0);
  base::Semaphore release_warmup(0);
  std::atomic<int> warmup_started{0};
  base::Semaphore short_delayed_ran(0);
  base::Semaphore blocking_immediate_done(0);
  std::atomic<bool> short_delayed_on_time{false};
  DefaultWorkerThreadsTaskRunner runner(2, ControlledClock::time);

  // Spawn both workers so W1 and W2 are both idle in idle_threads_.
  for (int i = 0; i < 2; ++i) {
    runner.PostTask(std::make_unique<TestTask>([&] {
      if (warmup_started.fetch_add(1) + 1 == 2) {
        both_idle_warmed.Signal();
      }
      release_warmup.Wait();
    }));
  }
  both_idle_warmed.Wait();
  int calls_while_busy = clock.worker_calls();
  release_warmup.Signal();
  release_warmup.Signal();
  // Wait until both workers have returned from warmup and entered
  // kWaitIndefinite in idle_threads_.
  ASSERT_TRUE(clock.WaitForWorkerCalls(calls_while_busy + 2));

  // Put one idle worker into a long kWaitDelayed (3600 s) and wait until it
  // has polled TryGetNext() and entered WaitFor(3600 s).
  int calls_before_long_delayed = clock.worker_calls();
  runner.PostDelayedTask(std::make_unique<TestTask>([] {}), 3600.0);
  ASSERT_TRUE(clock.WaitForWorkerCalls(calls_before_long_delayed + 1));

  // Wake the other worker for a blocking immediate task and immediately post a
  // 1 ms delayed task while that immediate worker is searching. The 3600 s
  // delayed waiter must be woken to recompute its 1 ms timeout.
  runner.PostTask(std::make_unique<TestTask>([&] {
    short_delayed_on_time =
        short_delayed_ran.WaitFor(base::TimeDelta::FromSeconds(10));
    blocking_immediate_done.Signal();
  }));
  runner.PostDelayedTask(
      std::make_unique<TestTask>([&] { short_delayed_ran.Signal(); }), 0.001);
  clock.Advance(1.0);

  blocking_immediate_done.Wait();
  runner.Terminate();
  ASSERT_TRUE(short_delayed_on_time);
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest,
     SingleDelayedWaiterPreventsStaleTimeoutHang) {
  ControlledClock clock;
  base::Semaphore both_running(0);
  base::Semaphore release_warmup(0);
  std::atomic<int> warmup_started{0};
  base::Semaphore short_delayed_ran(0);
  base::Semaphore blocking_immediate_done(0);
  std::atomic<bool> short_delayed_on_time{false};
  DefaultWorkerThreadsTaskRunner runner(2, ControlledClock::time);

  // Queue a 3600 s delayed task and two immediate warmup tasks so both workers
  // finish their tasks while a delayed task is pending.
  runner.PostDelayedTask(std::make_unique<TestTask>([] {}), 3600.0);
  for (int i = 0; i < 2; ++i) {
    runner.PostTask(std::make_unique<TestTask>([&] {
      if (warmup_started.fetch_add(1) + 1 == 2) {
        both_running.Signal();
      }
      release_warmup.Wait();
    }));
  }
  both_running.Wait();
  int calls_while_busy = clock.worker_calls();
  release_warmup.Signal();
  release_warmup.Signal();
  // Wait until both workers have finished warmup and polled TryGetNext() with
  // the 3600 s delayed task queued: only one worker may enter WaitFor(3600 s)
  // (delayed_waiters_ == 1) while the second parks indefinitely.
  ASSERT_TRUE(clock.WaitForWorkerCalls(calls_while_busy + 2));

  // Posting a 1 ms delayed task (waking the single timer waiter) followed
  // immediately by a blocking immediate task must wake the second parked
  // worker so the 1 ms delayed task is not stranded on a stale 3600 s timer.
  runner.PostDelayedTask(
      std::make_unique<TestTask>([&] { short_delayed_ran.Signal(); }), 0.001);
  runner.PostTask(std::make_unique<TestTask>([&] {
    short_delayed_on_time =
        short_delayed_ran.WaitFor(base::TimeDelta::FromSeconds(10));
    blocking_immediate_done.Signal();
  }));
  clock.Advance(1.0);

  blocking_immediate_done.Wait();
  runner.Terminate();
  ASSERT_TRUE(short_delayed_on_time);
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest,
     FailedWorkerSpawnFallsBackToExistingWorker) {
  ControlledClock clock;
  base::Semaphore task1_started(0);
  base::Semaphore release_task1(0);
  base::Semaphore all_done(0);
  std::atomic<int> completed{0};
  DefaultWorkerThreadsTaskRunner runner(4, ControlledClock::time);
  runner.SetFailWorkerStartForTesting(true);

  runner.PostTask(std::make_unique<TestTask>([&] {
    task1_started.Signal();
    release_task1.Wait();
    if (completed.fetch_add(1) + 1 == 3) all_done.Signal();
  }));
  task1_started.Wait();

  // Both spawns fail (first triggers failure + 50 ms cooldown, second hits
  // cooldown); tasks stay queued and are drained by Worker 1 once unblocked.
  runner.PostTask(std::make_unique<TestTask>([&] {
    if (completed.fetch_add(1) + 1 == 3) all_done.Signal();
  }));
  runner.PostTask(std::make_unique<TestTask>([&] {
    if (completed.fetch_add(1) + 1 == 3) all_done.Signal();
  }));

  release_task1.Signal();
  bool finished = all_done.WaitFor(base::TimeDelta::FromSeconds(10));
  runner.Terminate();

  ASSERT_TRUE(finished);
  ASSERT_EQ(3, completed.load());
  ASSERT_EQ(1u, clock.worker_threads());
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, DelayedTaskQueueAppendEarliest) {
  ControlledClock clock;
  DelayedTaskQueue queue(ControlledClock::time);

  ASSERT_TRUE(queue.AppendDelayed(std::make_unique<TestTask>([] {}), 0.5));
  ASSERT_FALSE(queue.AppendDelayed(std::make_unique<TestTask>([] {}), 0.5));
  ASSERT_FALSE(queue.AppendDelayed(std::make_unique<TestTask>([] {}), 1.0));
  ASSERT_TRUE(queue.AppendDelayed(std::make_unique<TestTask>([] {}), 0.1));
  ASSERT_TRUE(queue.has_delayed_tasks());
  ASSERT_EQ(0u, queue.immediate_size());

  clock.Advance(1.0);
  DelayedTaskQueue::MaybeNextTask next = queue.TryGetNext();
  ASSERT_EQ(DelayedTaskQueue::MaybeNextTask::kTask, next.state);
  ASSERT_EQ(3u, queue.immediate_size());
  ASSERT_FALSE(queue.has_delayed_tasks());

  for (int i = 0; i < 3; ++i) {
    next = queue.TryGetNext();
    ASSERT_EQ(DelayedTaskQueue::MaybeNextTask::kTask, next.state);
  }
  ASSERT_EQ(0u, queue.immediate_size());
  queue.Terminate();
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest, TaskDestructorCanPostTask) {
  base::Semaphore done(0);
  DefaultWorkerThreadsTaskRunner runner(1, RealTime);

  class RepostingTask final : public v8::Task {
   public:
    RepostingTask(DefaultWorkerThreadsTaskRunner* runner, base::Semaphore* done)
        : runner_(runner), done_(done) {}
    ~RepostingTask() override {
      runner_->PostTask(
          std::make_unique<TestTask>([done = done_] { done->Signal(); }));
    }
    void Run() override {}

   private:
    DefaultWorkerThreadsTaskRunner* runner_;
    base::Semaphore* done_;
  };

  runner.PostTask(std::make_unique<RepostingTask>(&runner, &done));
  bool signalled = done.WaitFor(base::TimeDelta::FromSeconds(10));
  runner.Terminate();
  ASSERT_TRUE(signalled);
}

#if V8_OS_LINUX
namespace {

bool CanTestUnprivilegedFallback(int expected_nice) {
  std::atomic<bool> can_test{false};
  base::Semaphore probe_done(0);
  DefaultWorkerThreadsTaskRunner probe_runner(
      1, RealTime, base::Thread::Priority::kBestEffort);
  probe_runner.PostTask(std::make_unique<TestTask>([&] {
    int low_nice = getpriority(PRIO_PROCESS, 0);
    if (low_nice > expected_nice) {
      if (setpriority(PRIO_PROCESS, 0, expected_nice) == 0) {
        setpriority(PRIO_PROCESS, 0, low_nice);
      } else {
        can_test.store(true);
      }
    }
    probe_done.Signal();
  }));
  probe_done.Wait();
  probe_runner.Terminate();
  return can_test.load();
}

}  // namespace

TEST(DefaultWorkerThreadsTaskRunnerUnittest,
     LowerPriorityThreadDoesNotPoisonHigherPriorityPool) {
  const int expected_nice = std::max(0, getpriority(PRIO_PROCESS, 0));

  // Case 1: When a kBestEffort (nice=10) worker queues a burst of tasks while
  // high_runner's nice=0 worker wakes up, high_runner's nice=0 worker spawns
  // all additional workers at nice=0 rather than inheriting nice=10.
  {
    ControlledClock clock1;
    std::atomic<uint32_t> started{0};
    std::atomic<int> max_observed_nice{expected_nice};
    base::Semaphore all_started(0);
    base::Semaphore release_workers(0);
    base::Semaphore posted(0);
    DefaultWorkerThreadsTaskRunner high_runner(
        4, ControlledClock::time, base::Thread::Priority::kUserBlocking);
    ASSERT_TRUE(clock1.WaitForWorkerCalls(1));
    int calls_before_burst = clock1.worker_calls();

    {
      DefaultWorkerThreadsTaskRunner low_runner(
          1, RealTime, base::Thread::Priority::kBestEffort);
      low_runner.PostTask(std::make_unique<TestTask>([&] {
        clock1.IgnoreCurrentThread();
        for (int i = 0; i < 4; ++i) {
          high_runner.PostDelayedTask(
              std::make_unique<TestTask>([&] {
                int nice = getpriority(PRIO_PROCESS, 0);
                int prev = max_observed_nice.load();
                while (nice > prev &&
                       !max_observed_nice.compare_exchange_weak(prev, nice)) {
                }
                if (started.fetch_add(1) + 1 == 4) {
                  all_started.Signal();
                }
                release_workers.Wait();
              }),
              0.001);
        }
        posted.Signal();
      }));
      posted.Wait();
      low_runner.Terminate();
    }

    ASSERT_TRUE(clock1.WaitForWorkerCalls(calls_before_burst + 1));
    uint32_t workers_before_advance = clock1.worker_threads();
    clock1.Advance();

    bool all_running = all_started.WaitFor(base::TimeDelta::FromSeconds(10));
    for (int i = 0; i < 4; ++i) {
      release_workers.Signal();
    }
    high_runner.Terminate();
    ASSERT_EQ(1u, workers_before_advance);
    ASSERT_TRUE(all_running);
    ASSERT_EQ(4u, clock1.worker_threads());
    ASSERT_EQ(expected_nice, max_observed_nice.load());
  }

  // Case 2: When all existing kUserBlocking workers are blocked and an
  // unprivileged kBestEffort (nice=10) thread posts a burst of tasks, a single
  // temporary fallback worker handles the burst and retires once idle rather
  // than permanently occupying a pool slot at nice=10.
  if (CanTestUnprivilegedFallback(expected_nice)) {
    ControlledClock clock2;
    base::Semaphore worker1_blocked(0);
    base::Semaphore unblock_worker1(0);
    base::Semaphore release_task_a(0);
    base::Semaphore task_a_done(0);
    base::Semaphore burst_done(0);
    std::atomic<int> fallback_tid{0};
    std::atomic<int> fallback_calls_in_task_b{0};
    std::atomic<uint32_t> check_started{0};
    std::atomic<int> post_retire_max_nice{expected_nice};
    base::Semaphore both_started(0);
    base::Semaphore release_both(0);
    DefaultWorkerThreadsTaskRunner high_runner2(
        4, ControlledClock::time, base::Thread::Priority::kUserBlocking);
    DefaultWorkerThreadsTaskRunner low_runner(
        1, RealTime, base::Thread::Priority::kBestEffort);
    ASSERT_TRUE(clock2.WaitForWorkerCalls(1));

    high_runner2.PostTask(std::make_unique<TestTask>([&] {
      worker1_blocked.Signal();
      unblock_worker1.Wait();
      burst_done.Signal();
    }));
    worker1_blocked.Wait();

    low_runner.PostTask(std::make_unique<TestTask>([&] {
      clock2.IgnoreCurrentThread();
      // Hold Task A until Task B is queued so the fallback worker cannot
      // finish Task A and retire before Task B arrives.
      high_runner2.PostTask(std::make_unique<TestTask>([&] {
        release_task_a.Wait();
        task_a_done.Signal();
      }));
      high_runner2.PostTask(std::make_unique<TestTask>([&] {
        const int tid = base::OS::GetCurrentThreadId();
        fallback_tid.store(tid);
        fallback_calls_in_task_b.store(clock2.worker_calls_for_tid(tid));
        unblock_worker1.Signal();
      }));
      release_task_a.Signal();
    }));
    bool task_a_completed =
        task_a_done.WaitFor(base::TimeDelta::FromSeconds(10));
    bool burst_completed = burst_done.WaitFor(base::TimeDelta::FromSeconds(10));
    if (!task_a_completed || !burst_completed) {
      release_task_a.Signal();
      unblock_worker1.Signal();
    }
    low_runner.Terminate();
    ASSERT_TRUE(task_a_completed);
    ASSERT_TRUE(burst_completed);
    // Wait directly for the fallback worker's own empty-queue retirement poll
    // so spurious wakeups of Worker 1 cannot satisfy the wait prematurely.
    ASSERT_NE(0, fallback_tid.load());
    ASSERT_TRUE(clock2.WaitForWorkerCallsForTid(
        fallback_tid.load(), fallback_calls_in_task_b.load() + 1));
    uint32_t workers_after_fallback_burst = clock2.worker_threads();

    // Now post 2 concurrent tasks from the main thread and verify both pool
    // workers run at expected_nice (the temporary nice=10 worker retired).
    for (int i = 0; i < 2; ++i) {
      high_runner2.PostTask(std::make_unique<TestTask>([&] {
        int nice = getpriority(PRIO_PROCESS, 0);
        int prev = post_retire_max_nice.load();
        while (nice > prev &&
               !post_retire_max_nice.compare_exchange_weak(prev, nice)) {
        }
        if (check_started.fetch_add(1) + 1 == 2) {
          both_started.Signal();
        }
        release_both.Wait();
      }));
    }
    bool both_running = both_started.WaitFor(base::TimeDelta::FromSeconds(10));
    release_both.Signal();
    release_both.Signal();

    high_runner2.Terminate();
    ASSERT_EQ(2u, workers_after_fallback_burst);
    ASSERT_TRUE(both_running);
    ASSERT_EQ(expected_nice, post_retire_max_nice.load());
  }
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest,
     LowerPriorityDelayedAndImmediateTasksDoNotStarve) {
  // Case 1: D1 is still in delayed_task_queue_ when fallback worker F1 pops T2.
  {
    ControlledClock clock;
    base::Semaphore worker1_blocked(0);
    base::Semaphore release_worker1(0);
    base::Semaphore delayed_ran(0);
    base::Semaphore immediate_done(0);
    std::atomic<bool> delayed_completed_in_time{false};
    DefaultWorkerThreadsTaskRunner high_runner(
        4, ControlledClock::time, base::Thread::Priority::kUserBlocking);
    DefaultWorkerThreadsTaskRunner low_runner(
        1, RealTime, base::Thread::Priority::kBestEffort);

    high_runner.PostTask(std::make_unique<TestTask>([&] {
      worker1_blocked.Signal();
      release_worker1.Wait();
    }));
    worker1_blocked.Wait();
    int calls_before = clock.worker_calls();

    low_runner.PostTask(std::make_unique<TestTask>([&] {
      clock.IgnoreCurrentThread();
      high_runner.PostDelayedTask(
          std::make_unique<TestTask>([&] { delayed_ran.Signal(); }), 0.005);
      high_runner.PostTask(std::make_unique<TestTask>([&] {
        // Wait until fallback worker F2 has spawned and polled TryGetNext()
        // for D1, then advance the clock so D1 becomes due.
        if (clock.WaitForWorkerCalls(calls_before + 2)) {
          clock.Advance(1.0);
        }
        delayed_completed_in_time =
            delayed_ran.WaitFor(base::TimeDelta::FromSeconds(10));
        immediate_done.Signal();
      }));
    }));

    bool finished = immediate_done.WaitFor(base::TimeDelta::FromSeconds(10));
    release_worker1.Signal();
    low_runner.Terminate();
    high_runner.Terminate();
    ASSERT_TRUE(finished);
    ASSERT_TRUE(delayed_completed_in_time);
  }

  // Case 2: D1's deadline has already elapsed when T2 wakes the parked
  // fallback delayed waiter F1, so TryGetNext() moves D1 from
  // delayed_task_queue_ into task_queue_ behind T2 and pops T2 first. Even
  // though delayed_task_queue_ is now empty, D1 is still pending in
  // task_queue_ and must not be stranded behind T2.
  {
    ControlledClock clock;
    base::Semaphore worker1_blocked(0);
    base::Semaphore release_worker1(0);
    base::Semaphore delayed_ran(0);
    base::Semaphore immediate_done(0);
    std::atomic<bool> delayed_completed_in_time{false};
    DefaultWorkerThreadsTaskRunner high_runner(
        4, ControlledClock::time, base::Thread::Priority::kUserBlocking);
    DefaultWorkerThreadsTaskRunner low_runner(
        1, RealTime, base::Thread::Priority::kBestEffort);

    high_runner.PostTask(std::make_unique<TestTask>([&] {
      worker1_blocked.Signal();
      release_worker1.Wait();
    }));
    worker1_blocked.Wait();
    int calls_before = clock.worker_calls();

    // Post D1 (delay 1.0s) from low_runner so fallback worker F1 spawns and
    // parks in kWaitDelayed as the delayed waiter.
    low_runner.PostTask(std::make_unique<TestTask>([&] {
      clock.IgnoreCurrentThread();
      high_runner.PostDelayedTask(
          std::make_unique<TestTask>([&] { delayed_ran.Signal(); }), 1.0);
    }));
    bool waiter_polled = clock.WaitForWorkerCalls(calls_before + 1);
    if (!waiter_polled) {
      release_worker1.Signal();
      low_runner.Terminate();
      high_runner.Terminate();
    }
    ASSERT_TRUE(waiter_polled);

    // Advance clock past D1's deadline before posting T2 from low_runner.
    // When T2 wakes F1, TryGetNext() moves D1 into task_queue_ behind T2 and
    // returns T2 to F1.
    clock.Advance(2.0);
    low_runner.PostTask(std::make_unique<TestTask>([&] {
      high_runner.PostTask(std::make_unique<TestTask>([&] {
        delayed_completed_in_time =
            delayed_ran.WaitFor(base::TimeDelta::FromSeconds(10));
        immediate_done.Signal();
      }));
    }));

    bool finished = immediate_done.WaitFor(base::TimeDelta::FromSeconds(10));
    release_worker1.Signal();
    low_runner.Terminate();
    high_runner.Terminate();
    ASSERT_TRUE(finished);
    ASSERT_TRUE(delayed_completed_in_time);
  }
}

TEST(DefaultWorkerThreadsTaskRunnerUnittest,
     OptimalWorkerTakesOverDelayedWaitFromFallbackWorker) {
  const int expected_nice = std::max(0, getpriority(PRIO_PROCESS, 0));
  if (!CanTestUnprivilegedFallback(expected_nice)) {
    GTEST_SKIP();
  }
  ControlledClock clock;
  base::Semaphore worker1_blocked(0);
  base::Semaphore release_worker1(0);
  base::Semaphore worker1_finished_block(0);
  std::atomic<int> worker1_tid{0};
  std::atomic<int> worker1_calls_in_task{0};
  std::atomic<uint32_t> check_started{0};
  std::atomic<int> max_observed_nice{expected_nice};
  base::Semaphore all_started(0);
  base::Semaphore release_all(0);
  DefaultWorkerThreadsTaskRunner high_runner(
      4, ControlledClock::time, base::Thread::Priority::kUserBlocking);
  ASSERT_TRUE(clock.WaitForWorkerCalls(1));

  // Block Worker 1 while a kBestEffort thread posts a long delayed task, so
  // fallback worker F1 (nice=10) spawns and becomes the active delayed waiter.
  high_runner.PostTask(std::make_unique<TestTask>([&] {
    const int tid = base::OS::GetCurrentThreadId();
    worker1_tid.store(tid);
    worker1_calls_in_task.store(clock.worker_calls_for_tid(tid));
    worker1_blocked.Signal();
    release_worker1.Wait();
    worker1_finished_block.Signal();
  }));
  worker1_blocked.Wait();

  int fallback_tid = 0;
  {
    DefaultWorkerThreadsTaskRunner low_runner(
        1, RealTime, base::Thread::Priority::kBestEffort);
    low_runner.PostTask(std::make_unique<TestTask>([&] {
      clock.IgnoreCurrentThread();
      high_runner.PostDelayedTask(std::make_unique<TestTask>([] {}), 3600.0);
    }));
    bool fallback_parked = clock.WaitForWorkerThreads(2);
    if (!fallback_parked) {
      release_worker1.Signal();
      low_runner.Terminate();
      high_runner.Terminate();
    }
    ASSERT_TRUE(fallback_parked);
    fallback_tid = clock.FirstWorkerTidOtherThan(worker1_tid.load());
    low_runner.Terminate();
  }
  ASSERT_NE(0, fallback_tid);

  // Unblock Worker 1: when Worker 1 reaches kWaitDelayed, it must wake F1 so
  // F1 retires immediately and Worker 1 takes over the 3600 s timer wait.
  // Wait specifically for F1's retirement poll strictly after Worker 1's
  // post-block kWaitDelayed poll so spurious wakeups of either worker cannot
  // satisfy the wait prematurely.
  release_worker1.Signal();
  ASSERT_TRUE(worker1_finished_block.WaitFor(base::TimeDelta::FromSeconds(10)));
  ASSERT_TRUE(clock.WaitForTidCallAfter(fallback_tid, worker1_tid.load(),
                                        worker1_calls_in_task.load() + 1));

  for (int i = 0; i < 4; ++i) {
    high_runner.PostTask(std::make_unique<TestTask>([&] {
      int nice = getpriority(PRIO_PROCESS, 0);
      int prev = max_observed_nice.load();
      while (nice > prev &&
             !max_observed_nice.compare_exchange_weak(prev, nice)) {
      }
      if (check_started.fetch_add(1) + 1 == 4) {
        all_started.Signal();
      }
      release_all.Wait();
    }));
  }
  bool all_running = all_started.WaitFor(base::TimeDelta::FromSeconds(10));
  for (int i = 0; i < 4; ++i) {
    release_all.Signal();
  }
  high_runner.Terminate();
  ASSERT_TRUE(all_running);
  ASSERT_EQ(expected_nice, max_observed_nice.load());
}
#endif  // V8_OS_LINUX

#if V8_HAS_PKU_SUPPORT
TEST(DefaultWorkerThreadsTaskRunnerUnittest, LazyWorkerRestoresBaselinePkru) {
  if (!base::MemoryProtectionKey::TestKeyAllocation()) {
    GTEST_SKIP();
  }
  int key = base::MemoryProtectionKey::AllocateKey();
  if (key == base::MemoryProtectionKey::kNoMemoryProtectionKey) {
    GTEST_SKIP();
  }

  base::Semaphore worker1_blocked(0);
  base::Semaphore release_worker1(0);
  base::Semaphore worker2_done(0);
  base::MemoryProtectionKey::Permission observed_perm =
      base::MemoryProtectionKey::kDisableAccess;
  // Constructor captures baseline PKRU (with |key| unrestricted).
  DefaultWorkerThreadsTaskRunner runner(2, RealTime);

  runner.PostTask(std::make_unique<TestTask>([&] {
    worker1_blocked.Signal();
    release_worker1.Wait();
  }));
  worker1_blocked.Wait();

  // Simulate DisallowSandboxAccess on the posting thread while spawning Worker
  // 2 lazily; Worker 2 must restore constructor baseline PKRU instead of
  // inheriting kDisableAccess.
  base::MemoryProtectionKey::SetPermissionsForKey(
      key, base::MemoryProtectionKey::kDisableAccess);
  runner.PostTask(std::make_unique<TestTask>([&] {
    observed_perm = base::MemoryProtectionKey::GetKeyPermission(key);
    worker2_done.Signal();
  }));
  base::MemoryProtectionKey::SetPermissionsForKey(
      key, base::MemoryProtectionKey::kNoRestrictions);

  bool ran = worker2_done.WaitFor(base::TimeDelta::FromSeconds(10));
  release_worker1.Signal();
  runner.Terminate();
  base::MemoryProtectionKey::FreeKey(key);

  ASSERT_TRUE(ran);
  ASSERT_EQ(base::MemoryProtectionKey::kNoRestrictions, observed_perm);
}
#endif  // V8_HAS_PKU_SUPPORT

}  // namespace platform
}  // namespace v8
