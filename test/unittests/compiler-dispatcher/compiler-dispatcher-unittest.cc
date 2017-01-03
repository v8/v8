// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler-dispatcher/compiler-dispatcher.h"

#include "include/v8-platform.h"
#include "src/base/platform/semaphore.h"
#include "src/compiler-dispatcher/compiler-dispatcher-job.h"
#include "src/compiler-dispatcher/compiler-dispatcher-tracer.h"
#include "src/flags.h"
#include "src/handles.h"
#include "src/objects-inl.h"
#include "src/v8.h"
#include "test/unittests/compiler-dispatcher/compiler-dispatcher-helper.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

class CompilerDispatcherTest : public TestWithContext {
 public:
  CompilerDispatcherTest() = default;
  ~CompilerDispatcherTest() override = default;

  static void SetUpTestCase() {
    old_flag_ = i::FLAG_ignition;
    i::FLAG_compiler_dispatcher = true;
    TestWithContext::SetUpTestCase();
  }

  static void TearDownTestCase() {
    TestWithContext::TearDownTestCase();
    i::FLAG_compiler_dispatcher = old_flag_;
  }

 private:
  static bool old_flag_;

  DISALLOW_COPY_AND_ASSIGN(CompilerDispatcherTest);
};

bool CompilerDispatcherTest::old_flag_;

class IgnitionCompilerDispatcherTest : public CompilerDispatcherTest {
 public:
  IgnitionCompilerDispatcherTest() = default;
  ~IgnitionCompilerDispatcherTest() override = default;

  static void SetUpTestCase() {
    old_flag_ = i::FLAG_ignition;
    i::FLAG_ignition = true;
    CompilerDispatcherTest::SetUpTestCase();
  }

  static void TearDownTestCase() {
    CompilerDispatcherTest::TearDownTestCase();
    i::FLAG_ignition = old_flag_;
  }

 private:
  static bool old_flag_;
  DISALLOW_COPY_AND_ASSIGN(IgnitionCompilerDispatcherTest);
};

bool IgnitionCompilerDispatcherTest::old_flag_;

namespace {

class MockPlatform : public v8::Platform {
 public:
  MockPlatform() : idle_task_(nullptr), time_(0.0), time_step_(0.0), sem_(0) {}
  ~MockPlatform() override {
    EXPECT_TRUE(tasks_.empty());
    EXPECT_TRUE(idle_task_ == nullptr);
  }

  size_t NumberOfAvailableBackgroundThreads() override { return 1; }

  void CallOnBackgroundThread(Task* task,
                              ExpectedRuntime expected_runtime) override {
    tasks_.push_back(task);
  }

  void CallOnForegroundThread(v8::Isolate* isolate, Task* task) override {
    UNREACHABLE();
  }

  void CallDelayedOnForegroundThread(v8::Isolate* isolate, Task* task,
                                     double delay_in_seconds) override {
    UNREACHABLE();
  }

  void CallIdleOnForegroundThread(v8::Isolate* isolate,
                                  IdleTask* task) override {
    ASSERT_TRUE(idle_task_ == nullptr);
    idle_task_ = task;
  }

  bool IdleTasksEnabled(v8::Isolate* isolate) override { return true; }

  double MonotonicallyIncreasingTime() override {
    time_ += time_step_;
    return time_;
  }

  void RunIdleTask(double deadline_in_seconds, double time_step) {
    ASSERT_TRUE(idle_task_ != nullptr);
    time_step_ = time_step;
    IdleTask* task = idle_task_;
    idle_task_ = nullptr;
    task->Run(deadline_in_seconds);
    delete task;
  }

  bool IdleTaskPending() const { return idle_task_; }

  bool BackgroundTasksPending() const { return !tasks_.empty(); }

  void RunBackgroundTasksAndBlock(Platform* platform) {
    std::vector<Task*> tasks;
    tasks.swap(tasks_);
    platform->CallOnBackgroundThread(new TaskWrapper(this, tasks, true),
                                     kShortRunningTask);
    sem_.Wait();
  }

  void RunBackgroundTasks(Platform* platform) {
    std::vector<Task*> tasks;
    tasks.swap(tasks_);
    platform->CallOnBackgroundThread(new TaskWrapper(this, tasks, false),
                                     kShortRunningTask);
  }

  void ClearBackgroundTasks() {
    std::vector<Task*> tasks;
    tasks.swap(tasks_);
    for (auto& task : tasks) {
      delete task;
    }
  }

  void ClearIdleTask() {
    ASSERT_TRUE(idle_task_ != nullptr);
    delete idle_task_;
    idle_task_ = nullptr;
  }

 private:
  class TaskWrapper : public Task {
   public:
    TaskWrapper(MockPlatform* platform, const std::vector<Task*>& tasks,
                bool signal)
        : platform_(platform), tasks_(tasks), signal_(signal) {}
    ~TaskWrapper() = default;

    void Run() override {
      for (auto& task : tasks_) {
        task->Run();
        delete task;
      }
      if (signal_) platform_->sem_.Signal();
    }

