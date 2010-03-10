// Copyright 2006-2008 the V8 project authors. All rights reserved.

#include <stdlib.h>

#include "v8.h"

#include "platform.h"
#include "cctest.h"
#include "diy_fp.h"
#include "double.h"
#include "grisu3.h"

#include "test-dtoa.h"

using namespace v8::internal;

TEST(DoubleExtremes) {
  char buffer[kBufferSize];
  int length;
  int sign;
  int point;
  bool status;
  double min_double = 5e-324;
  status = grisu3(min_double, buffer, &sign, &length, &point);
  CHECK(status);
  CHECK_EQ(0, sign);
  CHECK_EQ("5", buffer);
  CHECK_EQ(-323, point);

  double max_double = 1.7976931348623157e308;
  status = grisu3(max_double, buffer, &sign, &length, &point);
  CHECK(status);
  CHECK_EQ(0, sign);
  CHECK_EQ("17976931348623157", buffer);
  CHECK_EQ(309, point);
}


TEST(DoubleTestFunctions) {
  char buffer[kBufferSize];

  strncpy(buffer, "12345", kBufferSize);
  CHECK(IsCorrect(123.45, buffer, 0, 5, 3));
  strncpy(buffer, "12345", kBufferSize);
  CHECK(IsCorrect(1.2345, buffer, 0, 5, 1));
  strncpy(buffer, "12345", kBufferSize);
  CHECK(!IsCorrect(1.2344, buffer, 0, 5, 1));
  strncpy(buffer, "12345", kBufferSize);
  CHECK(!IsCorrect(1.2345, buffer, 0, 5, 2));
  strncpy(buffer, "12345", kBufferSize);
  CHECK(!IsCorrect(1.2345, buffer, 0, 4, 1));

  strncpy(buffer, "1234", kBufferSize);
  CHECK(IsRounded(123.44, buffer, 0, 4, 3));
  strncpy(buffer, "1234", kBufferSize);
  CHECK(!IsRounded(123.4500000000001, buffer, 0, 4, 3));
  strncpy(buffer, "1234", kBufferSize);
  CHECK(IsRounded(123.44999999, buffer, 0, 4, 3));
  strncpy(buffer, "1234", kBufferSize);
  CHECK(IsRounded(123.44999999, buffer, 0, 3, 3));

  strncpy(buffer, "1234567000000000000000000001", kBufferSize);
  CHECK(IsShortest(123.45, buffer, 0, 5, 3));
  strncpy(buffer, "1234567000000000000000000001", kBufferSize);
  CHECK(IsShortest(123.4567, buffer, 0, 7, 3));
  strncpy(buffer, "1234567000000000000000000001", kBufferSize);
  CHECK(!IsShortest(123.4567, buffer, 0, strlen(buffer), 3));

  strncpy(buffer, "123456699999999999999999999999999999", kBufferSize);
  CHECK(!IsShortest(123.4567, buffer, 0, strlen(buffer), 3));
  strncpy(buffer, "123456699999999999999999999999999999", kBufferSize);
  CHECK(IsShortest(123.456, buffer, 0, 6, 3));
}


TEST(VariousDoubles) {
  char buffer[kBufferSize];
  int sign;
  int length;
  int point;
  int status;
  status = grisu3(4294967272.0, buffer, &sign, &length, &point);
  CHECK(status);
  CHECK_EQ(0, sign);
  CHECK_EQ("4294967272", buffer);
  CHECK_EQ(10, point);

  status = grisu3(4.1855804968213567e298, buffer, &sign, &length, &point);
  CHECK(status);
  CHECK_EQ(0, sign);
  CHECK_EQ("4185580496821357", buffer);
  CHECK_EQ(299, point);

  status = grisu3(5.5626846462680035e-309, buffer, &sign, &length, &point);
  CHECK(status);
  CHECK_EQ(0, sign);
  CHECK_EQ("5562684646268003", buffer);
  CHECK_EQ(-308, point);

  status = grisu3(2147483648.0, buffer, &sign, &length, &point);
  CHECK(status);
  CHECK_EQ(0, sign);
  CHECK_EQ("2147483648", buffer);
  CHECK_EQ(10, point);

  status = grisu3(3.5844466002796428e+298, buffer, &sign, &length, &point);
  if (status) {  // Not all grisu3 variants manage to compute this number.
    CHECK_EQ("35844466002796428", buffer);
    CHECK_EQ(0, sign);
    CHECK_EQ(299, point);
  }

  uint64_t smallest_normal64 = V8_2PART_UINT64_C(0x00100000, 00000000);
  double v = Double(smallest_normal64).value();
  status = grisu3(v, buffer, &sign, &length, &point);
  if (status) {
    CHECK_EQ(0, sign);
    CHECK(IsCorrect(v, buffer, 0, length, point));
    CHECK(IsRounded(v, buffer, 0, length, point));
    CHECK(IsShortest(v, buffer, 0, length, point));
  }

  uint64_t largest_denormal64 = V8_2PART_UINT64_C(0x000FFFFF, FFFFFFFF);
  v = Double(largest_denormal64).value();
  status = grisu3(v, buffer, &sign, &length, &point);
  if (status) {
    CHECK_EQ(0, sign);
    CHECK(IsCorrect(v, buffer, 0, length, point));
    CHECK(IsRounded(v, buffer, 0, length, point));
    CHECK(IsShortest(v, buffer, 0, length, point));
  }
}


static double random_double() {
  uint64_t double64 = 0;
  for (int i = 0; i < 8; i++) {
    double64 <<= 8;
    double64 += rand() % 256;  // NOLINT
  }
  return Double(double64).value();
}


TEST(RandomDoubles) {
  // For a more thorough testing increase the iteration count.
  // We also check kGrisu3MaximalLength in here.
  const int kIterationCount = 100000;
  int succeeded = 0;
  int total = 0;
  char buffer[kBufferSize];
  int length;
  int sign;
  int point;
  bool needed_max_length = false;

  for (int i = 0; i < kIterationCount; ++i) {
    double v = random_double();
    if (v != v) continue;  // NaN
    if (v == 0.0) continue;
    if (v < 0) v = -v;
    total++;
    int status = grisu3(v, buffer, &sign, &length, &point);
    CHECK_GE(kGrisu3MaximalLength, length);
    if (length == kGrisu3MaximalLength) needed_max_length = true;
    if (!status) continue;
    succeeded++;
    CHECK(IsCorrect(v, buffer, 0, length, point));
    CHECK(IsRounded(v, buffer, 0, length, point));
    CHECK(IsShortest(v, buffer, 0, length, point));
  }
  CHECK_GT(succeeded*1.0/total, 0.99);
  CHECK(needed_max_length);
}
