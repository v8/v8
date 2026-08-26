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

namespace {

void VerifyScopeTreeParity(Scope* ast_scope, DebugScriptScope debug_scope) {
  EXPECT_EQ(ast_scope->start_position(), debug_scope.start_position());
  EXPECT_EQ(ast_scope->end_position(), debug_scope.end_position());
  EXPECT_EQ(ast_scope->scope_type(), debug_scope.scope_type());
  EXPECT_EQ(ast_scope->is_script_scope(), debug_scope.is_script_scope());
  EXPECT_EQ(ast_scope->is_function_scope(), debug_scope.is_function_scope());
  EXPECT_EQ(ast_scope->is_block_scope(), debug_scope.is_block_scope());
  EXPECT_EQ(ast_scope->is_declaration_scope(),
            debug_scope.is_declaration_scope());
  EXPECT_EQ(ast_scope->is_class_scope(), debug_scope.is_class_scope());
  EXPECT_EQ(ast_scope->is_with_scope(), debug_scope.is_with_scope());
  EXPECT_EQ(ast_scope->is_module_scope(), debug_scope.is_module_scope());
  EXPECT_EQ(ast_scope->is_eval_scope(), debug_scope.is_eval_scope());
  EXPECT_EQ(ast_scope->is_catch_scope(), debug_scope.is_catch_scope());
  EXPECT_EQ(ast_scope->is_repl_mode_scope(), debug_scope.is_repl_mode_scope());
  EXPECT_EQ(ast_scope->is_hidden(), debug_scope.is_hidden());
  EXPECT_EQ(ast_scope->language_mode(), debug_scope.language_mode());
  EXPECT_EQ(ast_scope->HasThisReference(), debug_scope.has_this_reference());
  if (ast_scope->is_declaration_scope()) {
    DeclarationScope* decl = ast_scope->AsDeclarationScope();
    EXPECT_EQ(decl->is_arrow_scope(), debug_scope.is_arrow_scope());
    EXPECT_EQ(decl->has_this_declaration(), debug_scope.has_this_declaration());
    EXPECT_EQ(decl->has_simple_parameters(),
              debug_scope.has_simple_parameters());
    EXPECT_EQ(decl->sloppy_eval_can_extend_vars(),
              debug_scope.sloppy_eval_can_extend_vars());
  } else {
    EXPECT_FALSE(debug_scope.is_arrow_scope());
    EXPECT_FALSE(debug_scope.has_this_declaration());
    EXPECT_FALSE(debug_scope.has_simple_parameters());
    EXPECT_FALSE(debug_scope.sloppy_eval_can_extend_vars());
  }

  Scope* ast_child = ast_scope->inner_scope();
  auto debug_child = debug_scope.first_child();

  while (ast_child != nullptr) {
    ASSERT_TRUE(debug_child.has_value());
    EXPECT_TRUE(debug_child->parent().has_value());
    EXPECT_EQ(debug_child->parent()->scope_index(), debug_scope.scope_index());
    VerifyScopeTreeParity(ast_child, *debug_child);
    ast_child = ast_child->sibling();
    debug_child = debug_child->next_sibling();
  }
  EXPECT_FALSE(debug_child.has_value());
}

}  // namespace

TEST_F(DebugScopeInfoTest, EmptyScript) {
  HandleScope scope(isolate());
  ParsedScript parsed = ParseAndSerialize("");
  DirectHandle<DebugScriptScopeInfo> info = parsed.scope_info;

  DebugScriptScope root = DebugScriptScope::FromIndex(info, 0);
  EXPECT_EQ(root.scope_index(), 0);
  EXPECT_EQ(root.start_position(), 0);
  EXPECT_EQ(root.end_position(), 0);
  EXPECT_EQ(root.scope_type(), ScopeType::SCRIPT_SCOPE);
  EXPECT_TRUE(root.is_script_scope());
  EXPECT_FALSE(root.is_function_scope());
  EXPECT_FALSE(root.is_block_scope());
  EXPECT_TRUE(root.is_declaration_scope());
  EXPECT_FALSE(root.parent().has_value());
  EXPECT_FALSE(root.first_child().has_value());
  EXPECT_FALSE(root.next_sibling().has_value());

  VerifyScopeTreeParity(parsed.script_scope(), root);
}

