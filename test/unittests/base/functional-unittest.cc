// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/functional.h"

#include <limits>
#include <set>

#include "src/utils.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace base {

TEST(FunctionalTest, HashBool) {
  hash<bool> h, h1, h2;
  EXPECT_EQ(h1(true), h2(true));
  EXPECT_EQ(h1(false), h2(false));
  EXPECT_NE(h(true), h(false));
}


TEST(FunctionalTest, HashFloatZero) {
  hash<float> h;
  EXPECT_EQ(h(0.0f), h(-0.0f));
}


TEST(FunctionalTest, HashDoubleZero) {
  hash<double> h;
  EXPECT_EQ(h(0.0), h(-0.0));
}

namespace {

inline int64_t GetRandomSeedFromFlag(int random_seed) {
  return random_seed ? random_seed : TimeTicks::Now().ToInternalValue();
}

}  // namespace

template <typename T>
class FunctionalTest : public ::testing::Test {
 public:
  FunctionalTest()
      : rng_(GetRandomSeedFromFlag(::v8::internal::FLAG_random_seed)) {}
  virtual ~FunctionalTest() {}

  RandomNumberGenerator* rng() { return &rng_; }

 private:
  RandomNumberGenerator rng_;

  DISALLOW_COPY_AND_ASSIGN(FunctionalTest);
};

typedef ::testing::Types<signed char, unsigned char,
                         short,                    // NOLINT(runtime/int)
                         unsigned short,           // NOLINT(runtime/int)
                         int, unsigned int, long,  // NOLINT(runtime/int)
                         unsigned long,            // NOLINT(runtime/int)
                         long long,                // NOLINT(runtime/int)
                         unsigned long long,       // NOLINT(runtime/int)
                         int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t,
                         int64_t, uint64_t, float, double> FunctionalTypes;

TYPED_TEST_CASE(FunctionalTest, FunctionalTypes);


TYPED_TEST(FunctionalTest, EqualToImpliesSameHashCode) {
  hash<TypeParam> h;
  std::equal_to<TypeParam> e;
  TypeParam values[32];
  this->rng()->NextBytes(values, sizeof(values));
  TRACED_FOREACH(TypeParam, v1, values) {
    TRACED_FOREACH(TypeParam, v2, values) {
      if (e(v1, v2)) EXPECT_EQ(h(v1), h(v2));
    }
  }
}


TYPED_TEST(FunctionalTest, HashEqualsHashValue) {
  for (int i = 0; i < 128; ++i) {
    TypeParam v;
    this->rng()->NextBytes(&v, sizeof(v));
    hash<TypeParam> h;
    EXPECT_EQ(h(v), hash_value(v));
  }
}


TYPED_TEST(FunctionalTest, HashIsStateless) {
  hash<TypeParam> h1, h2;
  for (int i = 0; i < 128; ++i) {
    TypeParam v;
    this->rng()->NextBytes(&v, sizeof(v));
    EXPECT_EQ(h1(v), h2(v));
  }
}


TYPED_TEST(FunctionalTest, HashIsOkish) {
  std::set<TypeParam> vs;
  for (size_t i = 0; i < 128; ++i) {
    TypeParam v;
    this->rng()->NextBytes(&v, sizeof(v));
    vs.insert(v);
  }
  std::set<size_t> hs;
  for (const auto& v : vs) {
    hash<TypeParam> h;
    hs.insert(h(v));
  }
  EXPECT_LE(vs.size() / 4u, hs.size());
}


TYPED_TEST(FunctionalTest, HashValueArrayUsesHashRange) {
  TypeParam values[128];
  this->rng()->NextBytes(&values, sizeof(values));
  EXPECT_EQ(hash_range(values, values + arraysize(values)), hash_value(values));
}


