// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-interface.h"
#include "src/property-descriptor.h"
#include "src/wasm/wasm-macro-gen.h"
#include "src/wasm/wasm-objects.h"

#include "test/cctest/cctest.h"
#include "test/cctest/compiler/value-helper.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/test-signatures.h"

using namespace v8::internal;
using namespace v8::internal::wasm;
namespace debug = v8::debug;

namespace {

void CheckLocations(
    WasmCompiledModule *compiled_module, debug::Location start,
    debug::Location end,
    std::initializer_list<debug::Location> expected_locations_init) {
  std::vector<debug::Location> locations;
  bool success =
      compiled_module->GetPossibleBreakpoints(start, end, &locations);
  CHECK(success);

  printf("got %d locations: ", static_cast<int>(locations.size()));
  for (size_t i = 0, e = locations.size(); i != e; ++i) {
    printf("%s<%d,%d>", i == 0 ? "" : ", ", locations[i].GetLineNumber(),
           locations[i].GetColumnNumber());
  }
  printf("\n");

  std::vector<debug::Location> expected_locations(expected_locations_init);
  CHECK_EQ(expected_locations.size(), locations.size());
  for (size_t i = 0, e = locations.size(); i != e; ++i) {
    CHECK_EQ(expected_locations[i].GetLineNumber(),
             locations[i].GetLineNumber());
    CHECK_EQ(expected_locations[i].GetColumnNumber(),
             locations[i].GetColumnNumber());
  }
}
void CheckLocationsFail(WasmCompiledModule *compiled_module,
                        debug::Location start, debug::Location end) {
  std::vector<debug::Location> locations;
  bool success =
      compiled_module->GetPossibleBreakpoints(start, end, &locations);
  CHECK(!success);
}

class BreakHandler {
 public:
  explicit BreakHandler(Isolate* isolate) : isolate_(isolate) {
    current_handler = this;
    v8::Debug::SetDebugEventListener(reinterpret_cast<v8::Isolate*>(isolate),
                                     DebugEventListener);
  }
  ~BreakHandler() {
    CHECK_EQ(this, current_handler);
    current_handler = nullptr;
    v8::Debug::SetDebugEventListener(reinterpret_cast<v8::Isolate*>(isolate_),
                                     nullptr);
  }

  int count() const { return count_; }

 private:
  Isolate* isolate_;
  int count_ = 0;

  static BreakHandler* current_handler;

  static void DebugEventListener(const v8::Debug::EventDetails& event_details) {
    if (event_details.GetEvent() != v8::DebugEvent::Break) return;

    printf("break!\n");
    CHECK_NOT_NULL(current_handler);
    current_handler->count_ += 1;
    // Don't run into an endless loop.
    CHECK_GT(100, current_handler->count_);
  }
};

// static
BreakHandler* BreakHandler::current_handler = nullptr;

Handle<JSObject> MakeFakeBreakpoint(Isolate* isolate, int position) {
  Handle<JSObject> obj =
      isolate->factory()->NewJSObject(isolate->object_function());
  // Generate an "isTriggered" method that always returns true.
  // This can/must be refactored once we remove remaining JS parts from the
  // debugger (bug 5530).
  Handle<String> source = isolate->factory()->NewStringFromStaticChars("true");
  Handle<Context> context(isolate->context(), isolate);
  Handle<JSFunction> triggered_fun =
      Compiler::GetFunctionFromString(context, source, NO_PARSE_RESTRICTION)
          .ToHandleChecked();
  PropertyDescriptor desc;
  desc.set_value(triggered_fun);
  Handle<String> name =
      isolate->factory()->InternalizeUtf8String(CStrVector("isTriggered"));
  CHECK(
      JSObject::DefineOwnProperty(isolate, obj, name, &desc, Object::DONT_THROW)
          .FromMaybe(false));
  return obj;
}

void SetBreakpoint(WasmRunnerBase& runner, int function_index, int byte_offset,
                   int expected_set_byte_offset = -1) {
  int func_offset =
      runner.module().module->functions[function_index].code_start_offset;
  int code_offset = func_offset + byte_offset;
  if (expected_set_byte_offset == -1) expected_set_byte_offset = byte_offset;
  Handle<WasmInstanceObject> instance = runner.module().instance_object();
  Handle<WasmCompiledModule> compiled_module(instance->compiled_module());
  Handle<JSObject> fake_breakpoint_object =
      MakeFakeBreakpoint(runner.main_isolate(), code_offset);
  CHECK(WasmCompiledModule::SetBreakPoint(compiled_module, &code_offset,
                                          fake_breakpoint_object));
  int set_byte_offset = code_offset - func_offset;
  CHECK_EQ(expected_set_byte_offset, set_byte_offset);
  // Also set breakpoint on the debug info of the instance directly, since the
  // instance chain is not setup properly in tests.
  Handle<WasmDebugInfo> debug_info =
      WasmInstanceObject::GetOrCreateDebugInfo(instance);
  WasmDebugInfo::SetBreakpoint(debug_info, function_index, set_byte_offset);
}

}  // namespace

TEST(CollectPossibleBreakpoints) {
  WasmRunner<int> runner(kExecuteCompiled);

  BUILD(runner, WASM_NOP, WASM_I32_ADD(WASM_ZERO, WASM_ONE));

  Handle<WasmInstanceObject> instance = runner.module().instance_object();
  std::vector<debug::Location> locations;
  // Check all locations for function 0.
  CheckLocations(instance->compiled_module(), {0, 0}, {1, 0},
                 {{0, 1}, {0, 2}, {0, 4}, {0, 6}, {0, 7}});
  // Check a range ending at an instruction.
  CheckLocations(instance->compiled_module(), {0, 2}, {0, 4}, {{0, 2}});
  // Check a range ending one behind an instruction.
  CheckLocations(instance->compiled_module(), {0, 2}, {0, 5}, {{0, 2}, {0, 4}});
  // Check a range starting at an instruction.
  CheckLocations(instance->compiled_module(), {0, 7}, {0, 8}, {{0, 7}});
  // Check from an instruction to beginning of next function.
  CheckLocations(instance->compiled_module(), {0, 7}, {1, 0}, {{0, 7}});
  // Check from end of one function (no valid instruction position) to beginning
  // of next function. Must be empty, but not fail.
  CheckLocations(instance->compiled_module(), {0, 8}, {1, 0}, {});
  // Check from one after the end of the function. Must fail.
  CheckLocationsFail(instance->compiled_module(), {0, 9}, {1, 0});
}

TEST(TestSimpleBreak) {
  WasmRunner<int> runner(kExecuteCompiled);
  Isolate* isolate = runner.main_isolate();

  BUILD(runner, WASM_NOP, WASM_I32_ADD(WASM_I32V_1(11), WASM_I32V_1(3)));

  Handle<JSFunction> main_fun_wrapper =
      runner.module().WrapCode(runner.function_index());
  SetBreakpoint(runner, runner.function_index(), 4, 4);

  BreakHandler count_breaks(isolate);
  CHECK_EQ(0, count_breaks.count());

  Handle<Object> global(isolate->context()->global_object(), isolate);
  MaybeHandle<Object> retval =
      Execution::Call(isolate, main_fun_wrapper, global, 0, nullptr);
  CHECK(!retval.is_null());
  int result;
  CHECK(retval.ToHandleChecked()->ToInt32(&result));
  CHECK_EQ(14, result);

  CHECK_EQ(1, count_breaks.count());
}
