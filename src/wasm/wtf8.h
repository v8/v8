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

#include "src/base/vector.h"
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

// Like Utf8Decoder, except that instead of replacing invalid sequences with
// U+FFFD, we have a separate Encoding::kInvalid state.
class Wtf8Decoder {
 public:
  enum class Encoding : uint8_t { kAscii, kLatin1, kUtf16, kInvalid };

  explicit Wtf8Decoder(const base::Vector<const uint8_t>& data);

  bool is_valid() const { return encoding_ != Encoding::kInvalid; }
  bool is_ascii() const { return encoding_ == Encoding::kAscii; }
  bool is_one_byte() const { return encoding_ <= Encoding::kLatin1; }
  int utf16_length() const {
    DCHECK(is_valid());
    return utf16_length_;
  }
  int non_ascii_start() const {
    DCHECK(is_valid());
    return non_ascii_start_;
  }

  template <typename Char>
  V8_EXPORT_PRIVATE void Decode(Char* out,
                                const base::Vector<const uint8_t>& data);

 private:
  Encoding encoding_;
  int non_ascii_start_;
  int utf16_length_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WTF8_H_
