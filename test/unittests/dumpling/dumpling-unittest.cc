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

TEST_F(DumplingTest, InterpreterHeapNumberParams) {
  const char* program =
      "function foo(x, y) {\n"
      "  return x + y;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "foo(3.14, 2.5);\n";

  // Check that we see the start frame of "foo" with the parameters a0 and a1.
  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"            // Bytecode offset 0
                         R"(f:\d+\s+)"          // Function id can be anything
                         R"(x:<undefined>\s+)"  // Accumulator
                         R"(n:2\s+)"            // Number of params
                         R"(m:0\s+)"            // Number of registers
                         R"(a0:3.14\s+)"
                         R"(a1:2.5\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterOddballParams) {
  const char* program =
      "function foo(x, y, z, z2) {\n"
      "  return x ? (y ? z : z2) : y;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "foo(true, false, null, undefined);\n";

  // Check that we see the start frame of "foo" with the parameters a0 and a1.
  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"    // Bytecode offset 0
                         R"(f:\d+\s+)"  // Function id can be anything
                         R"(n:4\s+)"    // Number of params
                         R"(m:0\s+)"    // Number of registers
                         R"(a0:<true>\s+)"
                         R"(a1:<false>\s+)"
                         R"(a2:<null>\s+)"
                         R"(a3:<undefined>\s+)";

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

TEST_F(DumplingTest, InterpreterGlobalObject) {
  const char* program =
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "foo(globalThis);\n";

  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"            // Bytecode offset 0
                         R"(f:\d+\s+)"          // Function id can be anything
                         R"(x:<undefined>\s+)"  // Accumulator
                         R"(n:1\s+)"            // Number of params
                         R"(m:0\s+)"            // Number of registers
                         R"(a0:<global object>\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterDictionaryModeObject) {
  const char* program =
      "let obj = {};\n"
      "for (let i = 0; i < 20; ++i) {\n"
      "  obj['p' + i] = 0;\n"
      "}\n"
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "foo(obj);\n";

  const char* expected =
      R"(---I\s+)"
      R"(b:0\s+)"            // Bytecode offset 0
      R"(f:\d+\s+)"          // Function id can be anything
      R"(x:<undefined>\s+)"  // Accumulator
      R"(n:1\s+)"            // Number of params
      R"(m:0\s+)"            // Number of registers
      R"(a0:<Object>\{)"
      // Verify that the dictionary properties are printed in the standard order
      // (here we verify only the beginning).
      R"(p0\[WEC\]0, p1\[WEC\]0, p2\[WEC\]0, p3\[WEC\]0, p4\[WEC\]0, p5\[WEC\]0,.*)"
      R"(\}\s+)";

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
                           R"(a0:<JSArray>.*[1,2,3,]\s+)";

    RunInterpreterTest(program, expected);
  }
}

