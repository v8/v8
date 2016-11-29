// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/bytecode-liveness-map.h"

namespace v8 {
namespace internal {
namespace compiler {

Liveness::Liveness(int size, Zone* zone)
    : in(new (zone) BitVector(size, zone)),
      out(new (zone) BitVector(size, zone)) {}

BytecodeLivenessMap::BytecodeLivenessMap(int bytecode_size, Zone* zone)
    : liveness_map_(base::bits::RoundUpToPowerOfTwo32(bytecode_size / 4 + 1),
                    base::KeyEqualityMatcher<int>(),
                    ZoneAllocationPolicy(zone)) {}

uint32_t OffsetHash(int offset) { return offset; }

Liveness& BytecodeLivenessMap::InitializeLiveness(int offset, int size,
                                                  Zone* zone) {
  return liveness_map_
      .LookupOrInsert(offset, OffsetHash(offset),
                      [&]() { return Liveness(size, zone); },
                      ZoneAllocationPolicy(zone))
      ->value;
}

Liveness& BytecodeLivenessMap::GetLiveness(int offset) {
  return liveness_map_.Lookup(offset, OffsetHash(offset))->value;
}

const Liveness& BytecodeLivenessMap::GetLiveness(int offset) const {
  return liveness_map_.Lookup(offset, OffsetHash(offset))->value;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