   private:
    MockPlatform* platform_;
    std::vector<Task*> tasks_;
    bool signal_;

    DISALLOW_COPY_AND_ASSIGN(TaskWrapper);
  };

  IdleTask* idle_task_;
  double time_;
  double time_step_;

  std::vector<Task*> tasks_;
  base::Semaphore sem_;

  DISALLOW_COPY_AND_ASSIGN(MockPlatform);
};

}  // namespace

TEST_F(CompilerDispatcherTest, Construct) {
  MockPlatform platform;
  CompilerDispatcher dispatcher(i_isolate(), &platform, FLAG_stack_size);
}

TEST_F(CompilerDispatcherTest, IsEnqueued) {
  MockPlatform platform;
  CompilerDispatcher dispatcher(i_isolate(), &platform, FLAG_stack_size);

  const char script[] =
      "function g() { var y = 1; function f1(x) { return x * y }; return f1; } "
      "g();";
  Handle<JSFunction> f = Handle<JSFunction>::cast(RunJS(isolate(), script));
  Handle<SharedFunctionInfo> shared(f->shared(), i_isolate());

  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_TRUE(dispatcher.Enqueue(shared));
  ASSERT_TRUE(dispatcher.IsEnqueued(shared));
  dispatcher.AbortAll(CompilerDispatcher::BlockingBehavior::kBlock);
  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_TRUE(platform.IdleTaskPending());
  platform.ClearIdleTask();
}

TEST_F(CompilerDispatcherTest, FinishNow) {
  MockPlatform platform;
  CompilerDispatcher dispatcher(i_isolate(), &platform, FLAG_stack_size);

  const char script[] =
      "function g() { var y = 1; function f2(x) { return x * y }; return f2; } "
      "g();";
  Handle<JSFunction> f = Handle<JSFunction>::cast(RunJS(isolate(), script));
  Handle<SharedFunctionInfo> shared(f->shared(), i_isolate());

  ASSERT_FALSE(shared->is_compiled());
  ASSERT_TRUE(dispatcher.Enqueue(shared));
  ASSERT_TRUE(dispatcher.FinishNow(shared));
  // Finishing removes the SFI from the queue.
  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_TRUE(shared->is_compiled());
  ASSERT_TRUE(platform.IdleTaskPending());
  platform.ClearIdleTask();
}

TEST_F(CompilerDispatcherTest, IdleTask) {
  MockPlatform platform;
  CompilerDispatcher dispatcher(i_isolate(), &platform, FLAG_stack_size);

  const char script[] =
      "function g() { var y = 1; function f3(x) { return x * y }; return f3; } "
      "g();";
  Handle<JSFunction> f = Handle<JSFunction>::cast(RunJS(isolate(), script));
  Handle<SharedFunctionInfo> shared(f->shared(), i_isolate());

  ASSERT_FALSE(platform.IdleTaskPending());
  ASSERT_TRUE(dispatcher.Enqueue(shared));
  ASSERT_TRUE(platform.IdleTaskPending());

  // Since time doesn't progress on the MockPlatform, this is enough idle time
  // to finish compiling the function.
  platform.RunIdleTask(1000.0, 0.0);

  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_TRUE(shared->is_compiled());
}

TEST_F(CompilerDispatcherTest, IdleTaskSmallIdleTime) {
  MockPlatform platform;
  CompilerDispatcher dispatcher(i_isolate(), &platform, FLAG_stack_size);

  const char script[] =
      "function g() { var y = 1; function f4(x) { return x * y }; return f4; } "
      "g();";
  Handle<JSFunction> f = Handle<JSFunction>::cast(RunJS(isolate(), script));
  Handle<SharedFunctionInfo> shared(f->shared(), i_isolate());

  ASSERT_FALSE(platform.IdleTaskPending());
  ASSERT_TRUE(dispatcher.Enqueue(shared));
  ASSERT_TRUE(platform.IdleTaskPending());

  // The job should be scheduled for the main thread.
  ASSERT_EQ(dispatcher.jobs_.size(), 1u);
  ASSERT_TRUE(dispatcher.jobs_.begin()->second->status() ==
              CompileJobStatus::kInitial);

  // Only grant a little idle time and have time advance beyond it in one step.
  platform.RunIdleTask(2.0, 1.0);

  ASSERT_TRUE(dispatcher.IsEnqueued(shared));
  ASSERT_FALSE(shared->is_compiled());
  ASSERT_TRUE(platform.IdleTaskPending());

  // The job should be still scheduled for the main thread, but ready for
  // parsing.
  ASSERT_EQ(dispatcher.jobs_.size(), 1u);
  ASSERT_TRUE(dispatcher.jobs_.begin()->second->status() ==
              CompileJobStatus::kReadyToParse);

  // Now grant a lot of idle time and freeze time.
  platform.RunIdleTask(1000.0, 0.0);

  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_TRUE(shared->is_compiled());
  ASSERT_FALSE(platform.IdleTaskPending());
}

