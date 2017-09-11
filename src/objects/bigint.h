// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_BIGINT_H_
#define V8_OBJECTS_BIGINT_H_

#include "src/globals.h"
#include "src/objects.h"
#include "src/utils.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// UNDER CONSTRUCTION!
// Arbitrary precision integers in JavaScript.
class BigInt : public HeapObject {
 public:
  DECL_CAST(BigInt)
  DECL_VERIFIER(BigInt)
  DECL_PRINTER(BigInt)

  DECL_INT_ACCESSORS(value)

  static const int kValueOffset = HeapObject::kHeaderSize;
  static const int kSize = kValueOffset + kPointerSize;

  bool Equals(BigInt* other) const;
  static Handle<String> ToString(Handle<BigInt> bigint);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(BigInt);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_BIGINT_H_
