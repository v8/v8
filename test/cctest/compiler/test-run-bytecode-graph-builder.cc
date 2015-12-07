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
#include "src/parsing/parser.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace compiler {


static const char kFunctionName[] = "f";

static const Token::Value kCompareOperators[] = {
    Token::Value::EQ,        Token::Value::NE, Token::Value::EQ_STRICT,
    Token::Value::NE_STRICT, Token::Value::LT, Token::Value::LTE,
    Token::Value::GT,        Token::Value::GTE};

static const int SMI_MAX = (1 << 30) - 1;
static const int SMI_MIN = -(1 << 30);

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
  BytecodeGraphTester(Isolate* isolate, Zone* zone, const char* script,
                      const char* filter = kFunctionName)
      : isolate_(isolate), zone_(zone), script_(script) {
    i::FLAG_ignition = true;
    i::FLAG_always_opt = false;
    i::FLAG_allow_natives_syntax = true;
    // Set ignition filter flag via SetFlagsFromString to avoid double-free
    // (or potential leak with StrDup() based on ownership confusion).
    ScopedVector<char> ignition_filter(64);
    SNPrintF(ignition_filter, "--ignition-filter=%s", filter);
    FlagList::SetFlagsFromString(ignition_filter.start(),
                                 ignition_filter.length());
    // Ensure handler table is generated.
    isolate->interpreter()->Initialize();
  }
  virtual ~BytecodeGraphTester() {}

  template <class... A>
  BytecodeGraphCallable<A...> GetCallable(
      const char* functionName = kFunctionName) {
    return BytecodeGraphCallable<A...>(isolate_, GetFunction(functionName));
  }

  Local<Message> CheckThrowsReturnMessage() {
    TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate_));
    auto callable = GetCallable<>();
    MaybeHandle<Object> no_result = callable();
    CHECK(isolate_->has_pending_exception());
    CHECK(try_catch.HasCaught());
    CHECK(no_result.is_null());
    isolate_->OptionalRescheduleException(true);
    CHECK(!try_catch.Message().IsEmpty());
    return try_catch.Message();
  }

  static Handle<Object> NewObject(const char* script) {
    return v8::Utils::OpenHandle(*CompileRun(script));
  }

 private:
  Isolate* isolate_;
  Zone* zone_;
  const char* script_;

  Handle<JSFunction> GetFunction(const char* functionName) {
    CompileRun(script_);
    Local<Function> api_function = Local<Function>::Cast(
        CcTest::global()
            ->Get(CcTest::isolate()->GetCurrentContext(), v8_str(functionName))
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


template <int N, typename T = Handle<Object>>
struct ExpectedSnippet {
  const char* code_snippet;
  T return_value_and_parameters[N + 1];

  inline T return_value() const { return return_value_and_parameters[0]; }

  inline T parameter(int i) const {
    CHECK_GE(i, 0);
    CHECK_LT(i, N);
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


TEST(BytecodeGraphBuilderKeyedLoad) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<2> snippets[] = {
      {"return p1[p2];",
       {factory->NewNumberFromInt(10),
        BytecodeGraphTester::NewObject("({val : 10})"),
        factory->NewStringFromStaticChars("val")}},
      {"return p1[100];",
       {factory->NewStringFromStaticChars("abc"),
        BytecodeGraphTester::NewObject("({100 : 'abc'})"),
        factory->NewNumberFromInt(0)}},
      {"var b = 100; return p1[b];",
       {factory->NewStringFromStaticChars("abc"),
        BytecodeGraphTester::NewObject("({100 : 'abc'})"),
        factory->NewNumberFromInt(0)}},
      {"'use strict'; return p1[p2];",
       {factory->NewNumberFromInt(10),
        BytecodeGraphTester::NewObject("({val : 10 })"),
        factory->NewStringFromStaticChars("val")}},
      {"'use strict'; return p1[100];",
       {factory->NewNumberFromInt(10),
        BytecodeGraphTester::NewObject("({100 : 10})"),
        factory->NewNumberFromInt(0)}},
      {"'use strict'; var b = p2; return p1[b];",
       {factory->NewStringFromStaticChars("abc"),
        BytecodeGraphTester::NewObject("({100 : 'abc'})"),
        factory->NewNumberFromInt(100)}},
      {"var b;\n" REPEAT_127(SPACE, " b = p1[p2]; ") " return p1[p2];\n",
       {factory->NewStringFromStaticChars("abc"),
        BytecodeGraphTester::NewObject("({100 : 'abc'})"),
        factory->NewNumberFromInt(100)}},
      {"'use strict'; var b;\n" REPEAT_127(SPACE,
                                           " b = p1[p2]; ") "return p1[p2];\n",
       {factory->NewStringFromStaticChars("abc"),
        BytecodeGraphTester::NewObject("({ 100 : 'abc'})"),
        factory->NewNumberFromInt(100)}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(2048);
    SNPrintF(script, "function %s(p1, p2) { %s };\n%s(0);", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, zone, script.start());
    auto callable = tester.GetCallable<Handle<Object>, Handle<Object>>();
    Handle<Object> return_value =
        callable(snippets[i].parameter(0), snippets[i].parameter(1))
            .ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


TEST(BytecodeGraphBuilderNamedStore) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return p1.val = 20;",
       {factory->NewNumberFromInt(20),
        BytecodeGraphTester::NewObject("({val : 10})")}},
      {"p1.type = 'int'; return p1.type;",
       {factory->NewStringFromStaticChars("int"),
        BytecodeGraphTester::NewObject("({val : 10})")}},
      {"p1.name = 'def'; return p1[\"name\"];",
       {factory->NewStringFromStaticChars("def"),
        BytecodeGraphTester::NewObject("({name : 'abc'})")}},
      {"'use strict'; p1.val = 20; return p1.val;",
       {factory->NewNumberFromInt(20),
        BytecodeGraphTester::NewObject("({val : 10 })")}},
      {"'use strict'; return p1.type = 'int';",
       {factory->NewStringFromStaticChars("int"),
        BytecodeGraphTester::NewObject("({val : 10})")}},
      {"'use strict'; p1.val = 20; return p1[\"val\"];",
       {factory->NewNumberFromInt(20),
        BytecodeGraphTester::NewObject("({val : 10, name : 'abc'})")}},
      {"var b = 'abc';\n" REPEAT_127(
           SPACE, " p1.name = b; ") " p1.name = 'def'; return p1.name;\n",
       {factory->NewStringFromStaticChars("def"),
        BytecodeGraphTester::NewObject("({name : 'abc'})")}},
      {"'use strict'; var b = 'def';\n" REPEAT_127(
           SPACE, " p1.name = 'abc'; ") "p1.name = b; return p1.name;\n",
       {factory->NewStringFromStaticChars("def"),
        BytecodeGraphTester::NewObject("({ name : 'abc'})")}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(3072);
    SNPrintF(script, "function %s(p1) { %s };\n%s({});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, zone, script.start());
    auto callable = tester.GetCallable<Handle<Object>>();
    Handle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


TEST(BytecodeGraphBuilderKeyedStore) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<2> snippets[] = {
      {"p1[p2] = 20; return p1[p2];",
       {factory->NewNumberFromInt(20),
        BytecodeGraphTester::NewObject("({val : 10})"),
        factory->NewStringFromStaticChars("val")}},
      {"return p1[100] = 'def';",
       {factory->NewStringFromStaticChars("def"),
        BytecodeGraphTester::NewObject("({100 : 'abc'})"),
        factory->NewNumberFromInt(0)}},
      {"var b = 100; p1[b] = 'def'; return p1[b];",
       {factory->NewStringFromStaticChars("def"),
        BytecodeGraphTester::NewObject("({100 : 'abc'})"),
        factory->NewNumberFromInt(0)}},
      {"'use strict'; p1[p2] = 20; return p1[p2];",
       {factory->NewNumberFromInt(20),
        BytecodeGraphTester::NewObject("({val : 10 })"),
        factory->NewStringFromStaticChars("val")}},
      {"'use strict'; return p1[100] = 20;",
       {factory->NewNumberFromInt(20),
        BytecodeGraphTester::NewObject("({100 : 10})"),
        factory->NewNumberFromInt(0)}},
      {"'use strict'; var b = p2; p1[b] = 'def'; return p1[b];",
       {factory->NewStringFromStaticChars("def"),
        BytecodeGraphTester::NewObject("({100 : 'abc'})"),
        factory->NewNumberFromInt(100)}},
      {"var b;\n" REPEAT_127(
           SPACE, " b = p1[p2]; ") " p1[p2] = 'def'; return p1[p2];\n",
       {factory->NewStringFromStaticChars("def"),
        BytecodeGraphTester::NewObject("({100 : 'abc'})"),
        factory->NewNumberFromInt(100)}},
      {"'use strict'; var b;\n" REPEAT_127(
           SPACE, " b = p1[p2]; ") " p1[p2] = 'def'; return p1[p2];\n",
       {factory->NewStringFromStaticChars("def"),
        BytecodeGraphTester::NewObject("({ 100 : 'abc'})"),
        factory->NewNumberFromInt(100)}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(2048);
    SNPrintF(script, "function %s(p1, p2) { %s };\n%s({});", kFunctionName,
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


TEST(BytecodeGraphBuilderCallNew) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"function counter() { this.count = 20; }\n"
       "function f() {\n"
       "  var c = new counter();\n"
       "  return c.count;\n"
       "}; f()",
       {factory->NewNumberFromInt(20)}},
      {"function counter(arg0) { this.count = 17; this.x = arg0; }\n"
       "function f() {\n"
       "  var c = new counter(6);\n"
       "  return c.count + c.x;\n"
       "}; f()",
       {factory->NewNumberFromInt(23)}},
      {"function counter(arg0, arg1) {\n"
       "  this.count = 17; this.x = arg0; this.y = arg1;\n"
       "}\n"
       "function f() {\n"
       "  var c = new counter(3, 5);\n"
       "  return c.count + c.x + c.y;\n"
       "}; f()",
       {factory->NewNumberFromInt(25)}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    BytecodeGraphTester tester(isolate, zone, snippets[i].code_snippet);
    auto callable = tester.GetCallable<>();
    Handle<Object> return_value = callable().ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


TEST(BytecodeGraphBuilderCreateClosure) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"function f() {\n"
       "  function counter() { this.count = 20; }\n"
       "  var c = new counter();\n"
       "  return c.count;\n"
       "}; f()",
       {factory->NewNumberFromInt(20)}},
      {"function f() {\n"
       "  function counter(arg0) { this.count = 17; this.x = arg0; }\n"
       "  var c = new counter(6);\n"
       "  return c.count + c.x;\n"
       "}; f()",
       {factory->NewNumberFromInt(23)}},
      {"function f() {\n"
       "  function counter(arg0, arg1) {\n"
       "    this.count = 17; this.x = arg0; this.y = arg1;\n"
       "  }\n"
       "  var c = new counter(3, 5);\n"
       "  return c.count + c.x + c.y;\n"
       "}; f()",
       {factory->NewNumberFromInt(25)}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    BytecodeGraphTester tester(isolate, zone, snippets[i].code_snippet);
    auto callable = tester.GetCallable<>();
    Handle<Object> return_value = callable().ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


TEST(BytecodeGraphBuilderCallRuntime) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"function f(arg0) { return %MaxSmi(); }\nf()",
       {factory->NewNumberFromInt(Smi::kMaxValue), factory->undefined_value()}},
      {"function f(arg0) { return %IsArray(arg0) }\nf(undefined)",
       {factory->true_value(), BytecodeGraphTester::NewObject("[1, 2, 3]")}},
      {"function f(arg0) { return %Add(arg0, 2) }\nf(1)",
       {factory->NewNumberFromInt(5), factory->NewNumberFromInt(3)}},
      {"function f(arg0) { return %spread_arguments(arg0).length }\nf([])",
       {factory->NewNumberFromInt(3),
        BytecodeGraphTester::NewObject("[1, 2, 3]")}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    BytecodeGraphTester tester(isolate, zone, snippets[i].code_snippet);
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


TEST(BytecodeGraphBuilderCast) {
  // TODO(mythria): tests for ToBoolean, ToObject, ToName, ToNumber.
  // They need other unimplemented features to test.
  // ToBoolean -> If
  // ToObject -> ForIn
  // ToNumber -> Inc/Dec
  // ToName -> CreateObjectLiteral
}


TEST(BytecodeGraphBuilderLogicalNot) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return !p1;",
       {factory->false_value(),
        BytecodeGraphTester::NewObject("({val : 10})")}},
      {"return !p1;", {factory->true_value(), factory->NewNumberFromInt(0)}},
      {"return !p1;", {factory->true_value(), factory->undefined_value()}},
      {"return !p1;", {factory->false_value(), factory->NewNumberFromInt(10)}},
      {"return !p1;", {factory->false_value(), factory->true_value()}},
      {"return !p1;",
       {factory->false_value(), factory->NewStringFromStaticChars("abc")}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1) { %s }\n%s({});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, zone, script.start());
    auto callable = tester.GetCallable<Handle<Object>>();
    Handle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


TEST(BytecodeGraphBuilderTypeOf) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return typeof p1;",
       {factory->NewStringFromStaticChars("object"),
        BytecodeGraphTester::NewObject("({val : 10})")}},
      {"return typeof p1;",
       {factory->NewStringFromStaticChars("undefined"),
        factory->undefined_value()}},
      {"return typeof p1;",
       {factory->NewStringFromStaticChars("number"),
        factory->NewNumberFromInt(10)}},
      {"return typeof p1;",
       {factory->NewStringFromStaticChars("boolean"), factory->true_value()}},
      {"return typeof p1;",
       {factory->NewStringFromStaticChars("string"),
        factory->NewStringFromStaticChars("abc")}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1) { %s }\n%s({});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, zone, script.start());
    auto callable = tester.GetCallable<Handle<Object>>();
    Handle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


TEST(BytecodeGraphBuilderCountOperation) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return ++p1;",
       {factory->NewNumberFromInt(11), factory->NewNumberFromInt(10)}},
      {"return p1++;",
       {factory->NewNumberFromInt(10), factory->NewNumberFromInt(10)}},
      {"return p1++ + 10;",
       {factory->NewHeapNumber(15.23), factory->NewHeapNumber(5.23)}},
      {"return 20 + ++p1;",
       {factory->NewHeapNumber(27.23), factory->NewHeapNumber(6.23)}},
      {"return --p1;",
       {factory->NewHeapNumber(9.8), factory->NewHeapNumber(10.8)}},
      {"return p1--;",
       {factory->NewHeapNumber(10.8), factory->NewHeapNumber(10.8)}},
      {"return p1-- + 10;",
       {factory->NewNumberFromInt(20), factory->NewNumberFromInt(10)}},
      {"return 20 + --p1;",
       {factory->NewNumberFromInt(29), factory->NewNumberFromInt(10)}},
      {"return p1.val--;",
       {factory->NewNumberFromInt(10),
        BytecodeGraphTester::NewObject("({val : 10})")}},
      {"return ++p1['val'];",
       {factory->NewNumberFromInt(11),
        BytecodeGraphTester::NewObject("({val : 10})")}},
      {"return ++p1[1];",
       {factory->NewNumberFromInt(11),
        BytecodeGraphTester::NewObject("({1 : 10})")}},
      {" function inner() { return p1 } return --p1;",
       {factory->NewNumberFromInt(9), factory->NewNumberFromInt(10)}},
      {" function inner() { return p1 } return p1--;",
       {factory->NewNumberFromInt(10), factory->NewNumberFromInt(10)}},
      {"return ++p1;",
       {factory->nan_value(), factory->NewStringFromStaticChars("String")}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1) { %s }\n%s({});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, zone, script.start());
    auto callable = tester.GetCallable<Handle<Object>>();
    Handle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


TEST(BytecodeGraphBuilderDelete) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return delete p1.val;",
       {factory->true_value(), BytecodeGraphTester::NewObject("({val : 10})")}},
      {"delete p1.val; return p1.val;",
       {factory->undefined_value(),
        BytecodeGraphTester::NewObject("({val : 10})")}},
      {"delete p1.name; return p1.val;",
       {factory->NewNumberFromInt(10),
        BytecodeGraphTester::NewObject("({val : 10, name:'abc'})")}},
      {"'use strict'; return delete p1.val;",
       {factory->true_value(), BytecodeGraphTester::NewObject("({val : 10})")}},
      {"'use strict'; delete p1.val; return p1.val;",
       {factory->undefined_value(),
        BytecodeGraphTester::NewObject("({val : 10})")}},
      {"'use strict'; delete p1.name; return p1.val;",
       {factory->NewNumberFromInt(10),
        BytecodeGraphTester::NewObject("({val : 10, name:'abc'})")}},
      // TODO(mythria): Add tests for global and unallocated when we have
      // support for LdaContextSlot
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1) { %s }\n%s({});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, zone, script.start());
    auto callable = tester.GetCallable<Handle<Object>>();
    Handle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


bool get_compare_result(Token::Value opcode, Handle<Object> lhs_value,
                        Handle<Object> rhs_value) {
  switch (opcode) {
    case Token::Value::EQ:
      return Object::Equals(lhs_value, rhs_value).FromJust();
    case Token::Value::NE:
      return !Object::Equals(lhs_value, rhs_value).FromJust();
    case Token::Value::EQ_STRICT:
      return lhs_value->StrictEquals(*rhs_value);
    case Token::Value::NE_STRICT:
      return !lhs_value->StrictEquals(*rhs_value);
    case Token::Value::LT:
      return Object::LessThan(lhs_value, rhs_value).FromJust();
    case Token::Value::LTE:
      return Object::LessThanOrEqual(lhs_value, rhs_value).FromJust();
    case Token::Value::GT:
      return Object::GreaterThan(lhs_value, rhs_value).FromJust();
    case Token::Value::GTE:
      return Object::GreaterThanOrEqual(lhs_value, rhs_value).FromJust();
    default:
      UNREACHABLE();
      return false;
  }
}


const char* get_code_snippet(Token::Value opcode) {
  switch (opcode) {
    case Token::Value::EQ:
      return "return p1 == p2;";
    case Token::Value::NE:
      return "return p1 != p2;";
    case Token::Value::EQ_STRICT:
      return "return p1 === p2;";
    case Token::Value::NE_STRICT:
      return "return p1 !== p2;";
    case Token::Value::LT:
      return "return p1 < p2;";
    case Token::Value::LTE:
      return "return p1 <= p2;";
    case Token::Value::GT:
      return "return p1 > p2;";
    case Token::Value::GTE:
      return "return p1 >= p2;";
    default:
      UNREACHABLE();
      return "";
  }
}


TEST(BytecodeGraphBuilderCompare) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();
  Handle<Object> lhs_values[] = {
      factory->NewNumberFromInt(10), factory->NewHeapNumber(3.45),
      factory->NewStringFromStaticChars("abc"),
      factory->NewNumberFromInt(SMI_MAX), factory->NewNumberFromInt(SMI_MIN)};
  Handle<Object> rhs_values[] = {factory->NewNumberFromInt(10),
                                 factory->NewStringFromStaticChars("10"),
                                 factory->NewNumberFromInt(20),
                                 factory->NewStringFromStaticChars("abc"),
                                 factory->NewHeapNumber(3.45),
                                 factory->NewNumberFromInt(SMI_MAX),
                                 factory->NewNumberFromInt(SMI_MIN)};

  for (size_t i = 0; i < arraysize(kCompareOperators); i++) {
    ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1, p2) { %s }\n%s({}, {});", kFunctionName,
             get_code_snippet(kCompareOperators[i]), kFunctionName);

    BytecodeGraphTester tester(isolate, zone, script.start());
    auto callable = tester.GetCallable<Handle<Object>, Handle<Object>>();
    for (size_t j = 0; j < arraysize(lhs_values); j++) {
      for (size_t k = 0; k < arraysize(rhs_values); k++) {
        Handle<Object> return_value =
            callable(lhs_values[j], rhs_values[k]).ToHandleChecked();
        bool result = get_compare_result(kCompareOperators[i], lhs_values[j],
                                         rhs_values[k]);
        CHECK(return_value->SameValue(*factory->ToBoolean(result)));
      }
    }
  }
}


