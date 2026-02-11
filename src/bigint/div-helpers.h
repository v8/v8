// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BIGINT_DIV_HELPERS_H_
#define V8_BIGINT_DIV_HELPERS_H_

#include <memory>

#include "src/bigint/bigint-internal.h"
#include "src/bigint/bigint.h"
#include "src/bigint/util.h"

namespace v8 {
namespace bigint {

namespace {

void Copy(RWDigits Z, Digits X) {
  if (Z == X) return;
  uint32_t i = 0;
  for (; i < X.len(); i++) Z[i] = X[i];
  for (; i < Z.len(); i++) Z[i] = 0;
}

}  // namespace

// Z := X << shift
// Z and X may alias for an in-place shift.
inline void LeftShift(RWDigits Z, Digits X, int shift) {
  DCHECK(shift >= 0);
  DCHECK(shift < kDigitBits);
  DCHECK(Z.len() >= X.len());
  if (shift == 0) return Copy(Z, X);
  digit_t carry = 0;
  uint32_t i = 0;
  for (; i < X.len(); i++) {
    digit_t d = X[i];
    Z[i] = (d << shift) | carry;
    carry = d >> (kDigitBits - shift);
  }
  if (i < Z.len()) {
    Z[i++] = carry;
  } else {
    DCHECK(carry == 0);
  }
  for (; i < Z.len(); i++) Z[i] = 0;
}

// Z := X >> shift
// Z and X may alias for an in-place shift.
inline void RightShift(RWDigits Z, Digits X, int shift) {
  DCHECK(shift >= 0);
  DCHECK(shift < kDigitBits);
  X.Normalize();
  DCHECK(Z.len() >= X.len());
  if (shift == 0) return Copy(Z, X);
  uint32_t i = 0;
  if (X.len() > 0) {
    digit_t carry = X[0] >> shift;
    uint32_t last = X.len() - 1;
    for (; i < last; i++) {
      digit_t d = X[i + 1];
      Z[i] = (d << (kDigitBits - shift)) | carry;
      carry = d >> shift;
    }
    Z[i++] = carry;
  }
  for (; i < Z.len(); i++) Z[i] = 0;
}

inline void PutAt(RWDigits Z, Digits A, uint32_t count) {
  uint32_t len = std::min(A.len(), count);
  uint32_t i = 0;
  for (; i < len; i++) Z[i] = A[i];
  for (; i < count; i++) Z[i] = 0;
}

// Division algorithms typically need to left-shift their inputs into
// "bit-normalized" form (i.e. top bit is set). The inputs are considered
// read-only, and V8 relies on that by allowing concurrent reads from them,
// so by default, {ShiftedDigits} allocate temporary storage for their
// contents. In-place modification is opt-in for cases where callers can
// guarantee that it is safe.
// When callers allow in-place shifting and wish to undo it, they have to do
// so manually using {Reset()}.
// If {shift} is not given, it is auto-detected from {original}'s
// leading zeros.
class ShiftedDigits : public Digits {
 public:
  explicit ShiftedDigits(Digits& original, int shift = -1,
                         bool allow_inplace = false)
      : Digits(original.digits_, original.len_) {
    int leading_zeros = CountLeadingZeros(original.msd());
    if (shift < 0) {
      shift = leading_zeros;
    } else if (shift > leading_zeros) {
      allow_inplace = false;
      len_++;
    }
    shift_ = shift;
    if (shift == 0) {
      inplace_ = true;
      return;
    }
    inplace_ = allow_inplace;
    if (!inplace_) {
      digit_t* digits = new digit_t[len_];
      storage_.reset(digits);
      digits_ = digits;
    }
    RWDigits rw_view(digits_, len_);
    LeftShift(rw_view, original, shift_);
  }

  // For callers that have available scratch memory.
  ShiftedDigits(Digits& original, RWDigits scratch)
      : Digits(original.digits_, original.len_) {
    DCHECK(scratch.len() >= original.len());
    shift_ = CountLeadingZeros(original.msd());
    if (shift_ == 0) {
      inplace_ = true;
      return;
    }
    digits_ = scratch.digits_;
    RWDigits rw_view(digits_, len_);
    LeftShift(rw_view, original, shift_);
  }

  ~ShiftedDigits() = default;

  void Reset() {
    if (inplace_ && shift_) {
      RWDigits rw_view(digits_, len_);
      RightShift(rw_view, rw_view, shift_);
    }
  }

  int shift() { return shift_; }

 private:
  int shift_;
  bool inplace_;
  std::unique_ptr<digit_t[]> storage_;
};

}  // namespace bigint
}  // namespace v8

#endif  // V8_BIGINT_DIV_HELPERS_H_
