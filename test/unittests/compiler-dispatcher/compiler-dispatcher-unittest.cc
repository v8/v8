// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler-dispatcher/compiler-dispatcher.h"

#include "include/v8-platform.h"
#include "src/compiler-dispatcher/compiler-dispatcher-job.h"
#include "src/flags.h"
#include "src/handles.h"
#include "src/objects-inl.h"
#include "test/unittests/compiler-dispatcher/compiler-dispatcher-helper.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

typedef TestWithContext CompilerDispatcherTest;

namespace {

class MockPlatform : public v8::Platform {
 public:
  MockPlatform() : task_(nullptr), time_(0.0), time_step_(0.0) {}
  ~MockPlatform() override = default;

  void CallOnBackgroundThread(Task* task,
                              ExpectedRuntime expected_runtime) override {
    UNREACHABLE();
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
    task_ = task;
  }

  bool IdleTasksEnabled(v8::Isolate* isolate) override { return true; }

  double MonotonicallyIncreasingTime() override {
    time_ += time_step_;
    return time_;
  }

  void RunIdleTask(double deadline_in_seconds, double time_step) {
    ASSERT_TRUE(task_ != nullptr);
    time_step_ = time_step;
    IdleTask* task = task_;
    task_ = nullptr;
    task->Run(deadline_in_seconds);
    delete task;
  }

  bool IdleTaskPending() const { return !!task_; }

 private:
  IdleTask* task_;
  double time_;
  double time_step_;

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
  dispatcher.Abort(shared, CompilerDispatcher::BlockingBehavior::kBlock);
  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
}

TEST_F(CompilerDispatcherTest, FinishNow) {
  MockPlatform platform;
  CompilerDispatcher dispatcher(i_isolate(), &platform, FLAG_stack_size);

  const char script[] =
      "function g() { var y = 1; function f2(x) { return x * y }; return f2; } "
      "g();";
  Handle<JSFunction> f = Handle<JSFunction>::cast(RunJS(isolate(), script));
  Handle<SharedFunctionInfo> shared(f->shared(), i_isolate());

  ASSERT_FALSE(shared->HasBaselineCode());
  ASSERT_TRUE(dispatcher.Enqueue(shared));
  ASSERT_TRUE(dispatcher.FinishNow(shared));
  // Finishing removes the SFI from the queue.
  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_TRUE(shared->HasBaselineCode());
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
  ASSERT_TRUE(shared->HasBaselineCode());
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
  ASSERT_FALSE(shared->HasBaselineCode());
  ASSERT_TRUE(platform.IdleTaskPending());

  // The job should be still scheduled for the main thread, but ready for
  // parsing.
  ASSERT_EQ(dispatcher.jobs_.size(), 1u);
  ASSERT_TRUE(dispatcher.jobs_.begin()->second->status() ==
              CompileJobStatus::kReadyToParse);

  // Only grant a lot of idle time and freeze time.
  platform.RunIdleTask(1000.0, 0.0);

  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_TRUE(shared->HasBaselineCode());
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
  ASSERT_FALSE(shared->HasBaselineCode());
  ASSERT_FALSE(try_catch.HasCaught());
}

}  // namespace internal
}  // namespace v8
