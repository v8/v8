// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "src/compiler-dispatcher/compiler-dispatcher-job.h"
#include "src/flags.h"
#include "src/isolate-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

typedef TestWithContext CompilerDispatcherJobTest;

namespace {

class ScriptResource : public v8::String::ExternalOneByteStringResource {
 public:
  ScriptResource(const char* data, size_t length)
      : data_(data), length_(length) {}

  const char* data() const { return data_; }
  size_t length() const { return length_; }

 private:
  const char* data_;
  size_t length_;

  DISALLOW_COPY_AND_ASSIGN(ScriptResource);
};

Handle<JSFunction> CreateFunction(
    Isolate* isolate, ExternalOneByteString::Resource* maybe_resource) {
  HandleScope scope(isolate);
  Handle<String> source;
  if (maybe_resource) {
    source = isolate->factory()
                 ->NewExternalStringFromOneByte(maybe_resource)
                 .ToHandleChecked();
  } else {
    source = isolate->factory()->NewStringFromStaticChars("source");
  }
  Handle<Script> script = isolate->factory()->NewScript(source);
  Handle<SharedFunctionInfo> shared = isolate->factory()->NewSharedFunctionInfo(
      isolate->factory()->NewStringFromStaticChars("f"), MaybeHandle<Code>(),
      false);
  SharedFunctionInfo::SetScript(shared, script);
  Handle<JSFunction> function =
      isolate->factory()->NewFunctionFromSharedFunctionInfo(
          shared, handle(isolate->context(), isolate));
  return scope.CloseAndEscape(function);
}

}  // namespace

TEST_F(CompilerDispatcherJobTest, Construct) {
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate());
  std::unique_ptr<CompilerDispatcherJob> job(new CompilerDispatcherJob(
      i_isolate, CreateFunction(i_isolate, nullptr), FLAG_stack_size));
}

TEST_F(CompilerDispatcherJobTest, CanParseOnBackgroundThread) {
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate());
  {
    std::unique_ptr<CompilerDispatcherJob> job(new CompilerDispatcherJob(
        i_isolate, CreateFunction(i_isolate, nullptr), FLAG_stack_size));
    ASSERT_FALSE(job->can_parse_on_background_thread());
  }
  {
    ScriptResource script("script", strlen("script"));
    std::unique_ptr<CompilerDispatcherJob> job(new CompilerDispatcherJob(
        i_isolate, CreateFunction(i_isolate, &script), FLAG_stack_size));
    ASSERT_TRUE(job->can_parse_on_background_thread());
  }
}

TEST_F(CompilerDispatcherJobTest, StateTransitions) {
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate());
  std::unique_ptr<CompilerDispatcherJob> job(new CompilerDispatcherJob(
      i_isolate, CreateFunction(i_isolate, nullptr), FLAG_stack_size));

  ASSERT_TRUE(job->status() == CompileJobStatus::kInitial);
  job->PrepareToParseOnMainThread();
  ASSERT_TRUE(job->status() == CompileJobStatus::kReadyToParse);
  job->Parse();
  ASSERT_TRUE(job->status() == CompileJobStatus::kParsed);
}

}  // namespace internal
}  // namespace v8
