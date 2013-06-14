// Copyright 2013 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdlib.h>

#include "v8.h"

#include "hydrogen-instructions.h"
#include "cctest.h"

using namespace v8::internal;

static int32_t GetLo(const BitRange& range) {
  int32_t lo = kMaxInt, hi = kMinInt;
  range.ExtendRange(&lo, &hi);
  return lo;
}


static int32_t GetHi(const BitRange& range) {
  int32_t lo = kMaxInt, hi = kMinInt;
  range.ExtendRange(&lo, &hi);
  return hi;
}


static void CheckOp(int32_t a_lo, int32_t a_hi,
                    int32_t b_lo, int32_t b_hi,
                    BitRange op(BitRange, BitRange),
                    int32_t expected_lo, int32_t expected_hi) {
  BitRange a_range;
  BitRange b_range;
  BitRange::SetFromRange(&a_range, a_lo, a_hi);
  BitRange::SetFromRange(&b_range, b_lo, b_hi);
  BitRange result = op(a_range, b_range);
  CHECK_EQ(expected_lo, GetLo(result));
  CHECK_EQ(expected_hi, GetHi(result));
}


TEST(BitRangeConstants) {
  // Converting a constant to BitRange and back is lossless.
  for (int32_t i = -100; i <= 100; i++) {
    BitRange r;
    BitRange::SetFromRange(&r, i, i);
    int32_t lo = kMaxInt, hi = kMinInt;
    r.ExtendRange(&lo, &hi);
    CHECK_EQ(i, lo);
    CHECK_EQ(i, hi);
  }
}


TEST(BitRangeConstantOps) {
  for (int32_t a = -16; a <= 15; a++) {
    for (int32_t b = -16; b <= 15; b++) {
      CheckOp(a, a, b, b, &BitRange::And, a & b, a & b);
      CheckOp(a, a, b, b, &BitRange::Or, a | b, a | b);
      CheckOp(a, a, b, b, &BitRange::Xor, a ^ b, a ^ b);
    }
  }
}


static void CheckConvert(int32_t lo, int32_t hi,
                         int32_t expected_lo, int32_t expected_hi) {
  BitRange range;
  BitRange::SetFromRange(&range, lo, hi);
  CHECK_EQ(expected_lo, GetLo(range));
  CHECK_EQ(expected_hi, GetHi(range));
}


TEST(BitRangeConversion) {
  // [0, 4] -->  000xxx
  CheckConvert(0, 4, 0, 7);
  CheckConvert(0, 5, 0, 7);
  CheckConvert(0, 6, 0, 7);
  CheckConvert(0, 7, 0, 7);

  CheckConvert(1, 4, 0, 7);
  CheckConvert(1, 5, 0, 7);
  CheckConvert(1, 6, 0, 7);
  CheckConvert(1, 7, 0, 7);
}


TEST(BitRangeConservativeApproximation) {
  // Exhaustive test of 5-bit integers.
  // The BitRange operation must always include all real possible values.
  const int32_t kMin = -16;
  const int32_t kMax = 15;

  int count = 0;
  int and_precise_count = 0;
  int or_precise_count = 0;
  int xor_precise_count = 0;

  for (int32_t a_lo = kMin; a_lo <= kMax; a_lo++) {
    for (int32_t a_hi = a_lo; a_hi <= kMax; a_hi++) {
      for (int32_t b_lo = kMin; b_lo <= kMax; b_lo++) {
        for (int32_t b_hi = a_lo; b_hi <= kMax; b_hi++) {
          // Compute precise ranges.
          int32_t and_lo = kMaxInt, and_hi = kMinInt;
          int32_t or_lo = kMaxInt, or_hi = kMinInt;
          int32_t xor_lo = kMaxInt, xor_hi = kMinInt;

          for (int32_t a = a_lo; a <= a_hi; a++) {
            for (int32_t b = b_lo; b <= b_hi; b++) {
              int32_t a_and_b = a & b;
              and_lo = Min(and_lo, a_and_b);
              and_hi = Max(and_hi, a_and_b);
              int32_t a_or_b = a | b;
              or_lo = Min(or_lo, a_or_b);
              or_hi = Max(or_hi, a_or_b);
              int32_t a_xor_b = a ^ b;
              xor_lo = Min(xor_lo, a_xor_b);
              xor_hi = Max(xor_hi, a_xor_b);
            }
          }

          BitRange a_range;
          BitRange::SetFromRange(&a_range, a_lo, a_hi);
          BitRange b_range;
          BitRange::SetFromRange(&b_range, b_lo, b_hi);

          ++count;
          // Precise range must always be included in approximate result.
          BitRange and_range = BitRange::And(a_range, b_range);
          CHECK(GetLo(and_range) <= and_lo);
          CHECK(GetHi(and_range) >= and_hi);
          if (GetLo(and_range) == and_lo && GetHi(and_range) == and_hi) {
            ++and_precise_count;
          }

          BitRange or_range = BitRange::Or(a_range, b_range);
          CHECK(GetLo(or_range) <= or_lo);
          CHECK(GetHi(or_range) >= or_hi);
          if (GetLo(or_range) == or_lo && GetHi(or_range) == or_hi) {
            ++or_precise_count;
          }

          BitRange xor_range = BitRange::Xor(a_range, b_range);
          CHECK(GetLo(xor_range) <= xor_lo);
          CHECK(GetHi(xor_range) >= xor_hi);
          if (GetLo(xor_range) == xor_lo && GetHi(xor_range) == xor_hi) {
            ++xor_precise_count;
          }
        }
      }
    }
  }

  CHECK_EQ(366080, count);
  CHECK_EQ(35668, and_precise_count);
  CHECK_EQ(35668, or_precise_count);
  CHECK_EQ(37480, xor_precise_count);
}


