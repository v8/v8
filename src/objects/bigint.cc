// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Parts of the implementation below:

// Copyright (c) 2014 the Dart project authors.  Please see the AUTHORS file [1]
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file [2].
//
// [1] https://github.com/dart-lang/sdk/blob/master/AUTHORS
// [2] https://github.com/dart-lang/sdk/blob/master/LICENSE

// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file [3].
//
// [3] https://golang.org/LICENSE

#include "src/objects/bigint.h"

#include "src/objects-inl.h"

namespace v8 {
namespace internal {

Handle<BigInt> BigInt::UnaryMinus(Handle<BigInt> x) {
  // Special case: There is no -0n.
  if (x->is_zero()) {
    return x;
  }
  Handle<BigInt> result = BigInt::Copy(x);
  result->set_sign(!x->sign());
  return result;
}

Handle<BigInt> BigInt::BitwiseNot(Handle<BigInt> x) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

MaybeHandle<BigInt> BigInt::Exponentiate(Handle<BigInt> base,
                                         Handle<BigInt> exponent) {
  UNIMPLEMENTED();  // TODO(jkummerow): Implement.
}

Handle<BigInt> BigInt::Multiply(Handle<BigInt> x, Handle<BigInt> y) {
  if (x->is_zero()) return x;
  if (y->is_zero()) return y;
  Handle<BigInt> result =
      x->GetIsolate()->factory()->NewBigInt(x->length() + y->length());
  for (int i = 0; i < x->length(); i++) {
    MultiplyAccumulate(y, x->digit(i), result, i);
  }
  result->set_sign(x->sign() != y->sign());
  result->RightTrim();
  return result;
}

MaybeHandle<BigInt> BigInt::Divide(Handle<BigInt> x, Handle<BigInt> y) {
  // 1. If y is 0n, throw a RangeError exception.
  if (y->is_zero()) {
    THROW_NEW_ERROR(y->GetIsolate(),
                    NewRangeError(MessageTemplate::kBigIntDivZero), BigInt);
  }
  // 2. Let quotient be the mathematical value of x divided by y.
  // 3. Return a BigInt representing quotient rounded towards 0 to the next
  //    integral value.
  if (AbsoluteCompare(x, y) < 0) {
    // TODO(jkummerow): Consider caching a canonical zero-BigInt.
    return x->GetIsolate()->factory()->NewBigInt(0);
  }
  Handle<BigInt> quotient;
  if (y->length() == 1) {
    digit_t remainder;
    AbsoluteDivSmall(x, y->digit(0), &quotient, &remainder);
  } else {
    AbsoluteDivLarge(x, y, &quotient, nullptr);
  }
  quotient->set_sign(x->sign() != y->sign());
  quotient->RightTrim();
  return quotient;
}

MaybeHandle<BigInt> BigInt::Remainder(Handle<BigInt> x, Handle<BigInt> y) {
  // 1. If y is 0n, throw a RangeError exception.
  if (y->is_zero()) {
    THROW_NEW_ERROR(y->GetIsolate(),
                    NewRangeError(MessageTemplate::kBigIntDivZero), BigInt);
  }
  // 2. Return the BigInt representing x modulo y.
  // See https://github.com/tc39/proposal-bigint/issues/84 though.
  if (AbsoluteCompare(x, y) < 0) return x;
  Handle<BigInt> remainder;
  if (y->length() == 1) {
    digit_t remainder_digit;
    AbsoluteDivSmall(x, y->digit(0), nullptr, &remainder_digit);
    if (remainder_digit == 0) {
      return x->GetIsolate()->factory()->NewBigInt(0);
    }
    remainder = x->GetIsolate()->factory()->NewBigIntRaw(1);
    remainder->set_digit(0, remainder_digit);
  } else {
    AbsoluteDivLarge(x, y, nullptr, &remainder);
  }
  remainder->set_sign(x->sign());
  return remainder;
}

Handle<BigInt> BigInt::Add(Handle<BigInt> x, Handle<BigInt> y) {
  bool xsign = x->sign();
  if (xsign == y->sign()) {
    // x + y == x + y
    // -x + -y == -(x + y)
    return AbsoluteAdd(x, y, xsign);
  }
  // x + -y == x - y == -(y - x)
  // -x + y == y - x == -(x - y)
  if (AbsoluteCompare(x, y) >= 0) {
    return AbsoluteSub(x, y, xsign);
  }
  return AbsoluteSub(y, x, !xsign);
}

Handle<BigInt> BigInt::Subtract(Handle<BigInt> x, Handle<BigInt> y) {
  bool xsign = x->sign();
  if (xsign != y->sign()) {
    // x - (-y) == x + y
    // (-x) - y == -(x + y)
    return AbsoluteAdd(x, y, xsign);
  }
  // x - y == -(y - x)
  // (-x) - (-y) == y - x == -(x - y)
  if (AbsoluteCompare(x, y) >= 0) {
    return AbsoluteSub(x, y, xsign);
  }
  return AbsoluteSub(y, x, !xsign);
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

MaybeHandle<String> BigInt::ToString(Handle<BigInt> bigint, int radix) {
  // TODO(jkummerow): Support non-power-of-two radixes.
  if (!base::bits::IsPowerOfTwo(radix)) radix = 16;
  return ToStringBasePowerOfTwo(bigint, radix);
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

Handle<BigInt> BigInt::AbsoluteAdd(Handle<BigInt> x, Handle<BigInt> y,
                                   bool result_sign) {
  if (x->length() < y->length()) return AbsoluteAdd(y, x, result_sign);
  if (x->is_zero()) {
    DCHECK(y->is_zero());
    return x;
  }
  if (y->is_zero()) {
    return result_sign == x->sign() ? x : UnaryMinus(x);
  }
  Handle<BigInt> result =
      x->GetIsolate()->factory()->NewBigIntRaw(x->length() + 1);
  digit_t carry = 0;
  int i = 0;
  for (; i < y->length(); i++) {
    digit_t new_carry = 0;
    digit_t sum = digit_add(x->digit(i), y->digit(i), &new_carry);
    sum = digit_add(sum, carry, &new_carry);
    result->set_digit(i, sum);
    carry = new_carry;
  }
  for (; i < x->length(); i++) {
    digit_t new_carry = 0;
    digit_t sum = digit_add(x->digit(i), carry, &new_carry);
    result->set_digit(i, sum);
    carry = new_carry;
  }
  result->set_digit(i, carry);
  result->set_sign(result_sign);
  result->RightTrim();
  return result;
}

Handle<BigInt> BigInt::AbsoluteSub(Handle<BigInt> x, Handle<BigInt> y,
                                   bool result_sign) {
  DCHECK(x->length() >= y->length());
  SLOW_DCHECK(AbsoluteCompare(x, y) >= 0);
  if (x->is_zero()) {
    DCHECK(y->is_zero());
    return x;
  }
  if (y->is_zero()) {
    return result_sign == x->sign() ? x : UnaryMinus(x);
  }
  Handle<BigInt> result = x->GetIsolate()->factory()->NewBigIntRaw(x->length());
  digit_t borrow = 0;
  int i = 0;
  for (; i < y->length(); i++) {
    digit_t new_borrow = 0;
    digit_t difference = digit_sub(x->digit(i), y->digit(i), &new_borrow);
    difference = digit_sub(difference, borrow, &new_borrow);
    result->set_digit(i, difference);
    borrow = new_borrow;
  }
  for (; i < x->length(); i++) {
    digit_t new_borrow = 0;
    digit_t difference = digit_sub(x->digit(i), borrow, &new_borrow);
    result->set_digit(i, difference);
    borrow = new_borrow;
  }
  DCHECK_EQ(0, borrow);
  result->set_sign(result_sign);
  result->RightTrim();
  return result;
}

int BigInt::AbsoluteCompare(Handle<BigInt> x, Handle<BigInt> y) {
  int diff = x->length() - y->length();
  if (diff != 0) return diff;
  int i = x->length() - 1;
  while (i >= 0 && x->digit(i) == y->digit(i)) i--;
  if (i < 0) return 0;
  return x->digit(i) > y->digit(i) ? 1 : -1;
}

// Multiplies {multiplicand} with {multiplier} and adds the result to
// {accumulator}, starting at {accumulator_index} for the least-significant
// digit.
// Callers must ensure that {accumulator} is big enough to hold the result.
void BigInt::MultiplyAccumulate(Handle<BigInt> multiplicand, digit_t multiplier,
                                Handle<BigInt> accumulator,
                                int accumulator_index) {
  // This is a minimum requirement; the DCHECK in the second loop below
  // will enforce more as needed.
  DCHECK(accumulator->length() > multiplicand->length() + accumulator_index);
  if (multiplier == 0L) return;
  digit_t carry = 0;
  digit_t high = 0;
  for (int i = 0; i < multiplicand->length(); i++, accumulator_index++) {
    digit_t acc = accumulator->digit(accumulator_index);
    digit_t new_carry = 0;
    // Add last round's carryovers.
    acc = digit_add(acc, high, &new_carry);
    acc = digit_add(acc, carry, &new_carry);
    // Compute this round's multiplication.
    digit_t m_digit = multiplicand->digit(i);
    digit_t low = digit_mul(multiplier, m_digit, &high);
    acc = digit_add(acc, low, &new_carry);
    // Store result and prepare for next round.
    accumulator->set_digit(accumulator_index, acc);
    carry = new_carry;
  }
  for (; carry != 0 || high != 0; accumulator_index++) {
    DCHECK(accumulator_index < accumulator->length());
    digit_t acc = accumulator->digit(accumulator_index);
    digit_t new_carry = 0;
    acc = digit_add(acc, high, &new_carry);
    high = 0;
    acc = digit_add(acc, carry, &new_carry);
    accumulator->set_digit(accumulator_index, acc);
    carry = new_carry;
  }
}

// Multiplies {source} with {factor} and adds {summand} to the result.
// {result} and {source} may be the same BigInt for inplace modification.
void BigInt::InternalMultiplyAdd(BigInt* source, digit_t factor,
                                 digit_t summand, int n, BigInt* result) {
  DCHECK(source->length() >= n);
  DCHECK(result->length() >= n);
  digit_t carry = summand;
  digit_t high = 0;
  for (int i = 0; i < n; i++) {
    digit_t current = source->digit(i);
    digit_t new_carry = 0;
    // Compute this round's multiplication.
    digit_t new_high = 0;
    current = digit_mul(current, factor, &new_high);
    // Add last round's carryovers.
    current = digit_add(current, high, &new_carry);
    current = digit_add(current, carry, &new_carry);
    // Store result and prepare for next round.
    result->set_digit(i, current);
    carry = new_carry;
    high = new_high;
  }
  if (result->length() > n) {
    result->set_digit(n++, carry + high);
    // Current callers don't pass in such large results, but let's be robust.
    while (n < result->length()) {
      result->set_digit(n++, 0);
    }
  } else {
    CHECK((carry + high) == 0);
  }
}

// Multiplies {this} with {factor} and adds {summand} to the result.
void BigInt::InplaceMultiplyAdd(uintptr_t factor, uintptr_t summand) {
  STATIC_ASSERT(sizeof(factor) == sizeof(digit_t));
  STATIC_ASSERT(sizeof(summand) == sizeof(digit_t));
  InternalMultiplyAdd(this, factor, summand, length(), this);
}

// Divides {x} by {divisor}, returning the result in {quotient} and {remainder}.
// Mathematically, the contract is:
// quotient = (x - remainder) / divisor, with 0 <= remainder < divisor.
// If {quotient} is an empty handle, an appropriately sized BigInt will be
// allocated for it; otherwise the caller must ensure that it is big enough.
// {quotient} can be the same as {x} for an in-place division. {quotient} can
// also be nullptr if the caller is only interested in the remainder.
void BigInt::AbsoluteDivSmall(Handle<BigInt> x, digit_t divisor,
                              Handle<BigInt>* quotient, digit_t* remainder) {
  DCHECK(divisor != 0);
  DCHECK(!x->is_zero());  // Callers check anyway, no need to handle this.
  *remainder = 0;
  if (divisor == 1) {
    if (quotient != nullptr) *quotient = x;
    return;
  }

  int length = x->length();
  if (quotient != nullptr) {
    if ((*quotient).is_null()) {
      *quotient = x->GetIsolate()->factory()->NewBigIntRaw(length);
    }
    for (int i = length - 1; i >= 0; i--) {
      digit_t q = digit_div(*remainder, x->digit(i), divisor, remainder);
      (*quotient)->set_digit(i, q);
    }
  } else {
    for (int i = length - 1; i >= 0; i--) {
      digit_div(*remainder, x->digit(i), divisor, remainder);
    }
  }
}

// Divides {dividend} by {divisor}, returning the result in {quotient} and
// {remainder}. Mathematically, the contract is:
// quotient = (dividend - remainder) / divisor, with 0 <= remainder < divisor.
// Both {quotient} and {remainder} are optional, for callers that are only
// interested in one of them.
// See Knuth, Volume 2, section 4.3.1, Algorithm D.
void BigInt::AbsoluteDivLarge(Handle<BigInt> dividend, Handle<BigInt> divisor,
                              Handle<BigInt>* quotient,
                              Handle<BigInt>* remainder) {
  DCHECK(divisor->length() >= 2);
  DCHECK(dividend->length() >= divisor->length());
  Factory* factory = dividend->GetIsolate()->factory();
  // The unusual variable names inside this function are consistent with
  // Knuth's book, as well as with Go's implementation of this algorithm.
  // Maintaining this consistency is probably more useful than trying to
  // come up with more descriptive names for them.
  int n = divisor->length();
  int m = dividend->length() - n;

  // The quotient to be computed.
  Handle<BigInt> q;
  if (quotient != nullptr) q = factory->NewBigIntRaw(m + 1);
  // In each iteration, {qhatv} holds {divisor} * {current quotient digit}.
  // "v" is the book's name for {divisor}, "qhat" the current quotient digit.
  Handle<BigInt> qhatv = factory->NewBigIntRaw(n + 1);

  // D1.
  // Left-shift inputs so that the divisor's MSB is set. This is necessary
  // to prevent the digit-wise divisions (see digit_div call below) from
  // overflowing (they take a two digits wide input, and return a one digit
  // result).
  int shift = base::bits::CountLeadingZeros(divisor->digit(n - 1));
  if (shift > 0) {
    divisor = SpecialLeftShift(divisor, shift, kSameSizeResult);
  }
  // Holds the (continuously updated) remaining part of the dividend, which
  // eventually becomes the remainder.
  Handle<BigInt> u = SpecialLeftShift(dividend, shift, kAlwaysAddOneDigit);

  // D2.
  // Iterate over the dividend's digit (like the "grad school" algorithm).
  // {vn1} is the divisor's most significant digit.
  digit_t vn1 = divisor->digit(n - 1);
  for (int j = m; j >= 0; j--) {
    // D3.
    // Estimate the current iteration's quotient digit (see Knuth for details).
    // {qhat} is the current quotient digit.
    digit_t qhat = std::numeric_limits<digit_t>::max();
    // {ujn} is the dividend's most significant remaining digit.
    digit_t ujn = u->digit(j + n);
    if (ujn != vn1) {
      // {rhat} is the current iteration's remainder.
      digit_t rhat = 0;
      // Estimate the current quotient digit by dividing the most significant
      // digits of dividend and divisor. The result will not be too small,
      // but could be a bit too large.
      qhat = digit_div(ujn, u->digit(j + n - 1), vn1, &rhat);

      // Decrement the quotient estimate as needed by looking at the next
      // digit, i.e. by testing whether
      // qhat * v_{n-2} > (rhat << kDigitBits) + u_{j+n-2}.
      digit_t vn2 = divisor->digit(n - 2);
      digit_t ujn2 = u->digit(j + n - 2);
      while (ProductGreaterThan(qhat, vn2, rhat, ujn2)) {
        qhat--;
        digit_t prev_rhat = rhat;
        rhat += vn1;
        // v[n-1] >= 0, so this tests for overflow.
        if (rhat < prev_rhat) break;
      }
    }

    // D4.
    // Multiply the divisor with the current quotient digit, and subtract
    // it from the dividend. If there was "borrow", then the quotient digit
    // was one too high, so we must correct it and undo one subtraction of
    // the (shifted) divisor.
    InternalMultiplyAdd(*divisor, qhat, 0, n, *qhatv);
    digit_t c = u->InplaceSub(*qhatv, j);
    if (c != 0) {
      c = u->InplaceAdd(*divisor, j);
      u->set_digit(j + n, u->digit(j + n) + c);
      qhat--;
    }

    if (quotient != nullptr) q->set_digit(j, qhat);
  }
  if (quotient != nullptr) {
    *quotient = q;  // Caller will right-trim.
  }
  if (remainder != nullptr) {
    u->InplaceRightShift(shift);
    *remainder = u;
  }
}

// Returns whether (factor1 * factor2) > (high << kDigitBits) + low.
bool BigInt::ProductGreaterThan(digit_t factor1, digit_t factor2, digit_t high,
                                digit_t low) {
  digit_t result_high;
  digit_t result_low = digit_mul(factor1, factor2, &result_high);
  return result_high > high || (result_high == high && result_low > low);
}

// Adds {summand} onto {this}, starting with {summand}'s 0th digit
// at {this}'s {start_index}'th digit. Returns the "carry" (0 or 1).
BigInt::digit_t BigInt::InplaceAdd(BigInt* summand, int start_index) {
  digit_t carry = 0;
  int n = summand->length();
  DCHECK(length() >= start_index + n);
  for (int i = 0; i < n; i++) {
    digit_t new_carry = 0;
    digit_t sum =
        digit_add(digit(start_index + i), summand->digit(i), &new_carry);
    sum = digit_add(sum, carry, &new_carry);
    set_digit(start_index + i, sum);
    carry = new_carry;
  }
  return carry;
}

// Subtracts {subtrahend} from {this}, starting with {subtrahend}'s 0th digit
// at {this}'s {start_index}-th digit. Returns the "borrow" (0 or 1).
BigInt::digit_t BigInt::InplaceSub(BigInt* subtrahend, int start_index) {
  digit_t borrow = 0;
  int n = subtrahend->length();
  DCHECK(length() >= start_index + n);
  for (int i = 0; i < n; i++) {
    digit_t new_borrow = 0;
    digit_t difference =
        digit_sub(digit(start_index + i), subtrahend->digit(i), &new_borrow);
    difference = digit_sub(difference, borrow, &new_borrow);
    set_digit(start_index + i, difference);
    borrow = new_borrow;
  }
  return borrow;
}

void BigInt::InplaceRightShift(int shift) {
  DCHECK(shift >= 0);
  DCHECK(shift < kDigitBits);
  DCHECK(length() > 0);
  DCHECK((digit(0) & ((1 << shift) - 1)) == 0);
  if (shift == 0) return;
  digit_t carry = digit(0) >> shift;
  int last = length() - 1;
  for (int i = 0; i < last; i++) {
    digit_t d = digit(i + 1);
    set_digit(i, (d << (kDigitBits - shift)) | carry);
    carry = d >> shift;
  }
  set_digit(last, carry);
  RightTrim();
}

// Always copies the input, even when {shift} == 0.
// {shift} must be less than kDigitBits, {x} must be non-zero.
Handle<BigInt> BigInt::SpecialLeftShift(Handle<BigInt> x, int shift,
                                        SpecialLeftShiftMode mode) {
  DCHECK(shift >= 0);
  DCHECK(shift < kDigitBits);
  DCHECK(x->length() > 0);
  int n = x->length();
  int result_length = mode == kAlwaysAddOneDigit ? n + 1 : n;
  Handle<BigInt> result =
      x->GetIsolate()->factory()->NewBigIntRaw(result_length);
  digit_t carry = 0;
  for (int i = 0; i < n; i++) {
    digit_t d = x->digit(i);
    result->set_digit(i, (d << shift) | carry);
    carry = d >> (kDigitBits - shift);
  }
  if (mode == kAlwaysAddOneDigit) {
    result->set_digit(n, carry);
  } else {
    DCHECK(mode == kSameSizeResult);
    DCHECK(carry == 0);
  }
  return result;
}

Handle<BigInt> BigInt::Copy(Handle<BigInt> source) {
  int length = source->length();
  Handle<BigInt> result = source->GetIsolate()->factory()->NewBigIntRaw(length);
  memcpy(result->address() + HeapObject::kHeaderSize,
         source->address() + HeapObject::kHeaderSize,
         SizeFor(length) - HeapObject::kHeaderSize);
  return result;
}

// Lookup table for the maximum number of bits required per character of a
// base-N string representation of a number. To increase accuracy, the array
// value is the actual value multiplied by 32. To generate this table:
// for (var i = 0; i <= 36; i++) { print(Math.ceil(Math.log2(i) * 32) + ","); }
uint8_t kMaxBitsPerChar[] = {
    0,   0,   32,  51,  64,  75,  83,  90,  96,  // 0..8
    102, 107, 111, 115, 119, 122, 126, 128,      // 9..16
    131, 134, 136, 139, 141, 143, 145, 147,      // 17..24
    149, 151, 153, 154, 156, 158, 159, 160,      // 25..32
    162, 163, 165, 166,                          // 33..36
};

static const int kBitsPerCharTableShift = 5;
static const size_t kBitsPerCharTableMultiplier = 1u << kBitsPerCharTableShift;

MaybeHandle<BigInt> BigInt::AllocateFor(Isolate* isolate, int radix,
                                        int charcount) {
  DCHECK(2 <= radix && radix <= 36);
  DCHECK(charcount >= 0);
  size_t bits_min;
  size_t bits_per_char = kMaxBitsPerChar[radix];
  size_t chars = static_cast<size_t>(charcount);
  const int roundup = kBitsPerCharTableMultiplier - 1;
  if (chars <= 1000000) {
    // More precise path: multiply first, then divide.
    bits_min = bits_per_char * chars;
    // Divide by 32 (see table), rounding up.
    bits_min = (bits_min + roundup) >> kBitsPerCharTableShift;
  } else {
    // Overflow avoidance path: divide first, then multiply.
    // The addition can't overflow because of the int -> size_t cast.
    bits_min = ((chars + roundup) >> kBitsPerCharTableShift) * bits_per_char;
    // Check if overflow happened.
    if (bits_min < chars) {
      THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kBigIntTooBig),
                      BigInt);
    }
  }
  if (bits_min > static_cast<size_t>(kMaxInt)) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kBigIntTooBig),
                    BigInt);
  }
  // Divide by kDigitsBits, rounding up.
  int length = (static_cast<int>(bits_min) + kDigitBits - 1) / kDigitBits;
  if (length > BigInt::kMaxLength) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kBigIntTooBig),
                    BigInt);
  }
  return isolate->factory()->NewBigInt(length);
}

