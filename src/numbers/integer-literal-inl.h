// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_NUMBERS_INTEGER_LITERAL_INL_H_
#define V8_NUMBERS_INTEGER_LITERAL_INL_H_

#include "src/numbers/integer-literal.h"

namespace v8 {
namespace internal {

inline std::string IntegerLiteral::ToString() const {
  // Special case 0 here.
  if (IsZero()) return "0";

  int len = bigint::ToStringResultLength(GetDigits(), 10, sign());
  std::vector<char> buffer(len);
  bigint::Processor* processor = bigint::Processor::New(new bigint::Platform());
  processor->ToString(buffer.data(), &len, GetDigits(), 10, sign());
  processor->Destroy();
  return std::string(buffer.begin(), buffer.begin() + len);
}

inline IntegerLiteral operator<<(const IntegerLiteral& lhs,
                                 const IntegerLiteral& rhs) {
  if (lhs.IsZero() || rhs.IsZero()) return lhs;
  CHECK_EQ(rhs.length(), 1);
  // We don't support negative left shift here.
  CHECK(!rhs.sign());
  const int result_length = bigint::LeftShift_ResultLength(
      lhs.length(), lhs.GetDigits().msd(), rhs.GetDigits()[0]);
  DCHECK_LE(result_length, IntegerLiteral::kMaxLength);
  auto result = IntegerLiteral::ForLength(result_length, lhs.sign());
  bigint::LeftShift(result.GetRWDigits(), lhs.GetDigits(), rhs.GetDigits()[0]);
  return result;
}

inline IntegerLiteral operator+(const IntegerLiteral& lhs,
                                const IntegerLiteral& rhs) {
  const int result_length = bigint::AddSignedResultLength(
      lhs.length(), rhs.length(), lhs.sign() == rhs.sign());
  auto result = IntegerLiteral::ForLength(result_length);
  bool result_sign = bigint::AddSigned(result.GetRWDigits(), lhs.GetDigits(),
                                       lhs.sign(), rhs.GetDigits(), rhs.sign());
  result.set_sign(result_sign);
  result.Normalize();
  return result;
}

inline IntegerLiteral operator|(const IntegerLiteral& lhs,
                                const IntegerLiteral& rhs) {
  int result_length = bigint::BitwiseOrResultLength(lhs.length(), rhs.length());
  auto result =
      IntegerLiteral::ForLength(result_length, lhs.sign() || rhs.sign());
  if (lhs.sign()) {
    if (rhs.sign()) {
      bigint::BitwiseOr_NegNeg(result.GetRWDigits(), lhs.GetDigits(),
                               rhs.GetDigits());
    } else {
      bigint::BitwiseOr_PosNeg(result.GetRWDigits(), rhs.GetDigits(),
                               lhs.GetDigits());
    }
  } else {
    if (rhs.sign()) {
      bigint::BitwiseOr_PosNeg(result.GetRWDigits(), lhs.GetDigits(),
                               rhs.GetDigits());
    } else {
      bigint::BitwiseOr_PosPos(result.GetRWDigits(), lhs.GetDigits(),
                               rhs.GetDigits());
    }
  }
  return result;
}

}  // namespace internal
}  // namespace v8
#endif  // V8_NUMBERS_INTEGER_LITERAL_INL_H_