TEST(BitRangeMultiRange) {
  // Multiple ranges can be unioned with multiple calls to ExtendRange.
  //
  // HBitWise::InferRange is a 1x1 decomposition.  Each input range is
  // 'decomposed' into 1 BitRange.  It is possible to do a more precise
  // decompostion into several BitRanges.  2 BitRanges might be the sweet-spot
  // since it prevents change-of-sign polluting the result.
  //
  // E.g.  [-2,3] = {xxxxxxxx} as one BitRange, but is {1111111x, 000000xx} as
  // two.
  //
  //   [-2,3] ^ [-1,5] = {xxxxxxxx} ^ {xxxxxxxx} = xxxxxxxx
  //
  // With a 2x2 decomposition, there are 4 intermediate results.
  //
  //   [-2,3] ^ [-1,5] = {1111111x, 000000xx} ^ {11111111, 00000xxx}
  //     result11 = 1111111x ^ 11111111 = 0000000x
  //     result12 = 1111111x ^ 00000xxx = 11111xxx
  //     result21 = 000000xx ^ 11111111 = 111111xx
  //     result22 = 000000xx ^ 00000xxx = 00000xxx
  //
  // These can be accumulated into a range as follows:
  //
  //     result11.ExtendRange(&lower, &upper);  // 0, 1
  //     result12.ExtendRange(&lower, &upper);  // -8, 1
  //     result21.ExtendRange(&lower, &upper);  // -8, 1
  //     result22.ExtendRange(&lower, &upper);  // -8, 7
  //   = [-8,7]

  {
    BitRange r1(~0x000C, 0x0022);   // 0010xx10
    BitRange r2(~0x0003, 0x0004);   // 0000x1xx
    int32_t lo = kMaxInt, hi = kMinInt;
    r1.ExtendRange(&lo, &hi);
    CHECK_EQ(0x22, lo);
    CHECK_EQ(0x2E, hi);

    r2.ExtendRange(&lo, &hi);
    CHECK_EQ(0x04, lo);
    CHECK_EQ(0x2E, hi);
  }

  {
    BitRange r1(~0, -1);   // 11111111
    BitRange r2(~1, 0);    // 0000000x
    int32_t lo = kMaxInt, hi = kMinInt;
    r1.ExtendRange(&lo, &hi);
    CHECK_EQ(-1, lo);
    CHECK_EQ(-1, hi);

    r2.ExtendRange(&lo, &hi);
    CHECK_EQ(-1, lo);
    CHECK_EQ(1, hi);
  }
}


TEST(BitRangeOps) {
  // xxxx & 000x => 000x
  CheckOp(kMinInt, kMaxInt,  0, 1,  &BitRange::And,  0, 1);

  CheckOp(3, 7,  0, 0,  &BitRange::Or,  0, 7);
  CheckOp(4, 5,  0, 0,  &BitRange::Or,  4, 5);
  CheckOp(3, 7,  4, 4,  &BitRange::Or,  4, 7);
  CheckOp(0, 99,  4, 4, &BitRange::Or,  4, 127);

  // 01xx ^ 0100 -> 00xx
  CheckOp(4, 7,  4, 4,  &BitRange::Xor,  0, 3);
  // 00xx ^ 0100 -> 01xx
  CheckOp(0, 3,  4, 4,  &BitRange::Xor,  4, 7);
}
