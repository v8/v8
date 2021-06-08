// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bigint/vector-arithmetic.h"

#include "src/bigint/digit-arithmetic.h"

namespace v8 {
namespace bigint {

void AddAt(RWDigits Z, Digits X) {
  X.Normalize();
  if (X.len() == 0) return;
  digit_t carry = 0;
  int i = 0;
  for (; i < X.len(); i++) {
    Z[i] = digit_add3(Z[i], X[i], carry, &carry);
  }
  for (; carry != 0; i++) {
    Z[i] = digit_add2(Z[i], carry, &carry);
  }
}

void SubAt(RWDigits Z, Digits X) {
  X.Normalize();
  digit_t borrow = 0;
  int i = 0;
  for (; i < X.len(); i++) {
    Z[i] = digit_sub2(Z[i], X[i], borrow, &borrow);
  }
  for (; borrow != 0; i++) {
    Z[i] = digit_sub(Z[i], borrow, &borrow);
  }
}

}  // namespace bigint
}  // namespace v8
