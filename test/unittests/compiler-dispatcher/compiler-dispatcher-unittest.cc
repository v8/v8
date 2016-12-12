// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "src/compiler-dispatcher/compiler-dispatcher.h"
#include "src/flags.h"
#include "src/handles.h"
#include "src/objects-inl.h"
#include "test/unittests/compiler-dispatcher/compiler-dispatcher-helper.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

typedef TestWithContext CompilerDispatcherTest;

TEST_F(CompilerDispatcherTest, Construct) {
  std::unique_ptr<CompilerDispatcher> dispatcher(
      new CompilerDispatcher(i_isolate(), FLAG_stack_size));
}

TEST_F(CompilerDispatcherTest, IsEnqueued) {
  std::unique_ptr<CompilerDispatcher> dispatcher(
      new CompilerDispatcher(i_isolate(), FLAG_stack_size));

  const char script[] =
      "function g() { var y = 1; function f(x) { return x * y }; return f; } "
      "g();";
  Handle<JSFunction> f = Handle<JSFunction>::cast(RunJS(isolate(), script));
  Handle<SharedFunctionInfo> shared(f->shared(), i_isolate());

  ASSERT_FALSE(dispatcher->IsEnqueued(shared));
  ASSERT_TRUE(dispatcher->Enqueue(shared));
  ASSERT_TRUE(dispatcher->IsEnqueued(shared));
  dispatcher->Abort(shared, CompilerDispatcher::BlockingBehavior::kBlock);
  ASSERT_FALSE(dispatcher->IsEnqueued(shared));
}

TEST_F(CompilerDispatcherTest, FinishNow) {
  std::unique_ptr<CompilerDispatcher> dispatcher(
      new CompilerDispatcher(i_isolate(), FLAG_stack_size));

  const char script[] =
      "function g() { var y = 1; function f(x) { return x * y }; return f; } "
      "g();";
  Handle<JSFunction> f = Handle<JSFunction>::cast(RunJS(isolate(), script));
  Handle<SharedFunctionInfo> shared(f->shared(), i_isolate());

  ASSERT_FALSE(shared->HasBaselineCode());
  ASSERT_TRUE(dispatcher->Enqueue(shared));
  ASSERT_TRUE(dispatcher->FinishNow(shared));
  // Finishing removes the SFI from the queue.
  ASSERT_FALSE(dispatcher->IsEnqueued(shared));
  ASSERT_TRUE(shared->HasBaselineCode());
}

}  // namespace internal
}  // namespace v8
