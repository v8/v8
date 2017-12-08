// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/flags.h"

#include "test/cctest/cctest.h"

namespace {

using v8::Context;
using v8::HandleScope;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Module;
using v8::ScriptCompiler;
using v8::ScriptOrigin;
using v8::String;
using v8::Value;

ScriptOrigin ModuleOrigin(Local<v8::Value> resource_name, Isolate* isolate) {
  ScriptOrigin origin(resource_name, Local<v8::Integer>(), Local<v8::Integer>(),
                      Local<v8::Boolean>(), Local<v8::Integer>(),
                      Local<v8::Value>(), Local<v8::Boolean>(),
                      Local<v8::Boolean>(), True(isolate));
  return origin;
}

static Local<Module> dep1;
static Local<Module> dep2;
MaybeLocal<Module> ResolveCallback(Local<Context> context,
                                   Local<String> specifier,
                                   Local<Module> referrer) {
  Isolate* isolate = CcTest::isolate();
  if (specifier->StrictEquals(v8_str("./dep1.js"))) {
    return dep1;
  } else if (specifier->StrictEquals(v8_str("./dep2.js"))) {
    return dep2;
  } else {
    isolate->ThrowException(v8_str("boom"));
    return MaybeLocal<Module>();
  }
}

TEST(ModuleInstantiationFailures1) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  Local<Module> module;
  {
    Local<String> source_text = v8_str(
        "import './foo.js';\n"
        "export {} from './bar.js';");
    ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    module = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
    CHECK_EQ(2, module->GetModuleRequestsLength());
    CHECK(v8_str("./foo.js")->StrictEquals(module->GetModuleRequest(0)));
    v8::Location loc = module->GetModuleRequestLocation(0);
    CHECK_EQ(0, loc.GetLineNumber());
    CHECK_EQ(7, loc.GetColumnNumber());
    CHECK(v8_str("./bar.js")->StrictEquals(module->GetModuleRequest(1)));
    loc = module->GetModuleRequestLocation(1);
    CHECK_EQ(1, loc.GetLineNumber());
    CHECK_EQ(15, loc.GetColumnNumber());
  }