TYPED_TEST(FunctionalTest, BitEqualTo) {
  bit_equal_to<TypeParam> pred;
  for (size_t i = 0; i < 128; ++i) {
    TypeParam v1, v2;
    this->rng()->NextBytes(&v1, sizeof(v1));
    this->rng()->NextBytes(&v2, sizeof(v2));
    EXPECT_PRED2(pred, v1, v1);
    EXPECT_PRED2(pred, v2, v2);
    EXPECT_EQ(memcmp(&v1, &v2, sizeof(TypeParam)) == 0, pred(v1, v2));
  }
}


TYPED_TEST(FunctionalTest, BitEqualToImpliesSameBitHash) {
  bit_hash<TypeParam> h;
  bit_equal_to<TypeParam> e;
  TypeParam values[32];
  this->rng()->NextBytes(&values, sizeof(values));
  TRACED_FOREACH(TypeParam, v1, values) {
    TRACED_FOREACH(TypeParam, v2, values) {
      if (e(v1, v2)) EXPECT_EQ(h(v1), h(v2));
    }
  }
}


namespace {

struct Foo {
  int x;
  double y;
};


size_t hash_value(Foo const& v) { return hash_combine(v.x, v.y); }

}  // namespace


TEST(FunctionalTest, HashUsesArgumentDependentLookup) {
  const int kIntValues[] = {std::numeric_limits<int>::min(), -1, 0, 1, 42,
                            std::numeric_limits<int>::max()};
  const double kDoubleValues[] = {
      std::numeric_limits<double>::min(), -1, -0, 0, 1,
      std::numeric_limits<double>::max()};
  TRACED_FOREACH(int, x, kIntValues) {
    TRACED_FOREACH(double, y, kDoubleValues) {
      hash<Foo> h;
      Foo foo = {x, y};
      EXPECT_EQ(hash_combine(x, y), h(foo));
    }
  }
}


TEST(FunctionalTest, BitEqualToFloat) {
  bit_equal_to<float> pred;
  EXPECT_FALSE(pred(0.0f, -0.0f));
  EXPECT_FALSE(pred(-0.0f, 0.0f));
  float const qNaN = std::numeric_limits<float>::quiet_NaN();
  float const sNaN = std::numeric_limits<float>::signaling_NaN();
  EXPECT_PRED2(pred, qNaN, qNaN);
  EXPECT_PRED2(pred, sNaN, sNaN);
}


TEST(FunctionalTest, BitHashFloatDifferentForZeroAndMinusZero) {
  bit_hash<float> h;
  EXPECT_NE(h(0.0f), h(-0.0f));
}


TEST(FunctionalTest, BitEqualToDouble) {
  bit_equal_to<double> pred;
  EXPECT_FALSE(pred(0.0, -0.0));
  EXPECT_FALSE(pred(-0.0, 0.0));
  double const qNaN = std::numeric_limits<double>::quiet_NaN();
  double const sNaN = std::numeric_limits<double>::signaling_NaN();
  EXPECT_PRED2(pred, qNaN, qNaN);
  EXPECT_PRED2(pred, sNaN, sNaN);
}


TEST(FunctionalTest, BitHashDoubleDifferentForZeroAndMinusZero) {
  bit_hash<double> h;
  EXPECT_NE(h(0.0), h(-0.0));
}

// src/utils.h functions
template <typename T>
class UtilsTest : public ::testing::Test {};

typedef ::testing::Types<signed char, unsigned char,
                         short,                    // NOLINT(runtime/int)
                         unsigned short,           // NOLINT(runtime/int)
                         int, unsigned int, long,  // NOLINT(runtime/int)
                         unsigned long,            // NOLINT(runtime/int)
                         long long,                // NOLINT(runtime/int)
                         unsigned long long,       // NOLINT(runtime/int)
                         int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t,
                         int64_t, uint64_t>
    IntegerTypes;

TYPED_TEST_CASE(UtilsTest, IntegerTypes);

