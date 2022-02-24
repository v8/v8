// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev.h"

#include "src/common/globals.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/js-heap-broker.h"
#include "src/maglev/maglev-compiler.h"
#include "src/objects/js-function-inl.h"
#include "src/objects/shared-function-info-inl.h"

namespace v8 {
namespace internal {

MaybeHandle<CodeT> Maglev::Compile(Isolate* isolate,
                                   Handle<JSFunction> function) {
  CanonicalHandleScope canonical_handle_scope(isolate);
  Zone broker_zone(isolate->allocator(), "maglev-broker-zone");
  compiler::JSHeapBroker broker(isolate, &broker_zone, FLAG_trace_heap_broker,
                                CodeKind::MAGLEV);

  compiler::CompilationDependencies* deps =
      broker_zone.New<compiler::CompilationDependencies>(&broker, &broker_zone);
  USE(deps);  // The deps register themselves in the heap broker.

  broker.SetTargetNativeContextRef(handle(function->native_context(), isolate));
  broker.InitializeAndStartSerializing();
  broker.StopSerializing();

  maglev::MaglevCompiler compiler(&broker, function);
  return ToCodeT(compiler.Compile(), isolate);
}

}  // namespace internal
}  // namespace v8
