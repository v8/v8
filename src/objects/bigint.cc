// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/bigint.h"

#include "src/objects-inl.h"

namespace v8 {
namespace internal {

Handle<BigInt> BigInt::UnaryMinus(Handle<BigInt> x) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

Handle<BigInt> BigInt::BitwiseNot(Handle<BigInt> x) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

MaybeHandle<BigInt> BigInt::Exponentiate(Handle<BigInt> base,
                                         Handle<BigInt> exponent) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

Handle<BigInt> BigInt::Multiply(Handle<BigInt> x, Handle<BigInt> y) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

MaybeHandle<BigInt> BigInt::Divide(Handle<BigInt> x, Handle<BigInt> y) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

MaybeHandle<BigInt> BigInt::Remainder(Handle<BigInt> x, Handle<BigInt> y) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

Handle<BigInt> BigInt::Add(Handle<BigInt> x, Handle<BigInt> y) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

Handle<BigInt> BigInt::Subtract(Handle<BigInt> x, Handle<BigInt> y) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

Handle<BigInt> BigInt::LeftShift(Handle<BigInt> x, Handle<BigInt> y) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

Handle<BigInt> BigInt::SignedRightShift(Handle<BigInt> x, Handle<BigInt> y) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

MaybeHandle<BigInt> BigInt::UnsignedRightShift(Handle<BigInt> x,
                                               Handle<BigInt> y) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

bool BigInt::LessThan(Handle<BigInt> x, Handle<BigInt> y) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

bool BigInt::Equal(BigInt* x, BigInt* y) {
  if (x->sign() != y->sign()) return false;
  if (x->length() != y->length()) return false;
  for (int i = 0; i < x->length(); i++) {
    if (x->digit(i) != y->digit(i)) return false;
  }
  return true;
}

Handle<BigInt> BigInt::BitwiseAnd(Handle<BigInt> x, Handle<BigInt> y) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

Handle<BigInt> BigInt::BitwiseXor(Handle<BigInt> x, Handle<BigInt> y) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

Handle<BigInt> BigInt::BitwiseOr(Handle<BigInt> x, Handle<BigInt> y) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

Handle<String> BigInt::ToString(Handle<BigInt> bigint, int radix) {
  // TODO(jkummerow): Implement this properly.
  Factory* factory = bigint->GetIsolate()->factory();
  if (bigint->is_zero()) return factory->NewStringFromStaticChars("0");
  DCHECK(bigint->length() == 1);
  int value = static_cast<uint32_t>(bigint->digit(0));
  if (bigint->sign()) value = -value;  // This can overflow. We don't care.
  Handle<Object> number = factory->NewNumberFromInt(value);
  return factory->NumberToString(number);
}

void BigInt::Initialize(int length, bool zero_initialize) {
  set_length(length);
  set_sign(false);
  if (zero_initialize) {
    memset(reinterpret_cast<void*>(reinterpret_cast<Address>(this) +
                                   kDigitsOffset - kHeapObjectTag),
           0, length * kDigitSize);
#if DEBUG
  } else {
    memset(reinterpret_cast<void*>(reinterpret_cast<Address>(this) +
                                   kDigitsOffset - kHeapObjectTag),
           0xbf, length * kDigitSize);
#endif
  }
}

#ifdef OBJECT_PRINT
void BigInt::BigIntPrint(std::ostream& os) {
  DisallowHeapAllocation no_gc;
  HeapObject::PrintHeader(os, "BigInt");
  int len = length();
  os << "- length: " << len << "\n";
  os << "- sign: " << sign() << "\n";
  if (len > 0) {
    os << "- digits:";
    for (int i = 0; i < len; i++) {
      os << "\n    0x" << std::hex << digit(i);
    }
    os << std::dec << "\n";
  }
}
#endif  // OBJECT_PRINT

}  // namespace internal
}  // namespace v8
