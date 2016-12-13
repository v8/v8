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

namespace {

enum {
  kWasmDebugInfoWasmObj,
  kWasmDebugInfoWasmBytesHash,
  kWasmDebugInfoAsmJsOffsets,
  kWasmDebugInfoNumEntries
};

}  // namespace

Handle<WasmDebugInfo> WasmDebugInfo::New(Handle<WasmInstanceObject> instance) {
  Isolate *isolate = instance->GetIsolate();
  Factory *factory = isolate->factory();
  Handle<FixedArray> arr =
      factory->NewFixedArray(kWasmDebugInfoNumEntries, TENURED);
  arr->set(kWasmDebugInfoWasmObj, *instance);
  int hash = 0;
  Handle<SeqOneByteString> wasm_bytes =
      instance->get_compiled_module()->module_bytes();
  {
    DisallowHeapAllocation no_gc;
    hash = StringHasher::HashSequentialString(
        wasm_bytes->GetChars(), wasm_bytes->length(), kZeroHashSeed);
  }
  Handle<Object> hash_obj = factory->NewNumberFromInt(hash, TENURED);
  arr->set(kWasmDebugInfoWasmBytesHash, *hash_obj);

  return Handle<WasmDebugInfo>::cast(arr);
}

bool WasmDebugInfo::IsDebugInfo(Object *object) {
  if (!object->IsFixedArray()) return false;
  FixedArray *arr = FixedArray::cast(object);
  return arr->length() == kWasmDebugInfoNumEntries &&
         IsWasmInstance(arr->get(kWasmDebugInfoWasmObj)) &&
         arr->get(kWasmDebugInfoWasmBytesHash)->IsNumber();
}

WasmDebugInfo *WasmDebugInfo::cast(Object *object) {
  DCHECK(IsDebugInfo(object));
  return reinterpret_cast<WasmDebugInfo *>(object);
}

WasmInstanceObject *WasmDebugInfo::wasm_instance() {
  return WasmInstanceObject::cast(get(kWasmDebugInfoWasmObj));
}
