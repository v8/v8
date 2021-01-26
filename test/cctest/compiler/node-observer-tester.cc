// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/compiler/node-observer-tester.h"

#include "src/api/api-inl.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/pipeline.h"

namespace v8 {
namespace internal {
namespace compiler {

void TestWithObserveNode::OptimizeFunctionWithObserver(
    const char* function_name, NodeObserver* observer) {
  DCHECK_NOT_NULL(function_name);
  DCHECK_NOT_NULL(observer);
  Local<Function> api_function = Local<Function>::Cast(
      CcTest::global()
          ->Get(CcTest::isolate()->GetCurrentContext(), v8_str(function_name))
          .ToLocalChecked());
  Handle<JSFunction> function =
      Handle<JSFunction>::cast(v8::Utils::OpenHandle(*api_function));
  CHECK(function->shared().HasBytecodeArray());
  Handle<SharedFunctionInfo> sfi(function->shared(), isolate_);
  IsCompiledScope is_compiled_scope(sfi->is_compiled_scope(isolate_));
  JSFunction::EnsureFeedbackVector(function, &is_compiled_scope);

  OptimizedCompilationInfo compilation_info(main_zone(), isolate_, sfi,
                                            function, CodeKind::TURBOFAN);
  compilation_info.SetNodeObserver(observer);
  compilation_info.ReopenHandlesInNewHandleScope(isolate_);
  Handle<Code> code =
      Pipeline::GenerateCodeForTesting(&compilation_info, isolate_)
          .ToHandleChecked();
  function->set_code(*code);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
