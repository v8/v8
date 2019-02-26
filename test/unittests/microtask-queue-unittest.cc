// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/microtask-queue.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "src/heap/factory.h"
#include "src/objects/foreign.h"
#include "src/objects/promise-inl.h"
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

class MicrotaskQueueTest : public TestWithNativeContext {
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

TEST_F(MicrotaskQueueTest, PromiseHandlerContext) {
  Local<v8::Context> v8_context2 = v8::Context::New(v8_isolate());
  Local<v8::Context> v8_context3 = v8::Context::New(v8_isolate());
  Local<v8::Context> v8_context4 = v8::Context::New(v8_isolate());
  Handle<Context> context2 = Utils::OpenHandle(*v8_context2, isolate());
  Handle<Context> context3 = Utils::OpenHandle(*v8_context3, isolate());
  Handle<Context> context4 = Utils::OpenHandle(*v8_context3, isolate());
  context2->native_context()->set_microtask_queue(microtask_queue());
  context3->native_context()->set_microtask_queue(microtask_queue());
  context4->native_context()->set_microtask_queue(microtask_queue());

  Handle<JSFunction> handler;
  Handle<JSProxy> proxy;
  Handle<JSProxy> revoked_proxy;
  Handle<JSBoundFunction> bound;

  // Create a JSFunction on |context2|
  {
    v8::Context::Scope scope(v8_context2);
    handler = RunJS<JSFunction>("()=>{}");
    EXPECT_EQ(*context2,
              *JSReceiver::GetContextForMicrotask(handler).ToHandleChecked());
  }

  // Create a JSProxy on |context3|.
  {
    v8::Context::Scope scope(v8_context3);
    ASSERT_TRUE(
        v8_context3->Global()
            ->Set(v8_context3, NewString("handler"), Utils::ToLocal(handler))
            .FromJust());
    proxy = RunJS<JSProxy>("new Proxy(handler, {})");
    revoked_proxy = RunJS<JSProxy>(
        "let {proxy, revoke} = Proxy.revocable(handler, {});"
        "revoke();"
        "proxy");
    EXPECT_EQ(*context2,
              *JSReceiver::GetContextForMicrotask(proxy).ToHandleChecked());
    EXPECT_TRUE(JSReceiver::GetContextForMicrotask(revoked_proxy).is_null());
  }

  // Create a JSBoundFunction on |context4|.
  // Note that its CreationContext and ContextForTaskCancellation is |context2|.
  {
    v8::Context::Scope scope(v8_context4);
    ASSERT_TRUE(
        v8_context4->Global()
            ->Set(v8_context4, NewString("handler"), Utils::ToLocal(handler))
            .FromJust());
    bound = RunJS<JSBoundFunction>("handler.bind()");
    EXPECT_EQ(*context2,
              *JSReceiver::GetContextForMicrotask(bound).ToHandleChecked());
  }

  // Give the objects to the main context.
  SetGlobalProperty("handler", Utils::ToLocal(handler));
  SetGlobalProperty("proxy", Utils::ToLocal(proxy));
  SetGlobalProperty("revoked_proxy", Utils::ToLocal(revoked_proxy));
  SetGlobalProperty("bound", Utils::ToLocal(Handle<JSReceiver>::cast(bound)));
  RunJS(
      "Promise.resolve().then(handler);"
      "Promise.reject().catch(proxy);"
      "Promise.resolve().then(revoked_proxy);"
      "Promise.resolve().then(bound);");

  ASSERT_EQ(4, microtask_queue()->size());
  Handle<Microtask> microtask1(microtask_queue()->get(0), isolate());
  ASSERT_TRUE(microtask1->IsPromiseFulfillReactionJobTask());
  EXPECT_EQ(*context2,
            Handle<PromiseFulfillReactionJobTask>::cast(microtask1)->context());

  Handle<Microtask> microtask2(microtask_queue()->get(1), isolate());
  ASSERT_TRUE(microtask2->IsPromiseRejectReactionJobTask());
  EXPECT_EQ(*context2,
            Handle<PromiseRejectReactionJobTask>::cast(microtask2)->context());

  Handle<Microtask> microtask3(microtask_queue()->get(2), isolate());
  ASSERT_TRUE(microtask3->IsPromiseFulfillReactionJobTask());
  // |microtask3| corresponds to a PromiseReaction for |revoked_proxy|.
  // As |revoked_proxy| doesn't have a context, the current context should be
  // used as the fallback context.
  EXPECT_EQ(*native_context(),
            Handle<PromiseFulfillReactionJobTask>::cast(microtask3)->context());

  Handle<Microtask> microtask4(microtask_queue()->get(3), isolate());
  ASSERT_TRUE(microtask4->IsPromiseFulfillReactionJobTask());
  EXPECT_EQ(*context2,
            Handle<PromiseFulfillReactionJobTask>::cast(microtask4)->context());

  v8_context4->DetachGlobal();
  v8_context3->DetachGlobal();
  v8_context2->DetachGlobal();
}

}  // namespace internal
}  // namespace v8