void BigInt::RightTrim() {
  int old_length = length();
  int new_length = old_length;
  while (new_length > 0 && digit(new_length - 1) == 0) new_length--;
  int to_trim = old_length - new_length;
  if (to_trim == 0) return;
  int size_delta = to_trim * kDigitSize;
  Address new_end = this->address() + SizeFor(new_length);
  Heap* heap = GetHeap();
  heap->CreateFillerObjectAt(new_end, size_delta, ClearRecordedSlots::kNo);
  // Canonicalize -0n.
  if (new_length == 0) set_sign(false);
  set_length(new_length);
}

static const char kConversionChars[] = "0123456789abcdefghijklmnopqrstuvwxyz";

// TODO(jkummerow): Add more tests for this when we have a way to construct
// multi-digit BigInts.
MaybeHandle<String> BigInt::ToStringBasePowerOfTwo(Handle<BigInt> x,
                                                   int radix) {
  STATIC_ASSERT(base::bits::IsPowerOfTwo(kDigitBits));
  DCHECK(base::bits::IsPowerOfTwo(radix));
  DCHECK(radix >= 2 && radix <= 32);
  Isolate* isolate = x->GetIsolate();
  // TODO(jkummerow): check in caller?
  if (x->is_zero()) return isolate->factory()->NewStringFromStaticChars("0");

  const int length = x->length();
  const bool sign = x->sign();
  const int bits_per_char = base::bits::CountTrailingZeros32(radix);
  const int char_mask = radix - 1;
  // Compute the length of the resulting string: divide the bit length of the
  // BigInt by the number of bits representable per character (rounding up).
  const digit_t msd = x->digit(length - 1);
  const int msd_leading_zeros = base::bits::CountLeadingZeros(msd);
  const size_t bit_length = length * kDigitBits - msd_leading_zeros;
  const size_t chars_required =
      (bit_length + bits_per_char - 1) / bits_per_char + sign;

  if (chars_required > String::kMaxLength) {
    THROW_NEW_ERROR(isolate, NewInvalidStringLengthError(), String);
  }

  Handle<SeqOneByteString> result =
      isolate->factory()
          ->NewRawOneByteString(static_cast<int>(chars_required))
          .ToHandleChecked();
  uint8_t* buffer = result->GetChars();
  // Print the number into the string, starting from the last position.
  int pos = static_cast<int>(chars_required - 1);
  digit_t digit = 0;
  // Keeps track of how many unprocessed bits there are in {digit}.
  int available_bits = 0;
  for (int i = 0; i < length - 1; i++) {
    digit_t new_digit = x->digit(i);
    // Take any leftover bits from the last iteration into account.
    int current = (digit | (new_digit << available_bits)) & char_mask;
    buffer[pos--] = kConversionChars[current];
    int consumed_bits = bits_per_char - available_bits;
    digit = new_digit >> consumed_bits;
    available_bits = kDigitBits - consumed_bits;
    while (available_bits >= bits_per_char) {
      buffer[pos--] = kConversionChars[digit & char_mask];
      digit >>= bits_per_char;
      available_bits -= bits_per_char;
    }
  }
  // Take any leftover bits from the last iteration into account.
  int current = (digit | (msd << available_bits)) & char_mask;
  buffer[pos--] = kConversionChars[current];
  digit = msd >> (bits_per_char - available_bits);
  while (digit != 0) {
    buffer[pos--] = kConversionChars[digit & char_mask];
    digit >>= bits_per_char;
  }
  if (sign) buffer[pos--] = '-';
  DCHECK(pos == -1);
  return result;
}

