// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bigint/bigint-internal.h"
#include "src/bigint/digit-arithmetic.h"
#include "src/bigint/vector-arithmetic.h"

namespace v8 {
namespace bigint {

void BitwiseAnd_PosPos(RWDigits Z, Digits X, Digits Y) {
  int pairs = std::min(X.len(), Y.len());
  DCHECK(Z.len() >= pairs);
  int i = 0;
  for (; i < pairs; i++) Z[i] = X[i] & Y[i];
  for (; i < Z.len(); i++) Z[i] = 0;
}

void BitwiseAnd_NegNeg(RWDigits Z, Digits X, Digits Y) {
  // (-x) & (-y) == ~(x-1) & ~(y-1)
  //             == ~((x-1) | (y-1))
  //             == -(((x-1) | (y-1)) + 1)
  int pairs = std::min(X.len(), Y.len());
  digit_t x_borrow = 1;
  digit_t y_borrow = 1;
  int i = 0;
  for (; i < pairs; i++) {
    Z[i] = digit_sub(X[i], x_borrow, &x_borrow) |
           digit_sub(Y[i], y_borrow, &y_borrow);
  }
  // (At least) one of the next two loops will perform zero iterations:
  for (; i < X.len(); i++) Z[i] = digit_sub(X[i], x_borrow, &x_borrow);
  for (; i < Y.len(); i++) Z[i] = digit_sub(Y[i], y_borrow, &y_borrow);
  DCHECK(x_borrow == 0);  // NOLINT(readability/check)
  DCHECK(y_borrow == 0);  // NOLINT(readability/check)
  for (; i < Z.len(); i++) Z[i] = 0;
  Add(Z, 1);
}

void BitwiseAnd_PosNeg(RWDigits Z, Digits X, Digits Y) {
  // x & (-y) == x & ~(y-1)
  int pairs = std::min(X.len(), Y.len());
  digit_t borrow = 1;
  int i = 0;
  for (; i < pairs; i++) Z[i] = X[i] & ~digit_sub(Y[i], borrow, &borrow);
  for (; i < X.len(); i++) Z[i] = X[i];
  for (; i < Z.len(); i++) Z[i] = 0;
}

void BitwiseOr_PosPos(RWDigits Z, Digits X, Digits Y) {
  int pairs = std::min(X.len(), Y.len());
  int i = 0;
  for (; i < pairs; i++) Z[i] = X[i] | Y[i];
  // (At least) one of the next two loops will perform zero iterations:
  for (; i < X.len(); i++) Z[i] = X[i];
  for (; i < Y.len(); i++) Z[i] = Y[i];
  for (; i < Z.len(); i++) Z[i] = 0;
}

void BitwiseOr_NegNeg(RWDigits Z, Digits X, Digits Y) {
  // (-x) | (-y) == ~(x-1) | ~(y-1)
  //             == ~((x-1) & (y-1))
  //             == -(((x-1) & (y-1)) + 1)
  int pairs = std::min(X.len(), Y.len());
  digit_t x_borrow = 1;
  digit_t y_borrow = 1;
  int i = 0;
  for (; i < pairs; i++) {
    Z[i] = digit_sub(X[i], x_borrow, &x_borrow) &
           digit_sub(Y[i], y_borrow, &y_borrow);
  }
  // Any leftover borrows don't matter, the '&' would drop them anyway.
  for (; i < Z.len(); i++) Z[i] = 0;
  Add(Z, 1);
}

void BitwiseOr_PosNeg(RWDigits Z, Digits X, Digits Y) {
  // x | (-y) == x | ~(y-1) == ~((y-1) &~ x) == -(((y-1) &~ x) + 1)
  int pairs = std::min(X.len(), Y.len());
  digit_t borrow = 1;
  int i = 0;
  for (; i < pairs; i++) Z[i] = digit_sub(Y[i], borrow, &borrow) & ~X[i];
  for (; i < Y.len(); i++) Z[i] = digit_sub(Y[i], borrow, &borrow);
  DCHECK(borrow == 0);  // NOLINT(readability/check)
  for (; i < Z.len(); i++) Z[i] = 0;
  Add(Z, 1);
}

void BitwiseXor_PosPos(RWDigits Z, Digits X, Digits Y) {
  int pairs = X.len();
  if (Y.len() < X.len()) {
    std::swap(X, Y);
    pairs = X.len();
  }
  DCHECK(X.len() <= Y.len());
  int i = 0;
  for (; i < pairs; i++) Z[i] = X[i] ^ Y[i];
  for (; i < Y.len(); i++) Z[i] = Y[i];
  for (; i < Z.len(); i++) Z[i] = 0;
}

void BitwiseXor_NegNeg(RWDigits Z, Digits X, Digits Y) {
  // (-x) ^ (-y) == ~(x-1) ^ ~(y-1) == (x-1) ^ (y-1)
  int pairs = std::min(X.len(), Y.len());
  digit_t x_borrow = 1;
  digit_t y_borrow = 1;
  int i = 0;
  for (; i < pairs; i++) {
    Z[i] = digit_sub(X[i], x_borrow, &x_borrow) ^
           digit_sub(Y[i], y_borrow, &y_borrow);
  }
  // (At least) one of the next two loops will perform zero iterations:
  for (; i < X.len(); i++) Z[i] = digit_sub(X[i], x_borrow, &x_borrow);
  for (; i < Y.len(); i++) Z[i] = digit_sub(Y[i], y_borrow, &y_borrow);
  DCHECK(x_borrow == 0);  // NOLINT(readability/check)
  DCHECK(y_borrow == 0);  // NOLINT(readability/check)
  for (; i < Z.len(); i++) Z[i] = 0;
}

void BitwiseXor_PosNeg(RWDigits Z, Digits X, Digits Y) {
  // x ^ (-y) == x ^ ~(y-1) == ~(x ^ (y-1)) == -((x ^ (y-1)) + 1)
  int pairs = std::min(X.len(), Y.len());
  digit_t borrow = 1;
  int i = 0;
  for (; i < pairs; i++) Z[i] = X[i] ^ digit_sub(Y[i], borrow, &borrow);
  // (At least) one of the next two loops will perform zero iterations:
  for (; i < X.len(); i++) Z[i] = X[i];
  for (; i < Y.len(); i++) Z[i] = digit_sub(Y[i], borrow, &borrow);
  DCHECK(borrow == 0);  // NOLINT(readability/check)
  for (; i < Z.len(); i++) Z[i] = 0;
  Add(Z, 1);
}

}  // namespace bigint
}  // namespace v8
