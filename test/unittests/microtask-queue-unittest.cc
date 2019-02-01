// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/microtask-queue.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "src/heap/factory.h"
#include "src/objects-inl.h"
#include "src/objects/foreign.h"
#include "src/objects/js-array-inl.h"
#include "src/visitors.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using Closure = std::function<void()>;

void RunStdFunction(void* data) {
  std::unique_ptr<Closure> f(static_cast<Closure*>(data));
  (*f)();
}

template <typename TMixin>
class WithFinalizationGroupMixin : public TMixin {
 public:
  WithFinalizationGroupMixin() {
    FLAG_harmony_weak_refs = true;
    FLAG_expose_gc = true;
  }

 private:
  SaveFlags save_flags_;

  DISALLOW_COPY_AND_ASSIGN(WithFinalizationGroupMixin);
};

using TestWithNativeContextAndFinalizationGroup =  //
    WithInternalIsolateMixin<                      //
        WithContextMixin<                          //
            WithFinalizationGroupMixin<            //
                WithIsolateScopeMixin<             //
                    WithSharedIsolateMixin<        //
                        ::testing::Test>>>>>;

class MicrotaskQueueTest : public TestWithNativeContextAndFinalizationGroup {
 public:
  template <typename F>
  Handle<Microtask> NewMicrotask(F&& f) {
    Handle<Foreign> runner =
        factory()->NewForeign(reinterpret_cast<Address>(&RunStdFunction));
    Handle<Foreign> data = factory()->NewForeign(
        reinterpret_cast<Address>(new Closure(std::forward<F>(f))));
    return factory()->NewCallbackTask(runner, data);
  }

  void SetUp() override {
    microtask_queue_ = MicrotaskQueue::New(isolate());
    native_context()->set_microtask_queue(microtask_queue());
  }

  void TearDown() override {
    if (microtask_queue()) {
      microtask_queue()->RunMicrotasks(isolate());
      context()->DetachGlobal();
    }
  }

  MicrotaskQueue* microtask_queue() const { return microtask_queue_.get(); }

  void ClearTestMicrotaskQueue() {
    context()->DetachGlobal();
    microtask_queue_ = nullptr;
  }

 private:
  std::unique_ptr<MicrotaskQueue> microtask_queue_;
};

class RecordingVisitor : public RootVisitor {
 public:
  RecordingVisitor() = default;
  ~RecordingVisitor() override = default;

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    for (FullObjectSlot current = start; current != end; ++current) {
      visited_.push_back(*current);
    }
  }

  const std::vector<Object>& visited() const { return visited_; }

 private:
  std::vector<Object> visited_;
};

// Sanity check. Ensure a microtask is stored in a queue and run.
TEST_F(MicrotaskQueueTest, EnqueueAndRun) {
  bool ran = false;
  EXPECT_EQ(0, microtask_queue()->capacity());
  EXPECT_EQ(0, microtask_queue()->size());
  microtask_queue()->EnqueueMicrotask(*NewMicrotask([&ran] {
    EXPECT_FALSE(ran);
    ran = true;
  }));
  EXPECT_EQ(MicrotaskQueue::kMinimumCapacity, microtask_queue()->capacity());
  EXPECT_EQ(1, microtask_queue()->size());
  EXPECT_EQ(1, microtask_queue()->RunMicrotasks(isolate()));
  EXPECT_TRUE(ran);
  EXPECT_EQ(0, microtask_queue()->size());
}

