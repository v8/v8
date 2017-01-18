// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen.h"
#include "src/ffi/ffi-compiler.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace ffi {

static void hello_world() { printf("hello world from native code\n"); }

TEST(Run_FFI_Hello) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);

  Handle<String> name =
      isolate->factory()->InternalizeUtf8String("hello_world");
  Handle<Object> undefined = isolate->factory()->undefined_value();

  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  FFISignature::Builder sig_builder(&zone, 0, 0);
  NativeFunction func = {sig_builder.Build(),
                         reinterpret_cast<uint8_t*>(hello_world)};

  Handle<JSFunction> jsfunc = CompileJSToNativeWrapper(isolate, name, func);

  Handle<Object> result =
      Execution::Call(isolate, jsfunc, undefined, 0, nullptr).ToHandleChecked();

  CHECK(result->IsUndefined(isolate));
}

}  // namespace ffi
}  // namespace internal
}  // namespace v8
