// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TYPE_HINTS_H_
#define V8_COMPILER_TYPE_HINTS_H_

#include "src/utils.h"

namespace v8 {
namespace internal {
namespace compiler {

// Type hints for an binary operation.
class BinaryOperationHints final {
 public:
  enum Hint { kNone, kSignedSmall, kSigned32, kNumber, kString, kAny };

  BinaryOperationHints() : BinaryOperationHints(kNone, kNone, kNone) {}
  BinaryOperationHints(Hint left, Hint right, Hint result)
      : bit_field_(LeftField::encode(left) | RightField::encode(right) |
                   ResultField::encode(result)) {}

  static BinaryOperationHints Any() {
    return BinaryOperationHints(kAny, kAny, kAny);
  }

  Hint left() const { return LeftField::decode(bit_field_); }
  Hint right() const { return RightField::decode(bit_field_); }
  Hint result() const { return ResultField::decode(bit_field_); }

  bool operator==(BinaryOperationHints const& that) const {
    return this->bit_field_ == that.bit_field_;
  }
  bool operator!=(BinaryOperationHints const& that) const {
    return !(*this == that);
  }

  friend size_t hash_value(BinaryOperationHints const& hints) {
    return hints.bit_field_;
  }

 private:
  typedef BitField<Hint, 0, 3> LeftField;
  typedef BitField<Hint, 3, 3> RightField;
  typedef BitField<Hint, 6, 3> ResultField;

  uint32_t bit_field_;
};

std::ostream& operator<<(std::ostream&, BinaryOperationHints::Hint);
std::ostream& operator<<(std::ostream&, BinaryOperationHints);

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TYPE_HINTS_H_
