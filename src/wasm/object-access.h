// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_OBJECT_ACCESS_H_
#define V8_WASM_OBJECT_ACCESS_H_

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#include "src/common/globals.h"
#include "src/objects/fixed-array.h"
#include "src/objects/js-function.h"
#include "src/objects/shared-function-info.h"

namespace v8 {
namespace internal {
namespace wasm {

class ObjectAccess : public AllStatic {
 public:
  // Convert an offset into an object to an offset into a tagged object.
  static constexpr int ToTagged(int offset) { return offset - kHeapObjectTag; }
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_OBJECT_ACCESS_H_
