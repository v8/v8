// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap-object-header.h"

#include "src/base/macros.h"

namespace cppgc {
namespace internal {

void HeapObjectHeader::CheckApiConstants() {
  STATIC_ASSERT(api_constants::kFullyConstructedBitMask ==
                FullyConstructedField::kMask);
  STATIC_ASSERT(api_constants::kFullyConstructedBitFieldOffsetFromPayload ==
                (sizeof(encoded_high_) + sizeof(encoded_low_)));
}

}  // namespace internal
}  // namespace cppgc
