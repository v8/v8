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
  // TODO(jkummerow): Support non-power-of-two radixes.
  if (!base::bits::IsPowerOfTwo(radix)) radix = 16;
  return bigint->ToStringBasePowerOfTwo(bigint, radix);
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

// Private helpers for public methods.

static const char kConversionChars[] = "0123456789abcdefghijklmnopqrstuvwxyz";

// TODO(jkummerow): Add more tests for this when it is exposed on
// BigInt.prototype.
Handle<String> BigInt::ToStringBasePowerOfTwo(Handle<BigInt> x, int radix) {
  STATIC_ASSERT(base::bits::IsPowerOfTwo(kDigitBits));
  DCHECK(base::bits::IsPowerOfTwo(radix));
  DCHECK(radix >= 2 && radix <= 32);
  Factory* factory = x->GetIsolate()->factory();
  // TODO(jkummerow): check in caller?
  if (is_zero()) return factory->NewStringFromStaticChars("0");

  const int len = x->length();
  const bool sign = x->sign();
  const int bits_per_char = base::bits::CountTrailingZeros32(radix);
  const int char_mask = radix - 1;
  const int chars_per_digit = kDigitBits / bits_per_char;
  // Compute the number of chars needed to represent the most significant
  // bigint digit.
  int chars_for_msd = 0;
  for (digit_t msd = x->digit(len - 1); msd != 0; msd >>= bits_per_char) {
    chars_for_msd++;
  }
  // All other digits need chars_per_digit characters; a leading "-" needs one.
  const int chars = chars_for_msd + (len - 1) * chars_per_digit + sign;

  Handle<SeqOneByteString> result =
      factory->NewRawOneByteString(chars).ToHandleChecked();
  uint8_t* buffer = result->GetChars();
  // Print the number into the string, starting from the last position.
  int pos = chars - 1;
  for (int i = 0; i < len - 1; i++) {
    digit_t digit = x->digit(i);
    for (int j = 0; j < chars_per_digit; j++) {
      buffer[pos--] = kConversionChars[digit & char_mask];
      digit >>= bits_per_char;
    }
  }
  // Print the most significant digit.
  for (digit_t msd = x->digit(len - 1); msd != 0; msd >>= bits_per_char) {
    buffer[pos--] = kConversionChars[msd & char_mask];
  }
  if (sign) buffer[pos--] = '-';
  DCHECK(pos == -1);
  return result;
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
