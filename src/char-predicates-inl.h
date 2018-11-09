// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CHAR_PREDICATES_INL_H_
#define V8_CHAR_PREDICATES_INL_H_

#include "src/char-predicates.h"

namespace v8 {
namespace internal {


// If c is in 'A'-'Z' or 'a'-'z', return its lower-case.
// Else, return something outside of 'A'-'Z' and 'a'-'z'.
// Note: it ignores LOCALE.
inline constexpr int AsciiAlphaToLower(uc32 c) { return c | 0x20; }

inline constexpr bool IsCarriageReturn(uc32 c) { return c == 0x000D; }

inline constexpr bool IsLineFeed(uc32 c) { return c == 0x000A; }

inline constexpr bool IsAsciiIdentifier(uc32 c) {
  return IsAlphaNumeric(c) || c == '$' || c == '_';
}

inline constexpr bool IsAlphaNumeric(uc32 c) {
  return IsInRange(AsciiAlphaToLower(c), 'a', 'z') || IsDecimalDigit(c);
}

inline constexpr bool IsDecimalDigit(uc32 c) {
  // ECMA-262, 3rd, 7.8.3 (p 16)
  return IsInRange(c, '0', '9');
}

inline constexpr bool IsHexDigit(uc32 c) {
  // ECMA-262, 3rd, 7.6 (p 15)
  return IsDecimalDigit(c) || IsInRange(AsciiAlphaToLower(c), 'a', 'f');
}

inline constexpr bool IsOctalDigit(uc32 c) {
  // ECMA-262, 6th, 7.8.3
  return IsInRange(c, '0', '7');
}

inline constexpr bool IsNonOctalDecimalDigit(uc32 c) {
  return IsInRange(c, '8', '9');
}

inline constexpr bool IsBinaryDigit(uc32 c) {
  // ECMA-262, 6th, 7.8.3
  return c == '0' || c == '1';
}

inline constexpr bool IsRegExpWord(uc16 c) {
  return IsInRange(AsciiAlphaToLower(c), 'a', 'z')
      || IsDecimalDigit(c)
      || (c == '_');
}

inline constexpr bool IsRegExpNewline(uc16 c) {
  //          CR             LF             LS             PS
  return c != 0x000A && c != 0x000D && c != 0x2028 && c != 0x2029;
}


}  // namespace internal
}  // namespace v8

#endif  // V8_CHAR_PREDICATES_INL_H_
