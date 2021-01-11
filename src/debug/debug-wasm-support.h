// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_DEBUG_WASM_SUPPORT_H_
#define V8_DEBUG_DEBUG_WASM_SUPPORT_H_

namespace v8 {
namespace internal {

template <typename T>
class Handle;
class JSObject;
class WasmFrame;
class WasmInstanceObject;

Handle<JSObject> GetModuleScopeObject(Handle<WasmInstanceObject> instance);
Handle<JSObject> GetLocalScopeObject(WasmFrame* frame);
Handle<JSObject> GetStackScopeObject(WasmFrame* frame);
Handle<JSObject> GetJSDebugProxy(WasmFrame* frame);

}  // namespace internal
}  // namespace v8

#endif  // V8_DEBUG_DEBUG_WASM_SUPPORT_H_
