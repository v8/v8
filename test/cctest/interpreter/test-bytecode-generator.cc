// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/compiler.h"
#include "src/interpreter/bytecode-generator.h"
#include "src/interpreter/interpreter.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace interpreter {

class BytecodeGeneratorHelper {
 public:
  const char* kFunctionName = "f";

  BytecodeGeneratorHelper() {
    i::FLAG_ignition = true;
    i::FLAG_ignition_filter = kFunctionName;
    CcTest::i_isolate()->interpreter()->Initialize();
  }


  Handle<BytecodeArray> MakeBytecode(const char* script,
                                     const char* function_name) {
    CompileRun(script);
    Local<Function> function =
        Local<Function>::Cast(CcTest::global()->Get(v8_str(function_name)));
    i::Handle<i::JSFunction> js_function = v8::Utils::OpenHandle(*function);
    return handle(js_function->shared()->bytecode_array(), CcTest::i_isolate());
  }


  Handle<BytecodeArray> MakeBytecodeForFunctionBody(const char* body) {
    ScopedVector<char> program(1024);
    SNPrintF(program, "function %s() { %s }\n%s();", kFunctionName, body,
             kFunctionName);
    return MakeBytecode(program.start(), kFunctionName);
  }

  Handle<BytecodeArray> MakeBytecodeForFunction(const char* function) {
    ScopedVector<char> program(1024);
    SNPrintF(program, "%s\n%s();", function, kFunctionName);
    return MakeBytecode(program.start(), kFunctionName);
  }
};


// Structure for containing expected bytecode snippets.
struct ExpectedSnippet {
  const char* body;
  int frame_size;
  int parameter_count;
  int bytecode_length;
  const uint8_t bytecode[16];
};


// Helper macros for handcrafting bytecode sequences.
#define B(x) static_cast<uint8_t>(Bytecode::k##x)
#define U8(x) static_cast<uint8_t>((x) & 0xff)
#define R(x) static_cast<uint8_t>(-(x) & 0xff)


TEST(PrimitiveReturnStatements) {
  InitializedHandleScope handle_scope;
  BytecodeGeneratorHelper helper;

  ExpectedSnippet snippets[] = {
      {"return;", 0, 1, 2, {B(LdaUndefined), B(Return)}},
      {"return null;", 0, 1, 2, {B(LdaNull), B(Return)}},
      {"return true;", 0, 1, 2, {B(LdaTrue), B(Return)}},
      {"return false;", 0, 1, 2, {B(LdaFalse), B(Return)}},
      {"return 0;", 0, 1, 2, {B(LdaZero), B(Return)}},
      {"return +1;", 0, 1, 3, {B(LdaSmi8), U8(1), B(Return)}},
      {"return -1;", 0, 1, 3, {B(LdaSmi8), U8(-1), B(Return)}},
      {"return +127;", 0, 1, 3, {B(LdaSmi8), U8(127), B(Return)}},
      {"return -128;", 0, 1, 3, {B(LdaSmi8), U8(-128), B(Return)}},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    Handle<BytecodeArray> ba =
        helper.MakeBytecodeForFunctionBody(snippets[i].body);
    CHECK_EQ(ba->frame_size(), snippets[i].frame_size);
    CHECK_EQ(ba->parameter_count(), snippets[i].parameter_count);
    CHECK_EQ(ba->length(), snippets[i].bytecode_length);
    CHECK(!memcmp(ba->GetFirstBytecodeAddress(), snippets[i].bytecode,
                  ba->length()));
  }
}


TEST(PrimitiveExpressions) {
  InitializedHandleScope handle_scope;
  BytecodeGeneratorHelper helper;

  ExpectedSnippet snippets[] = {
      {"var x = 0; return x;",
       kPointerSize,
       1,
       6,
       {
           B(LdaZero),     //
           B(Star), R(0),  //
           B(Ldar), R(0),  //
           B(Return)       //
       }},
      {"var x = 0; return x + 3;",
       2 * kPointerSize,
       1,
       12,
       {
           B(LdaZero),         //
           B(Star), R(0),      //
           B(Ldar), R(0),      // Easy to spot r1 not really needed here.
           B(Star), R(1),      // Dead store.
           B(LdaSmi8), U8(3),  //
           B(Add), R(1),       //
           B(Return)           //
       }}};

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    Handle<BytecodeArray> ba =
        helper.MakeBytecodeForFunctionBody(snippets[i].body);
    CHECK_EQ(ba->frame_size(), snippets[i].frame_size);
    CHECK_EQ(ba->parameter_count(), snippets[i].parameter_count);
    CHECK_EQ(ba->length(), snippets[i].bytecode_length);
    CHECK(!memcmp(ba->GetFirstBytecodeAddress(), snippets[i].bytecode,
                  ba->length()));
  }
}


TEST(Parameters) {
  InitializedHandleScope handle_scope;
  BytecodeGeneratorHelper helper;

  int last_param_index =
      -InterpreterFrameConstants::kLastParamFromRegisterPointer / kPointerSize;
  ExpectedSnippet snippets[] = {
      {"function f() { return this; }",
       0, 1, 3, {B(Ldar), R(last_param_index), B(Return)}},
      {"function f(arg1) { return arg1; }",
       0, 2, 3, {B(Ldar), R(last_param_index), B(Return)}},
      {"function f(arg1) { return this; }",
       0, 2, 3, {B(Ldar), R(last_param_index - 1), B(Return)}},
      {"function f(arg1, arg2, arg3, arg4, arg5, arg6, arg7) { return arg4; }",
       0, 8, 3, {B(Ldar), R(last_param_index - 3), B(Return)}},
      {"function f(arg1, arg2, arg3, arg4, arg5, arg6, arg7) { return this; }",
       0, 8, 3, {B(Ldar), R(last_param_index - 7), B(Return)}}
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    Handle<BytecodeArray> ba = helper.MakeBytecodeForFunction(snippets[i].body);
    CHECK_EQ(ba->frame_size(), snippets[i].frame_size);
    CHECK_EQ(ba->parameter_count(), snippets[i].parameter_count);
    CHECK_EQ(ba->length(), snippets[i].bytecode_length);
    CHECK(!memcmp(ba->GetFirstBytecodeAddress(), snippets[i].bytecode,
                  ba->length()));
  }
}

}  // namespace interpreter
}  // namespace internal
}  // namespance v8