TEST_F(DebugScopeInfoTest, SingleScriptScope) {
  HandleScope scope(isolate());
  ParsedScript parsed = ParseAndSerialize("let x = 1;");
  DirectHandle<DebugScriptScopeInfo> info = parsed.scope_info;

  DebugScriptScope root = DebugScriptScope::FromIndex(info, 0);
  EXPECT_EQ(root.scope_index(), 0);
  EXPECT_EQ(root.start_position(), 0);
  EXPECT_EQ(root.end_position(), 10);
  EXPECT_EQ(root.scope_type(), ScopeType::SCRIPT_SCOPE);
  EXPECT_TRUE(root.is_script_scope());
  EXPECT_FALSE(root.is_function_scope());
  EXPECT_FALSE(root.is_block_scope());
  EXPECT_TRUE(root.is_declaration_scope());
  EXPECT_FALSE(root.parent().has_value());
  EXPECT_FALSE(root.first_child().has_value());
  EXPECT_FALSE(root.next_sibling().has_value());

  VerifyScopeTreeParity(parsed.script_scope(), root);
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
  EXPECT_EQ(s0.scope_type(), ScopeType::SCRIPT_SCOPE);
  EXPECT_TRUE(s0.is_script_scope());
  EXPECT_FALSE(s0.is_block_scope());
  EXPECT_FALSE(s0.parent().has_value());
  EXPECT_FALSE(s0.next_sibling().has_value());
  ASSERT_TRUE(s0.first_child().has_value());
  EXPECT_EQ(s0.first_child()->scope_index(), 1);

  // Block 2 scope (1)
  DebugScriptScope s1 = DebugScriptScope::FromIndex(info, 1);
  EXPECT_EQ(s1.scope_index(), 1);
  EXPECT_EQ(s1.start_position(), 15);
  EXPECT_EQ(s1.end_position(), 29);
  EXPECT_EQ(s1.scope_type(), ScopeType::BLOCK_SCOPE);
  EXPECT_TRUE(s1.is_block_scope());
  EXPECT_FALSE(s1.is_script_scope());
  ASSERT_TRUE(s1.parent().has_value());
  EXPECT_EQ(s1.parent()->scope_index(), 0);
  EXPECT_FALSE(s1.first_child().has_value());
  ASSERT_TRUE(s1.next_sibling().has_value());
  EXPECT_EQ(s1.next_sibling()->scope_index(), 2);

  // Block 1 scope (2)
  DebugScriptScope s2 = DebugScriptScope::FromIndex(info, 2);
  EXPECT_EQ(s2.scope_index(), 2);
  EXPECT_EQ(s2.start_position(), 0);
  EXPECT_EQ(s2.end_position(), 14);
  EXPECT_EQ(s2.scope_type(), ScopeType::BLOCK_SCOPE);
  EXPECT_TRUE(s2.is_block_scope());
  EXPECT_FALSE(s2.is_script_scope());
  ASSERT_TRUE(s2.parent().has_value());
  EXPECT_EQ(s2.parent()->scope_index(), 0);
  EXPECT_FALSE(s2.first_child().has_value());
  EXPECT_FALSE(s2.next_sibling().has_value());

  VerifyScopeTreeParity(parsed.script_scope(), s0);
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
  EXPECT_EQ(s0.scope_type(), ScopeType::SCRIPT_SCOPE);
  EXPECT_TRUE(s0.is_script_scope());
  EXPECT_FALSE(s0.parent().has_value());
  EXPECT_FALSE(s0.next_sibling().has_value());
  ASSERT_TRUE(s0.first_child().has_value());
  EXPECT_EQ(s0.first_child()->scope_index(), 1);

  // Function foo scope (1)
  DebugScriptScope s1 = DebugScriptScope::FromIndex(info, 1);
  EXPECT_EQ(s1.scope_index(), 1);
  EXPECT_EQ(s1.start_position(), 12);
  EXPECT_EQ(s1.end_position(), 33);
  EXPECT_EQ(s1.scope_type(), ScopeType::FUNCTION_SCOPE);
  EXPECT_TRUE(s1.is_function_scope());
  EXPECT_TRUE(s1.is_declaration_scope());
  ASSERT_TRUE(s1.parent().has_value());
  EXPECT_EQ(s1.parent()->scope_index(), 0);
  EXPECT_FALSE(s1.next_sibling().has_value());
  ASSERT_TRUE(s1.first_child().has_value());
  EXPECT_EQ(s1.first_child()->scope_index(), 2);

  // Inner block scope (2)
  DebugScriptScope s2 = DebugScriptScope::FromIndex(info, 2);
  EXPECT_EQ(s2.scope_index(), 2);
  EXPECT_EQ(s2.start_position(), 17);
  EXPECT_EQ(s2.end_position(), 31);
  EXPECT_EQ(s2.scope_type(), ScopeType::BLOCK_SCOPE);
  EXPECT_TRUE(s2.is_block_scope());
  EXPECT_FALSE(s2.is_declaration_scope());
  ASSERT_TRUE(s2.parent().has_value());
  EXPECT_EQ(s2.parent()->scope_index(), 1);
  EXPECT_FALSE(s2.first_child().has_value());
  EXPECT_FALSE(s2.next_sibling().has_value());

  VerifyScopeTreeParity(parsed.script_scope(), s0);
}

