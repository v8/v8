// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_WASM_SUBTYPING_H_
#define V8_WASM_WASM_SUBTYPING_H_

#include "src/wasm/value-type.h"

namespace v8 {
namespace internal {
namespace wasm {

struct WasmModule;
V8_EXPORT_PRIVATE bool IsSubtypeOfRef(ValueType subtype, ValueType supertype,
                                      const WasmModule* module);

V8_INLINE bool IsSubtypeOf(ValueType subtype, ValueType supertype,
                           const WasmModule* module) {
  if (subtype == supertype) return true;
  bool both_reference_types =
      subtype.IsReferenceType() && supertype.IsReferenceType();
  if (!both_reference_types) return false;
  return IsSubtypeOfRef(subtype, supertype, module);
}

ValueType CommonSubtype(ValueType a, ValueType b, const WasmModule* module);

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_SUBTYPING_H_