// Digit arithmetic helpers.

#if V8_TARGET_ARCH_32_BIT
#define HAVE_TWODIGIT_T 1
typedef uint64_t twodigit_t;
#elif defined(__SIZEOF_INT128__)
// Both Clang and GCC support this on x64.
#define HAVE_TWODIGIT_T 1
typedef __uint128_t twodigit_t;
#endif

// {carry} must point to an initialized digit_t and will either be incremented
// by one or left alone.
inline BigInt::digit_t BigInt::digit_add(digit_t a, digit_t b, digit_t* carry) {
#if HAVE_TWODIGIT_T
  twodigit_t result = static_cast<twodigit_t>(a) + static_cast<twodigit_t>(b);
  *carry += result >> kDigitBits;
  return static_cast<digit_t>(result);
#else
  digit_t result = a + b;
  if (result < a) *carry += 1;
  return result;
#endif
}

// {borrow} must point to an initialized digit_t and will either be incremented
// by one or left alone.
inline BigInt::digit_t BigInt::digit_sub(digit_t a, digit_t b,
                                         digit_t* borrow) {
#if HAVE_TWODIGIT_T
  twodigit_t result = static_cast<twodigit_t>(a) - static_cast<twodigit_t>(b);
  *borrow += (result >> kDigitBits) & 1;
  return static_cast<digit_t>(result);
#else
  digit_t result = a - b;
  if (result > a) *borrow += 1;
  return static_cast<digit_t>(result);
#endif
}

