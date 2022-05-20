// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wtf8.h"

#include "src/strings/unicode.h"
#include "src/third_party/utf8-decoder/generalized-utf8-decoder.h"

namespace v8 {
namespace internal {
namespace wasm {

bool Wtf8::ValidateEncoding(const byte* bytes, size_t length) {
  auto state = GeneralizedUtf8DfaDecoder::kAccept;
  uint32_t current = 0;
  uint32_t previous = 0;
  for (size_t i = 0; i < length; i++) {
    GeneralizedUtf8DfaDecoder::Decode(bytes[i], &state, &current);
    if (state == GeneralizedUtf8DfaDecoder::kReject) return false;
    if (state == GeneralizedUtf8DfaDecoder::kAccept) {
      if (unibrow::Utf16::IsTrailSurrogate(current) &&
          unibrow::Utf16::IsLeadSurrogate(previous)) {
        return false;
      }
      previous = current;
      current = 0;
    }
  }
  return state == GeneralizedUtf8DfaDecoder::kAccept;
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
