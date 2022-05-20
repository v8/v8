// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WTF8_H_
#define V8_WASM_WTF8_H_

#include <cinttypes>
#include <cstdarg>
#include <memory>

#include "src/strings/unicode.h"

namespace v8 {
namespace internal {
namespace wasm {

using byte = unibrow::byte;

class Wtf8 {
 public:
  // Validate that the input has a valid WTF-8 encoding.
  //
  // This method checks for:
  // - valid utf-8 endcoding (e.g. no over-long encodings),
  // - absence of surrogate pairs,
  // - valid code point range.
  //
  // In terms of the WTF-8 specification (https://simonsapin.github.io/wtf-8/),
  // this function checks for a valid "generalized UTF-8" sequence, with the
  // additional constraint that surrogate pairs are not allowed.
  static bool ValidateEncoding(const byte* str, size_t length);
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WTF8_H_
