// Copyright 2013 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "cctest.h"
#include "platform/condition-variable.h"
#include "platform/time.h"

using namespace ::v8::internal;


TEST(WaitForAfterNofityOnSameThread) {
  for (int n = 0; n < 10; ++n) {
    Mutex mutex;
    ConditionVariable cv;

    LockGuard<Mutex> lock_guard(&mutex);

    cv.NotifyOne();
    CHECK_EQ(false, cv.WaitFor(&mutex, TimeDelta::FromMicroseconds(n)));

    cv.NotifyAll();
    CHECK_EQ(false, cv.WaitFor(&mutex, TimeDelta::FromMicroseconds(n)));
  }
}


class ThreadWithMutexAndConditionVariable V8_FINAL : public Thread {
 public:
  ThreadWithMutexAndConditionVariable()
      : Thread("ThreadWithMutexAndConditionVariable"),
        running_(false), finished_(false) {}
  virtual ~ThreadWithMutexAndConditionVariable() {}

  virtual void Run() V8_OVERRIDE {
    LockGuard<Mutex> lock_guard(&mutex_);
    running_ = true;
    cv_.NotifyOne();
    cv_.Wait(&mutex_);
    running_ = false;
    finished_ = true;
    cv_.NotifyOne();
  }

  volatile bool running_;
  volatile bool finished_;
  ConditionVariable cv_;
  Mutex mutex_;
};


TEST(MultipleThreadsWithSeparateConditionVariables) {
  static const int kThreadCount = 16;
  static const TimeDelta kMaxThreadStartTime =
      TimeDelta::FromMilliseconds(250) * kThreadCount;
  ThreadWithMutexAndConditionVariable threads[kThreadCount];

  for (int n = 0; n < kThreadCount; ++n) {
    LockGuard<Mutex> lock_guard(&threads[n].mutex_);
    CHECK(!threads[n].running_);
    CHECK(!threads[n].finished_);
    threads[n].Start();
    // Wait for nth thread to start.
    CHECK(threads[n].cv_.WaitFor(&threads[n].mutex_, kMaxThreadStartTime));
  }

  for (int n = kThreadCount - 1; n >= 0; --n) {
    LockGuard<Mutex> lock_guard(&threads[n].mutex_);
    CHECK(threads[n].running_);
    CHECK(!threads[n].finished_);
  }

  for (int n = 0; n < kThreadCount; ++n) {
    threads[n].cv_.NotifyOne();
  }

  for (int n = kThreadCount - 1; n >= 0; --n) {
    // Wait for nth thread to quit.
    threads[n].Join();
    LockGuard<Mutex> lock_guard(&threads[n].mutex_);
    CHECK(!threads[n].running_);
    CHECK(threads[n].finished_);
  }
}


static int loop_counter = 0;
static const int kLoopCounterLimit = 100;

class LoopIncrementThread V8_FINAL : public Thread {
 public:
  LoopIncrementThread(const char* name,
                      int rem,
                      ConditionVariable* cv,
                      Mutex* mutex)
      : Thread(name), rem_(rem), cv_(cv), mutex_(mutex) {}
  virtual ~LoopIncrementThread() {}

  virtual void Run() V8_OVERRIDE {
    int last_count = -1;
    while (true) {
      LockGuard<Mutex> lock_guard(mutex_);
      int count = loop_counter;
      while (count % 2 != rem_ && count < kLoopCounterLimit) {
        cv_->Wait(mutex_);
        count = loop_counter;
      }
      if (count >= kLoopCounterLimit) break;
      CHECK_EQ(loop_counter, count);
      if (last_count != -1) {
        CHECK_EQ(last_count + 1, count);
      }
      count++;
      loop_counter = count;
      last_count = count;
      cv_->NotifyOne();
    }
  }

 private:
  const int rem_;
  ConditionVariable* cv_;
  Mutex* mutex_;
};


TEST(LoopIncrement) {
  Mutex mutex;
  ConditionVariable cv;
  LoopIncrementThread t0("t0", 0, &cv, &mutex);
  LoopIncrementThread t1("t1", 1, &cv, &mutex);
  t0.Start();
  t1.Start();
  t0.Join();
  t1.Join();
  CHECK_EQ(kLoopCounterLimit, loop_counter);
}
