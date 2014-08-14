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

inline uint32_t RotateRight32(uint32_t value, uint32_t shift) {
  if (shift == 0) return value;
  return (value >> shift) | (value << (32 - shift));
}


inline uint64_t RotateRight64(uint64_t value, uint64_t shift) {
  if (shift == 0) return value;
  return (value >> shift) | (value << (64 - shift));
}

}  // namespace bits
}  // namespace base
}  // namespace v8

#endif  // V8_BASE_BITS_H_
