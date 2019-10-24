// Copyright 2019 the V8 project authors. All rights reserved.  Use of
// this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_WASM_DEBUG_H_
#define V8_WASM_WASM_DEBUG_H_

namespace v8 {
namespace internal {

template <typename T>
class Handle;
class JSObject;
class WasmInstanceObject;

namespace wasm {

// Get the global scope for a given instance. This will contain the wasm memory
// (if the instance has a memory) and the values of all globals.
Handle<JSObject> GetGlobalScopeObject(Handle<WasmInstanceObject>);

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_DEBUG_H_
