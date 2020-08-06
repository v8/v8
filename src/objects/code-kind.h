// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_CODE_KIND_H_
#define V8_OBJECTS_CODE_KIND_H_

#include "src/flags/flags.h"

namespace v8 {
namespace internal {

// TODO(jgruber,rmcilroy): Rename OPTIMIZED_FUNCTION once we've fully
// disambiguated Turboprop, Turbofan, and NCI code kinds.
// TODO(jgruber): Rename STUB to DEOPT_ENTRIES_OR_FOR_TESTING, or split it into
// DEOPT_ENTRIES and FOR_TESTING, or convert DEOPT_ENTRIES into a builtin.
#define CODE_KIND_LIST(V)  \
  V(OPTIMIZED_FUNCTION)    \
  V(BYTECODE_HANDLER)      \
  V(STUB)                  \
  V(BUILTIN)               \
  V(REGEXP)                \
  V(WASM_FUNCTION)         \
  V(WASM_TO_CAPI_FUNCTION) \
  V(WASM_TO_JS_FUNCTION)   \
  V(JS_TO_WASM_FUNCTION)   \
  V(JS_TO_JS_FUNCTION)     \
  V(C_WASM_ENTRY)          \
  V(INTERPRETED_FUNCTION)  \
  V(NATIVE_CONTEXT_INDEPENDENT)

enum class CodeKind {
#define DEFINE_CODE_KIND_ENUM(name) name,
  CODE_KIND_LIST(DEFINE_CODE_KIND_ENUM)
#undef DEFINE_CODE_KIND_ENUM
};

#define V(name) +1
static constexpr int kCodeKindCount = CODE_KIND_LIST(V);
#undef V

const char* CodeKindToString(CodeKind kind);

inline bool CodeKindIsInterpretedJSFunction(CodeKind kind) {
  return kind == CodeKind::INTERPRETED_FUNCTION;
}

inline bool CodeKindIsNativeContextIndependentJSFunction(CodeKind kind) {
  return kind == CodeKind::NATIVE_CONTEXT_INDEPENDENT;
}

inline bool CodeKindIsBuiltinOrJSFunction(CodeKind kind) {
  return kind == CodeKind::BUILTIN || kind == CodeKind::INTERPRETED_FUNCTION ||
         kind == CodeKind::OPTIMIZED_FUNCTION ||
         kind == CodeKind::NATIVE_CONTEXT_INDEPENDENT;
}

inline bool CodeKindIsOptimizedJSFunction(CodeKind kind) {
  return kind == CodeKind::OPTIMIZED_FUNCTION ||
         kind == CodeKind::NATIVE_CONTEXT_INDEPENDENT;
}

inline bool CodeKindCanDeoptimize(CodeKind kind) {
  // Even though NCI code does not deopt by itself at the time of writing,
  // tests may trigger deopts manually and thus we cannot make a narrower
  // distinction here.
  return CodeKindIsOptimizedJSFunction(kind);
}

inline CodeKind CodeKindForTopTier() {
  return FLAG_turbo_nci_as_highest_tier ? CodeKind::NATIVE_CONTEXT_INDEPENDENT
                                        : CodeKind::OPTIMIZED_FUNCTION;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_CODE_KIND_H_
