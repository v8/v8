// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(jochen): Remove this after the setting is turned on globally.
#define V8_IMMINENT_DEPRECATION_WARNINGS

#include <utility>

#include "src/compiler/pipeline.h"
#include "src/execution.h"
#include "src/handles.h"
#include "src/interpreter/bytecode-array-builder.h"
#include "src/interpreter/interpreter.h"
#include "src/parser.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace compiler {


static const char kFunctionName[] = "f";


static MaybeHandle<Object> CallFunction(Isolate* isolate,
                                        Handle<JSFunction> function) {
  return Execution::Call(isolate, function,
                         isolate->factory()->undefined_value(), 0, nullptr);
}


template <class... A>
static MaybeHandle<Object> CallFunction(Isolate* isolate,
                                        Handle<JSFunction> function,
                                        A... args) {
  Handle<Object> argv[] = {args...};
  return Execution::Call(isolate, function,
                         isolate->factory()->undefined_value(), sizeof...(args),
                         argv);
}


template <class... A>
class BytecodeGraphCallable {
 public:
  BytecodeGraphCallable(Isolate* isolate, Handle<JSFunction> function)
      : isolate_(isolate), function_(function) {}
  virtual ~BytecodeGraphCallable() {}

  MaybeHandle<Object> operator()(A... args) {
    return CallFunction(isolate_, function_, args...);
  }

 private:
  Isolate* isolate_;
  Handle<JSFunction> function_;
};


class BytecodeGraphTester {
 public:
  BytecodeGraphTester(Isolate* isolate, Zone* zone, const char* script)
      : isolate_(isolate), zone_(zone), script_(script) {
    i::FLAG_ignition = true;
    i::FLAG_always_opt = false;
    // Set ignition filter flag via SetFlagsFromString to avoid double-free
    // (or potential leak with StrDup() based on ownership confusion).
    ScopedVector<char> ignition_filter(64);
    SNPrintF(ignition_filter, "--ignition-filter=%s", kFunctionName);
    FlagList::SetFlagsFromString(ignition_filter.start(),
                                 ignition_filter.length());
    // Ensure handler table is generated.
    isolate->interpreter()->Initialize();
  }
  virtual ~BytecodeGraphTester() {}

  template <class... A>
  BytecodeGraphCallable<A...> GetCallable() {
    return BytecodeGraphCallable<A...>(isolate_, GetFunction());
  }

  static Handle<Object> NewObject(const char* script) {
    return v8::Utils::OpenHandle(*CompileRun(script));
  }

 private:
  Isolate* isolate_;
  Zone* zone_;
  const char* script_;

  Handle<JSFunction> GetFunction() {
    CompileRun(script_);
    Local<Function> api_function = Local<Function>::Cast(
        CcTest::global()
            ->Get(CcTest::isolate()->GetCurrentContext(), v8_str(kFunctionName))
            .ToLocalChecked());
    Handle<JSFunction> function =
        Handle<JSFunction>::cast(v8::Utils::OpenHandle(*api_function));
    CHECK(function->shared()->HasBytecodeArray());

    ParseInfo parse_info(zone_, function);

    CompilationInfo compilation_info(&parse_info);
    compilation_info.SetOptimizing(BailoutId::None(), Handle<Code>());
    // TODO(mythria): Remove this step once parse_info is not needed.
    CHECK(Compiler::ParseAndAnalyze(&parse_info));
    compiler::Pipeline pipeline(&compilation_info);
    Handle<Code> code = pipeline.GenerateCode();
    function->ReplaceCode(*code);

    return function;
  }

  DISALLOW_COPY_AND_ASSIGN(BytecodeGraphTester);
};


#define SPACE()

#define REPEAT_2(SEP, ...) __VA_ARGS__ SEP() __VA_ARGS__
#define REPEAT_4(SEP, ...) \
  REPEAT_2(SEP, __VA_ARGS__) SEP() REPEAT_2(SEP, __VA_ARGS__)
#define REPEAT_8(SEP, ...) \
  REPEAT_4(SEP, __VA_ARGS__) SEP() REPEAT_4(SEP, __VA_ARGS__)
#define REPEAT_16(SEP, ...) \
  REPEAT_8(SEP, __VA_ARGS__) SEP() REPEAT_8(SEP, __VA_ARGS__)
#define REPEAT_32(SEP, ...) \
  REPEAT_16(SEP, __VA_ARGS__) SEP() REPEAT_16(SEP, __VA_ARGS__)
#define REPEAT_64(SEP, ...) \
  REPEAT_32(SEP, __VA_ARGS__) SEP() REPEAT_32(SEP, __VA_ARGS__)
