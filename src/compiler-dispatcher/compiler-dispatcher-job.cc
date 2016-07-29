// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler-dispatcher/compiler-dispatcher-job.h"

#include "src/assert-scope.h"
#include "src/global-handles.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/parsing/parser.h"
#include "src/parsing/scanner-character-streams.h"
#include "src/unicode-cache.h"
#include "src/zone.h"

namespace v8 {
namespace internal {

CompilerDispatcherJob::CompilerDispatcherJob(Isolate* isolate,
                                             Handle<JSFunction> function,
                                             size_t max_stack_size)
    : isolate_(isolate),
      function_(Handle<JSFunction>::cast(
          isolate_->global_handles()->Create(*function))),
      max_stack_size_(max_stack_size) {
  HandleScope scope(isolate_);
  Handle<SharedFunctionInfo> shared(function_->shared(), isolate_);
  Handle<Script> script(Script::cast(shared->script()), isolate_);
  Handle<String> source(String::cast(script->source()), isolate_);
  if (source->IsExternalTwoByteString()) {
    can_parse_on_background_thread_ = true;
  } else if (source->IsExternalOneByteString()) {
    can_parse_on_background_thread_ = true;
  } else {
    can_parse_on_background_thread_ = false;
  }
}

CompilerDispatcherJob::~CompilerDispatcherJob() {
  DCHECK(ThreadId::Current().Equals(isolate_->thread_id()));
  i::GlobalHandles::Destroy(Handle<Object>::cast(function_).location());
}

void CompilerDispatcherJob::PrepareToParseOnMainThread() {
  DCHECK(ThreadId::Current().Equals(isolate_->thread_id()));
  DCHECK(status() == CompileJobStatus::kInitial);
  HandleScope scope(isolate_);
  unicode_cache_.reset(new UnicodeCache());
  zone_.reset(new Zone(isolate_->allocator()));
  Handle<SharedFunctionInfo> shared(function_->shared(), isolate_);
  Handle<Script> script(Script::cast(shared->script()), isolate_);
  Handle<String> source(String::cast(script->source()), isolate_);
  if (source->IsExternalTwoByteString()) {
    can_parse_on_background_thread_ = true;
    character_stream_.reset(new ExternalTwoByteStringUtf16CharacterStream(
        Handle<ExternalTwoByteString>::cast(source), shared->start_position(),
        shared->end_position()));
  } else if (source->IsExternalOneByteString()) {
    can_parse_on_background_thread_ = true;
    character_stream_.reset(new ExternalOneByteStringUtf16CharacterStream(
        Handle<ExternalOneByteString>::cast(source), shared->start_position(),
        shared->end_position()));
  } else {
    can_parse_on_background_thread_ = false;
    character_stream_.reset(new GenericStringUtf16CharacterStream(
        source, shared->start_position(), shared->end_position()));
  }
  parse_info_.reset(new ParseInfo(zone_.get()));
  parse_info_->set_isolate(isolate_);
  parse_info_->set_character_stream(character_stream_.get());
  parse_info_->set_hash_seed(isolate_->heap()->HashSeed());
  parse_info_->set_unicode_cache(unicode_cache_.get());
  parser_.reset(new Parser(parse_info_.get()));
  status_.SetValue(CompileJobStatus::kReadyToParse);
}

void CompilerDispatcherJob::Parse() {
  DCHECK(can_parse_on_background_thread_ ||
         ThreadId::Current().Equals(isolate_->thread_id()));
  DCHECK(status() == CompileJobStatus::kReadyToParse);

  DisallowHeapAllocation no_allocation;
  DisallowHandleAllocation no_handles;
  DisallowHandleDereference no_deref;

  // Nullify the Isolate temporarily so that the parser doesn't accidentally
  // use it.
  parse_info_->set_isolate(nullptr);

  uintptr_t stack_limit =
      reinterpret_cast<uintptr_t>(&stack_limit) - max_stack_size_ * KB;

  parser_->set_stack_limit(stack_limit);
  parser_->ParseOnBackground(parse_info_.get());

  parse_info_->set_isolate(isolate_);

  status_.SetValue(CompileJobStatus::kParsed);
}

}  // namespace internal
}  // namespace v8