// Returns the low half of the result. High half is in {high}.
inline BigInt::digit_t BigInt::digit_mul(digit_t a, digit_t b, digit_t* high) {
#if HAVE_TWODIGIT_T
  twodigit_t result = static_cast<twodigit_t>(a) * static_cast<twodigit_t>(b);
  *high = result >> kDigitBits;
  return static_cast<digit_t>(result);
#else
  // Multiply in half-pointer-sized chunks.
  // For inputs [AH AL]*[BH BL], the result is:
  //
  //            [AL*BL]  // r_low
  //    +    [AL*BH]     // r_mid1
  //    +    [AH*BL]     // r_mid2
  //    + [AH*BH]        // r_high
  //    = [R4 R3 R2 R1]  // high = [R4 R3], low = [R2 R1]
  //
  // Where of course we must be careful with carries between the columns.
  digit_t a_low = a & kHalfDigitMask;
  digit_t a_high = a >> kHalfDigitBits;
  digit_t b_low = b & kHalfDigitMask;
  digit_t b_high = b >> kHalfDigitBits;

  digit_t r_low = a_low * b_low;
  digit_t r_mid1 = a_low * b_high;
  digit_t r_mid2 = a_high * b_low;
  digit_t r_high = a_high * b_high;

  digit_t carry = 0;
  digit_t low = digit_add(r_low, r_mid1 << kHalfDigitBits, &carry);
  low = digit_add(low, r_mid2 << kHalfDigitBits, &carry);
  *high =
      (r_mid1 >> kHalfDigitBits) + (r_mid2 >> kHalfDigitBits) + r_high + carry;
  return low;
#endif
}

