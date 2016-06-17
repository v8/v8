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

TEST(Ieee754, Atanh) {
  EXPECT_THAT(atanh(std::numeric_limits<double>::quiet_NaN()), IsNaN());
  EXPECT_THAT(atanh(std::numeric_limits<double>::signaling_NaN()), IsNaN());
  EXPECT_THAT(atanh(std::numeric_limits<double>::infinity()), IsNaN());
  EXPECT_EQ(std::numeric_limits<double>::infinity(), atanh(1));
  EXPECT_EQ(-std::numeric_limits<double>::infinity(), atanh(-1));
  EXPECT_DOUBLE_EQ(0.54930614433405478, atanh(0.5));
}

TEST(Ieee754, Exp) {
  EXPECT_THAT(exp(std::numeric_limits<double>::quiet_NaN()), IsNaN());
  EXPECT_THAT(exp(std::numeric_limits<double>::signaling_NaN()), IsNaN());
  EXPECT_EQ(0.0, exp(-std::numeric_limits<double>::infinity()));
  EXPECT_EQ(0.0, exp(-1000));
  EXPECT_EQ(0.0, exp(-745.1332191019412));
  EXPECT_EQ(2.2250738585072626e-308, exp(-708.39641853226408));
  EXPECT_EQ(3.307553003638408e-308, exp(-708.0));
  EXPECT_EQ(4.9406564584124654e-324, exp(-7.45133219101941108420e+02));
  EXPECT_EQ(0.36787944117144233, exp(-1.0));
  EXPECT_EQ(1.0, exp(-0.0));
  EXPECT_EQ(1.0, exp(0.0));
  EXPECT_EQ(1.0, exp(2.2250738585072014e-308));
  EXPECT_GE(exp(1.0), exp(0.9999999999999999));
  EXPECT_LE(exp(1.0), exp(1.0000000000000002));
  EXPECT_EQ(2.7182818284590455, exp(1.0));
  EXPECT_EQ(7.38905609893065e0, exp(2.0));
  EXPECT_EQ(1.7976931348622732e308, exp(7.09782712893383973096e+02));
  EXPECT_EQ(2.6881171418161356e+43, exp(100.0));
  EXPECT_EQ(8.218407461554972e+307, exp(709.0));
  EXPECT_EQ(1.7968190737295725e308, exp(709.7822265625e0));
  EXPECT_EQ(std::numeric_limits<double>::infinity(), exp(709.7827128933841e0));
  EXPECT_EQ(std::numeric_limits<double>::infinity(), exp(710.0));
  EXPECT_EQ(std::numeric_limits<double>::infinity(), exp(1000.0));
  EXPECT_EQ(std::numeric_limits<double>::infinity(),
            exp(std::numeric_limits<double>::infinity()));
}

TEST(Ieee754, Expm1) {
  EXPECT_THAT(expm1(std::numeric_limits<double>::quiet_NaN()), IsNaN());
  EXPECT_THAT(expm1(std::numeric_limits<double>::signaling_NaN()), IsNaN());
  EXPECT_EQ(-1.0, expm1(-std::numeric_limits<double>::infinity()));
  EXPECT_EQ(std::numeric_limits<double>::infinity(),
            expm1(std::numeric_limits<double>::infinity()));
  EXPECT_EQ(0.0, expm1(-0.0));
  EXPECT_EQ(0.0, expm1(0.0));
  EXPECT_EQ(1.718281828459045, expm1(1.0));
  EXPECT_EQ(2.6881171418161356e+43, expm1(100.0));
  EXPECT_EQ(8.218407461554972e+307, expm1(709.0));
  EXPECT_EQ(std::numeric_limits<double>::infinity(), expm1(710.0));
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

TEST(Ieee754, Log2) {
  EXPECT_THAT(log2(std::numeric_limits<double>::quiet_NaN()), IsNaN());
  EXPECT_THAT(log2(std::numeric_limits<double>::signaling_NaN()), IsNaN());
  EXPECT_THAT(log2(-std::numeric_limits<double>::infinity()), IsNaN());
  EXPECT_THAT(log2(-1.0), IsNaN());
  EXPECT_EQ(-std::numeric_limits<double>::infinity(), log2(0.0));
  EXPECT_EQ(-std::numeric_limits<double>::infinity(), log2(-0.0));
  EXPECT_EQ(std::numeric_limits<double>::infinity(),
            log2(std::numeric_limits<double>::infinity()));
}

TEST(Ieee754, Log10) {
  EXPECT_THAT(log10(std::numeric_limits<double>::quiet_NaN()), IsNaN());
  EXPECT_THAT(log10(std::numeric_limits<double>::signaling_NaN()), IsNaN());
  EXPECT_THAT(log10(-std::numeric_limits<double>::infinity()), IsNaN());
  EXPECT_THAT(log10(-1.0), IsNaN());
  EXPECT_EQ(-std::numeric_limits<double>::infinity(), log10(0.0));
  EXPECT_EQ(-std::numeric_limits<double>::infinity(), log10(-0.0));
  EXPECT_EQ(std::numeric_limits<double>::infinity(),
            log10(std::numeric_limits<double>::infinity()));
  EXPECT_EQ(3.0, log10(1000.0));
  EXPECT_EQ(14.0, log10(100000000000000));  // log10(10 ^ 14)
  EXPECT_EQ(3.7389561269540406, log10(5482.2158));
  EXPECT_EQ(14.661551142893833, log10(458723662312872.125782332587));
  EXPECT_EQ(-0.9083828622192334, log10(0.12348583358871));
  EXPECT_EQ(5.0, log10(100000.0));
}

TEST(Ieee754, cbrt) {
  EXPECT_THAT(cbrt(std::numeric_limits<double>::quiet_NaN()), IsNaN());
  EXPECT_THAT(cbrt(std::numeric_limits<double>::signaling_NaN()), IsNaN());
  EXPECT_EQ(std::numeric_limits<double>::infinity(),
            cbrt(std::numeric_limits<double>::infinity()));
  EXPECT_EQ(-std::numeric_limits<double>::infinity(),
            cbrt(-std::numeric_limits<double>::infinity()));
  EXPECT_EQ(1.4422495703074083, cbrt(3));
  EXPECT_EQ(100, cbrt(100 * 100 * 100));
  EXPECT_EQ(46.415888336127786, cbrt(100000));
}

}  // namespace ieee754
}  // namespace base
}  // namespace v8
