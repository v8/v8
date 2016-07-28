// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler-dispatcher/compiler-dispatcher-job.h"

#include "src/global-handles.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/parsing/parser.h"
#include "src/unicode-cache.h"
#include "src/zone.h"

namespace v8 {
namespace internal {

CompilerDispatcherJob::CompilerDispatcherJob(Isolate* isolate,
                                             Handle<JSFunction> function)
    : isolate_(isolate),
      function_(Handle<JSFunction>::cast(
          isolate_->global_handles()->Create(*function))) {}

CompilerDispatcherJob::~CompilerDispatcherJob() {
  DCHECK(ThreadId::Current().Equals(isolate_->thread_id()));
  i::GlobalHandles::Destroy(Handle<Object>::cast(function_).location());
}

void CompilerDispatcherJob::PrepareToParseOnMainThread() {
  DCHECK(ThreadId::Current().Equals(isolate_->thread_id()));
  DCHECK(status_ == CompileJobStatus::kInitial);
  unicode_cache_.reset(new UnicodeCache());
  zone_.reset(new Zone(isolate_->allocator()));
  parse_info_.reset(new ParseInfo(zone_.get()));
  parse_info_->set_isolate(isolate_);
  // TODO(jochen): We need to hook up a fake source stream here.
  parse_info_->set_hash_seed(isolate_->heap()->HashSeed());
  parse_info_->set_unicode_cache(unicode_cache_.get());
  status_ = CompileJobStatus::kReadyToParse;
}

}  // namespace internal
}  // namespace v8
