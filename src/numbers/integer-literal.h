// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_NUMBERS_INTEGER_LITERAL_H_
#define V8_NUMBERS_INTEGER_LITERAL_H_

#include "src/base/optional.h"
#include "src/bigint/bigint.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class IntegerLiteral {
 public:
  using digit_t = bigint::digit_t;
  static constexpr int kMaxLength =
      (1 << 30) / (kSystemPointerSize * kBitsPerByte);

  template <typename T>
  explicit IntegerLiteral(T value) : IntegerLiteral(value, true) {}

  static IntegerLiteral ForLength(int length, bool sign = false) {
    return IntegerLiteral(sign, std::vector<digit_t>(length));
  }

  bool sign() const { return sign_; }
  void set_sign(bool sign) { sign_ = sign; }
  int length() const { return static_cast<int>(digits_.size()); }
  bigint::RWDigits GetRWDigits() {
    return bigint::RWDigits(digits_.data(), static_cast<int>(digits_.size()));
  }
  bigint::Digits GetDigits() const {
    return bigint::Digits(const_cast<digit_t*>(digits_.data()),
                          static_cast<int>(digits_.size()));
  }

  template <typename T>
  bool IsRepresentableAs() const {
    static_assert(std::is_integral<T>::value, "Integral type required");
    return Compare(IntegerLiteral(std::numeric_limits<T>::min(), false)) >= 0 &&
           Compare(IntegerLiteral(std::numeric_limits<T>::max(), false)) <= 0;
  }

  template <typename T>
  T To() const {
    static_assert(std::is_integral<T>::value, "Integral type required");
    base::Optional<T> result_opt = TryTo<T>();
    DCHECK(result_opt.has_value());
    return *result_opt;
  }

  template <typename T>
  base::Optional<T> TryTo() const {
    static_assert(std::is_integral<T>::value, "Integral type required");
    if (!IsRepresentableAs<T>()) return base::nullopt;
    using unsigned_t = std::make_unsigned_t<T>;
    unsigned_t value = 0;
    if (digits_.empty()) return static_cast<T>(value);
    for (size_t i = 0; i < digits_.size(); ++i) {
      value = value | (static_cast<unsigned_t>(digits_[i])
                       << (sizeof(digit_t) * kBitsPerByte * i));
    }
    if (sign_) {
      value = (~value + 1);
    }
    return static_cast<T>(value);
  }

  bool IsZero() const {
    return std::all_of(digits_.begin(), digits_.end(),
                       [](digit_t d) { return d == 0; });
  }

  void Normalize() {
    while (digits_.size() > 0 && digits_[0] == 0) digits_.pop_back();
    if (digits_.empty()) sign_ = false;
  }

  int Compare(const IntegerLiteral& other) const {
    int diff = bigint::Compare(GetDigits(), other.GetDigits());
    if (diff == 0) {
      if (IsZero() || sign() == other.sign()) return 0;
      return sign() ? -1 : 1;
    } else if (diff < 0) {
      return other.sign() ? 1 : -1;
    } else {
      return sign() ? -1 : 1;
    }
  }

  std::string ToString() const;

 private:
  IntegerLiteral(bool sign, std::vector<digit_t> digits)
      : sign_(sign), digits_(std::move(digits)) {}

  template <typename T>
  explicit IntegerLiteral(T value, bool perform_dcheck) : sign_(false) {
    static_assert(std::is_integral<T>::value, "Integral type required");
    if (value == T(0)) return;
    auto absolute = static_cast<typename std::make_unsigned<T>::type>(value);
    if (value < T(0)) {
      sign_ = true;
      absolute = (~absolute) + 1;
    }
    if (sizeof(absolute) <= sizeof(digit_t)) {
      digits_.push_back(absolute);
    } else {
      do {
        digits_.push_back(static_cast<digit_t>(absolute));
        absolute >>= sizeof(digit_t) * kBitsPerByte;
      } while (absolute != 0);
    }
    if (perform_dcheck) DCHECK_EQ(To<T>(), value);
  }

  bool sign_;
  std::vector<digit_t> digits_;
};

inline bool operator==(const IntegerLiteral& lhs, const IntegerLiteral& rhs) {
  return lhs.Compare(rhs) == 0;
}

inline bool operator!=(const IntegerLiteral& lhs, const IntegerLiteral& rhs) {
  return lhs.Compare(rhs) != 0;
}

inline std::ostream& operator<<(std::ostream& stream,
                                const IntegerLiteral& literal) {
  return stream << literal.ToString();
}

IntegerLiteral operator<<(const IntegerLiteral& lhs, const IntegerLiteral& rhs);
IntegerLiteral operator+(const IntegerLiteral& lhs, const IntegerLiteral& rhs);
IntegerLiteral operator|(const IntegerLiteral& lhs, const IntegerLiteral& rhs);

}  // namespace internal
}  // namespace v8
#endif  // V8_NUMBERS_INTEGER_LITERAL_H_