  // Instantiation should fail.
  {
    v8::TryCatch inner_try_catch(isolate);
    CHECK(module->InstantiateModule(env.local(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(v8_str("boom")));
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
  }

  // Start over again...
  {
    Local<String> source_text = v8_str(
        "import './dep1.js';\n"
        "export {} from './bar.js';");
    ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    module = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  // dep1.js
  {
    Local<String> source_text = v8_str("");
    ScriptOrigin origin = ModuleOrigin(v8_str("dep1.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep1 = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  // Instantiation should fail because a sub-module fails to resolve.
  {
    v8::TryCatch inner_try_catch(isolate);
    CHECK(module->InstantiateModule(env.local(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(v8_str("boom")));
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
  }

  CHECK(!try_catch.HasCaught());
}

TEST(ModuleInstantiationFailures2) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  // root1.js
  Local<Module> root;
  {
    Local<String> source_text =
        v8_str("import './dep1.js'; import './dep2.js'");
    ScriptOrigin origin = ModuleOrigin(v8_str("root1.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    root = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  // dep1.js
  {
    Local<String> source_text = v8_str("export let x = 42");
    ScriptOrigin origin = ModuleOrigin(v8_str("dep1.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep1 = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  // dep2.js
  {
    Local<String> source_text = v8_str("import {foo} from './dep3.js'");
    ScriptOrigin origin = ModuleOrigin(v8_str("dep2.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep2 = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  {
    v8::TryCatch inner_try_catch(isolate);
    CHECK(root->InstantiateModule(env.local(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(v8_str("boom")));
    CHECK_EQ(Module::kUninstantiated, root->GetStatus());
    CHECK_EQ(Module::kUninstantiated, dep1->GetStatus());
    CHECK_EQ(Module::kUninstantiated, dep2->GetStatus());
  }

  // Change dep2.js
  {
    Local<String> source_text = v8_str("import {foo} from './dep2.js'");
    ScriptOrigin origin = ModuleOrigin(v8_str("dep2.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep2 = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  {
    v8::TryCatch inner_try_catch(isolate);
    CHECK(root->InstantiateModule(env.local(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(!inner_try_catch.Exception()->StrictEquals(v8_str("boom")));
    CHECK_EQ(Module::kUninstantiated, root->GetStatus());
    CHECK_EQ(Module::kInstantiated, dep1->GetStatus());
    CHECK_EQ(Module::kUninstantiated, dep2->GetStatus());
  }

  // Change dep2.js again
  {
    Local<String> source_text = v8_str("import {foo} from './dep3.js'");
    ScriptOrigin origin = ModuleOrigin(v8_str("dep2.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep2 = ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  }

  {
    v8::TryCatch inner_try_catch(isolate);
    CHECK(root->InstantiateModule(env.local(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(v8_str("boom")));
    CHECK_EQ(Module::kUninstantiated, root->GetStatus());
    CHECK_EQ(Module::kInstantiated, dep1->GetStatus());
    CHECK_EQ(Module::kUninstantiated, dep2->GetStatus());
  }
}

static MaybeLocal<Module> CompileSpecifierAsModuleResolveCallback(
    Local<Context> context, Local<String> specifier, Local<Module> referrer) {
  ScriptOrigin origin = ModuleOrigin(v8_str("module.js"), CcTest::isolate());
  ScriptCompiler::Source source(specifier, origin);
  return ScriptCompiler::CompileModule(CcTest::isolate(), &source)
      .ToLocalChecked();
}

TEST(ModuleEvaluation) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  Local<String> source_text = v8_str(
      "import 'Object.expando = 5';"
      "import 'Object.expando *= 2';");
  ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
  ScriptCompiler::Source source(source_text, origin);
  Local<Module> module =
      ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  CHECK_EQ(Module::kUninstantiated, module->GetStatus());
  CHECK(module
            ->InstantiateModule(env.local(),
                                CompileSpecifierAsModuleResolveCallback)
            .FromJust());
  CHECK_EQ(Module::kInstantiated, module->GetStatus());
  CHECK(!module->Evaluate(env.local()).IsEmpty());
  CHECK_EQ(Module::kEvaluated, module->GetStatus());
  ExpectInt32("Object.expando", 10);

  CHECK(!try_catch.HasCaught());
}

TEST(ModuleEvaluationError) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  Local<String> source_text =
      v8_str("Object.x = (Object.x || 0) + 1; throw 'boom';");
  ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
  ScriptCompiler::Source source(source_text, origin);
  Local<Module> module =
      ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  CHECK_EQ(Module::kUninstantiated, module->GetStatus());
  CHECK(module
            ->InstantiateModule(env.local(),
                                CompileSpecifierAsModuleResolveCallback)
            .FromJust());
  CHECK_EQ(Module::kInstantiated, module->GetStatus());

  {
    v8::TryCatch inner_try_catch(isolate);
    CHECK(module->Evaluate(env.local()).IsEmpty());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(v8_str("boom")));
    CHECK_EQ(Module::kErrored, module->GetStatus());
    Local<Value> exception = module->GetException();
    CHECK(exception->StrictEquals(v8_str("boom")));
    ExpectInt32("Object.x", 1);
  }

  {
    v8::TryCatch inner_try_catch(isolate);
    CHECK(module->Evaluate(env.local()).IsEmpty());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(v8_str("boom")));
    CHECK_EQ(Module::kErrored, module->GetStatus());
    Local<Value> exception = module->GetException();
    CHECK(exception->StrictEquals(v8_str("boom")));
    ExpectInt32("Object.x", 1);
  }

  CHECK(!try_catch.HasCaught());
}

TEST(ModuleEvaluationCompletion1) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  const char* sources[] = {
      "",
      "var a = 1",
      "import '42'",
      "export * from '42'",
      "export {} from '42'",
      "export {}",
      "var a = 1; export {a}",
      "export function foo() {}",
      "export class C extends null {}",
      "export let a = 1",
      "export default 1",
      "export default function foo() {}",
      "export default function () {}",
      "export default (function () {})",
      "export default class C extends null {}",
      "export default (class C extends null {})",
      "for (var i = 0; i < 5; ++i) {}",
  };

  for (auto src : sources) {
    Local<String> source_text = v8_str(src);
    ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> module =
        ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
    CHECK(module
              ->InstantiateModule(env.local(),
                                  CompileSpecifierAsModuleResolveCallback)
              .FromJust());
    CHECK_EQ(Module::kInstantiated, module->GetStatus());
    CHECK(module->Evaluate(env.local()).ToLocalChecked()->IsUndefined());
    CHECK_EQ(Module::kEvaluated, module->GetStatus());
    CHECK(module->Evaluate(env.local()).ToLocalChecked()->IsUndefined());
    CHECK_EQ(Module::kEvaluated, module->GetStatus());
  }

  CHECK(!try_catch.HasCaught());
}

TEST(ModuleEvaluationCompletion2) {
  Isolate* isolate = CcTest::isolate();
  HandleScope scope(isolate);
  LocalContext env;
  v8::TryCatch try_catch(isolate);

  const char* sources[] = {
      "'gaga'; ",
      "'gaga'; var a = 1",
      "'gaga'; import '42'",
      "'gaga'; export * from '42'",
      "'gaga'; export {} from '42'",
      "'gaga'; export {}",
      "'gaga'; var a = 1; export {a}",
      "'gaga'; export function foo() {}",
      "'gaga'; export class C extends null {}",
      "'gaga'; export let a = 1",
      "'gaga'; export default 1",
      "'gaga'; export default function foo() {}",
      "'gaga'; export default function () {}",
      "'gaga'; export default (function () {})",
      "'gaga'; export default class C extends null {}",
      "'gaga'; export default (class C extends null {})",
  };

  for (auto src : sources) {
    Local<String> source_text = v8_str(src);
    ScriptOrigin origin = ModuleOrigin(v8_str("file.js"), CcTest::isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> module =
        ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
    CHECK(module
              ->InstantiateModule(env.local(),
                                  CompileSpecifierAsModuleResolveCallback)
              .FromJust());
    CHECK_EQ(Module::kInstantiated, module->GetStatus());
    CHECK(module->Evaluate(env.local())
              .ToLocalChecked()
              ->StrictEquals(v8_str("gaga")));
    CHECK_EQ(Module::kEvaluated, module->GetStatus());
    CHECK(module->Evaluate(env.local()).ToLocalChecked()->IsUndefined());
    CHECK_EQ(Module::kEvaluated, module->GetStatus());
  }

  CHECK(!try_catch.HasCaught());
}

}  // anonymous namespace
