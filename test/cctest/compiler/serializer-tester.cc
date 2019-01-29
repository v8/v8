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
  FLAG_concurrent_inlining = true;
  FLAG_opt = true;
  FLAG_use_ic = true;

  Handle<JSFunction> function = Handle<JSFunction>::cast(v8::Utils::OpenHandle(
      *v8::Local<v8::Function>::Cast(CompileRun(source))));
  Optimize(function, main_zone(), main_isolate(), 0, &broker_);
  function_ = JSFunctionRef(broker_, function);
}

TEST(SerializeEmptyFunction) {
  SerializerTester tester("(function() { function f() {}; return f; })();");
  CHECK(tester.function().shared().IsSerializedForCompilation(
      tester.function().feedback_vector()));
}
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_LITE_MODE
