// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_SIMD_SHUFFLE_H_
#define V8_WASM_SIMD_SHUFFLE_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {
namespace wasm {

// Converts a shuffle into canonical form, meaning that the first lane index
// is in the range [0 .. 15]. Set |inputs_equal| true if this is an explicit
// swizzle. Returns canonicalized |shuffle|, |needs_swap|, and |is_swizzle|.
// If |needs_swap| is true, inputs must be swapped. If |is_swizzle| is true,
// the second input can be ignored.
void CanonicalizeShuffle(bool inputs_equal, uint8_t* shuffle, bool* needs_swap,
                         bool* is_swizzle);

bool TryMatchIdentity(const uint8_t* shuffle);

// Tries to match a byte shuffle to a scalar splat operation. Returns the
// index of the lane if successful.
template <int LANES>
bool TryMatchSplat(const uint8_t* shuffle, int* index) {
  const int kBytesPerLane = kSimd128Size / LANES;
  // Get the first lane's worth of bytes and check that indices start at a
  // lane boundary and are consecutive.
  uint8_t lane0[kBytesPerLane];
  lane0[0] = shuffle[0];
  if (lane0[0] % kBytesPerLane != 0) return false;
  for (int i = 1; i < kBytesPerLane; ++i) {
    lane0[i] = shuffle[i];
    if (lane0[i] != lane0[0] + i) return false;
  }
  // Now check that the other lanes are identical to lane0.
  for (int i = 1; i < LANES; ++i) {
    for (int j = 0; j < kBytesPerLane; ++j) {
      if (lane0[j] != shuffle[i * kBytesPerLane + j]) return false;
    }
  }
  *index = lane0[0] / kBytesPerLane;
  return true;
}

bool TryMatch32x4Shuffle(const uint8_t* shuffle, uint8_t* shuffle32x4);

bool TryMatch16x8Shuffle(const uint8_t* shuffle, uint8_t* shuffle16x8);

bool TryMatchConcat(const uint8_t* shuffle, uint8_t* offset);

bool TryMatchBlend(const uint8_t* shuffle);

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_SIMD_SHUFFLE_H_