TEST_F(DumplingTest, InterpreterInstanceOfClass) {
  const char* program =
      "class MyClass {\n"
      "  constructor() { this.x = 1; }\n"
      "  method() { return 2; }\n"
      "};\n"
      "let obj = new MyClass();\n"
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "foo(obj);\n";

  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"            // Bytecode offset 0
                         R"(f:\d+\s+)"          // Function id can be anything
                         R"(x:<undefined>\s+)"  // Accumulator
                         R"(n:1\s+)"            // Number of params
                         R"(m:0\s+)"            // Number of registers
                         R"(a0:<MyClass>\{x\[WEC\]1\}__proto__:<MyClass>.*\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterBigIntParams) {
  const char* program =
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "foo(1234567890123456789n);\n";

  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"            // Bytecode offset 0
                         R"(f:\d+\s+)"          // Function id can be anything
                         R"(x:<undefined>\s+)"  // Accumulator
                         R"(n:1\s+)"            // Number of params
                         R"(m:0\s+)"            // Number of registers
                         R"(a0:<BigIntBase 1234567890123456789>\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterHoleySmiElements) {
  const char* program =
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "const arr = [1, 2];\n"
      "arr[3] = 4;\n"
      "foo(arr);\n";

  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"
                         R"(f:\d+\s+)"
                         R"(x:<undefined>\s+)"
                         R"(n:1\s+)"
                         R"(m:0\s+)"
                         R"(a0:<JSArray>.*\[1,2,2-2:the_hole,4,\]\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterConsecutiveHoles) {
  const char* program =
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "const arr = [1];\n"
      "arr[4] = 5;\n"
      "foo(arr);\n";

  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"
                         R"(f:\d+\s+)"
                         R"(x:<undefined>\s+)"
                         R"(n:1\s+)"
                         R"(m:0\s+)"
                         R"(a0:<JSArray>.*\[1,1-3:the_hole,5,\]\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterDoubleElements) {
  const char* program =
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "const arr = [1.5, 2.25];\n"
      "foo(arr);\n";

  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"
                         R"(f:\d+\s+)"
                         R"(x:<undefined>\s+)"
                         R"(n:1\s+)"
                         R"(m:0\s+)"
                         R"(a0:<JSArray>.*\[1\.50*,2\.250*,\]\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterHoleyDoubleElements) {
  const char* program =
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "const arr = [1.5];\n"
      "arr[2] = 2.5;\n"
      "foo(arr);\n";

  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"
                         R"(f:\d+\s+)"
                         R"(x:<undefined>\s+)"
                         R"(n:1\s+)"
                         R"(m:0\s+)"
                         R"(a0:<JSArray>.*\[1\.50*,1-1:the_hole,2\.50*,\]\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterObjectElements) {
  const char* program =
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "const arr = [{val: 10}, {val: 20}];\n"
      "foo(arr);\n";

  const char* expected =
      R"(---I\s+)"
      R"(b:0\s+)"
      R"(f:\d+\s+)"
      R"(x:<undefined>\s+)"
      R"(n:1\s+)"
      R"(m:0\s+)"
      R"(a0:<JSArray>.*\[<Object>\{val\[WEC\]10\},<Object>\{val\[WEC\]20\},\]\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterSanitizeStringValue) {
  const char* program =
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "foo('line1\\nline2');\n";

  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"
                         R"(f:\d+\s+)"
                         R"(x:<undefined>\s+)"
                         R"(n:1\s+)"
                         R"(m:0\s+)"
                         R"(a0:<String\[11\]: #line1\\nline2>\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterSanitizeObjectKey) {
  const char* program =
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "const obj = {'key\\nwith\\nnewline': 42};\n"
      "foo(obj);\n";

  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"
                         R"(f:\d+\s+)"
                         R"(x:<undefined>\s+)"
                         R"(n:1\s+)"
                         R"(m:0\s+)"
                         R"(a0:<Object>\{key\\nwith\\nnewline\[WEC\]42\}\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterSanitizeCarriageReturn) {
  const char* program =
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "foo('row1\\rrow2');\n";

  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"
                         R"(f:\d+\s+)"
                         R"(x:<undefined>\s+)"
                         R"(n:1\s+)"
                         R"(m:0\s+)"
                         R"(a0:<String\[9\]: #row1\\rrow2>\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterEmptyElements) {
  // Test that an empty array prints no brackets at all.
  const char* program =
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "const arr = [];\n"
      "foo(arr);\n";

  // Expect <JSArray> followed immediately by whitespace/end-of-line,
  // with NO "[]" or "[...]" printed.
  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"
                         R"(f:\d+\s+)"
                         R"(x:<undefined>\s+)"
                         R"(n:1\s+)"
                         R"(m:0\s+)"
                         R"(a0:<JSArray>\s+)";

  RunInterpreterTest(program, expected);
}

TEST_F(DumplingTest, InterpreterHolesOnly) {
  // Test that an array containing ONLY holes also prints no brackets.
  // [hole, hole, hole] -> Should behave like empty for printing.
  const char* program =
      "function foo(x) {\n"
      "  return x;\n"
      "}\n"
      "%PrepareFunctionForOptimization(foo);\n"
      "const arr = [1, 2, 3];\n"
      "delete arr[0];\n"
      "delete arr[1];\n"
      "delete arr[2];\n"
      "foo(arr);\n";

  const char* expected = R"(---I\s+)"
                         R"(b:0\s+)"
                         R"(f:\d+\s+)"
                         R"(x:<undefined>\s+)"
                         R"(n:1\s+)"
                         R"(m:0\s+)"
                         R"(a0:<JSArray>\s+)";

  RunInterpreterTest(program, expected);
}

}  // namespace v8

#endif  // V8_DUMPLING
