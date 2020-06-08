// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/gc-invoker.h"

#include "include/cppgc/platform.h"
#include "src/heap/cppgc/heap.h"
#include "testing/gmock/include/gmock/gmock-matchers.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

class MockGarbageCollector : public GarbageCollector {
 public:
  MOCK_METHOD1(CollectGarbage, void(GarbageCollector::Config));
  MOCK_CONST_METHOD0(epoch, size_t());
};

class MockTaskRunner : public cppgc::TaskRunner {
 public:
  MOCK_METHOD1(PostTask, void(std::unique_ptr<cppgc::Task>));
  MOCK_METHOD1(PostNonNestableTask, void(std::unique_ptr<cppgc::Task>));
  MOCK_METHOD2(PostDelayedTask, void(std::unique_ptr<cppgc::Task>, double));
  MOCK_METHOD2(PostNonNestableDelayedTask,
               void(std::unique_ptr<cppgc::Task>, double));
  MOCK_METHOD1(PostIdleTask, void(std::unique_ptr<cppgc::IdleTask>));

  virtual bool IdleTasksEnabled() { return true; }               // NOLINT
  virtual bool NonNestableTasksEnabled() const { return true; }  // NOLINT
  virtual bool NonNestableDelayedTasksEnabled() const {          // NOLINT
    return true;
  }
};

class MockPlatform : public cppgc::Platform {
 public:
  explicit MockPlatform(std::shared_ptr<TaskRunner> runner)
      : runner_(std::move(runner)) {}

  PageAllocator* GetPageAllocator() override { return nullptr; }
  double MonotonicallyIncreasingTime() override { return 0.0; }

  std::shared_ptr<TaskRunner> GetForegroundTaskRunner() override {
    return runner_;
  }

 private:
  std::shared_ptr<TaskRunner> runner_;
};

}  // namespace

TEST(GCInvokerTest, PrecideGCIsInvokedSynchronously) {
  MockPlatform platform(nullptr);
  MockGarbageCollector gc;
  GCInvoker invoker(&gc, &platform,
                    cppgc::Heap::StackSupport::kNoConservativeStackScan);
  EXPECT_CALL(gc, CollectGarbage(::testing::Field(
                      &GarbageCollector::Config::stack_state,
                      GarbageCollector::Config::StackState::kNoHeapPointers)));
  invoker.CollectGarbage(GarbageCollector::Config::PreciseAtomicConfig());
}

TEST(GCInvokerTest, ConservativeGCIsInvokedSynchronouslyWhenSupported) {
  MockPlatform platform(nullptr);
  MockGarbageCollector gc;
  GCInvoker invoker(&gc, &platform,
                    cppgc::Heap::StackSupport::kSupportsConservativeStackScan);
  EXPECT_CALL(
      gc, CollectGarbage(::testing::Field(
              &GarbageCollector::Config::stack_state,
              GarbageCollector::Config::StackState::kMayContainHeapPointers)));
  invoker.CollectGarbage(GarbageCollector::Config::ConservativeAtomicConfig());
}

TEST(GCInvokerTest, ConservativeGCIsInvokedAsPreciseGCViaPlatform) {
  std::shared_ptr<cppgc::TaskRunner> runner =
      std::shared_ptr<cppgc::TaskRunner>(new MockTaskRunner());
  MockPlatform platform(runner);
  MockGarbageCollector gc;
  GCInvoker invoker(&gc, &platform,
                    cppgc::Heap::StackSupport::kNoConservativeStackScan);
  EXPECT_CALL(gc, epoch).WillOnce(::testing::Return(0));
  EXPECT_CALL(*static_cast<MockTaskRunner*>(runner.get()),
              PostNonNestableTask(::testing::_));
  invoker.CollectGarbage(GarbageCollector::Config::ConservativeAtomicConfig());
}

}  // namespace internal
}  // namespace cppgc