TYPED_TEST(UtilsTest, SaturateSub) {
  TypeParam min = std::numeric_limits<TypeParam>::min();
  TypeParam max = std::numeric_limits<TypeParam>::max();
  EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(min, 0), min);
  EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(max, 0), max);
  EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(max, min), max);
  EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(min, max), min);
  EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(min, max / 3), min);
  EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(min + 1, 2), min);
  if (std::numeric_limits<TypeParam>::is_signed) {
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(min, min),
              static_cast<TypeParam>(0));
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(0, min), max);
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(0, max), -max);
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(max - 1, -2), max);
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(max / 3, min), max);
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(max / 5, min), max);
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(min / 3, max), min);
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(min / 9, max), min);
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(max, min / 3), max);
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(min, max / 3), min);
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(max / 3 * 2, min / 2), max);
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(min / 3 * 2, max / 2), min);
  } else {
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(min, min), min);
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(0, min), min);
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(0, max), min);
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(max / 3, max), min);
    EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(max - 3, max), min);
  }
  TypeParam test_cases[] = {static_cast<TypeParam>(min / 23),
                            static_cast<TypeParam>(max / 3),
                            63,
                            static_cast<TypeParam>(min / 6),
                            static_cast<TypeParam>(max / 55),
                            static_cast<TypeParam>(min / 2),
                            static_cast<TypeParam>(max / 2),
                            0,
                            1,
                            2,
                            3,
                            4,
                            42};
  TRACED_FOREACH(TypeParam, x, test_cases) {
    TRACED_FOREACH(TypeParam, y, test_cases) {
      if (std::numeric_limits<TypeParam>::is_signed) {
        EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(x, y), x - y);
      } else {
        EXPECT_EQ(v8::internal::SaturateSub<TypeParam>(x, y),
                  y > x ? min : x - y);
      }
    }
  }
}

TYPED_TEST(UtilsTest, SaturateAdd) {
  TypeParam min = std::numeric_limits<TypeParam>::min();
  TypeParam max = std::numeric_limits<TypeParam>::max();
  EXPECT_EQ(v8::internal::SaturateAdd<TypeParam>(min, min), min);
  EXPECT_EQ(v8::internal::SaturateAdd<TypeParam>(max, max), max);
  EXPECT_EQ(v8::internal::SaturateAdd<TypeParam>(min, min / 3), min);
  EXPECT_EQ(v8::internal::SaturateAdd<TypeParam>(max / 8 * 7, max / 3 * 2),
            max);
  EXPECT_EQ(v8::internal::SaturateAdd<TypeParam>(min / 3 * 2, min / 8 * 7),
            min);
  EXPECT_EQ(v8::internal::SaturateAdd<TypeParam>(max / 20 * 18, max / 25 * 18),
            max);
  EXPECT_EQ(v8::internal::SaturateAdd<TypeParam>(min / 3 * 2, min / 3 * 2),
            min);
  EXPECT_EQ(v8::internal::SaturateAdd<TypeParam>(max - 1, 2), max);
  EXPECT_EQ(v8::internal::SaturateAdd<TypeParam>(max - 100, 101), max);
  if (std::numeric_limits<TypeParam>::is_signed) {
    EXPECT_EQ(v8::internal::SaturateAdd<TypeParam>(min + 100, -101), min);
    EXPECT_EQ(v8::internal::SaturateAdd<TypeParam>(min + 1, -2), min);
  }
  TypeParam test_cases[] = {static_cast<TypeParam>(min / 23),
                            static_cast<TypeParam>(max / 3),
                            63,
                            static_cast<TypeParam>(min / 6),
                            static_cast<TypeParam>(max / 55),
                            static_cast<TypeParam>(min / 2),
                            static_cast<TypeParam>(max / 2),
                            0,
                            1,
                            2,
                            3,
                            4,
                            42};
  TRACED_FOREACH(TypeParam, x, test_cases) {
    TRACED_FOREACH(TypeParam, y, test_cases) {
      EXPECT_EQ(v8::internal::SaturateAdd<TypeParam>(x, y), x + y);
    }
  }
}

}  // namespace base
}  // namespace v8
