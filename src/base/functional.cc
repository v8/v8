// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This also contains public domain code from MurmurHash. From the
// MurmurHash header:
//
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.

#include "src/base/functional.h"

#include <limits>

#include "src/base/bits.h"

namespace v8 {
namespace base {

namespace {

template <typename T>
inline size_t hash_value_unsigned(T value) {
  const unsigned size_t_bits = std::numeric_limits<size_t>::digits;
  // ceiling(std::numeric_limits<T>::digits / size_t_bits) - 1
  const unsigned length = (std::numeric_limits<T>::digits - 1) / size_t_bits;
  size_t seed = 0;
  // Hopefully, this loop can be unrolled.
  for (unsigned i = length * size_t_bits; i > 0; i -= size_t_bits) {
    seed ^= static_cast<size_t>(value >> i) + (seed << 6) + (seed >> 2);
  }
  seed ^= static_cast<size_t>(value) + (seed << 6) + (seed >> 2);
  return seed;
}

}  // namespace


// This code was taken from MurmurHash.
size_t hash_combine(size_t seed, size_t value) {
#if V8_HOST_ARCH_32_BIT
  const uint32_t c1 = 0xcc9e2d51;
  const uint32_t c2 = 0x1b873593;

  value *= c1;
  value = bits::RotateRight32(value, 15);
  value *= c2;

  seed ^= value;
  seed = bits::RotateRight32(seed, 13);
  seed = seed * 5 + 0xe6546b64;
#else
  const uint64_t m = V8_UINT64_C(0xc6a4a7935bd1e995);
  const uint32_t r = 47;

  value *= m;
  value ^= value >> r;
  value *= m;

  seed ^= value;
  seed *= m;
#endif  // V8_HOST_ARCH_32_BIT
  return seed;
}


// Thomas Wang, Integer Hash Functions.
// http://www.concentric.net/~Ttwang/tech/inthash.htm
size_t hash_value(unsigned int v) {
  v = ~v + (v << 15);  // v = (v << 15) - v - 1;
  v = v ^ (v >> 12);
  v = v + (v << 2);
  v = v ^ (v >> 4);
  v = v * 2057;  // v = (v + (v << 3)) + (v << 11);
  v = v ^ (v >> 16);
  return v;
}


size_t hash_value(unsigned long v) {  // NOLINT(runtime/int)
  return hash_value_unsigned(v);
}


size_t hash_value(unsigned long long v) {  // NOLINT(runtime/int)
  return hash_value_unsigned(v);
}


size_t hash_value(float v) {
  // 0 and -0 both hash to zero.
  return v != 0.0f ? hash_value_unsigned(bit_cast<uint32_t>(v)) : 0;
}


size_t hash_value(double v) {
  // 0 and -0 both hash to zero.
  return v != 0.0 ? hash_value_unsigned(bit_cast<uint64_t>(v)) : 0;
}

}  // namespace base
}  // namespace v8
