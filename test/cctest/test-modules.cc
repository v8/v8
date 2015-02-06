// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/cctest.h"

TEST(ModuleCompilation) {
  v8::internal::FLAG_harmony_modules = true;
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope handle_scope(isolate);
  LocalContext context;

  CompileRun(
      "var data = [];"
      "function store(thing) {"
      "  data.push(thing);"
      "}");

  CompileRunModule(
      "export let a = 42;"
      "store(a)");

  CHECK_EQ(1, CompileRun("data.length")->Int32Value());
  CHECK_EQ(42, CompileRun("data[0]")->Int32Value());
}
