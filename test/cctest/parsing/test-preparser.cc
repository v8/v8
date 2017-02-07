// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast/ast.h"
#include "src/compiler.h"
#include "src/objects-inl.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parsing.h"

#include "test/cctest/cctest.h"
#include "test/cctest/scope-test-helper.h"
#include "test/cctest/unicode-helpers.h"

TEST(PreParserScopeAnalysis) {
  i::FLAG_lazy_inner_functions = true;
  i::FLAG_preparser_scope_analysis = true;
  i::Isolate* isolate = CcTest::i_isolate();
  i::Factory* factory = isolate->factory();
  i::HandleScope scope(isolate);
  LocalContext env;

  const char* prefix = "(function outer() { ";
  const char* suffix = " })();";
  int prefix_len = Utf8LengthHelper(prefix);
  int suffix_len = Utf8LengthHelper(suffix);

  // The scope start positions must match; note the extra space in lazy_inner.
  const char* lazy_inner = " function inner(%s) { %s }";
  const char* eager_inner = "(function inner(%s) { %s })()";

  struct {
    bool precise_maybe_assigned;
    const char* params;
    const char* source;
  } inners[] = {
      // Simple cases
      {1, "", "var1;"},
      {1, "", "var1 = 5;"},
      {1, "", "if (true) {}"},
      {1, "", "function f1() {}"},

      // Var declarations and assignments.
      {1, "", "var var1;"},
      {1, "", "var var1; var1 = 5;"},
      {0, "", "if (true) { var var1; }"},
      {1, "", "if (true) { var var1; var1 = 5; }"},
      {1, "", "var var1; function f() { var1; }"},
      {1, "", "var var1; var1 = 5; function f() { var1; }"},
      {1, "", "var var1; function f() { var1 = 5; }"},

      // Let declarations and assignments.
      {1, "", "let var1;"},
      {1, "", "let var1; var1 = 5;"},
      {1, "", "if (true) { let var1; }"},
      {1, "", "if (true) { let var1; var1 = 5; }"},
      {1, "", "let var1; function f() { var1; }"},
      {1, "", "let var1; var1 = 5; function f() { var1; }"},
      {1, "", "let var1; function f() { var1 = 5; }"},

      // Const declarations.
      {1, "", "const var1 = 5;"},
      {1, "", "if (true) { const var1 = 5; }"},
      {1, "", "const var1 = 5; function f() { var1; }"},

      // Redeclarations.
      {1, "", "var var1; var var1;"},
      {1, "", "var var1; var var1; var1 = 5;"},
      {1, "", "var var1; if (true) { var var1; }"},
      {1, "", "if (true) { var var1; var var1; }"},
      {1, "", "var var1; if (true) { var var1; var1 = 5; }"},
      {1, "", "if (true) { var var1; var var1; var1 = 5; }"},
      {1, "", "var var1; var var1; function f() { var1; }"},
      {1, "", "var var1; var var1; function f() { var1 = 5; }"},

      // Shadowing declarations.
      {1, "", "var var1; if (true) { var var1; }"},
      {1, "", "var var1; if (true) { let var1; }"},
      {1, "", "let var1; if (true) { let var1; }"},

      {1, "", "var var1; if (true) { const var1 = 0; }"},
      {1, "", "const var1 = 0; if (true) { const var1 = 0; }"},

      // Arguments and this.
      {1, "", "arguments;"},
      {1, "", "arguments = 5;"},
      {1, "", "if (true) { arguments; }"},
      {1, "", "if (true) { arguments = 5; }"},
      {1, "", "function f() { arguments; }"},
      {1, "", "function f() { arguments = 5; }"},

      {1, "", "this;"},
      {1, "", "if (true) { this; }"},
      {1, "", "function f() { this; }"},

      // Variable called "arguments"
      {1, "", "var arguments;"},
      {1, "", "var arguments; arguments = 5;"},
      {0, "", "if (true) { var arguments; }"},
      {1, "", "if (true) { var arguments; arguments = 5; }"},
      {1, "", "var arguments; function f() { arguments; }"},
      {1, "", "var arguments; arguments = 5; function f() { arguments; }"},
      {1, "", "var arguments; function f() { arguments = 5; }"},

      {1, "", "let arguments;"},
      {1, "", "let arguments; arguments = 5;"},
      {1, "", "if (true) { let arguments; }"},
      {1, "", "if (true) { let arguments; arguments = 5; }"},
      {1, "", "let arguments; function f() { arguments; }"},
      {1, "", "let arguments; arguments = 5; function f() { arguments; }"},
      {1, "", "let arguments; function f() { arguments = 5; }"},

      {1, "", "const arguments = 5;"},
      {1, "", "if (true) { const arguments = 5; }"},
      {1, "", "const arguments = 5; function f() { arguments; }"},

      // Destructuring declarations.
      {1, "", "var [var1, var2] = [1, 2];"},
      {1, "", "var [var1, var2, [var3, var4]] = [1, 2, [3, 4]];"},
      {1, "", "var [{var1: var2}, {var3: var4}] = [{var1: 1}, {var3: 2}];"},
      {1, "", "var [var1, ...var2] = [1, 2, 3];"},

      {1, "", "var {var1: var2, var3: var4} = {var1: 1, var3: 2};"},
      {1, "",
       "var {var1: var2, var3: {var4: var5}} = {var1: 1, var3: {var4: 2}};"},
      {1, "",
       "var {var1: var2, var3: [var4, var5]} = {var1: 1, var3: [2, 3]};"},

      {1, "", "let [var1, var2] = [1, 2];"},
      {1, "", "let [var1, var2, [var3, var4]] = [1, 2, [3, 4]];"},
      {1, "", "let [{var1: var2}, {var3: var4}] = [{var1: 1}, {var3: 2}];"},
      {1, "", "let [var1, ...var2] = [1, 2, 3];"},

      {1, "", "let {var1: var2, var3: var4} = {var1: 1, var3: 2};"},
      {1, "",
       "let {var1: var2, var3: {var4: var5}} = {var1: 1, var3: {var4: 2}};"},
      {1, "",
       "let {var1: var2, var3: [var4, var5]} = {var1: 1, var3: [2, 3]};"},

      {1, "", "const [var1, var2] = [1, 2];"},
      {1, "", "const [var1, var2, [var3, var4]] = [1, 2, [3, 4]];"},
      {1, "", "const [{var1: var2}, {var3: var4}] = [{var1: 1}, {var3: 2}];"},
      {1, "", "const [var1, ...var2] = [1, 2, 3];"},

      {1, "", "const {var1: var2, var3: var4} = {var1: 1, var3: 2};"},
      {1, "",
       "const {var1: var2, var3: {var4: var5}} = {var1: 1, var3: {var4: 2}};"},
      {1, "",
       "const {var1: var2, var3: [var4, var5]} = {var1: 1, var3: [2, 3]};"},

      // Referencing the function variable.
      {1, "", "inner;"},
      {1, "", "function f1() { f1; }"},
      {1, "", "function f1() { inner; }"},
      {1, "", "function f1() { function f2() { f1; } }"},
      {1, "", "function arguments() {}"},
      {1, "", "function f1() {} function f1() {}"},
      {1, "", "var f1; function f1() {}"},

      // Assigning to the function variable.
      {1, "", "inner = 3;"},
      {1, "", "function f1() { f1 = 3; }"},
      {1, "", "function f1() { f1; } f1 = 3;"},
      {1, "", "function arguments() {} arguments = 8"},
      {1, "", "function f1() {} f1 = 3; function f1() {}"},

      // Evals.
      {1, "", "var var1; eval('');"},
      {1, "", "var var1; function f1() { eval(''); }"},
      {1, "", "let var1; eval('');"},
      {1, "", "let var1; function f1() { eval(''); }"},
      {1, "", "const var1 = 10; eval('');"},
      {1, "", "const var1 = 10; function f1() { eval(''); }"},

      // Standard for loops.
      {1, "", "for (var var1 = 0; var1 < 10; ++var1) { }"},
      {1, "", "for (let var1 = 0; var1 < 10; ++var1) { }"},
      {1, "", "for (const var1 = 0; var1 < 10; ++var1) { }"},

      {1, "",
       "for (var var1 = 0; var1 < 10; ++var1) { function foo() { var1; } }"},
      {1, "",
       "for (let var1 = 0; var1 < 10; ++var1) { function foo() { var1; } }"},
      {1, "",
       "for (const var1 = 0; var1 < 10; ++var1) { function foo() { var1; } }"},
      {1, "",
       "'use strict'; for (var var1 = 0; var1 < 10; ++var1) { function foo() { "
       "var1; } }"},
      {1, "",
       "'use strict'; for (let var1 = 0; var1 < 10; ++var1) { function foo() { "
       "var1; } }"},
      {1, "",
       "'use strict'; for (const var1 = 0; var1 < 10; ++var1) { function foo() "
       "{ var1; } }"},

      // For of loops
      {1, "", "for (var1 of [1, 2]) { }"},
      {1, "", "for (var var1 of [1, 2]) { }"},
      {1, "", "for (let var1 of [1, 2]) { }"},
      {1, "", "for (const var1 of [1, 2]) { }"},

      {1, "", "for (var1 of [1, 2]) { var1; }"},
      {1, "", "for (var var1 of [1, 2]) { var1; }"},
      {1, "", "for (let var1 of [1, 2]) { var1; }"},
      {1, "", "for (const var1 of [1, 2]) { var1; }"},

      {1, "", "for (var1 of [1, 2]) { var1 = 0; }"},
      {1, "", "for (var var1 of [1, 2]) { var1 = 0; }"},
      {1, "", "for (let var1 of [1, 2]) { var1 = 0; }"},
      {1, "", "for (const var1 of [1, 2]) { var1 = 0; }"},

      {1, "", "for (var1 of [1, 2]) { function foo() { var1; } }"},
      {1, "", "for (var var1 of [1, 2]) { function foo() { var1; } }"},
      {1, "", "for (let var1 of [1, 2]) { function foo() { var1; } }"},
      {1, "", "for (const var1 of [1, 2]) { function foo() { var1; } }"},

      {1, "", "for (var1 of [1, 2]) { function foo() { var1 = 0; } }"},
      {1, "", "for (var var1 of [1, 2]) { function foo() { var1 = 0; } }"},
      {1, "", "for (let var1 of [1, 2]) { function foo() { var1 = 0; } }"},
      {1, "", "for (const var1 of [1, 2]) { function foo() { var1 = 0; } }"},

      // For in loops
      {1, "", "for (var1 in {a: 6}) { }"},
      {1, "", "for (var var1 in {a: 6}) { }"},
      {1, "", "for (let var1 in {a: 6}) { }"},
      {1, "", "for (const var1 in {a: 6}) { }"},

      {1, "", "for (var1 in {a: 6}) { var1; }"},
      {1, "", "for (var var1 in {a: 6}) { var1; }"},
      {1, "", "for (let var1 in {a: 6}) { var1; }"},
      {1, "", "for (const var1 in {a: 6}) { var1; }"},

      {1, "", "for (var1 in {a: 6}) { var1 = 0; }"},
      {1, "", "for (var var1 in {a: 6}) { var1 = 0; }"},
      {1, "", "for (let var1 in {a: 6}) { var1 = 0; }"},
      {1, "", "for (const var1 in {a: 6}) { var1 = 0; }"},

      {1, "", "for (var1 in {a: 6}) { function foo() { var1; } }"},
      {1, "", "for (var var1 in {a: 6}) { function foo() { var1; } }"},
      {1, "", "for (let var1 in {a: 6}) { function foo() { var1; } }"},
      {1, "", "for (const var1 in {a: 6}) { function foo() { var1; } }"},

      {1, "", "for (var1 in {a: 6}) { function foo() { var1 = 0; } }"},
      {1, "", "for (var var1 in {a: 6}) { function foo() { var1 = 0; } }"},
      {1, "", "for (let var1 in {a: 6}) { function foo() { var1 = 0; } }"},
      {1, "", "for (const var1 in {a: 6}) { function foo() { var1 = 0; } }"},

      {1, "", "for (var1 in {a: 6}) { function foo() { var1 = 0; } }"},
      {1, "", "for (var var1 in {a: 6}) { function foo() { var1 = 0; } }"},
      {1, "", "for (let var1 in {a: 6}) { function foo() { var1 = 0; } }"},
      {1, "", "for (const var1 in {a: 6}) { function foo() { var1 = 0; } }"},

      // Loops without declarations
      {1, "", "var var1 = 0; for ( ; var1 < 2; ++var1) { }"},
      {1, "",
       "var var1 = 0; for ( ; var1 < 2; ++var1) { function foo() { var1; } }"},
      {1, "", "var var1 = 0; for ( ; var1 > 2; ) { }"},
      {1, "", "var var1 = 0; for ( ; var1 > 2; ) { function foo() { var1; } }"},
      {1, "",
       "var var1 = 0; for ( ; var1 > 2; ) { function foo() { var1 = 6; } }"},

      {1, "", "var var1 = 0; for(var1; var1 < 2; ++var1) { }"},
      {1, "",
       "var var1 = 0; for (var1; var1 < 2; ++var1) { function foo() { var1; } "
       "}"},
      {1, "", "var var1 = 0; for (var1; var1 > 2; ) { }"},
      {1, "",
       "var var1 = 0; for (var1; var1 > 2; ) { function foo() { var1; } }"},
      {1, "",
       "var var1 = 0; for (var1; var1 > 2; ) { function foo() { var1 = 6; } }"},

      // Sloppy block functions.
      {1, "", "if (true) { function f1() {} }"},
      {1, "", "if (true) { function f1() {} function f1() {} }"},
      {1, "", "if (true) { if (true) { function f1() {} } }"},
      {1, "", "if (true) { if (true) { function f1() {} function f1() {} } }"},
      {1, "", "if (true) { function f1() {} f1 = 3; }"},

      {1, "", "if (true) { function f1() {} function foo() { f1; } }"},
      {1, "", "if (true) { function f1() {} } function foo() { f1; }"},
      {1, "",
       "if (true) { function f1() {} function f1() {} function foo() { f1; } "
       "}"},
      {1, "",
       "if (true) { function f1() {} function f1() {} } function foo() { f1; "
       "}"},
      {1, "",
       "if (true) { if (true) { function f1() {} } function foo() { f1; } }"},
      {1, "",
       "if (true) { if (true) { function f1() {} function f1() {} } function "
       "foo() { f1; } }"},
      {1, "", "if (true) { function f1() {} f1 = 3; function foo() { f1; } }"},
      {1, "", "if (true) { function f1() {} f1 = 3; } function foo() { f1; }"},

      {1, "", "function inner2() { if (true) { function f1() {} } }"},
      {1, "", "function inner2() { if (true) { function f1() {} f1 = 3; } }"},

      {1, "", "var f1 = 1; if (true) { function f1() {} }"},
      {1, "",
       "var f1 = 1; if (true) { function f1() {} } function foo() { f1; }"},
  };

  for (unsigned i = 0; i < arraysize(inners); ++i) {
    // First compile with the lazy inner function and extract the scope data.
    const char* inner_function = lazy_inner;
    int inner_function_len = Utf8LengthHelper(inner_function) - 4;

    int params_len = Utf8LengthHelper(inners[i].params);
    int source_len = Utf8LengthHelper(inners[i].source);
    int len =
        prefix_len + inner_function_len + params_len + source_len + suffix_len;

    i::ScopedVector<char> lazy_program(len + 1);
    i::SNPrintF(lazy_program, "%s", prefix);
    i::SNPrintF(lazy_program + prefix_len, inner_function, inners[i].params,
                inners[i].source);
    i::SNPrintF(lazy_program + prefix_len + inner_function_len + params_len +
                    source_len,
                "%s", suffix);

    i::Handle<i::String> source =
        factory->InternalizeUtf8String(lazy_program.start());
    source->PrintOn(stdout);
    printf("\n");

    i::Handle<i::Script> script = factory->NewScript(source);
    i::Zone zone(CcTest::i_isolate()->allocator(), ZONE_NAME);
    i::ParseInfo lazy_info(&zone, script);

    // No need to run scope analysis; preparser scope data is produced when
    // parsing.
    CHECK(i::parsing::ParseProgram(&lazy_info));

    // Then parse eagerly and check against the scope data.
    inner_function = eager_inner;
    inner_function_len = Utf8LengthHelper(inner_function) - 4;
    len =
        prefix_len + inner_function_len + params_len + source_len + suffix_len;

    i::ScopedVector<char> eager_program(len + 1);
    i::SNPrintF(eager_program, "%s", prefix);
    i::SNPrintF(eager_program + prefix_len, inner_function, inners[i].params,
                inners[i].source);
    i::SNPrintF(eager_program + prefix_len + inner_function_len + params_len +
                    source_len,
                "%s", suffix);

    source = factory->InternalizeUtf8String(eager_program.start());
    source->PrintOn(stdout);
    printf("\n");

    script = factory->NewScript(source);
    i::ParseInfo eager_info(&zone, script);
    eager_info.set_allow_lazy_parsing(false);

    CHECK(i::parsing::ParseProgram(&eager_info));
    CHECK(i::Compiler::Analyze(&eager_info));

    i::Scope* scope =
        eager_info.literal()->scope()->inner_scope()->inner_scope();
    DCHECK_NOT_NULL(scope);
    DCHECK_NULL(scope->sibling());
    DCHECK(scope->is_function_scope());

    size_t index = 0;
    i::ScopeTestHelper::CompareScopeToData(
        scope, lazy_info.preparsed_scope_data(), index,
        inners[i].precise_maybe_assigned);
  }
}
