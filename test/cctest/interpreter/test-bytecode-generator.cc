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

  const int kLastParamIndex =
      -InterpreterFrameConstants::kLastParamFromRegisterPointer / kPointerSize;

  BytecodeGeneratorHelper() {
    i::FLAG_vector_stores = true;
    i::FLAG_ignition = true;
    i::FLAG_ignition_filter = kFunctionName;
    CcTest::i_isolate()->interpreter()->Initialize();
  }


  Factory* factory() { return CcTest::i_isolate()->factory(); }


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
template<typename T>
struct ExpectedSnippet {
  const char* code_snippet;
  int frame_size;
  int parameter_count;
  int bytecode_length;
  const uint8_t bytecode[24];
  int constant_count;
  T constants[16];
};


// Helper macros for handcrafting bytecode sequences.
#define B(x) static_cast<uint8_t>(Bytecode::k##x)
#define U8(x) static_cast<uint8_t>((x) & 0xff)
#define R(x) static_cast<uint8_t>(-(x) & 0xff)


TEST(PrimitiveReturnStatements) {
  InitializedHandleScope handle_scope;
  BytecodeGeneratorHelper helper;

  ExpectedSnippet<void*> snippets[] = {
      {"", 0, 1, 2, {B(LdaUndefined), B(Return)}, 0},
      {"return;", 0, 1, 2, {B(LdaUndefined), B(Return)}, 0},
      {"return null;", 0, 1, 2, {B(LdaNull), B(Return)}, 0},
      {"return true;", 0, 1, 2, {B(LdaTrue), B(Return)}, 0},
      {"return false;", 0, 1, 2, {B(LdaFalse), B(Return)}, 0},
      {"return 0;", 0, 1, 2, {B(LdaZero), B(Return)}, 0},
      {"return +1;", 0, 1, 3, {B(LdaSmi8), U8(1), B(Return)}, 0},
      {"return -1;", 0, 1, 3, {B(LdaSmi8), U8(-1), B(Return)}, 0},
      {"return +127;", 0, 1, 3, {B(LdaSmi8), U8(127), B(Return)}, 0},
      {"return -128;", 0, 1, 3, {B(LdaSmi8), U8(-128), B(Return)}, 0},
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    Handle<BytecodeArray> ba =
        helper.MakeBytecodeForFunctionBody(snippets[i].code_snippet);
    CHECK_EQ(ba->frame_size(), snippets[i].frame_size);
    CHECK_EQ(ba->parameter_count(), snippets[i].parameter_count);
    CHECK_EQ(ba->length(), snippets[i].bytecode_length);
    CHECK(!memcmp(ba->GetFirstBytecodeAddress(), snippets[i].bytecode,
                  ba->length()));
    CHECK_EQ(ba->constant_pool(), CcTest::heap()->empty_fixed_array());
  }
}


TEST(PrimitiveExpressions) {
  InitializedHandleScope handle_scope;
  BytecodeGeneratorHelper helper;

  ExpectedSnippet<void*> snippets[] = {
      {"var x = 0; return x;",
       kPointerSize,
       1,
       6,
       {
           B(LdaZero),     //
           B(Star), R(0),  //
           B(Ldar), R(0),  //
           B(Return)       //
       },
       0
      },
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
       },
       0
     }};

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    Handle<BytecodeArray> ba =
        helper.MakeBytecodeForFunctionBody(snippets[i].code_snippet);
    CHECK_EQ(ba->frame_size(), snippets[i].frame_size);
    CHECK_EQ(ba->parameter_count(), snippets[i].parameter_count);
    CHECK_EQ(ba->length(), snippets[i].bytecode_length);
    CHECK(!memcmp(ba->GetFirstBytecodeAddress(), snippets[i].bytecode,
                  ba->length()));
    CHECK_EQ(ba->constant_pool(), CcTest::heap()->empty_fixed_array());
  }
}


TEST(Parameters) {
  InitializedHandleScope handle_scope;
  BytecodeGeneratorHelper helper;

  ExpectedSnippet<void*> snippets[] = {
      {"function f() { return this; }",
       0, 1, 3, {B(Ldar), R(helper.kLastParamIndex), B(Return)}, 0},
      {"function f(arg1) { return arg1; }",
       0, 2, 3, {B(Ldar), R(helper.kLastParamIndex), B(Return)}, 0},
      {"function f(arg1) { return this; }",
       0, 2, 3, {B(Ldar), R(helper.kLastParamIndex - 1), B(Return)}, 0},
      {"function f(arg1, arg2, arg3, arg4, arg5, arg6, arg7) { return arg4; }",
       0, 8, 3, {B(Ldar), R(helper.kLastParamIndex - 3), B(Return)}, 0},
      {"function f(arg1, arg2, arg3, arg4, arg5, arg6, arg7) { return this; }",
       0, 8, 3, {B(Ldar), R(helper.kLastParamIndex - 7), B(Return)}, 0}
  };

  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    Handle<BytecodeArray> ba =
        helper.MakeBytecodeForFunction(snippets[i].code_snippet);
    CHECK_EQ(ba->frame_size(), snippets[i].frame_size);
    CHECK_EQ(ba->parameter_count(), snippets[i].parameter_count);
    CHECK_EQ(ba->length(), snippets[i].bytecode_length);
    CHECK(!memcmp(ba->GetFirstBytecodeAddress(), snippets[i].bytecode,
                  ba->length()));
    CHECK_EQ(ba->constant_pool(), CcTest::heap()->empty_fixed_array());
  }
}


