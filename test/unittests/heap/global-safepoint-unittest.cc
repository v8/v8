// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/platform/mutex.h"
#include "src/base/platform/platform.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap.h"
#include "src/heap/parked-scope.h"
#include "src/heap/safepoint.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using GlobalSafepointTest = TestWithSharedIsolate;

namespace {

class ClientIsolateWithContextWrapper {
 public:
  explicit ClientIsolateWithContextWrapper(v8::Isolate* shared_isolate)
      : client_isolate_wrapper_(kNoCounters, kClientIsolate, shared_isolate),
        isolate_scope_(client_isolate_wrapper_.isolate()),
        handle_scope_(client_isolate_wrapper_.isolate()),
        context_(v8::Context::New(client_isolate_wrapper_.isolate())),
        context_scope_(context_) {}

  v8::Isolate* v8_isolate() const { return client_isolate_wrapper_.isolate(); }
  Isolate* isolate() const { return reinterpret_cast<Isolate*>(v8_isolate()); }

 private:
  IsolateWrapper client_isolate_wrapper_;
  v8::Isolate::Scope isolate_scope_;
  v8::HandleScope handle_scope_;
  v8::Local<v8::Context> context_;
  v8::Context::Scope context_scope_;
};

class ParkingThread : public v8::base::Thread {
 public:
  explicit ParkingThread(const Options& options) : v8::base::Thread(options) {}

  void ParkedJoin(const ParkedScope& scope) {
    USE(scope);
    Join();
  }

 private:
  using base::Thread::Join;
};

class InfiniteLooperThread final : public ParkingThread {
 public:
  InfiniteLooperThread(v8::Isolate* shared_isolate,
                       ParkingSemaphore* sema_ready,
                       ParkingSemaphore* sema_execute_start,
                       ParkingSemaphore* sema_execute_complete)
      : ParkingThread(Options("InfiniteLooperThread")),
        shared_isolate_(shared_isolate),
        sema_ready_(sema_ready),
        sema_execute_start_(sema_execute_start),
        sema_execute_complete_(sema_execute_complete) {}

  void Run() override {
    ClientIsolateWithContextWrapper client_isolate_wrapper(shared_isolate_);
    v8::Isolate* v8_isolate = client_isolate_wrapper.v8_isolate();
    v8::Isolate::Scope isolate_scope(v8_isolate);
    v8::HandleScope scope(v8_isolate);

    v8::Local<v8::String> source =
        v8::String::NewFromUtf8(v8_isolate, "for(;;) {}").ToLocalChecked();
    auto context = v8_isolate->GetCurrentContext();
    v8::Local<v8::Value> result;
    v8::Local<v8::Script> script =
        v8::Script::Compile(context, source).ToLocalChecked();

    sema_ready_->Signal();
    sema_execute_start_->ParkedWait(
        client_isolate_wrapper.isolate()->main_thread_local_isolate());

    USE(script->Run(context));

    sema_execute_complete_->Signal();
  }

 private:
  v8::Isolate* shared_isolate_;
  ParkingSemaphore* sema_ready_;
  ParkingSemaphore* sema_execute_start_;
  ParkingSemaphore* sema_execute_complete_;
};

}  // namespace

TEST_F(GlobalSafepointTest, Interrupt) {
  if (!IsJSSharedMemorySupported()) return;

  v8::Isolate* shared_isolate = v8_isolate();
  ClientIsolateWithContextWrapper client_isolate_wrapper(shared_isolate);

  constexpr int kThreads = 4;
  Isolate* isolate = client_isolate_wrapper.isolate();
  ParkingSemaphore sema_ready(0);
  ParkingSemaphore sema_execute_start(0);
  ParkingSemaphore sema_execute_complete(0);
  std::vector<std::unique_ptr<InfiniteLooperThread>> threads;
  for (int i = 0; i < kThreads; i++) {
    auto thread = std::make_unique<InfiniteLooperThread>(
        shared_isolate, &sema_ready, &sema_execute_start,
        &sema_execute_complete);
    CHECK(thread->Start());
    threads.push_back(std::move(thread));
  }

  LocalIsolate* local_isolate = isolate->main_thread_local_isolate();
  for (int i = 0; i < kThreads; i++) {
    sema_ready.ParkedWait(local_isolate);
  }
  for (int i = 0; i < kThreads; i++) {
    sema_execute_start.Signal();
  }

  {
    // Test that a global safepoint interrupts threads infinitely looping in JS.

    // This wait is a big hack to increase the likelihood that the infinite
    // looper threads will have entered into a steady state of infinitely
    // looping. Otherwise the safepoint may be reached during allocation, such
    // as of FeedbackVectors, and we wouldn't be testing the interrupt check.
    base::OS::Sleep(base::TimeDelta::FromMilliseconds(500));
    GlobalSafepointScope global_safepoint(isolate);
    reinterpret_cast<Isolate*>(shared_isolate)
        ->global_safepoint()
        ->IterateClientIsolates([](Isolate* client) {
          client->stack_guard()->RequestTerminateExecution();
        });
  }

  for (int i = 0; i < kThreads; i++) {
    sema_execute_complete.ParkedWait(local_isolate);
  }

  ParkedScope parked(local_isolate);
  for (auto& thread : threads) {
    thread->ParkedJoin(parked);
  }
}

}  // namespace internal
}  // namespace v8
