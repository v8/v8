// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_ALLOCATION_H_
#define INCLUDE_CPPGC_ALLOCATION_H_

#include <stdint.h>
#include <atomic>

#include "include/cppgc/api-constants.h"

namespace cppgc {
namespace internal {

// Marks an object as being fully constructed, resulting in precise handling
// by the garbage collector.
inline void MarkObjectAsFullyConstructed(const void* payload) {
  // See api_constants for an explanation of the constants.
  std::atomic<uint16_t>* atomic_mutable_bitfield =
      reinterpret_cast<std::atomic<uint16_t>*>(
          const_cast<uint16_t*>(reinterpret_cast<const uint16_t*>(
              reinterpret_cast<const uint8_t*>(payload) -
              api_constants::kFullyConstructedBitFieldOffsetFromPayload)));
  uint16_t value = atomic_mutable_bitfield->load(std::memory_order_relaxed);
  value = value | api_constants::kFullyConstructedBitMask;
  atomic_mutable_bitfield->store(value, std::memory_order_release);
}

}  // namespace internal
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_ALLOCATION_H_