TEST(Constants) {
  InitializedHandleScope handle_scope;
  BytecodeGeneratorHelper helper;

  // Check large SMIs.
  {
    ExpectedSnippet<int> snippets[] = {
        {"return 12345678;", 0, 1, 3,
         {
            B(LdaConstant), U8(0),
            B(Return)
         }, 1, { 12345678 }
        },
        {"var a = 1234; return 5678;", 1 * kPointerSize, 1, 7,
         {
            B(LdaConstant), U8(0),
            B(Star), R(0),
            B(LdaConstant), U8(1),
            B(Return)
         }, 2, { 1234, 5678 }
        },
        {"var a = 1234; return 1234;",
         1 * kPointerSize, 1, 7,
         {
            B(LdaConstant), U8(0),
            B(Star), R(0),
            B(LdaConstant), U8(0),
            B(Return)
         }, 1, { 1234 }
        }
    };

    size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
    for (size_t i = 0; i < num_snippets; i++) {
      Handle<BytecodeArray> ba =
          helper.MakeBytecodeForFunctionBody(snippets[i].code_snippet);
      CHECK_EQ(ba->frame_size(), snippets[i].frame_size);
      CHECK_EQ(ba->parameter_count(), snippets[i].parameter_count);
      CHECK_EQ(ba->length(), snippets[i].bytecode_length);
      CHECK(!memcmp(ba->GetFirstBytecodeAddress(), snippets[i].bytecode,
                    ba->length()));
      CHECK_EQ(ba->constant_pool()->length(), snippets[i].constant_count);
      for (int j = 0; j < snippets[i].constant_count; j++) {
        CHECK_EQ(Smi::cast(ba->constant_pool()->get(j))->value(),
                 snippets[i].constants[j]);
      }
    }
  }

  // Check heap number double constants
  {
    ExpectedSnippet<double> snippets[] = {
        {"return 1.2;",
         0, 1, 3,
         {
            B(LdaConstant), U8(0),
            B(Return)
         }, 1, { 1.2 }
        },
        {"var a = 1.2; return 2.6;", 1 * kPointerSize, 1, 7,
         {
            B(LdaConstant), U8(0),
            B(Star), R(0),
            B(LdaConstant), U8(1),
            B(Return)
         }, 2, { 1.2, 2.6 }
        },
        {"var a = 3.14; return 3.14;", 1 * kPointerSize, 1, 7,
         {
            B(LdaConstant), U8(0),
            B(Star), R(0),
            B(LdaConstant), U8(1),
            B(Return)
         }, 2,
         // TODO(rmcilroy): Currently multiple identical double literals end up
         // being allocated as new HeapNumbers and so require multiple constant
         // pool entries. De-dup identical values.
         { 3.14, 3.14 }
        }
    };

    size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
    for (size_t i = 0; i < num_snippets; i++) {
      Handle<BytecodeArray> ba =
          helper.MakeBytecodeForFunctionBody(snippets[i].code_snippet);
      CHECK_EQ(ba->frame_size(), snippets[i].frame_size);
      CHECK_EQ(ba->parameter_count(), snippets[i].parameter_count);
      CHECK_EQ(ba->length(), snippets[i].bytecode_length);
      CHECK(!memcmp(ba->GetFirstBytecodeAddress(), snippets[i].bytecode,
                    ba->length()));
      CHECK_EQ(ba->constant_pool()->length(), snippets[i].constant_count);
      for (int j = 0; j < snippets[i].constant_count; j++) {
        CHECK_EQ(HeapNumber::cast(ba->constant_pool()->get(j))->value(),
                 snippets[i].constants[j]);
      }
    }
  }

  // Check string literals
  {
    ExpectedSnippet<const char*> snippets[] = {
        {"return \"This is a string\";", 0, 1, 3,
         {
            B(LdaConstant), U8(0),
            B(Return)
         }, 1,
         { "This is a string" }
        },
        {"var a = \"First string\"; return \"Second string\";",
         1 * kPointerSize, 1, 7,
         {
            B(LdaConstant), U8(0),
            B(Star), R(0),
            B(LdaConstant), U8(1),
            B(Return)
         }, 2, { "First string", "Second string"}
        },
        {"var a = \"Same string\"; return \"Same string\";",
         1 * kPointerSize, 1, 7,
         {
            B(LdaConstant), U8(0),
            B(Star), R(0),
            B(LdaConstant), U8(0),
            B(Return)
         }, 1, { "Same string" }
        }
    };

    size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
    for (size_t i = 0; i < num_snippets; i++) {
      Handle<BytecodeArray> ba =
          helper.MakeBytecodeForFunctionBody(snippets[i].code_snippet);
      CHECK_EQ(ba->frame_size(), snippets[i].frame_size);
      CHECK_EQ(ba->parameter_count(), snippets[i].parameter_count);
      CHECK_EQ(ba->length(), snippets[i].bytecode_length);
      CHECK(!memcmp(ba->GetFirstBytecodeAddress(), snippets[i].bytecode,
                    ba->length()));
      CHECK_EQ(ba->constant_pool()->length(), snippets[i].constant_count);
      for (int j = 0; j < snippets[i].constant_count; j++) {
        Handle<String> expected = helper.factory()->NewStringFromAsciiChecked(
            snippets[i].constants[j]);
        CHECK(String::cast(ba->constant_pool()->get(j))->Equals(*expected));
      }
    }
  }
}


