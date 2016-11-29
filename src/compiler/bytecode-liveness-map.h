// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BYTECODE_LIVENESS_MAP_H_
#define V8_COMPILER_BYTECODE_LIVENESS_MAP_H_

#include "src/base/hashmap.h"
#include "src/bit-vector.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

class Zone;

namespace compiler {

struct Liveness {
  BitVector* in;
  BitVector* out;

  Liveness(int size, Zone* zone);
};

class V8_EXPORT_PRIVATE BytecodeLivenessMap {
 public:
  BytecodeLivenessMap(int size, Zone* zone);

  Liveness& InitializeLiveness(int offset, int size, Zone* zone);

  Liveness& GetLiveness(int offset);
  const Liveness& GetLiveness(int offset) const;

  BitVector* GetInLiveness(int offset) { return GetLiveness(offset).in; }
  const BitVector* GetInLiveness(int offset) const {
    return GetLiveness(offset).in;
  }

  BitVector* GetOutLiveness(int offset) { return GetLiveness(offset).out; }
  const BitVector* GetOutLiveness(int offset) const {
    return GetLiveness(offset).out;
  }

 private:
  base::TemplateHashMapImpl<int, Liveness, base::KeyEqualityMatcher<int>,
                            ZoneAllocationPolicy>
      liveness_map_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BYTECODE_LIVENESS_MAP_H_
