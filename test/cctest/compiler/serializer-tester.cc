// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Serializer tests don't make sense in lite mode, as it doesn't gather
// IC feedback.
#ifndef V8_LITE_MODE

#include "test/cctest/compiler/serializer-tester.h"

#include "src/api-inl.h"
#include "src/compiler/serializer-for-background-compilation.h"
#include "src/compiler/zone-stats.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

SerializerTester::SerializerTester(const char* source)
    : canonical_(main_isolate()) {
  // The tests only make sense in the context of concurrent compilation.
  FLAG_concurrent_inlining = true;
  // The tests don't make sense when optimizations are turned off.
  FLAG_opt = true;
  // We need the IC to feed it to the serializer.
  FLAG_use_ic = true;
  // We need manual control over when a given function is optimized.
  FLAG_always_opt = false;

  std::string function_string = "(function() { ";
  function_string += source;
  function_string += " })();";
  Handle<JSFunction> function = Handle<JSFunction>::cast(v8::Utils::OpenHandle(
      *v8::Local<v8::Function>::Cast(CompileRun(function_string.c_str()))));
  Optimize(function, main_zone(), main_isolate(), 0, &broker_);
  function_ = JSFunctionRef(broker_, function);
}

TEST(SerializeEmptyFunction) {
  SerializerTester tester("function f() {}; return f;");
  CHECK(tester.function().IsSerializedForCompilation());
}

// This helper function allows for testing weather an inlinee candidate
// was properly serialized. It expects that the top-level function (that is
// run through the SerializerTester) will return its inlinee candidate.
void CheckForSerializedInlinee(const char* source) {
  SerializerTester tester(source);
  JSFunctionRef f = tester.function();
  CHECK(f.IsSerializedForCompilation());

  MaybeHandle<Object> g_obj = Execution::Call(
      tester.isolate(), tester.function().object(),
      tester.isolate()->factory()->undefined_value(), 0, nullptr);
  Handle<Object> g;
  CHECK(g_obj.ToHandle(&g));

  Handle<JSFunction> g_func = Handle<JSFunction>::cast(g);
  SharedFunctionInfoRef g_sfi(tester.broker(),
                              handle(g_func->shared(), tester.isolate()));
  FeedbackVectorRef g_fv(tester.broker(),
                         handle(g_func->feedback_vector(), tester.isolate()));
  CHECK(g_sfi.IsSerializedForCompilation(g_fv));
}

TEST(SerializeInlinedClosure) {
  CheckForSerializedInlinee(
      "function f() {"
      "  return (function g(){ return g; })();"
      "}; f(); return f;");
}

TEST(SerializeInlinedFunction) {
  CheckForSerializedInlinee(
      "function g() {};"
      "function f() {"
      "  g(); return g;"
      "}; f(); return f;");
}
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_LITE_MODE