TEST_F(DebugScopeInfoTest, DeeplyNestedScopes) {
  HandleScope scope(isolate());
  // Scopes: 0 = Script, 1 = Function outer, 2 = Block 1, 3 = Block 2
  ParsedScript parsed = ParseAndSerialize(
      "function outer() { { let a = 1; { let nested = 42; } } }");
  DirectHandle<DebugScriptScopeInfo> info = parsed.scope_info;

  DebugScriptScope script = DebugScriptScope::FromIndex(info, 0);
  EXPECT_EQ(script.scope_type(), ScopeType::SCRIPT_SCOPE);
  EXPECT_FALSE(script.parent().has_value());

  auto func = script.first_child();
  ASSERT_TRUE(func.has_value());
  EXPECT_EQ(func->scope_index(), 1);
  EXPECT_EQ(func->scope_type(), ScopeType::FUNCTION_SCOPE);
  EXPECT_TRUE(func->parent().has_value());
  EXPECT_EQ(func->parent()->scope_index(), 0);

  auto block1 = func->first_child();
  ASSERT_TRUE(block1.has_value());
  EXPECT_EQ(block1->scope_index(), 2);
  EXPECT_EQ(block1->scope_type(), ScopeType::BLOCK_SCOPE);
  EXPECT_TRUE(block1->parent().has_value());
  EXPECT_EQ(block1->parent()->scope_index(), 1);

  auto block2 = block1->first_child();
  ASSERT_TRUE(block2.has_value());
  EXPECT_EQ(block2->scope_index(), 3);
  EXPECT_EQ(block2->scope_type(), ScopeType::BLOCK_SCOPE);
  EXPECT_TRUE(block2->parent().has_value());
  EXPECT_EQ(block2->parent()->scope_index(), 2);
  EXPECT_FALSE(block2->first_child().has_value());
  EXPECT_FALSE(block2->next_sibling().has_value());

  VerifyScopeTreeParity(parsed.script_scope(), script);
}

