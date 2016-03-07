// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WASM_EXTERNAL_REFS_H
#define WASM_EXTERNAL_REFS_H

namespace v8 {
namespace internal {
namespace wasm {

static void f32_trunc_wrapper(float* param) { *param = truncf(*param); }

static void f32_floor_wrapper(float* param) { *param = floorf(*param); }

static void f32_ceil_wrapper(float* param) { *param = ceilf(*param); }

static void f32_nearest_int_wrapper(float* param) {
  *param = nearbyintf(*param);
}

static void f64_trunc_wrapper(double* param) { *param = trunc(*param); }

static void f64_floor_wrapper(double* param) { *param = floor(*param); }

static void f64_ceil_wrapper(double* param) { *param = ceil(*param); }

static void f64_nearest_int_wrapper(double* param) {
  *param = nearbyint(*param);
}

static void int64_to_float32_wrapper(int64_t* input, float* output) {
  *output = static_cast<float>(*input);
}

static void uint64_to_float32_wrapper(uint64_t* input, float* output) {
#if V8_CC_MSVC
  // With MSVC we use static_cast<float>(uint32_t) instead of
  // static_cast<float>(uint64_t) to achieve round-to-nearest-ties-even
  // semantics. The idea is to calculate
  // static_cast<float>(high_word) * 2^32 + static_cast<float>(low_word). To
  // achieve proper rounding in all cases we have to adjust the high_word
  // with a "rounding bit" sometimes. The rounding bit is stored in the LSB of
  // the high_word if the low_word may affect the rounding of the high_word.
  uint32_t low_word = static_cast<uint32_t>(*input & 0xffffffff);
  uint32_t high_word = static_cast<uint32_t>(*input >> 32);

  float shift = static_cast<float>(1ull << 32);
  // If the MSB of the high_word is set, then we make space for a rounding bit.
  if (high_word < 0x80000000) {
    high_word <<= 1;
    shift = static_cast<float>(1ull << 31);
  }

  if ((high_word & 0xfe000000) && low_word) {
    // Set the rounding bit.
    high_word |= 1;
  }

  float result = static_cast<float>(high_word);
  result *= shift;
  result += static_cast<float>(low_word);
  *output = result;

#else
  *output = static_cast<float>(*input);
#endif
}

static void int64_to_float64_wrapper(int64_t* input, double* output) {
  *output = static_cast<double>(*input);
}

static void uint64_to_float64_wrapper(uint64_t* input, double* output) {
#if V8_CC_MSVC
  // With MSVC we use static_cast<double>(uint32_t) instead of
  // static_cast<double>(uint64_t) to achieve round-to-nearest-ties-even
  // semantics. The idea is to calculate
  // static_cast<double>(high_word) * 2^32 + static_cast<double>(low_word).
  uint32_t low_word = static_cast<uint32_t>(*input & 0xffffffff);
  uint32_t high_word = static_cast<uint32_t>(*input >> 32);

  double shift = static_cast<double>(1ull << 32);

  double result = static_cast<double>(high_word);
  result *= shift;
  result += static_cast<double>(low_word);
  *output = result;

#else
  *output = static_cast<double>(*input);
#endif
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif
