// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "test/cctest/compiler/function-tester.h"

#if V8_TURBOFAN_TARGET

using namespace v8::internal;
using namespace v8::internal::compiler;

// TODO(sigurds) At the moment we do not write optimization frames when
// inlining, thus the reported stack depth changes depending on inlining.
// AssertStackDepth checks the stack depth actually changes as a simple way
// to ensure that inlining actually occurs.
// Once inlining creates optimization frames, all these unit tests need to
// check that the optimization frame is there.


static void AssertStackDepth(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::HandleScope scope(args.GetIsolate());
  v8::Handle<v8::StackTrace> stackTrace = v8::StackTrace::CurrentStackTrace(
      args.GetIsolate(), 10, v8::StackTrace::kDetailed);
  CHECK_EQ(args[0]->ToInt32()->Value(), stackTrace->GetFrameCount());
}


static void InstallAssertStackDepthHelper(v8::Isolate* isolate) {
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::FunctionTemplate> t =
      v8::FunctionTemplate::New(isolate, AssertStackDepth);
  context->Global()->Set(v8_str("AssertStackDepth"), t->GetFunction());
}


TEST(SimpleInlining) {
  FunctionTester T(
      "(function(){"
      "function foo(s) { AssertStackDepth(1); return s; };"
      "function bar(s, t) { return foo(s); };"
      "return bar;})();",
      CompilationInfo::kInliningEnabled |
          CompilationInfo::kContextSpecializing);

  InstallAssertStackDepthHelper(CcTest::isolate());
  T.CheckCall(T.Val(1), T.Val(1), T.Val(2));
}


TEST(SimpleInliningContext) {
  FunctionTester T(
      "(function () {"
      "function foo(s) { AssertStackDepth(1); var x = 12; return s + x; };"
      "function bar(s, t) { return foo(s); };"
      "return bar;"
      "})();",
      CompilationInfo::kInliningEnabled |
          CompilationInfo::kContextSpecializing);

  InstallAssertStackDepthHelper(CcTest::isolate());
  T.CheckCall(T.Val(13), T.Val(1), T.Val(2));
}


TEST(CaptureContext) {
  FunctionTester T(
      "var f = (function () {"
      "var x = 42;"
      "function bar(s) { return x + s; };"
      "return (function (s) { return bar(s); });"
      "})();"
      "(function (s) { return f(s)})",
      CompilationInfo::kInliningEnabled |
          CompilationInfo::kContextSpecializing);

  InstallAssertStackDepthHelper(CcTest::isolate());
  T.CheckCall(T.Val(42 + 12), T.Val(12), T.undefined());
}


// TODO(sigurds) For now we do not inline any native functions. If we do at
// some point, change this test.
TEST(DontInlineEval) {
  FunctionTester T(
      "var x = 42;"
      "(function () {"
      "function bar(s, t) { return eval(\"AssertStackDepth(2); x\") };"
      "return bar;"
      "})();",
      CompilationInfo::kInliningEnabled |
          CompilationInfo::kContextSpecializing);

  InstallAssertStackDepthHelper(CcTest::isolate());
  T.CheckCall(T.Val(42), T.Val("x"), T.undefined());
}


TEST(InlineOmitArguments) {
  FunctionTester T(
      "(function () {"
      "var x = 42;"
      "function bar(s, t, u, v) { AssertStackDepth(1); return x + s; };"
      "return (function (s,t) { return bar(s); });"
      "})();",
      CompilationInfo::kInliningEnabled |
          CompilationInfo::kContextSpecializing);

  InstallAssertStackDepthHelper(CcTest::isolate());
  T.CheckCall(T.Val(42 + 12), T.Val(12), T.undefined());
}


TEST(InlineSurplusArguments) {
  FunctionTester T(
      "(function () {"
      "var x = 42;"
      "function foo(s) { AssertStackDepth(1); return x + s; };"
      "function bar(s,t) { return foo(s,t,13); };"
      "return bar;"
      "})();",
      CompilationInfo::kInliningEnabled |
          CompilationInfo::kContextSpecializing);

  InstallAssertStackDepthHelper(CcTest::isolate());
  T.CheckCall(T.Val(42 + 12), T.Val(12), T.undefined());
}


TEST(InlineTwice) {
  FunctionTester T(
      "(function () {"
      "var x = 42;"
      "function bar(s) { AssertStackDepth(1); return x + s; };"
      "return (function (s,t) { return bar(s) + bar(t); });"
      "})();",
      CompilationInfo::kInliningEnabled |
          CompilationInfo::kContextSpecializing);

  InstallAssertStackDepthHelper(CcTest::isolate());
  T.CheckCall(T.Val(2 * 42 + 12 + 4), T.Val(12), T.Val(4));
}


TEST(InlineTwiceDependent) {
  FunctionTester T(
      "(function () {"
      "var x = 42;"
      "function foo(s) { AssertStackDepth(1); return x + s; };"
      "function bar(s,t) { return foo(foo(s)); };"
      "return bar;"
      "})();",
      CompilationInfo::kInliningEnabled |
          CompilationInfo::kContextSpecializing);

  InstallAssertStackDepthHelper(CcTest::isolate());
  T.CheckCall(T.Val(42 + 42 + 12), T.Val(12), T.Val(4));
}


TEST(InlineTwiceDependentDiamond) {
  FunctionTester T(
      "(function () {"
      "var x = 41;"
      "function foo(s) { AssertStackDepth(1); if (s % 2 == 0) {"
      "                  return x - s } else { return x + s; } };"
      "function bar(s,t) { return foo(foo(s)); };"
      "return bar;"
      "})();",
      CompilationInfo::kInliningEnabled |
          CompilationInfo::kContextSpecializing);

  InstallAssertStackDepthHelper(CcTest::isolate());
  T.CheckCall(T.Val(-11), T.Val(11), T.Val(4));
}


TEST(InlineTwiceDependentDiamondDifferent) {
  FunctionTester T(
      "(function () {"
      "var x = 41;"
      "function foo(s,t) { AssertStackDepth(1); if (s % 2 == 0) {"
      "                    return x - s * t } else { return x + s * t; } };"
      "function bar(s,t) { return foo(foo(s, 3), 5); };"
      "return bar;"
      "})();",
      CompilationInfo::kInliningEnabled |
          CompilationInfo::kContextSpecializing);

  InstallAssertStackDepthHelper(CcTest::isolate());
  T.CheckCall(T.Val(-329), T.Val(11), T.Val(4));
}

#endif  // V8_TURBOFAN_TARGET
