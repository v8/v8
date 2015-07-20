// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CANCELABLE_TASK_H_
#define V8_CANCELABLE_TASK_H_

#include "include/v8-platform.h"
#include "src/base/macros.h"

namespace v8 {
namespace internal {

class Isolate;

class CancelableTask : public Task {
 public:
  explicit CancelableTask(Isolate* isolate);
  ~CancelableTask() override;

  void Cancel() { is_cancelled_ = true; }

  void Run() final {
    if (!is_cancelled_) {
      RunInternal();
    }
  }

  virtual void RunInternal() = 0;

 protected:
  Isolate* isolate_;

 private:
  bool is_cancelled_;

  DISALLOW_COPY_AND_ASSIGN(CancelableTask);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CANCELABLE_TASK_H_
