// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "src/compiler-dispatcher/compiler-dispatcher-job.h"
#include "src/isolate-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

typedef TestWithContext CompilerDispatcherJobTest;

TEST_F(CompilerDispatcherJobTest, Construct) {
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate());
  std::unique_ptr<CompilerDispatcherJob> job(
      new CompilerDispatcherJob(i_isolate, i_isolate->object_function()));
}

TEST_F(CompilerDispatcherJobTest, PrepareToParse) {
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate());
  std::unique_ptr<CompilerDispatcherJob> job(
      new CompilerDispatcherJob(i_isolate, i_isolate->object_function()));

  ASSERT_TRUE(job->status() == CompileJobStatus::kInitial);
  job->PrepareToParseOnMainThread();
  ASSERT_TRUE(job->status() == CompileJobStatus::kReadyToParse);
}

}  // namespace internal
}  // namespace v8
