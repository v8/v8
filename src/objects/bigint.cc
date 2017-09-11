// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/bigint.h"

#include "src/objects-inl.h"

namespace v8 {
namespace internal {

#ifdef OBJECT_PRINT
void BigInt::BigIntPrint(std::ostream& os) {
  DisallowHeapAllocation no_gc;
  HeapObject::PrintHeader(os, "BigInt");
  os << "- value: " << value();
  os << "\n";
}
#endif  // OBJECT_PRINT

bool BigInt::Equals(BigInt* other) const {
  DisallowHeapAllocation no_gc;
  return value() == other->value();
}

Handle<String> BigInt::ToString(Handle<BigInt> bigint) {
  Isolate* isolate = bigint->GetIsolate();
  Handle<Object> number = isolate->factory()->NewNumberFromInt(bigint->value());
  return isolate->factory()->NumberToString(number);
}

}  // namespace internal
}  // namespace v8