TEST(BytecodeGraphBuilderTestIn) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<2> snippets[] = {
      {"return p2 in p1;",
       {factory->true_value(), BytecodeGraphTester::NewObject("({val : 10})"),
        factory->NewStringFromStaticChars("val")}},
      {"return p2 in p1;",
       {factory->true_value(), BytecodeGraphTester::NewObject("[]"),
        factory->NewStringFromStaticChars("length")}},
      {"return p2 in p1;",
       {factory->true_value(), BytecodeGraphTester::NewObject("[]"),
        factory->NewStringFromStaticChars("toString")}},
      {"return p2 in p1;",
       {factory->true_value(), BytecodeGraphTester::NewObject("({val : 10})"),
        factory->NewStringFromStaticChars("toString")}},
      {"return p2 in p1;",
       {factory->false_value(), BytecodeGraphTester::NewObject("({val : 10})"),
        factory->NewStringFromStaticChars("abc")}},
      {"return p2 in p1;",
       {factory->false_value(), BytecodeGraphTester::NewObject("({val : 10})"),
        factory->NewNumberFromInt(10)}},
      {"return p2 in p1;",
       {factory->true_value(), BytecodeGraphTester::NewObject("({10 : 'val'})"),
        factory->NewNumberFromInt(10)}},
      {"return p2 in p1;",
       {factory->false_value(),
        BytecodeGraphTester::NewObject("({10 : 'val'})"),
        factory->NewNumberFromInt(1)}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1, p2) { %s }\n%s({}, {});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, zone, script.start());
    auto callable = tester.GetCallable<Handle<Object>, Handle<Object>>();
    Handle<Object> return_value =
        callable(snippets[i].parameter(0), snippets[i].parameter(1))
            .ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


