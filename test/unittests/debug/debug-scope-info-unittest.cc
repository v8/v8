// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-scope-info.h"

#include "src/ast/scopes.h"
#include "src/execution/isolate-inl.h"
#include "src/heap/factory.h"
#include "src/objects/debug-objects-inl.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parsing.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

class DebugScopeInfoTest : public TestWithIsolate {
 public:
  struct ParsedScript {
    std::unique_ptr<UnoptimizedCompileState> compile_state;
    std::unique_ptr<ReusableUnoptimizedCompileState> reusable_state;
    std::unique_ptr<ParseInfo> info;
    Handle<DebugScriptScopeInfo> scope_info;

    DeclarationScope* script_scope() const { return info->literal()->scope(); }
  };

  ParsedScript ParseAndSerialize(const char* source) {
    ParsedScript result;
    result.compile_state = std::make_unique<UnoptimizedCompileState>();
    result.reusable_state =
        std::make_unique<ReusableUnoptimizedCompileState>(isolate());
    Handle<String> source_str =
        isolate()->factory()->NewStringFromAsciiChecked(source);
    Handle<Script> script = isolate()->factory()->NewScript(source_str);
    UnoptimizedCompileFlags flags =
        UnoptimizedCompileFlags::ForScriptCompile(isolate(), *script)
            .set_is_eager(true);
    result.info = std::make_unique<ParseInfo>(isolate(), flags,
                                              result.compile_state.get(),
                                              result.reusable_state.get());
    CHECK(parsing::ParseProgram(result.info.get(), script, isolate(),
                                parsing::ReportStatisticsMode{false}));
    result.scope_info = SerializeDebugScriptScopeInfo(
        isolate(), result.info->literal()->scope());
    return result;
  }
};

TEST_F(DebugScopeInfoTest, EmptyScript) {
  HandleScope scope(isolate());
  ParsedScript parsed = ParseAndSerialize("");
  DirectHandle<DebugScriptScopeInfo> info = parsed.scope_info;

  DebugScriptScope root = DebugScriptScope::FromIndex(info, 0);
  EXPECT_EQ(root.scope_index(), 0);
  EXPECT_EQ(root.start_position(), 0);
  EXPECT_EQ(root.end_position(), 0);
}

TEST_F(DebugScopeInfoTest, SingleScriptScope) {
  HandleScope scope(isolate());
  ParsedScript parsed = ParseAndSerialize("let x = 1;");
  DirectHandle<DebugScriptScopeInfo> info = parsed.scope_info;

  DebugScriptScope root = DebugScriptScope::FromIndex(info, 0);
  EXPECT_EQ(root.scope_index(), 0);
  EXPECT_EQ(root.start_position(), 0);
  EXPECT_EQ(root.end_position(), 10);
}

TEST_F(DebugScopeInfoTest, NestedBlockScopes) {
  HandleScope scope(isolate());
  ParsedScript parsed = ParseAndSerialize("{ let a = 1; } { let b = 2; }");
  DirectHandle<DebugScriptScopeInfo> info = parsed.scope_info;

  // Script scope (0)
  DebugScriptScope s0 = DebugScriptScope::FromIndex(info, 0);
  EXPECT_EQ(s0.scope_index(), 0);
  EXPECT_EQ(s0.start_position(), 0);
  EXPECT_EQ(s0.end_position(), 29);

  // Block 2 scope (1)
  DebugScriptScope s1 = DebugScriptScope::FromIndex(info, 1);
  EXPECT_EQ(s1.scope_index(), 1);
  EXPECT_EQ(s1.start_position(), 15);
  EXPECT_EQ(s1.end_position(), 29);

  // Block 1 scope (2)
  DebugScriptScope s2 = DebugScriptScope::FromIndex(info, 2);
  EXPECT_EQ(s2.scope_index(), 2);
  EXPECT_EQ(s2.start_position(), 0);
  EXPECT_EQ(s2.end_position(), 14);
}

TEST_F(DebugScopeInfoTest, NestedFunctionScopes) {
  HandleScope scope(isolate());
  ParsedScript parsed = ParseAndSerialize("function foo() { { let x = 1; } }");
  DirectHandle<DebugScriptScopeInfo> info = parsed.scope_info;

  // Script scope (0)
  DebugScriptScope s0 = DebugScriptScope::FromIndex(info, 0);
  EXPECT_EQ(s0.scope_index(), 0);
  EXPECT_EQ(s0.start_position(), 0);
  EXPECT_EQ(s0.end_position(), 33);

  // Function foo scope (1)
  DebugScriptScope s1 = DebugScriptScope::FromIndex(info, 1);
  EXPECT_EQ(s1.scope_index(), 1);
  EXPECT_EQ(s1.start_position(), 12);
  EXPECT_EQ(s1.end_position(), 33);

  // Inner block scope (2)
  DebugScriptScope s2 = DebugScriptScope::FromIndex(info, 2);
  EXPECT_EQ(s2.scope_index(), 2);
  EXPECT_EQ(s2.start_position(), 17);
  EXPECT_EQ(s2.end_position(), 31);
}

}  // namespace internal
}  // namespace v8
