// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-block-list.h"

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/debug/debug-scope-info.h"
#include "src/execution/isolate-inl.h"
#include "src/objects/js-function-inl.h"
#include "src/objects/scope-info-inl.h"
#include "src/objects/script-inl.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/objects/string-set-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

class DebugBlockListTest : public TestWithNativeContext {
 public:
  DirectHandle<SharedFunctionInfo> GetSharedFunctionInfo(const char* name) {
    DirectHandle<Object> val = RunJS(name);
    DirectHandle<JSFunction> func = Cast<JSFunction>(val);
    return direct_handle(func->shared(), isolate());
  }

  bool HasInBlockList(DirectHandle<StringSet> blocklist, const char* name) {
    DirectHandle<String> str =
        isolate()->factory()->InternalizeUtf8String(name);
    return blocklist->Has(isolate(), str);
  }
};

TEST_F(DebugBlockListTest, CalculateScopeBlockList) {
  RunJS(R"(
    function f() {
      let a = 42;
      let b = 21;
      () => b;
      {
        const c = 'foo';
        return function g() {};
      }
    }
    var g_fn = f();
  )");

  DirectHandle<SharedFunctionInfo> g_sfi = GetSharedFunctionInfo("g_fn");
  DirectHandle<Script> script(Cast<Script>(g_sfi->script()), isolate());
  Handle<DebugScriptScopeInfo> debug_scope_info =
      EnsureDebugScriptScopeInfo(isolate(), script);
  std::optional<DebugScriptScope> g_scope =
      FindClosureScope(debug_scope_info, g_sfi->StartPosition(),
                       g_sfi->EndPosition(), FUNCTION_SCOPE);
  ASSERT_TRUE(g_scope.has_value());
  std::optional<DebugScriptScope> inner_block_scope = g_scope->parent();
  ASSERT_TRUE(inner_block_scope.has_value());
  EXPECT_FALSE(inner_block_scope->needs_context());

  Handle<StringSet> block_bl =
      CalculateScopeBlockList(isolate(), *inner_block_scope);
  EXPECT_TRUE(HasInBlockList(block_bl, "c"));
  EXPECT_FALSE(HasInBlockList(block_bl, "a"));
  EXPECT_FALSE(HasInBlockList(block_bl, "b"));
}

}  // namespace internal
}  // namespace v8