TEST(PropertyLoads) {
  InitializedHandleScope handle_scope;
  BytecodeGeneratorHelper helper;

  Code::Kind ic_kinds[] = { i::Code::LOAD_IC, i::Code::LOAD_IC };
  FeedbackVectorSpec feedback_spec(0, 1, ic_kinds);
  Handle<i::TypeFeedbackVector> vector =
      helper.factory()->NewTypeFeedbackVector(&feedback_spec);

  ExpectedSnippet<const char*> snippets[] = {
      {"function f(a) { return a.name; }\nf({name : \"test\"})",
       1 * kPointerSize, 2, 10,
       {
          B(Ldar), R(helper.kLastParamIndex),
          B(Star), R(0),
          B(LdaConstant), U8(0),
          B(LoadIC), R(0), U8(vector->first_ic_slot_index()),
          B(Return)
       },
       1, { "name" }
      },
      {"function f(a) { return a[\"key\"]; }\nf({key : \"test\"})",
       1 * kPointerSize, 2, 10,
       {
          B(Ldar), R(helper.kLastParamIndex),
          B(Star), R(0),
          B(LdaConstant), U8(0),
          B(LoadIC), R(0), U8(vector->first_ic_slot_index()),
          B(Return)
       },
       1, { "key" }
      },
      {"function f(a) { return a[100]; }\nf({100 : \"test\"})",
       1 * kPointerSize, 2, 10,
       {
          B(Ldar), R(helper.kLastParamIndex),
          B(Star), R(0),
          B(LdaSmi8), U8(100),
          B(KeyedLoadIC), R(0), U8(vector->first_ic_slot_index()),
          B(Return)
       }, 0
      },
      {"function f(a, b) { return a[b]; }\nf({arg : \"test\"}, \"arg\")",
       1 * kPointerSize, 3, 10,
       {
          B(Ldar), R(helper.kLastParamIndex - 1),
          B(Star), R(0),
          B(Ldar), R(helper.kLastParamIndex),
          B(KeyedLoadIC), R(0), U8(vector->first_ic_slot_index()),
          B(Return)
       }, 0
      },
      {"function f(a) { var b = a.name; return a[-124]; }\n"
       "f({\"-124\" : \"test\", name : 123 })",
       2 * kPointerSize, 2, 21,
       {
          B(Ldar), R(helper.kLastParamIndex),
          B(Star), R(1),
          B(LdaConstant), U8(0),
          B(LoadIC), R(1), U8(vector->first_ic_slot_index()),
          B(Star), R(0),
          B(Ldar), R(helper.kLastParamIndex),
          B(Star), R(1),
          B(LdaSmi8), U8(-124),
          B(KeyedLoadIC), R(1), U8(vector->first_ic_slot_index() + 2),
          B(Return)
       },
       1, { "name" }
      }
  };
  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    Handle<BytecodeArray> ba =
        helper.MakeBytecode(snippets[i].code_snippet, "f");
    CHECK_EQ(ba->frame_size(), snippets[i].frame_size);
    CHECK_EQ(ba->parameter_count(), snippets[i].parameter_count);
    CHECK_EQ(ba->length(), snippets[i].bytecode_length);
    CHECK(!memcmp(ba->GetFirstBytecodeAddress(), snippets[i].bytecode,
                  ba->length()));
    CHECK_EQ(ba->constant_pool()->length(), snippets[i].constant_count);
    for (int j = 0; j < snippets[i].constant_count; j++) {
      Handle<String> expected = helper.factory()->NewStringFromAsciiChecked(
          snippets[i].constants[j]);
      CHECK(String::cast(ba->constant_pool()->get(j))->Equals(*expected));
    }
  }
}