#define REPEAT_128(SEP, ...) \
  REPEAT_64(SEP, __VA_ARGS__) SEP() REPEAT_64(SEP, __VA_ARGS__)
#define REPEAT_256(SEP, ...) \
  REPEAT_128(SEP, __VA_ARGS__) SEP() REPEAT_128(SEP, __VA_ARGS__)

#define REPEAT_127(SEP, ...)  \
  REPEAT_64(SEP, __VA_ARGS__) \
  SEP()                       \
  REPEAT_32(SEP, __VA_ARGS__) \
  SEP()                       \
  REPEAT_16(SEP, __VA_ARGS__) \
  SEP()                       \
  REPEAT_8(SEP, __VA_ARGS__)  \
  SEP()                       \
  REPEAT_4(SEP, __VA_ARGS__) SEP() REPEAT_2(SEP, __VA_ARGS__) SEP() __VA_ARGS__


template <int N>
struct ExpectedSnippet {
  const char* code_snippet;
  Handle<Object> return_value_and_parameters[N + 1];

  inline Handle<Object> return_value() const {
    return return_value_and_parameters[0];
  }

  inline Handle<Object> parameter(int i) const {
    DCHECK_GE(i, 0);
    DCHECK_LT(i, N);
    return return_value_and_parameters[1 + i];
  }
};


TEST(BytecodeGraphBuilderReturnStatements) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"return;", {factory->undefined_value()}},
      {"return null;", {factory->null_value()}},
      {"return true;", {factory->true_value()}},
      {"return false;", {factory->false_value()}},
      {"return 0;", {factory->NewNumberFromInt(0)}},
      {"return +1;", {factory->NewNumberFromInt(1)}},
      {"return -1;", {factory->NewNumberFromInt(-1)}},
      {"return +127;", {factory->NewNumberFromInt(127)}},
      {"return -128;", {factory->NewNumberFromInt(-128)}},
      {"return 0.001;", {factory->NewNumber(0.001)}},
      {"return 3.7e-60;", {factory->NewNumber(3.7e-60)}},
      {"return -3.7e60;", {factory->NewNumber(-3.7e60)}},
      {"return '';", {factory->NewStringFromStaticChars("")}},
      {"return 'catfood';", {factory->NewStringFromStaticChars("catfood")}}
      // TODO(oth): {"return NaN;", {factory->NewNumber(NAN)}}
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, zone, script.start());
    auto callable = tester.GetCallable<>();
    Handle<Object> return_value = callable().ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


TEST(BytecodeGraphBuilderPrimitiveExpressions) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"return 1 + 1;", {factory->NewNumberFromInt(2)}},
      {"return 20 - 30;", {factory->NewNumberFromInt(-10)}},
      {"return 4 * 100;", {factory->NewNumberFromInt(400)}},
      {"return 100 / 5;", {factory->NewNumberFromInt(20)}},
      {"return 25 % 7;", {factory->NewNumberFromInt(4)}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, zone, script.start());
    auto callable = tester.GetCallable<>();
    Handle<Object> return_value = callable().ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


TEST(BytecodeGraphBuilderTwoParameterTests) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<2> snippets[] = {
      // Integers
      {"return p1 + p2;",
       {factory->NewNumberFromInt(-70), factory->NewNumberFromInt(3),
        factory->NewNumberFromInt(-73)}},
      {"return p1 + p2 + 3;",
       {factory->NewNumberFromInt(1139044), factory->NewNumberFromInt(300),
        factory->NewNumberFromInt(1138741)}},
      {"return p1 - p2;",
       {factory->NewNumberFromInt(1100), factory->NewNumberFromInt(1000),
        factory->NewNumberFromInt(-100)}},
      {"return p1 * p2;",
       {factory->NewNumberFromInt(-100000), factory->NewNumberFromInt(1000),
        factory->NewNumberFromInt(-100)}},
      {"return p1 / p2;",
       {factory->NewNumberFromInt(-10), factory->NewNumberFromInt(1000),
        factory->NewNumberFromInt(-100)}},
      {"return p1 % p2;",
       {factory->NewNumberFromInt(5), factory->NewNumberFromInt(373),
        factory->NewNumberFromInt(16)}},
      // Doubles
      {"return p1 + p2;",
       {factory->NewHeapNumber(9.999), factory->NewHeapNumber(3.333),
        factory->NewHeapNumber(6.666)}},
      {"return p1 - p2;",
       {factory->NewHeapNumber(-3.333), factory->NewHeapNumber(3.333),
        factory->NewHeapNumber(6.666)}},
      {"return p1 * p2;",
       {factory->NewHeapNumber(3.333 * 6.666), factory->NewHeapNumber(3.333),
        factory->NewHeapNumber(6.666)}},
      {"return p1 / p2;",
       {factory->NewHeapNumber(2.25), factory->NewHeapNumber(9),
        factory->NewHeapNumber(4)}},
      // Strings
      {"return p1 + p2;",
       {factory->NewStringFromStaticChars("abcdef"),
        factory->NewStringFromStaticChars("abc"),
        factory->NewStringFromStaticChars("def")}}};

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1, p2) { %s }\n%s(0, 0);", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, zone, script.start());
    auto callable = tester.GetCallable<Handle<Object>, Handle<Object>>();
    Handle<Object> return_value =
        callable(snippets[i].parameter(0), snippets[i].parameter(1))
            .ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


