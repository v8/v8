// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_API_CONSTANTS_H_
#define INCLUDE_CPPGC_API_CONSTANTS_H_

#include <stddef.h>
#include <stdint.h>

#include "include/v8config.h"

namespace cppgc {
namespace internal {

// Internal constants to avoid exposing internal types on the API surface.
// DO NOT USE THESE CONSTANTS FROM USER CODE!
namespace api_constants {
// Offset of the uint16_t bitfield from the payload contaning the
// in-construction bit. This is subtracted from the payload pointer to get
// to the right bitfield.
static constexpr size_t kFullyConstructedBitFieldOffsetFromPayload =
    2 * sizeof(uint16_t);
// Mask for in-construction bit.
static constexpr size_t kFullyConstructedBitMask = size_t{1};

}  // namespace api_constants

}  // namespace internal
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_API_CONSTANTS_H_
