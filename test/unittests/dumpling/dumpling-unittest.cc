// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef V8_DUMPLING

#include <regex>
#include <string>

#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "src/dumpling/dumpling-manager.h"
#include "test/common/flag-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock-support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

class DumplingTest : public TestWithContext {
 public:
  bool CheckOutput(const std::string& output, const char* expected) {
    if (!std::regex_search(output, std::regex(expected))) {
      std::cout << "Output:" << std::endl << output << std::endl;
      std::cout << "Expected:" << std::endl << expected << std::endl;
      return false;
    }
    return true;
  }

  void RunInterpreterTest(const char* program, const char* expected) {
    i::FlagScope<bool> dumping_flag_scope(&i::v8_flags.interpreter_dumping,
                                          true);
    i::FlagScope<bool> allow_natives_syntax_scope(
        &i::v8_flags.allow_natives_syntax, true);

    v8::Isolate* isolate = this->isolate();
    v8::HandleScope scope(isolate);

    i::Isolate* i_isolate = reinterpret_cast<v8::internal::Isolate*>(isolate);
    i::DumplingManager* dm = i_isolate->dumpling_manager();
    dm->set_print_into_string(true);
    dm->PrepareForNextREPRLCycle();

    v8::Local<Value> result = RunJS(program);
    CHECK(!result.IsEmpty());
    const std::string& output = i_isolate->dumpling_manager()->GetOutput();

    EXPECT_TRUE(CheckOutput(output, expected));
    dm->FinishCurrentREPRLCycle();
  }
};

TEST_F(DumplingTest, InterpreterSmiParams) {
  const char* program =
      "function foo(x, y) {\n"
      "  return x + y;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "foo(10, 2);\n";

  // Check that we see the start frame of "foo" with the parameters a0 and a1.
  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"            // Bytecode offset 0
                         R"(f:\d+\s+)"          // Function id can be anything
                         R"(x:<undefined>\s+)"  // Accumulator
                         R"(n:2\s+)"            // Number of params
                         R"(m:0\s+)"            // Number of registers
                         R"(a0:10\s+)"
                         R"(a1:2\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterObjectWithObjectPrototype) {
  const char* program =
      "function foo(x) {\n"
      "  return x.a;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "foo({a: 100});\n";

  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"            // Bytecode offset 0
                         R"(f:\d+\s+)"          // Function id can be anything
                         R"(x:<undefined>\s+)"  // Accumulator
                         R"(n:1\s+)"            // Number of params
                         R"(m:0\s+)"            // Number of registers
                         R"(a0:<Object>\{a\[WEC\]100\}\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterObjectWithCustomPrototype) {
  const char* program =
      "function foo(x) {\n"
      "  return x.a;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "foo({a: 100, __proto__: {b: 200}});\n";

  const char* expected =
      R"(---I\s+)"
      R"(b:0\s+)"            // Bytecode offset 0
      R"(f:\d+\s+)"          // Function id can be anything
      R"(x:<undefined>\s+)"  // Accumulator
      R"(n:1\s+)"            // Number of params
      R"(m:0\s+)"            // Number of registers
      R"(a0:<Object>\{a\[WEC\]100\}__proto__:<Object>\{b\[WEC\]200\}\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterObjectTypes) {
  const char* base_program =
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n";
  RunInterpreterTest(base_program, "");

  // Normal function
  {
    const char* program = "foo(foo);\n";

    const char* expected = R"(---I\s+)"
                           R"(b:0\s+)"            // Bytecode offset 0
                           R"(f:\d+\s+)"          // Function id can be anything
                           R"(x:<undefined>\s+)"  // Accumulator
                           R"(n:1\s+)"            // Number of params
                           R"(m:0\s+)"            // Number of registers
                           // Properties and proto will be printed out too (but
                           // we don't list them here)
                           R"(a0:<JSFunction foo>.*\s+)";

    RunInterpreterTest(program, expected);
  }

  // A different type of a function (an async generator)
  {
    const char* program = "async function *gen() { } foo(gen);\n";

    const char* expected = R"(---I\s+)"
                           R"(b:0\s+)"            // Bytecode offset 0
                           R"(f:\d+\s+)"          // Function id can be anything
                           R"(x:<undefined>\s+)"  // Accumulator
                           R"(n:1\s+)"            // Number of params
                           R"(m:0\s+)"            // Number of registers
                           // Properties and proto will be printed out too (but
                           // we don't list them here)
                           R"(a0:<JSFunction gen>.*\s+)";

    RunInterpreterTest(program, expected);
  }

  // JavaScript standard object (here Set)
  {
    const char* program = "foo(new Set());\n";

    const char* expected =
        R"(---I\s+)"
        R"(b:0\s+)"            // Bytecode offset 0
        R"(f:\d+\s+)"          // Function id can be anything
        R"(x:<undefined>\s+)"  // Accumulator
        R"(n:1\s+)"            // Number of params
        R"(m:0\s+)"            // Number of registers
        // The proto will be printed out too (but we don't list it here)
        R"(a0:<Set>\{\}.*\s+)";

    RunInterpreterTest(program, expected);
  }

  // Object with a user-defined ctor
  {
    const char* program = "function myCtor() { } foo(new myCtor());\n";

    const char* expected = R"(---I\s+)"
                           R"(b:0\s+)"            // Bytecode offset 0
                           R"(f:\d+\s+)"          // Function id can be anything
                           R"(x:<undefined>\s+)"  // Accumulator
                           R"(n:1\s+)"            // Number of params
                           R"(m:0\s+)"            // Number of registers
                           R"(a0:<myCtor>\{\}__proto__:<Object>\{)"
                           R"(constructor\[W_C\]<JSFunction myCtor>.*)"
                           R"(\}\s+)";

    RunInterpreterTest(program, expected);
  }

  // Object with a nameless user-defined ctor
  {
    const char* program = "let obj = new (function() {})(); foo(obj);\n";

    const char* expected = R"(---I\s+)"
                           R"(b:0\s+)"            // Bytecode offset 0
                           R"(f:\d+\s+)"          // Function id can be anything
                           R"(x:<undefined>\s+)"  // Accumulator
                           R"(n:1\s+)"            // Number of params
                           R"(m:0\s+)"            // Number of registers
                           R"(a0:<JSObject>\{\}__proto__:<Object>\{)"
                           R"(constructor\[W_C\]<JSFunction >.*)"
                           R"(\}\s+)";

    RunInterpreterTest(program, expected);
  }

  // Array
  {
    const char* program = "foo([1, 2, 3]);\n";

    const char* expected = R"(---I\s+)"
                           R"(b:0\s+)"            // Bytecode offset 0
                           R"(f:\d+\s+)"          // Function id can be anything
                           R"(x:<undefined>\s+)"  // Accumulator
                           R"(n:1\s+)"            // Number of params
                           R"(m:0\s+)"            // Number of registers
                           // Properties and proto will be printed out too (but
                           // we don't list them here)
                           R"(a0:<JSArray>.*[1,2,3]\s+)";

    RunInterpreterTest(program, expected);
  }
}

}  // namespace v8

#endif  // V8_DUMPLING
