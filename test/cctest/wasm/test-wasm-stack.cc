// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-macro-gen.h"

#include "test/cctest/cctest.h"
#include "test/cctest/compiler/value-helper.h"
#include "test/cctest/wasm/test-signatures.h"
#include "test/cctest/wasm/wasm-run-utils.h"

using namespace v8::base;
using namespace v8::internal;
using namespace v8::internal::compiler;
using namespace v8::internal::wasm;

using v8::Local;
using v8::Utils;

namespace {

#define CHECK_CSTREQ(exp, found)                                           \
  do {                                                                     \
    const char* exp_ = (exp);                                              \
    const char* found_ = (found);                                          \
    if (V8_UNLIKELY(strcmp(exp_, found_) != 0)) {                          \
      V8_Fatal(__FILE__, __LINE__,                                         \
               "Check failed: (%s) != (%s) ('%s' vs '%s').", #exp, #found, \
               exp_, found_);                                              \
    }                                                                      \
  } while (0)

void PrintStackTrace(v8::Local<v8::StackTrace> stack) {
  printf("Stack Trace (length %d):\n", stack->GetFrameCount());
  for (int i = 0, e = stack->GetFrameCount(); i != e; ++i) {
    v8::Local<v8::StackFrame> frame = stack->GetFrame(i);
    v8::Local<v8::String> script = frame->GetScriptName();
    v8::Local<v8::String> func = frame->GetFunctionName();
    printf("[%d] (%s) %s:%d:%d\n", i,
           script.IsEmpty() ? "<null>" : *v8::String::Utf8Value(script),
           func.IsEmpty() ? "<null>" : *v8::String::Utf8Value(func),
           frame->GetLineNumber(), frame->GetColumn());
  }
}

struct ExceptionInfo {
  const char* funcName;
  int lineNr;
};

template <int N>
void CheckExceptionInfos(Isolate* isolate, Handle<Object> exc,
                         const ExceptionInfo (&excInfos)[N]) {
  // check that it's indeed an Error object
  CHECK(Object::IsErrorObject(isolate, exc));

  // extract stack frame from exception
  Local<v8::Value> localExc = Utils::ToLocal(exc);
  v8::Local<v8::StackTrace> stack = v8::Exception::GetStackTrace(localExc);
  PrintStackTrace(stack);
  CHECK(!stack.IsEmpty());
  CHECK_EQ(N, stack->GetFrameCount());

  for (int frameNr = 0; frameNr < N; ++frameNr) {
    v8::Local<v8::StackFrame> frame = stack->GetFrame(frameNr);
    v8::String::Utf8Value funName(frame->GetFunctionName());
    CHECK_CSTREQ(excInfos[frameNr].funcName, *funName);
    CHECK_EQ(excInfos[frameNr].lineNr, frame->GetLineNumber());
  }
}

}  // namespace

// Call from JS to WASM to JS and throw an Error from JS.
TEST(CollectDetailedWasmStack_ExplicitThrowFromJs) {
  TestSignatures sigs;
  TestingModule module;

  WasmFunctionCompiler comp1(sigs.v_v(), &module);

  uint32_t jsThrowingIndex = module.AddJsFunction(
      sigs.v_v(),
      "(function js() {\n function a() {\n throw new Error(); };\n a(); })");

  BUILD(comp1, WASM_CALL_FUNCTION0(jsThrowingIndex));
  uint32_t wasmIndex = comp1.CompileAndAdd();

  WasmFunctionCompiler comp2(sigs.v_v(), &module);
  BUILD(comp2, WASM_CALL_FUNCTION0(wasmIndex));
  uint32_t wasmIndex2 = comp2.CompileAndAdd();

  Handle<JSFunction> jsWasmWrapper = module.WrapCode(wasmIndex2);

  Handle<JSFunction> jsTrampoline = Handle<JSFunction>::cast(
      v8::Utils::OpenHandle(*v8::Local<v8::Function>::Cast(
          CompileRun("(function callFn(fn) { fn(); })"))));

  Isolate* isolate = jsWasmWrapper->GetIsolate();
  isolate->SetCaptureStackTraceForUncaughtExceptions(true, 10,
                                                     v8::StackTrace::kOverview);
  Handle<Object> global(isolate->context()->global_object(), isolate);
  MaybeHandle<Object> excMaybe;
  Handle<Object> args[] = {jsWasmWrapper};
  MaybeHandle<Object> returnObjMaybe =
      Execution::TryCall(isolate, jsTrampoline, global, 1, args, &excMaybe);
  CHECK(returnObjMaybe.is_null());

  // line number is 1-based, with 0 == kNoLineNumberInfo
  ExceptionInfo expectedExceptions[] = {
      {"a", 3},       // comment to prevent clang-format complaints
      {"js", 4},      // -
      {"<WASM>", 0},  // -
      {"<WASM>", 0},  // -
      {"callFn", 1}};
  CheckExceptionInfos(isolate, excMaybe.ToHandleChecked(), expectedExceptions);
}
