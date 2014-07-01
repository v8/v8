// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/default-platform.h"

#include <algorithm>
#include <queue>

#include "src/base/logging.h"
#include "src/base/platform/platform.h"
#include "src/libplatform/worker-thread.h"

namespace v8 {
namespace platform {


const int DefaultPlatform::kMaxThreadPoolSize = 4;


DefaultPlatform::DefaultPlatform()
    : initialized_(false), thread_pool_size_(0) {}


DefaultPlatform::~DefaultPlatform() {
  base::LockGuard<base::Mutex> guard(&lock_);
  queue_.Terminate();
  if (initialized_) {
    for (std::vector<WorkerThread*>::iterator i = thread_pool_.begin();
         i != thread_pool_.end(); ++i) {
      delete *i;
    }
  }
}


void DefaultPlatform::SetThreadPoolSize(int thread_pool_size) {
  base::LockGuard<base::Mutex> guard(&lock_);
  ASSERT(thread_pool_size >= 0);
  if (thread_pool_size < 1)
    thread_pool_size = base::OS::NumberOfProcessorsOnline();
  thread_pool_size_ =
      std::max(std::min(thread_pool_size, kMaxThreadPoolSize), 1);
}


void DefaultPlatform::EnsureInitialized() {
  base::LockGuard<base::Mutex> guard(&lock_);
  if (initialized_) return;
  initialized_ = true;

  for (int i = 0; i < thread_pool_size_; ++i)
    thread_pool_.push_back(new WorkerThread(&queue_));
}

void DefaultPlatform::CallOnBackgroundThread(Task *task,
                                             ExpectedRuntime expected_runtime) {
  EnsureInitialized();
  queue_.Append(task);
}


void DefaultPlatform::CallOnForegroundThread(v8::Isolate* isolate, Task* task) {
  // TODO(jochen): implement.
  task->Run();
  delete task;
}

} }  // namespace v8::platform
