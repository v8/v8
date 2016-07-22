// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler-dispatcher/compiler-dispatcher-job.h"

#include "src/global-handles.h"
#include "src/isolate.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

CompilerDispatcherJob::CompilerDispatcherJob(Isolate* isolate,
                                             Handle<JSFunction> function)
    : isolate_(isolate),
      function_(Handle<JSFunction>::cast(
          isolate_->global_handles()->Create(*function))) {}

CompilerDispatcherJob::~CompilerDispatcherJob() {
  i::GlobalHandles::Destroy(Handle<Object>::cast(function_).location());
}

}  // namespace internal
}  // namespace v8
