// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_BITS_H_
#define V8_BASE_BITS_H_

#include "include/v8stdint.h"
#if V8_OS_WIN32
#include "src/base/win32-headers.h"
#endif

namespace v8 {
namespace base {
namespace bits {

#if V8_CC_MSVC

#pragma intrinsic(_rotr)
#pragma intrinsic(_rotr64)

inline uint32_t RotateRight32(uint32_t value, uint32_t shift) {
  return _rotr(value, shift);
}


inline uint64_t RotateRight64(uint64_t value, uint32_t shift) {
  return _rotr64(value, shift);
}

#else  // V8_CC_MSVC

inline uint32_t RotateRight32(uint32_t value, uint32_t shift) {
  if (shift == 0) return value;
  return (value >> shift) | (value << (32 - shift));
}


inline uint64_t RotateRight64(uint64_t value, uint32_t shift) {
  if (shift == 0) return value;
  return (value >> shift) | (value << (64 - shift));
}

#endif  // V8_CC_MSVC

}  // namespace bits
}  // namespace base
}  // namespace v8

#endif  // V8_BASE_BITS_H_
