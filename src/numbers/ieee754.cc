// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/numbers/ieee754.h"

#include <cmath>

#include "src/base/ieee754.h"
#include "src/flags/flags.h"

namespace v8::internal::math {

double pow(double x, double y) {
  if (v8_flags.use_std_math_pow) {
    if (std::isnan(y)) {
      // 1. If exponent is NaN, return NaN.
      return std::numeric_limits<double>::quiet_NaN();
    }
    if (std::isinf(y) && (x == 1 || x == -1)) {
      // 9. If exponent is +∞𝔽, then
      //   b. If abs(ℝ(base)) = 1, return NaN.
      // and
      // 10. If exponent is -∞𝔽, then
      //   b. If abs(ℝ(base)) = 1, return NaN.
      return std::numeric_limits<double>::quiet_NaN();
    }
    if (std::isnan(x)) {
      // std::pow distinguishes between quiet and signaling NaN; JS doesn't.
      x = std::numeric_limits<double>::quiet_NaN();
    }
    return std::pow(x, y);
  }
  return base::ieee754::legacy::pow(x, y);
}

}  // namespace v8::internal::math
