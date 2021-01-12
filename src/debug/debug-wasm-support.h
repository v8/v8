// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_DEBUG_WASM_SUPPORT_H_
#define V8_DEBUG_DEBUG_WASM_SUPPORT_H_

#include <memory>

namespace v8 {
namespace debug {
class ScopeIterator;
}  // namespace debug

namespace internal {

template <typename T>
class Handle;
class JSArray;
class JSObject;
class WasmFrame;
class WasmInstanceObject;
class WasmModuleObject;

Handle<JSObject> GetWasmDebugProxy(WasmFrame* frame);

std::unique_ptr<debug::ScopeIterator> GetWasmScopeIterator(WasmFrame* frame);

Handle<JSArray> GetWasmInstanceObjectInternalProperties(
    Handle<WasmInstanceObject> instance);
Handle<JSArray> GetWasmModuleObjectInternalProperties(
    Handle<WasmModuleObject> module_object);

}  // namespace internal
}  // namespace v8

#endif  // V8_DEBUG_DEBUG_WASM_SUPPORT_H_
