// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_WASM_FEATURE_FLAGS_H_
#define V8_WASM_WASM_FEATURE_FLAGS_H_

#define FOREACH_WASM_FEATURE_FLAG(V)                                  \
  V(mv, "multi-value support", false)                                 \
  V(eh, "exception handling opcodes", false)                          \
  V(se, "sign extension opcodes", true)                               \
  V(sat_f2i_conversions, "saturating float conversion opcodes", true) \
  V(threads, "thread opcodes", false)                                 \
  V(simd, "SIMD opcodes", false)                                      \
  V(anyref, "anyref opcodes", false)                                  \
  V(bigint, "JS BigInt support", false)                               \
  V(bulk_memory, "bulk memory opcodes", true)                         \
  V(return_call, "return call opcodes", false)                        \
  V(type_reflection, "wasm type reflection in JS", false)             \
  V(compilation_hints, "compilation hints section", false)
#endif  // V8_WASM_WASM_FEATURE_FLAGS_H_
