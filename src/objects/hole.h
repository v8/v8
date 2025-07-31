// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_HOLE_H_
#define V8_OBJECTS_HOLE_H_

#include "src/objects/heap-number.h"
#include "src/objects/heap-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/hole-tq.inc"

class Hole : public HeapObject {
 public:
  DECL_VERIFIER(Hole)

  static constexpr int kSize = kHeaderSize;

  using BodyDescriptor = FixedBodyDescriptor<kSize, kSize, kSize>;

  DECL_PRINTER(Hole)

  OBJECT_CONSTRUCTORS(Hole, HeapObject);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_HOLE_H_
