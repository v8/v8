// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wtf8.h"

#include "src/strings/unicode-decoder.h"
#include "src/strings/unicode.h"
#include "src/third_party/utf8-decoder/generalized-utf8-decoder.h"
#include "src/utils/memcopy.h"

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

Wtf8Decoder::Wtf8Decoder(const base::Vector<const uint8_t>& data)
    : encoding_(Encoding::kAscii),
      non_ascii_start_(NonAsciiStart(data.begin(), data.length())),
      utf16_length_(non_ascii_start_) {
  if (non_ascii_start_ == data.length()) return;

  bool is_one_byte = true;
  auto state = GeneralizedUtf8DfaDecoder::kAccept;
  uint32_t current = 0;
  uint32_t previous = 0;

  for (size_t i = non_ascii_start_; i < data.size(); i++) {
    GeneralizedUtf8DfaDecoder::Decode(data[i], &state, &current);
    if (state < GeneralizedUtf8DfaDecoder::kAccept) {
      DCHECK_EQ(state, GeneralizedUtf8DfaDecoder::kReject);
      encoding_ = Encoding::kInvalid;
      return;
    }
    if (state == GeneralizedUtf8DfaDecoder::kAccept) {
      if (unibrow::Utf16::IsTrailSurrogate(current) &&
          unibrow::Utf16::IsLeadSurrogate(previous)) {
        encoding_ = Encoding::kInvalid;
        return;
      }
      is_one_byte = is_one_byte && current <= unibrow::Latin1::kMaxChar;
      utf16_length_++;
      if (current > unibrow::Utf16::kMaxNonSurrogateCharCode) utf16_length_++;
      previous = current;
      current = 0;
    }
  }

  if (state == GeneralizedUtf8DfaDecoder::kAccept) {
    encoding_ = is_one_byte ? Encoding::kLatin1 : Encoding::kUtf16;
  } else {
    encoding_ = Encoding::kInvalid;
  }
}

template <typename Char>
void Wtf8Decoder::Decode(Char* out, const base::Vector<const uint8_t>& data) {
  DCHECK(is_valid());
  CopyChars(out, data.begin(), non_ascii_start_);

  out += non_ascii_start_;

  auto state = GeneralizedUtf8DfaDecoder::kAccept;
  uint32_t t = 0;
  for (size_t i = non_ascii_start_; i < data.size(); i++) {
    GeneralizedUtf8DfaDecoder::Decode(data[i], &state, &t);
    if (state == GeneralizedUtf8DfaDecoder::kAccept) {
      if (sizeof(Char) == 1 || t <= unibrow::Utf16::kMaxNonSurrogateCharCode) {
        *(out++) = static_cast<Char>(t);
      } else {
        *(out++) = unibrow::Utf16::LeadSurrogate(t);
        *(out++) = unibrow::Utf16::TrailSurrogate(t);
      }
      t = 0;
    }
  }
  DCHECK_EQ(state, GeneralizedUtf8DfaDecoder::kAccept);
}

template void Wtf8Decoder::Decode(uint8_t* out,
                                  const base::Vector<const uint8_t>& data);

template void Wtf8Decoder::Decode(uint16_t* out,
                                  const base::Vector<const uint8_t>& data);

}  // namespace wasm
}  // namespace internal
}  // namespace v8
