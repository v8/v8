// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test specific cases of the lazy/eager-parse decision.
//
// Note that presently most unit tests for parsing are found in
// cctest/test-parsing.cc.

#include <unordered_map>

#include "include/v8.h"
#include "src/api.h"
#include "src/handles-inl.h"
#include "src/isolate.h"
#include "src/utils.h"

#include "test/cctest/cctest.h"

using namespace v8::internal;

TEST(EagerlyCompileImmediateUseFunctions) {
  if (!FLAG_lazy) return;

  // Test parenthesized, exclaimed, and regular functions. Make sure these
  // occur both intermixed and after each other, to make sure the 'reset'
  // mechanism works.
  const char src[] =
      "function normal() { var a; }\n"             // Normal: Should lazy parse.
      "(function parenthesized() { var b; })()\n"  // Parenthesized: Pre-parse.
      "!function exclaimed() { var c; }() \n"      // Exclaimed: Pre-parse.
      "function normal2() { var d; }\n"
      "(function parenthesized2() { var e; })()\n"
      "function normal3() { var f; }\n"
      "!function exclaimed2() { var g; }() \n"
      "function normal4() { var h; }\n";

  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  LocalContext env;

  // Compile src & record the 'compiled' state of all top level functions in
  // is_compiled.
  std::unordered_map<std::string, bool> is_compiled;
  {
    v8::Local<v8::Script> api_script = v8_compile(src);
    Handle<JSFunction> toplevel_fn = v8::Utils::OpenHandle(*api_script);
    Handle<Script> script =
        handle(Script::cast(toplevel_fn->shared()->script()));

    WeakFixedArray::Iterator iter(script->shared_function_infos());
    while (SharedFunctionInfo* shared = iter.Next<SharedFunctionInfo>()) {
      std::unique_ptr<char[]> name = String::cast(shared->name())->ToCString();
      is_compiled[name.get()] = shared->is_compiled();
    }
  }

  DCHECK(is_compiled.find("normal") != is_compiled.end());

  DCHECK(is_compiled["parenthesized"]);
  DCHECK(is_compiled["parenthesized2"]);
  DCHECK(is_compiled["exclaimed"]);
  DCHECK(is_compiled["exclaimed2"]);
  DCHECK(!is_compiled["normal"]);
  DCHECK(!is_compiled["normal2"]);
  DCHECK(!is_compiled["normal3"]);
  DCHECK(!is_compiled["normal4"]);
}