TEST_F(DebugScopeInfoTest, MultipleFunctionsAndSiblings) {
  HandleScope scope(isolate());
  // Scopes: 0 = Script, 1 = Function f1, 2 = Function f2
  ParsedScript parsed = ParseAndSerialize(
      "function f1() { let a = 1; } function f2() { let b = 2; }");
  DirectHandle<DebugScriptScopeInfo> info = parsed.scope_info;

  DebugScriptScope script = DebugScriptScope::FromIndex(info, 0);
  EXPECT_EQ(script.scope_type(), ScopeType::SCRIPT_SCOPE);

  auto f1 = script.first_child();
  ASSERT_TRUE(f1.has_value());
  EXPECT_EQ(f1->scope_index(), 1);
  EXPECT_EQ(f1->scope_type(), ScopeType::FUNCTION_SCOPE);
  EXPECT_TRUE(f1->parent().has_value());
  EXPECT_EQ(f1->parent()->scope_index(), 0);

  auto f2 = f1->next_sibling();
  ASSERT_TRUE(f2.has_value());
  EXPECT_EQ(f2->scope_index(), 2);
  EXPECT_EQ(f2->scope_type(), ScopeType::FUNCTION_SCOPE);
  EXPECT_TRUE(f2->parent().has_value());
  EXPECT_EQ(f2->parent()->scope_index(), 0);
  EXPECT_FALSE(f2->next_sibling().has_value());

  VerifyScopeTreeParity(parsed.script_scope(), script);
}

TEST_F(DebugScopeInfoTest, ArrowAndNormalFunctionScope) {
  HandleScope scope(isolate());
  ParsedScript parsed =
      ParseAndSerialize("let fn = (x) => { return x + 1; }; function g() {}");
  DirectHandle<DebugScriptScopeInfo> info = parsed.scope_info;

  DebugScriptScope script = DebugScriptScope::FromIndex(info, 0);
  EXPECT_TRUE(script.is_script_scope());
  EXPECT_FALSE(script.is_arrow_scope());
  EXPECT_FALSE(script.has_this_declaration());

  // Function g scope (1) - child scopes are prepended in AST
  auto normal = script.first_child();
  ASSERT_TRUE(normal.has_value());
  EXPECT_TRUE(normal->is_function_scope());
  EXPECT_FALSE(normal->is_arrow_scope());
  EXPECT_TRUE(normal->has_this_declaration());

  // Arrow function fn scope (2)
  auto arrow = normal->next_sibling();
  ASSERT_TRUE(arrow.has_value());
  EXPECT_TRUE(arrow->is_function_scope());
  EXPECT_TRUE(arrow->is_arrow_scope());
  EXPECT_FALSE(arrow->has_this_declaration());
  EXPECT_TRUE(arrow->has_simple_parameters());

  VerifyScopeTreeParity(parsed.script_scope(), script);
}

TEST_F(DebugScopeInfoTest, StrictAndNonSimpleParams) {
  HandleScope scope(isolate());
  ParsedScript parsed =
      ParseAndSerialize("\"use strict\"; function f(a = 1) { eval(\"\"); }");
  DirectHandle<DebugScriptScopeInfo> info = parsed.scope_info;

  DebugScriptScope script = DebugScriptScope::FromIndex(info, 0);
  EXPECT_EQ(script.language_mode(), LanguageMode::kStrict);

  auto func = script.first_child();
  ASSERT_TRUE(func.has_value());
  EXPECT_EQ(func->language_mode(), LanguageMode::kStrict);
  EXPECT_FALSE(func->has_simple_parameters());
  EXPECT_FALSE(func->sloppy_eval_can_extend_vars());

  VerifyScopeTreeParity(parsed.script_scope(), script);
}

TEST_F(DebugScopeInfoTest, ComplexScopeTypes) {
  HandleScope scope(isolate());
  ParsedScript parsed = ParseAndSerialize(
      "class C { m() {} }\n"
      "try { } catch (e) { }\n"
      "with ({}) { }");
  DirectHandle<DebugScriptScopeInfo> info = parsed.scope_info;

  DebugScriptScope script = DebugScriptScope::FromIndex(info, 0);
  VerifyScopeTreeParity(parsed.script_scope(), script);
}

}  // namespace internal
}  // namespace v8
