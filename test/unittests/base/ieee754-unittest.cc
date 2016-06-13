// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "src/base/ieee754.h"
#include "src/base/macros.h"
#include "testing/gmock-support.h"
#include "testing/gtest-support.h"

using testing::BitEq;
using testing::IsNaN;

namespace v8 {
namespace base {
namespace ieee754 {

TEST(Ieee754, Atan) {
  EXPECT_THAT(atan(std::numeric_limits<double>::quiet_NaN()), IsNaN());
  EXPECT_THAT(atan(std::numeric_limits<double>::signaling_NaN()), IsNaN());
  EXPECT_THAT(atan(-0.0), BitEq(-0.0));
  EXPECT_THAT(atan(0.0), BitEq(0.0));
  EXPECT_DOUBLE_EQ(1.5707963267948966,
                   atan(std::numeric_limits<double>::infinity()));
  EXPECT_DOUBLE_EQ(-1.5707963267948966,
                   atan(-std::numeric_limits<double>::infinity()));
}

TEST(Ieee754, Atan2) {
  EXPECT_THAT(atan2(std::numeric_limits<double>::quiet_NaN(),
                    std::numeric_limits<double>::quiet_NaN()),
              IsNaN());
  EXPECT_THAT(atan2(std::numeric_limits<double>::quiet_NaN(),
                    std::numeric_limits<double>::signaling_NaN()),
              IsNaN());
  EXPECT_THAT(atan2(std::numeric_limits<double>::signaling_NaN(),
                    std::numeric_limits<double>::quiet_NaN()),
              IsNaN());
  EXPECT_THAT(atan2(std::numeric_limits<double>::signaling_NaN(),
                    std::numeric_limits<double>::signaling_NaN()),
              IsNaN());
  EXPECT_DOUBLE_EQ(0.7853981633974483,
                   atan2(std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::infinity()));
  EXPECT_DOUBLE_EQ(2.356194490192345,
                   atan2(std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity()));
  EXPECT_DOUBLE_EQ(-0.7853981633974483,
                   atan2(-std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::infinity()));
  EXPECT_DOUBLE_EQ(-2.356194490192345,
                   atan2(-std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity()));
}

TEST(Ieee754, Log) {
  EXPECT_THAT(log(std::numeric_limits<double>::quiet_NaN()), IsNaN());
  EXPECT_THAT(log(std::numeric_limits<double>::signaling_NaN()), IsNaN());
  EXPECT_THAT(log(-std::numeric_limits<double>::infinity()), IsNaN());
  EXPECT_THAT(log(-1.0), IsNaN());
  EXPECT_EQ(-std::numeric_limits<double>::infinity(), log(-0.0));
  EXPECT_EQ(-std::numeric_limits<double>::infinity(), log(0.0));
  EXPECT_EQ(0.0, log(1.0));
  EXPECT_EQ(std::numeric_limits<double>::infinity(),
            log(std::numeric_limits<double>::infinity()));
}

TEST(Ieee754, Log1p) {
  EXPECT_THAT(log1p(std::numeric_limits<double>::quiet_NaN()), IsNaN());
  EXPECT_THAT(log1p(std::numeric_limits<double>::signaling_NaN()), IsNaN());
  EXPECT_THAT(log1p(-std::numeric_limits<double>::infinity()), IsNaN());
  EXPECT_EQ(-std::numeric_limits<double>::infinity(), log1p(-1.0));
  EXPECT_EQ(0.0, log1p(0.0));
  EXPECT_EQ(-0.0, log1p(-0.0));
  EXPECT_EQ(std::numeric_limits<double>::infinity(),
            log1p(std::numeric_limits<double>::infinity()));
  EXPECT_EQ(6.9756137364252422e-03, log1p(0.007));
  EXPECT_EQ(709.782712893384, log1p(1.7976931348623157e308));
  EXPECT_EQ(2.7755575615628914e-17, log1p(2.7755575615628914e-17));
  EXPECT_EQ(9.313225741817976e-10, log1p(9.313225746154785e-10));
  EXPECT_EQ(-0.2876820724517809, log1p(-0.25));
  EXPECT_EQ(0.22314355131420976, log1p(0.25));
  EXPECT_EQ(2.3978952727983707, log1p(10));
  EXPECT_EQ(36.841361487904734, log1p(10e15));
  EXPECT_EQ(37.08337388996168, log1p(12738099905822720));
  EXPECT_EQ(37.08336444902049, log1p(12737979646738432));
  EXPECT_EQ(1.3862943611198906, log1p(3));
  EXPECT_EQ(1.3862945995384413, log1p(3 + 9.5367431640625e-7));
  EXPECT_EQ(0.5596157879354227, log1p(0.75));
  EXPECT_EQ(0.8109302162163288, log1p(1.25));
}

}  // namespace ieee754
}  // namespace base
}  // namespace v8