// Check for a buffer growth.
TEST_F(MicrotaskQueueTest, BufferGrowth) {
  int count = 0;

  // Enqueue and flush the queue first to have non-zero |start_|.
  microtask_queue()->EnqueueMicrotask(
      *NewMicrotask([&count] { EXPECT_EQ(0, count++); }));
  EXPECT_EQ(1, microtask_queue()->RunMicrotasks(isolate()));

  EXPECT_LT(0, microtask_queue()->capacity());
  EXPECT_EQ(0, microtask_queue()->size());
  EXPECT_EQ(1, microtask_queue()->start());

  // Fill the queue with Microtasks.
  for (int i = 1; i <= MicrotaskQueue::kMinimumCapacity; ++i) {
    microtask_queue()->EnqueueMicrotask(
        *NewMicrotask([&count, i] { EXPECT_EQ(i, count++); }));
  }
  EXPECT_EQ(MicrotaskQueue::kMinimumCapacity, microtask_queue()->capacity());
  EXPECT_EQ(MicrotaskQueue::kMinimumCapacity, microtask_queue()->size());

  // Add another to grow the ring buffer.
  microtask_queue()->EnqueueMicrotask(*NewMicrotask(
      [&] { EXPECT_EQ(MicrotaskQueue::kMinimumCapacity + 1, count++); }));

  EXPECT_LT(MicrotaskQueue::kMinimumCapacity, microtask_queue()->capacity());
  EXPECT_EQ(MicrotaskQueue::kMinimumCapacity + 1, microtask_queue()->size());

  // Run all pending Microtasks to ensure they run in the proper order.
  EXPECT_EQ(MicrotaskQueue::kMinimumCapacity + 1,
            microtask_queue()->RunMicrotasks(isolate()));
  EXPECT_EQ(MicrotaskQueue::kMinimumCapacity + 2, count);
}

// MicrotaskQueue instances form a doubly linked list.
TEST_F(MicrotaskQueueTest, InstanceChain) {
  ClearTestMicrotaskQueue();

  MicrotaskQueue* default_mtq = isolate()->default_microtask_queue();
  ASSERT_TRUE(default_mtq);
  EXPECT_EQ(default_mtq, default_mtq->next());
  EXPECT_EQ(default_mtq, default_mtq->prev());

  // Create two instances, and check their connection.
  // The list contains all instances in the creation order, and the next of the
  // last instance is the first instance:
  //   default_mtq -> mtq1 -> mtq2 -> default_mtq.
  std::unique_ptr<MicrotaskQueue> mtq1 = MicrotaskQueue::New(isolate());
  std::unique_ptr<MicrotaskQueue> mtq2 = MicrotaskQueue::New(isolate());
  EXPECT_EQ(default_mtq->next(), mtq1.get());
  EXPECT_EQ(mtq1->next(), mtq2.get());
  EXPECT_EQ(mtq2->next(), default_mtq);
  EXPECT_EQ(default_mtq, mtq1->prev());
  EXPECT_EQ(mtq1.get(), mtq2->prev());
  EXPECT_EQ(mtq2.get(), default_mtq->prev());

  // Deleted item should be also removed from the list.
  mtq1 = nullptr;
  EXPECT_EQ(default_mtq->next(), mtq2.get());
  EXPECT_EQ(mtq2->next(), default_mtq);
  EXPECT_EQ(default_mtq, mtq2->prev());
  EXPECT_EQ(mtq2.get(), default_mtq->prev());
}

// Pending Microtasks in MicrotaskQueues are strong roots. Ensure they are
// visited exactly once.
TEST_F(MicrotaskQueueTest, VisitRoot) {
  // Ensure that the ring buffer has separate in-use region.
  for (int i = 0; i < MicrotaskQueue::kMinimumCapacity / 2 + 1; ++i) {
    microtask_queue()->EnqueueMicrotask(*NewMicrotask([] {}));
  }
  EXPECT_EQ(MicrotaskQueue::kMinimumCapacity / 2 + 1,
            microtask_queue()->RunMicrotasks(isolate()));

  std::vector<Object> expected;
  for (int i = 0; i < MicrotaskQueue::kMinimumCapacity / 2 + 1; ++i) {
    Handle<Microtask> microtask = NewMicrotask([] {});
    expected.push_back(*microtask);
    microtask_queue()->EnqueueMicrotask(*microtask);
  }
  EXPECT_GT(microtask_queue()->start() + microtask_queue()->size(),
            microtask_queue()->capacity());

  RecordingVisitor visitor;
  microtask_queue()->IterateMicrotasks(&visitor);

  std::vector<Object> actual = visitor.visited();
  std::sort(expected.begin(), expected.end());
  std::sort(actual.begin(), actual.end());
  EXPECT_EQ(expected, actual);
}

