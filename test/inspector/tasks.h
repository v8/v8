// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TEST_INSPECTOR_TASKS_H_
#define V8_TEST_INSPECTOR_TASKS_H_

#include <vector>

#include "include/v8-inspector.h"
#include "include/v8.h"
#include "src/base/platform/semaphore.h"
#include "test/inspector/task-runner.h"
#include "test/inspector/utils.h"

namespace v8 {
namespace internal {

template <typename T>
void RunSyncTask(TaskRunner* task_runner, T callback) {
  class SyncTask : public TaskRunner::Task {
   public:
    SyncTask(v8::base::Semaphore* ready_semaphore, T callback)
        : ready_semaphore_(ready_semaphore), callback_(callback) {}
    ~SyncTask() override = default;
    bool is_priority_task() final { return true; }

   private:
    void Run(IsolateData* data) override {
      callback_(data);
      if (ready_semaphore_) ready_semaphore_->Signal();
    }

    v8::base::Semaphore* ready_semaphore_;
    T callback_;
  };

  v8::base::Semaphore ready_semaphore(0);
  task_runner->Append(new SyncTask(&ready_semaphore, callback));
  ready_semaphore.Wait();
}

class SendMessageToBackendTask : public TaskRunner::Task {
 public:
  SendMessageToBackendTask(int session_id, const std::vector<uint16_t>& message)
      : session_id_(session_id), message_(message) {}
  bool is_priority_task() final { return true; }

 private:
  void Run(IsolateData* data) override {
    v8_inspector::StringView message_view(message_.data(), message_.size());
    data->SendMessage(session_id_, message_view);
  }

  int session_id_;
  std::vector<uint16_t> message_;
};

inline void RunAsyncTask(TaskRunner* task_runner,
                         const v8_inspector::StringView& task_name,
                         TaskRunner::Task* task) {
  class AsyncTask : public TaskRunner::Task {
   public:
    explicit AsyncTask(TaskRunner::Task* inner) : inner_(inner) {}
    ~AsyncTask() override = default;
    bool is_priority_task() override { return inner_->is_priority_task(); }
    void Run(IsolateData* data) override {
      data->AsyncTaskStarted(inner_.get());
      inner_->Run(data);
      data->AsyncTaskFinished(inner_.get());
    }

   private:
    std::unique_ptr<TaskRunner::Task> inner_;
    DISALLOW_COPY_AND_ASSIGN(AsyncTask);
  };

  task_runner->data()->AsyncTaskScheduled(task_name, task, false);
  task_runner->Append(new AsyncTask(task));
}

class ExecuteStringTask : public TaskRunner::Task {
 public:
  ExecuteStringTask(v8::Isolate* isolate, int context_group_id,
                    const std::vector<uint16_t>& expression,
                    v8::Local<v8::String> name,
                    v8::Local<v8::Integer> line_offset,
                    v8::Local<v8::Integer> column_offset,
                    v8::Local<v8::Boolean> is_module)
      : expression_(expression),
        name_(ToVector(isolate, name)),
        line_offset_(line_offset.As<v8::Int32>()->Value()),
        column_offset_(column_offset.As<v8::Int32>()->Value()),
        is_module_(is_module->Value()),
        context_group_id_(context_group_id) {}

  ExecuteStringTask(const std::string& expression, int context_group_id)
      : expression_utf8_(expression), context_group_id_(context_group_id) {}

  ~ExecuteStringTask() override = default;
  bool is_priority_task() override { return false; }
  void Run(IsolateData* data) override;

 private:
  std::vector<uint16_t> expression_;
  std::string expression_utf8_;
  std::vector<uint16_t> name_;
  int32_t line_offset_ = 0;
  int32_t column_offset_ = 0;
  bool is_module_ = false;
  int context_group_id_;

  DISALLOW_COPY_AND_ASSIGN(ExecuteStringTask);
};

}  // namespace internal
}  // namespace v8

#endif  //  V8_TEST_INSPECTOR_TASKS_H_