TEST_F(CompilerDispatcherTest, IdleTaskException) {
  MockPlatform platform;
  CompilerDispatcher dispatcher(i_isolate(), &platform, 50);

  std::string script("function g() { function f5(x) { var a = ");
  for (int i = 0; i < 1000; i++) {
    script += "'x' + ";
  }
  script += " 'x'; }; return f5; } g();";
  Handle<JSFunction> f =
      Handle<JSFunction>::cast(RunJS(isolate(), script.c_str()));
  Handle<SharedFunctionInfo> shared(f->shared(), i_isolate());

  ASSERT_FALSE(platform.IdleTaskPending());
  ASSERT_TRUE(dispatcher.Enqueue(shared));
  ASSERT_TRUE(platform.IdleTaskPending());

  // Idle tasks shouldn't leave exceptions behind.
  v8::TryCatch try_catch(isolate());

  // Since time doesn't progress on the MockPlatform, this is enough idle time
  // to finish compiling the function.
  platform.RunIdleTask(1000.0, 0.0);

  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_FALSE(shared->is_compiled());
  ASSERT_FALSE(try_catch.HasCaught());
}

TEST_F(IgnitionCompilerDispatcherTest, CompileOnBackgroundThread) {
  MockPlatform platform;
  CompilerDispatcher dispatcher(i_isolate(), &platform, FLAG_stack_size);

  const char script[] =
      "function g() { var y = 1; function f6(x) { return x * y }; return f6; } "
      "g();";
  Handle<JSFunction> f = Handle<JSFunction>::cast(RunJS(isolate(), script));
  Handle<SharedFunctionInfo> shared(f->shared(), i_isolate());

  ASSERT_FALSE(platform.IdleTaskPending());
  ASSERT_TRUE(dispatcher.Enqueue(shared));
  ASSERT_TRUE(platform.IdleTaskPending());

  ASSERT_EQ(dispatcher.jobs_.size(), 1u);
  ASSERT_TRUE(dispatcher.jobs_.begin()->second->status() ==
              CompileJobStatus::kInitial);

  // Make compiling super expensive, and advance job as much as possible on the
  // foreground thread.
  dispatcher.tracer_->RecordCompile(50000.0, 1);
  platform.RunIdleTask(10.0, 0.0);
  ASSERT_TRUE(dispatcher.jobs_.begin()->second->status() ==
              CompileJobStatus::kReadyToCompile);

  ASSERT_TRUE(dispatcher.IsEnqueued(shared));
  ASSERT_FALSE(shared->is_compiled());
  ASSERT_FALSE(platform.IdleTaskPending());
  ASSERT_TRUE(platform.BackgroundTasksPending());

  platform.RunBackgroundTasksAndBlock(V8::GetCurrentPlatform());

  ASSERT_TRUE(platform.IdleTaskPending());
  ASSERT_FALSE(platform.BackgroundTasksPending());
  ASSERT_TRUE(dispatcher.jobs_.begin()->second->status() ==
              CompileJobStatus::kCompiled);

  // Now grant a lot of idle time and freeze time.
  platform.RunIdleTask(1000.0, 0.0);

  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_TRUE(shared->is_compiled());
  ASSERT_FALSE(platform.IdleTaskPending());
}

TEST_F(IgnitionCompilerDispatcherTest, FinishNowWithBackgroundTask) {
  MockPlatform platform;
  CompilerDispatcher dispatcher(i_isolate(), &platform, FLAG_stack_size);

  const char script[] =
      "function g() { var y = 1; function f7(x) { return x * y }; return f7; } "
      "g();";
  Handle<JSFunction> f = Handle<JSFunction>::cast(RunJS(isolate(), script));
  Handle<SharedFunctionInfo> shared(f->shared(), i_isolate());

  ASSERT_FALSE(platform.IdleTaskPending());
  ASSERT_TRUE(dispatcher.Enqueue(shared));
  ASSERT_TRUE(platform.IdleTaskPending());

  ASSERT_EQ(dispatcher.jobs_.size(), 1u);
  ASSERT_TRUE(dispatcher.jobs_.begin()->second->status() ==
              CompileJobStatus::kInitial);

  // Make compiling super expensive, and advance job as much as possible on the
  // foreground thread.
  dispatcher.tracer_->RecordCompile(50000.0, 1);
  platform.RunIdleTask(10.0, 0.0);
  ASSERT_TRUE(dispatcher.jobs_.begin()->second->status() ==
              CompileJobStatus::kReadyToCompile);

  ASSERT_TRUE(dispatcher.IsEnqueued(shared));
  ASSERT_FALSE(shared->is_compiled());
  ASSERT_FALSE(platform.IdleTaskPending());
  ASSERT_TRUE(platform.BackgroundTasksPending());

  // This does not block, but races with the FinishNow() call below.
  platform.RunBackgroundTasks(V8::GetCurrentPlatform());

  ASSERT_TRUE(dispatcher.FinishNow(shared));
  // Finishing removes the SFI from the queue.
  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_TRUE(shared->is_compiled());
  if (platform.IdleTaskPending()) platform.ClearIdleTask();
  ASSERT_FALSE(platform.BackgroundTasksPending());
}

}  // namespace internal
}  // namespace v8