// Returns the quotient.
// quotient = (high << kDigitBits + low - remainder) / divisor
BigInt::digit_t BigInt::digit_div(digit_t high, digit_t low, digit_t divisor,
                                  digit_t* remainder) {
  DCHECK(high < divisor);
// Clang on Windows defines __SIZEOF_INT128__, but does not support division
// of __uint128_t variables. See https://bugs.llvm.org/show_bug.cgi?id=25305.
#if HAVE_TWODIGIT_T && !(defined(_MSC_VER) && defined(__clang__))
  twodigit_t dividend = (static_cast<twodigit_t>(high) << kDigitBits) |
                        static_cast<twodigit_t>(low);
  digit_t result = dividend / divisor;
  *remainder = dividend % divisor;
  return result;
#else
  static const digit_t kHalfDigitBase = 1ull << kHalfDigitBits;
  // Adapted from Warren, Hacker's Delight, p. 152.
  int s = base::bits::CountLeadingZeros(divisor);
  divisor <<= s;

  digit_t vn1 = divisor >> kHalfDigitBits;
  digit_t vn0 = divisor & kHalfDigitMask;
  digit_t un32 = (high << s) | (low >> (kDigitBits - s));
  digit_t un10 = low << s;
  digit_t un1 = un10 >> kHalfDigitBits;
  digit_t un0 = un10 & kHalfDigitMask;
  digit_t q1 = un32 / vn1;
  digit_t rhat = un32 - q1 * vn1;

  while (q1 >= kHalfDigitBase || q1 * vn0 > rhat * kHalfDigitBase + un1) {
    q1--;
    rhat += vn1;
    if (rhat >= kHalfDigitBase) break;
  }

  digit_t un21 = un32 * kHalfDigitBase + un1 - q1 * divisor;
  digit_t q0 = un21 / vn1;
  rhat = un21 - q0 * vn1;

  while (q0 >= kHalfDigitBase || q0 * vn0 > rhat * kHalfDigitBase + un0) {
    q0--;
    rhat += vn1;
    if (rhat >= kHalfDigitBase) break;
  }

  *remainder = (un21 * kHalfDigitBase + un0 - q0 * divisor) >> s;
  return q1 * kHalfDigitBase + q0;
#endif
}

#undef HAVE_TWODIGIT_T

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
