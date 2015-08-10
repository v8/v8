// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/execution.h"
#include "src/handles.h"
#include "src/interpreter/bytecode-array-builder.h"
#include "src/interpreter/interpreter.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

class InterpreterCallable {
 public:
  InterpreterCallable(Isolate* isolate, Handle<JSFunction> function)
      : isolate_(isolate), function_(function) {}
  virtual ~InterpreterCallable() {}

  MaybeHandle<Object> operator()() {
    return Execution::Call(isolate_, function_,
                           isolate_->factory()->undefined_value(), 0, nullptr,
                           false);
  }

 private:
  Isolate* isolate_;
  Handle<JSFunction> function_;
};

class InterpreterTester {
 public:
  InterpreterTester(Isolate* isolate, Handle<BytecodeArray> bytecode)
      : isolate_(isolate), function_(GetBytecodeFunction(isolate, bytecode)) {
    i::FLAG_ignition = true;
    Handle<FixedArray> empty_array = isolate->factory()->empty_fixed_array();
    Handle<FixedArray> interpreter_table =
        isolate->factory()->interpreter_table();
    if (interpreter_table.is_identical_to(empty_array)) {
      // Ensure handler table is generated.
      isolate->interpreter()->Initialize(true);
    }
  }
  virtual ~InterpreterTester() {}

  InterpreterCallable GetCallable() {
    return InterpreterCallable(isolate_, function_);
  }

 private:
  Isolate* isolate_;
  Handle<JSFunction> function_;

  static Handle<JSFunction> GetBytecodeFunction(
      Isolate* isolate, Handle<BytecodeArray> bytecode_array) {
    Handle<JSFunction> function = v8::Utils::OpenHandle(
        *v8::Handle<v8::Function>::Cast(CompileRun("(function(){})")));
    function->ReplaceCode(*isolate->builtins()->InterpreterEntryTrampoline());
    function->shared()->set_function_data(*bytecode_array);
    return function;
  }

  DISALLOW_COPY_AND_ASSIGN(InterpreterTester);
};

}  // namespace internal
}  // namespace v8

using namespace v8::internal;
using namespace v8::internal::interpreter;

TEST(TestInterpreterReturn) {
  InitializedHandleScope handles;
  Handle<Object> undefined_value =
      handles.main_isolate()->factory()->undefined_value();

  BytecodeArrayBuilder builder(handles.main_isolate());
  // TODO(rmcilroy) set to 0 once BytecodeArray update to allow zero size
  // register file.
  builder.set_locals_count(1);
  builder.Return();
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray();

  InterpreterTester tester(handles.main_isolate(), bytecode_array);
  InterpreterCallable callable(tester.GetCallable());
  Handle<Object> return_val = callable().ToHandleChecked();
  CHECK(return_val.is_identical_to(undefined_value));
}