TEST(BytecodeGraphBuilderNamedLoad) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return p1.val;",
       {factory->NewNumberFromInt(10),
        BytecodeGraphTester::NewObject("({val : 10})")}},
      {"return p1[\"name\"];",
       {factory->NewStringFromStaticChars("abc"),
        BytecodeGraphTester::NewObject("({name : 'abc'})")}},
      {"'use strict'; return p1.val;",
       {factory->NewNumberFromInt(10),
        BytecodeGraphTester::NewObject("({val : 10 })")}},
      {"'use strict'; return p1[\"val\"];",
       {factory->NewNumberFromInt(10),
        BytecodeGraphTester::NewObject("({val : 10, name : 'abc'})")}},
      {"var b;\n" REPEAT_127(SPACE, " b = p1.name; ") " return p1.name;\n",
       {factory->NewStringFromStaticChars("abc"),
        BytecodeGraphTester::NewObject("({name : 'abc'})")}},
      {"'use strict'; var b;\n"
       REPEAT_127(SPACE, " b = p1.name; ")
       "return p1.name;\n",
       {factory->NewStringFromStaticChars("abc"),
        BytecodeGraphTester::NewObject("({ name : 'abc'})")}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(2048);
    SNPrintF(script, "function %s(p1) { %s };\n%s(0);", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, zone, script.start());
    auto callable = tester.GetCallable<Handle<Object>>();
    Handle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


TEST(BytecodeGraphBuilderPropertyCall) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return p1.func();",
       {factory->NewNumberFromInt(25),
        BytecodeGraphTester::NewObject("({func() { return 25; }})")}},
      {"return p1.func('abc');",
       {factory->NewStringFromStaticChars("abc"),
        BytecodeGraphTester::NewObject("({func(a) { return a; }})")}},
      {"return p1.func(1, 2, 3, 4, 5, 6, 7, 8);",
       {factory->NewNumberFromInt(36),
        BytecodeGraphTester::NewObject(
            "({func(a, b, c, d, e, f, g, h) {\n"
            "  return a + b + c + d + e + f + g + h;}})")}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(2048);
    SNPrintF(script, "function %s(p1) { %s };\n%s({func() {}});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, zone, script.start());
    auto callable = tester.GetCallable<Handle<Object>>();
    Handle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


TEST(BytecodeGraphBuilderGlobals) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"var global = 321;\n function f() { return global; };\n f();",
       {factory->NewNumberFromInt(321)}},
      {"var global = 321;\n"
       "function f() { global = 123; return global };\n f();",
       {factory->NewNumberFromInt(123)}},
      {"var global = function() { return 'abc'};\n"
       "function f() { return global(); };\n f();",
       {factory->NewStringFromStaticChars("abc")}},
      {"var global = 456;\n"
       "function f() { 'use strict'; return global; };\n f();",
       {factory->NewNumberFromInt(456)}},
      {"var global = 987;\n"
       "function f() { 'use strict'; global = 789; return global };\n f();",
       {factory->NewNumberFromInt(789)}},
      {"var global = function() { return 'xyz'};\n"
       "function f() { 'use strict'; return global(); };\n f();",
       {factory->NewStringFromStaticChars("xyz")}},
      {"var global = 'abc'; var global_obj = {val:123};\n"
       "function f() {\n" REPEAT_127(
           SPACE, " var b = global_obj.name;\n") "return global; };\n f();\n",
       {factory->NewStringFromStaticChars("abc")}},
      {"var global = 'abc'; var global_obj = {val:123};\n"
       "function f() { 'use strict';\n" REPEAT_127(
           SPACE, " var b = global_obj.name;\n") "global = 'xyz'; return "
                                                 "global };\n f();\n",
       {factory->NewStringFromStaticChars("xyz")}},
      // TODO(rmcilroy): Add tests for typeof_mode once we have typeof support.
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    BytecodeGraphTester tester(isolate, zone, snippets[i].code_snippet);
    auto callable = tester.GetCallable<>();
    Handle<Object> return_value = callable().ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
