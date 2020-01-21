// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-measurement-inl.h"
#include "src/heap/memory-measurement.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-tester.h"
#include "test/cctest/heap/heap-utils.h"

namespace v8 {
namespace internal {
namespace heap {

namespace {
Handle<NativeContext> GetNativeContext(Isolate* isolate,
                                       v8::Local<v8::Context> v8_context) {
  Handle<Context> context = v8::Utils::OpenHandle(*v8_context);
  return handle(context->native_context(), isolate);
}
}  // anonymous namespace

TEST(NativeContextInferrerGlobalObject) {
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handle_scope(isolate);
  Handle<NativeContext> native_context = GetNativeContext(isolate, env.local());
  Handle<JSGlobalObject> global =
      handle(native_context->global_object(), isolate);
  NativeContextInferrer inferrer;
  Address inferred_context = 0;
  CHECK(inferrer.Infer(isolate, global->map(), *global, &inferred_context));
  CHECK_EQ(native_context->ptr(), inferred_context);
}

TEST(NativeContextInferrerJSFunction) {
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  Handle<NativeContext> native_context = GetNativeContext(isolate, env.local());
  v8::Local<v8::Value> result = CompileRun("(function () { return 1; })");
  Handle<Object> object = Utils::OpenHandle(*result);
  Handle<HeapObject> function = Handle<HeapObject>::cast(object);
  NativeContextInferrer inferrer;
  Address inferred_context = 0;
  CHECK(inferrer.Infer(isolate, function->map(), *function, &inferred_context));
  CHECK_EQ(native_context->ptr(), inferred_context);
}

TEST(NativeContextInferrerJSObject) {
  LocalContext env;
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  Handle<NativeContext> native_context = GetNativeContext(isolate, env.local());
  v8::Local<v8::Value> result = CompileRun("({a : 10})");
  Handle<Object> object = Utils::OpenHandle(*result);
  Handle<HeapObject> function = Handle<HeapObject>::cast(object);
  NativeContextInferrer inferrer;
  Address inferred_context = 0;
  // TODO(ulan): Enable this test once we have more precise native
  // context inference.
  CHECK(inferrer.Infer(isolate, function->map(), *function, &inferred_context));
  CHECK_EQ(native_context->ptr(), inferred_context);
}

TEST(NativeContextStatsMerge) {
  NativeContextStats stats1, stats2;
  Address object = 0;
  stats1.IncrementSize(object, 10);
  stats2.IncrementSize(object, 20);
  stats1.Merge(stats2);
  CHECK_EQ(30, stats1.Get(object));
}

}  // namespace heap
}  // namespace internal
}  // namespace v8