TEST(PropertyStores) {
  InitializedHandleScope handle_scope;
  BytecodeGeneratorHelper helper;

  Code::Kind ic_kinds[] = { i::Code::STORE_IC, i::Code::STORE_IC };
  FeedbackVectorSpec feedback_spec(0, 1, ic_kinds);
  Handle<i::TypeFeedbackVector> vector =
      helper.factory()->NewTypeFeedbackVector(&feedback_spec);

  ExpectedSnippet<const char*> snippets[] = {
      {"function f(a) { a.name = \"val\"; }\nf({name : \"test\"})",
       2 * kPointerSize, 2, 16,
       {
          B(Ldar), R(helper.kLastParamIndex),
          B(Star), R(0),
          B(LdaConstant), U8(0),
          B(Star), R(1),
          B(LdaConstant), U8(1),
          B(StoreIC), R(0), R(1), U8(vector->first_ic_slot_index()),
          B(LdaUndefined),
          B(Return)
       },
       2, { "name", "val" }
      },
      {"function f(a) { a[\"key\"] = \"val\"; }\nf({key : \"test\"})",
       2 * kPointerSize, 2, 16,
       {
          B(Ldar), R(helper.kLastParamIndex),
          B(Star), R(0),
          B(LdaConstant), U8(0),
          B(Star), R(1),
          B(LdaConstant), U8(1),
          B(StoreIC), R(0), R(1), U8(vector->first_ic_slot_index()),
          B(LdaUndefined),
          B(Return)
       },
       2, { "key", "val" }
      },
      {"function f(a) { a[100] = \"val\"; }\nf({100 : \"test\"})",
       2 * kPointerSize, 2, 16,
       {
          B(Ldar), R(helper.kLastParamIndex),
          B(Star), R(0),
          B(LdaSmi8), U8(100),
          B(Star), R(1),
          B(LdaConstant), U8(0),
          B(KeyedStoreIC), R(0), R(1), U8(vector->first_ic_slot_index()),
          B(LdaUndefined),
          B(Return)
       },
       1, { "val" }
      },
      {"function f(a, b) { a[b] = \"val\"; }\nf({arg : \"test\"}, \"arg\")",
       2 * kPointerSize, 3, 16,
       {
          B(Ldar), R(helper.kLastParamIndex - 1),
          B(Star), R(0),
          B(Ldar), R(helper.kLastParamIndex),
          B(Star), R(1),
          B(LdaConstant), U8(0),
          B(KeyedStoreIC), R(0), R(1), U8(vector->first_ic_slot_index()),
          B(LdaUndefined),
          B(Return)
       },
       1, { "val" }
      },
      {"function f(a) { a.name = a[-124]; }\n"
       "f({\"-124\" : \"test\", name : 123 })",
       3 * kPointerSize, 2, 23,
       {
          B(Ldar), R(helper.kLastParamIndex),
          B(Star), R(0),
          B(LdaConstant), U8(0),
          B(Star), R(1),
          B(Ldar), R(helper.kLastParamIndex),
          B(Star), R(2),
          B(LdaSmi8), U8(-124),
          B(KeyedLoadIC), R(2), U8(vector->first_ic_slot_index()),
          B(StoreIC), R(0), R(1), U8(vector->first_ic_slot_index() + 2),
          B(LdaUndefined),
          B(Return)
       },
       1, { "name" }
      }
  };
  size_t num_snippets = sizeof(snippets) / sizeof(snippets[0]);
  for (size_t i = 0; i < num_snippets; i++) {
    Handle<BytecodeArray> ba =
        helper.MakeBytecode(snippets[i].code_snippet, "f");
    CHECK_EQ(ba->frame_size(), snippets[i].frame_size);
    CHECK_EQ(ba->parameter_count(), snippets[i].parameter_count);
    CHECK_EQ(ba->length(), snippets[i].bytecode_length);
    CHECK(!memcmp(ba->GetFirstBytecodeAddress(), snippets[i].bytecode,
                  ba->length()));
    CHECK_EQ(ba->constant_pool()->length(), snippets[i].constant_count);
    for (int j = 0; j < snippets[i].constant_count; j++) {
      Handle<String> expected =
          helper.factory()->NewStringFromAsciiChecked(snippets[i].constants[j]);
      CHECK(String::cast(ba->constant_pool()->get(j))->Equals(*expected));
    }
  }
}

}  // namespace interpreter
}  // namespace internal
}  // namespance v8
