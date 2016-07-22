// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_DISPATCHER_COMPILER_DISPATCHER_JOB_H_
#define V8_COMPILER_DISPATCHER_COMPILER_DISPATCHER_JOB_H_

#include "src/base/macros.h"

#include "src/handles.h"

namespace v8 {
namespace internal {

class CompilationInfo;
class Isolate;
class JSFunction;

enum class CompileJobStatus {
  kInitial,
};

class CompilerDispatcherJob {
 public:
  CompilerDispatcherJob(Isolate* isolate, Handle<JSFunction> function);
  ~CompilerDispatcherJob();

  CompileJobStatus status() const { return status_; }

 private:
  CompileJobStatus status_ = CompileJobStatus::kInitial;
  Isolate* isolate_;
  Handle<JSFunction> function_;  // Global handle.

  DISALLOW_COPY_AND_ASSIGN(CompilerDispatcherJob);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_DISPATCHER_COMPILER_DISPATCHER_JOB_H_