TEST(BytecodeGraphBuilderTestInstanceOf) {
  // TODO(mythria): Add tests when CreateLiterals/CreateClousre are supported.
}


TEST(BytecodeGraphBuilderThrow) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();

  // TODO(mythria): Add more tests when real try-catch and deoptimization
  // information are supported.
  ExpectedSnippet<0, const char*> snippets[] = {
      {"throw undefined;", {"Uncaught undefined"}},
      {"throw 1;", {"Uncaught 1"}},
      {"throw 'Error';", {"Uncaught Error"}},
      {"throw 'Error1'; throw 'Error2'", {"Uncaught Error1"}},
      // TODO(mythria): Enable these tests when JumpIfTrue is supported.
      // {"var a = true; if (a) { throw 'Error'; }", {"Error"}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);
    BytecodeGraphTester tester(isolate, zone, script.start());
    v8::Local<v8::String> message = tester.CheckThrowsReturnMessage()->Get();
    v8::Local<v8::String> expected_string = v8_str(snippets[i].return_value());
    CHECK(
        message->Equals(CcTest::isolate()->GetCurrentContext(), expected_string)
            .FromJust());
  }
}


TEST(BytecodeGraphBuilderContext) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"var x = 'outer';"
       "function f() {"
       " 'use strict';"
       " {"
       "   let x = 'inner';"
       "   (function() {x});"
       " }"
       "return(x);"
       "}"
       "f();",
       {factory->NewStringFromStaticChars("outer")}},
      {"var x = 'outer';"
       "function f() {"
       " 'use strict';"
       " {"
       "   let x = 'inner ';"
       "   var innerFunc = function() {return x};"
       " }"
       "return(innerFunc() + x);"
       "}"
       "f();",
       {factory->NewStringFromStaticChars("inner outer")}},
      {"var x = 'outer';"
       "function f() {"
       " 'use strict';"
       " {"
       "   let x = 'inner ';"
       "   var innerFunc = function() {return x;};"
       "   {"
       "     let x = 'innermost ';"
       "     var innerMostFunc = function() {return x + innerFunc();};"
       "   }"
       "   x = 'inner_changed ';"
       " }"
       " return(innerMostFunc() + x);"
       "}"
       "f();",
       {factory->NewStringFromStaticChars("innermost inner_changed outer")}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(1024);
    SNPrintF(script, "%s", snippets[i].code_snippet);

    BytecodeGraphTester tester(isolate, zone, script.start(), "f");
    auto callable = tester.GetCallable<>("f");
    Handle<Object> return_value = callable().ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}