TEST_F(MicrotaskQueueTest, DetachGlobal_Enqueue) {
  EXPECT_EQ(0, microtask_queue()->size());

  // Detach MicrotaskQueue from the current context.
  context()->DetachGlobal();

  // No microtask should be enqueued after DetachGlobal call.
  EXPECT_EQ(0, microtask_queue()->size());
  RunJS("Promise.resolve().then(()=>{})");
  EXPECT_EQ(0, microtask_queue()->size());
}

TEST_F(MicrotaskQueueTest, DetachGlobal_Run) {
  EXPECT_EQ(0, microtask_queue()->size());

  // Enqueue microtasks to the current context.
  Handle<JSArray> ran = RunJS<JSArray>(
      "var ran = [false, false, false, false];"
      "Promise.resolve().then(() => { ran[0] = true; });"
      "Promise.reject().catch(() => { ran[1] = true; });"
      "ran");

  Handle<JSFunction> function =
      RunJS<JSFunction>("(function() { ran[2] = true; })");
  Handle<CallableTask> callable =
      factory()->NewCallableTask(function, Utils::OpenHandle(*context()));
  microtask_queue()->EnqueueMicrotask(*callable);

  // The handler should not run at this point.
  const int kNumExpectedTasks = 3;
  for (int i = 0; i < kNumExpectedTasks; ++i) {
    EXPECT_TRUE(
        Object::GetElement(isolate(), ran, i).ToHandleChecked()->IsFalse());
  }
  EXPECT_EQ(kNumExpectedTasks, microtask_queue()->size());

  // Detach MicrotaskQueue from the current context.
  context()->DetachGlobal();

  // RunMicrotasks processes pending Microtasks, but Microtasks that are
  // associated to a detached context should be cancelled and should not take
  // effect.
  microtask_queue()->RunMicrotasks(isolate());
  EXPECT_EQ(0, microtask_queue()->size());
  for (int i = 0; i < kNumExpectedTasks; ++i) {
    EXPECT_TRUE(
        Object::GetElement(isolate(), ran, i).ToHandleChecked()->IsFalse());
  }
}

TEST_F(MicrotaskQueueTest, DetachGlobal_FinalizationGroup) {
  // Enqueue an FinalizationGroupCleanupTask.
  Handle<JSArray> ran = RunJS<JSArray>(
      "var ran = [false];"
      "var wf = new FinalizationGroup(() => { ran[0] = true; });"
      "(function() { wf.register({}, {}); })();"
      "gc();"
      "ran");

  EXPECT_TRUE(
      Object::GetElement(isolate(), ran, 0).ToHandleChecked()->IsFalse());
  EXPECT_EQ(1, microtask_queue()->size());

  // Detach MicrotaskQueue from the current context.
  context()->DetachGlobal();

  microtask_queue()->RunMicrotasks(isolate());

  // RunMicrotasks processes the pending Microtask, but Microtasks that are
  // associated to a detached context should be cancelled and should not take
  // effect.
  EXPECT_EQ(0, microtask_queue()->size());
  EXPECT_TRUE(
      Object::GetElement(isolate(), ran, 0).ToHandleChecked()->IsFalse());
}

namespace {

void DummyPromiseHook(PromiseHookType type, Local<Promise> promise,
                      Local<Value> parent) {}

}  // namespace

TEST_F(MicrotaskQueueTest, DetachGlobal_PromiseResolveThenableJobTask) {
  // Use a PromiseHook to switch the implementation to ResolvePromise runtime,
  // instead of ResolvePromise builtin.
  v8_isolate()->SetPromiseHook(&DummyPromiseHook);

  RunJS(
      "var resolve;"
      "var promise = new Promise(r => { resolve = r; });"
      "promise.then(() => {});"
      "resolve({});");

  // A PromiseResolveThenableJobTask is pending in the MicrotaskQueue.
  EXPECT_EQ(1, microtask_queue()->size());

  // Detach MicrotaskQueue from the current context.
  context()->DetachGlobal();

  // RunMicrotasks processes the pending Microtask, but Microtasks that are
  // associated to a detached context should be cancelled and should not take
  // effect.
  // As PromiseResolveThenableJobTask queues another task for resolution,
  // the return value is 2 if it ran.
  EXPECT_EQ(1, microtask_queue()->RunMicrotasks(isolate()));
  EXPECT_EQ(0, microtask_queue()->size());
}

}  // namespace internal
}  // namespace v8
