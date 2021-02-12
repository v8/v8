// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline/baseline.h"

// TODO(v8:11421): Remove #if once baseline compiler is ported to other
// architectures.
#if V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_ARM64

#include "src/baseline/baseline-compiler.h"
#include "src/heap/factory-inl.h"
#include "src/logging/counters.h"
#include "src/objects/script-inl.h"
#include "src/objects/shared-function-info-inl.h"

namespace v8 {
namespace internal {

Handle<Code> CompileWithBaseline(
    Isolate* isolate, Handle<SharedFunctionInfo> shared_function_info,
    Handle<BytecodeArray> bytecode) {
  RuntimeCallTimerScope runtimeTimer(isolate,
                                     RuntimeCallCounterId::kCompileBaseline);
  baseline::BaselineCompiler compiler(isolate, shared_function_info, bytecode);

  compiler.GenerateCode();

  return compiler.Build(isolate);
}

// TODO(v8:11429): This can be the basis of Compiler::CompileBaseline
Handle<Code> CompileWithBaseline(Isolate* isolate,
                                 Handle<SharedFunctionInfo> shared) {
  if (shared->HasBaselineData()) {
    return handle(shared->baseline_data().baseline_code(), isolate);
  }

  if (FLAG_trace_opt) {
    PrintF("[compiling method ");
    shared->ShortPrint();
    PrintF(" using Sparkplug]\n");
  }

  base::ElapsedTimer timer;
  timer.Start();

  Handle<Code> code = CompileWithBaseline(
      isolate, shared, handle(shared->GetBytecodeArray(isolate), isolate));

  // TODO(v8:11429): Extract to Factory::NewBaselineData
  Handle<BaselineData> baseline_data = Handle<BaselineData>::cast(
      isolate->factory()->NewStruct(BASELINE_DATA_TYPE, AllocationType::kOld));
  baseline_data->set_baseline_code(*code);
  baseline_data->set_data(
      HeapObject::cast(shared->function_data(kAcquireLoad)));

  shared->set_baseline_data(*baseline_data);

  if (FLAG_print_code) {
    code->Print();
  }

  if (shared->script().IsScript()) {
    Compiler::LogFunctionCompilation(
        isolate, CodeEventListener::FUNCTION_TAG, shared,
        handle(Script::cast(shared->script()), isolate),
        Handle<AbstractCode>::cast(code), CodeKind::SPARKPLUG,
        timer.Elapsed().InMillisecondsF());
  }

  if (FLAG_trace_opt) {
    // TODO(v8:11429): Move to Compiler.
    PrintF("[completed compiling ");
    shared->ShortPrint();
    PrintF(" using Sparkplug - took %0.3f ms]\n",
           timer.Elapsed().InMillisecondsF());
  }

  return code;
}

}  // namespace internal
}  // namespace v8

#else

namespace v8 {
namespace internal {

Handle<Code> CompileWithBaseline(Isolate* isolate,
                                 Handle<SharedFunctionInfo> shared) {
  UNREACHABLE();
}

}  // namespace internal
}  // namespace v8

#endif
