// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/assert-scope.h"
#include "src/debug/debug.h"
#include "src/factory.h"
#include "src/isolate.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects.h"

using namespace v8::internal;
using namespace v8::internal::wasm;

Handle<WasmDebugInfo> WasmDebugInfo::New(Handle<WasmInstanceObject> instance) {
  Isolate *isolate = instance->GetIsolate();
  Factory *factory = isolate->factory();
  Handle<FixedArray> arr = factory->NewFixedArray(kFieldCount, TENURED);
  arr->set(kInstance, *instance);

  return Handle<WasmDebugInfo>::cast(arr);
}

bool WasmDebugInfo::IsDebugInfo(Object *object) {
  if (!object->IsFixedArray()) return false;
  FixedArray *arr = FixedArray::cast(object);
  return arr->length() == kFieldCount && IsWasmInstance(arr->get(kInstance));
}

WasmDebugInfo *WasmDebugInfo::cast(Object *object) {
  DCHECK(IsDebugInfo(object));
  return reinterpret_cast<WasmDebugInfo *>(object);
}

WasmInstanceObject *WasmDebugInfo::wasm_instance() {
  return WasmInstanceObject::cast(get(kInstance));
}

void WasmDebugInfo::RunInterpreter(Handle<WasmDebugInfo> debug_info,
                                   int func_index, uint8_t *arg_buffer) {
  // TODO(clemensh): Implement this.
  UNIMPLEMENTED();
}