TEST(BytecodeGraphBuilderLoadContext) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"function Outer() {"
       "  var outerVar = 2;"
       "  function Inner(innerArg) {"
       "    this.innerFunc = function () {"
       "     return outerVar * innerArg;"
       "    };"
       "  };"
       "  this.getInnerFunc = function GetInner() {"
       "     return new Inner(3).innerFunc;"
       "   }"
       "}"
       "var f = new Outer().getInnerFunc();"
       "f();",
       {factory->NewNumberFromInt(6), factory->undefined_value()}},
      {"function Outer() {"
       "  var outerVar = 2;"
       "  function Inner(innerArg) {"
       "    this.innerFunc = function () {"
       "     outerVar = innerArg; return outerVar;"
       "    };"
       "  };"
       "  this.getInnerFunc = function GetInner() {"
       "     return new Inner(10).innerFunc;"
       "   }"
       "}"
       "var f = new Outer().getInnerFunc();"
       "f();",
       {factory->NewNumberFromInt(10), factory->undefined_value()}},
      {"function testOuter(outerArg) {"
       " this.testinnerFunc = function testInner(innerArg) {"
       "   return innerArg + outerArg;"
       " }"
       "}"
       "var f = new testOuter(10).testinnerFunc;"
       "f(0);",
       {factory->NewNumberFromInt(14), factory->NewNumberFromInt(4)}},
      {"function testOuter(outerArg) {"
       " var outerVar = outerArg * 2;"
       " this.testinnerFunc = function testInner(innerArg) {"
       "   outerVar = outerVar + innerArg; return outerVar;"
       " }"
       "}"
       "var f = new testOuter(10).testinnerFunc;"
       "f(0);",
       {factory->NewNumberFromInt(24), factory->NewNumberFromInt(4)}}};

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    ScopedVector<char> script(1024);
    SNPrintF(script, "%s", snippets[i].code_snippet);

    BytecodeGraphTester tester(isolate, zone, script.start(), "*");
    auto callable = tester.GetCallable<Handle<Object>>("f");
    Handle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(return_value->SameValue(*snippets[i].return_value()));
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
