// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/strings/unicode-decoder.h"

#include "src/strings/unicode-inl.h"
#include "src/utils/memcopy.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/third_party/utf8-decoder/generalized-utf8-decoder.h"
#endif

namespace v8 {
namespace internal {

template <class Decoder>
Utf8DecoderBase<Decoder>::Utf8DecoderBase(
    const base::Vector<const uint8_t>& data)
    : encoding_(Encoding::kAscii),
      non_ascii_start_(NonAsciiStart(data.begin(), data.length())),
      utf16_length_(non_ascii_start_) {
  if (non_ascii_start_ == data.length()) return;

  bool is_one_byte = true;
  auto state = Decoder::DfaDecoder::kAccept;
  uint32_t current = 0;
  uint32_t previous = 0;
  const uint8_t* cursor = data.begin() + non_ascii_start_;
  const uint8_t* end = data.begin() + data.length();

  while (cursor < end) {
    auto previous_state = state;
    Decoder::DfaDecoder::Decode(*cursor, &state, &current);
    if (state < Decoder::DfaDecoder::kAccept) {
      DCHECK_EQ(state, Decoder::DfaDecoder::kReject);
      if (Decoder::kAllowIncompleteSequences) {
        state = Decoder::DfaDecoder::kAccept;
        static_assert(unibrow::Utf8::kBadChar > unibrow::Latin1::kMaxChar);
        is_one_byte = false;
        utf16_length_++;
        previous = unibrow::Utf8::kBadChar;
        current = 0;
        // If we were trying to continue a multibyte sequence, try this byte
        // again.
        if (previous_state != Decoder::DfaDecoder::kAccept) continue;
      } else {
        encoding_ = Encoding::kInvalid;
        return;
      }
    } else if (state == Decoder::DfaDecoder::kAccept) {
      if (Decoder::InvalidCodePointSequence(current, previous)) {
        encoding_ = Encoding::kInvalid;
        return;
      }
      is_one_byte = is_one_byte && current <= unibrow::Latin1::kMaxChar;
      utf16_length_++;
      if (current > unibrow::Utf16::kMaxNonSurrogateCharCode) utf16_length_++;
      previous = current;
      current = 0;
    }
    cursor++;
  }

  if (state == Decoder::DfaDecoder::kAccept) {
    encoding_ = is_one_byte ? Encoding::kLatin1 : Encoding::kUtf16;
  } else if (Decoder::kAllowIncompleteSequences) {
    static_assert(unibrow::Utf8::kBadChar > unibrow::Latin1::kMaxChar);
    encoding_ = Encoding::kUtf16;
    utf16_length_++;
  } else {
    encoding_ = Encoding::kInvalid;
  }
}

template <class Decoder>
template <typename Char>
void Utf8DecoderBase<Decoder>::Decode(Char* out,
                                      const base::Vector<const uint8_t>& data) {
  DCHECK(!is_invalid());
  CopyChars(out, data.begin(), non_ascii_start_);

  out += non_ascii_start_;

  auto state = Decoder::DfaDecoder::kAccept;
  uint32_t current = 0;
  const uint8_t* cursor = data.begin() + non_ascii_start_;
  const uint8_t* end = data.begin() + data.length();

  while (cursor < end) {
    auto previous_state = state;
    Decoder::DfaDecoder::Decode(*cursor, &state, &current);
    if (Decoder::kAllowIncompleteSequences &&
        state < Decoder::DfaDecoder::kAccept) {
      state = Decoder::DfaDecoder::kAccept;
      *(out++) = static_cast<Char>(unibrow::Utf8::kBadChar);
      current = 0;
      // If we were trying to continue a multibyte sequence, try this byte
      // again.
      if (previous_state != Decoder::DfaDecoder::kAccept) continue;
    } else if (state == Decoder::DfaDecoder::kAccept) {
      if (sizeof(Char) == 1 ||
          current <= unibrow::Utf16::kMaxNonSurrogateCharCode) {
        *(out++) = static_cast<Char>(current);
      } else {
        *(out++) = unibrow::Utf16::LeadSurrogate(current);
        *(out++) = unibrow::Utf16::TrailSurrogate(current);
      }
      current = 0;
    }
    cursor++;
  }

  if (Decoder::kAllowIncompleteSequences &&
      state != Decoder::DfaDecoder::kAccept) {
    *out = static_cast<Char>(unibrow::Utf8::kBadChar);
  } else {
    DCHECK_EQ(state, Decoder::DfaDecoder::kAccept);
  }
}

template V8_EXPORT_PRIVATE Utf8DecoderBase<Utf8Decoder>::Utf8DecoderBase(
    const base::Vector<const uint8_t>& data);

template V8_EXPORT_PRIVATE void Utf8DecoderBase<Utf8Decoder>::Decode(
    uint8_t* out, const base::Vector<const uint8_t>& data);

template V8_EXPORT_PRIVATE void Utf8DecoderBase<Utf8Decoder>::Decode(
    uint16_t* out, const base::Vector<const uint8_t>& data);

#if V8_ENABLE_WEBASSEMBLY
template Utf8DecoderBase<Wtf8Decoder>::Utf8DecoderBase(
    const base::Vector<const uint8_t>& data);

template void Utf8DecoderBase<Wtf8Decoder>::Decode(
    uint8_t* out, const base::Vector<const uint8_t>& data);

template void Utf8DecoderBase<Wtf8Decoder>::Decode(
    uint16_t* out, const base::Vector<const uint8_t>& data);
#endif  // V8_ENABLE_WEBASSEMBLY

}  // namespace internal
}  // namespace v8
