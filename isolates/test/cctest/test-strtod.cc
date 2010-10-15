// Copyright 2006-2008 the V8 project authors. All rights reserved.

#include <stdlib.h>

#include "v8.h"

#include "platform.h"
#include "cctest.h"
#include "strtod.h"

using namespace v8::internal;

static const int kBufferSize = 100;


TEST(Strtod) {
  char buffer_array[kBufferSize];
  Vector<char> buffer(buffer_array, kBufferSize);
  Vector<char> vector;

  OS::StrNCpy(buffer, "0", kBufferSize);
  vector = Vector<char>(buffer_array, strlen(buffer_array));
  CHECK_EQ(0.0, Strtod(vector, 1));
  CHECK_EQ(0.0, Strtod(vector, 2));
  CHECK_EQ(0.0, Strtod(vector, -2));
  CHECK_EQ(0.0, Strtod(vector, -999));
  CHECK_EQ(0.0, Strtod(vector, +999));

  OS::StrNCpy(buffer, "1", kBufferSize);
  vector = Vector<char>(buffer_array, strlen(buffer_array));
  CHECK_EQ(1.0, Strtod(vector, 0));
  CHECK_EQ(10.0, Strtod(vector, 1));
  CHECK_EQ(100.0, Strtod(vector, 2));
  CHECK_EQ(1e20, Strtod(vector, 20));
  CHECK_EQ(1e22, Strtod(vector, 22));
  CHECK_EQ(1e23, Strtod(vector, 23));
  CHECK_EQ(1e35, Strtod(vector, 35));
  CHECK_EQ(1e36, Strtod(vector, 36));
  CHECK_EQ(1e37, Strtod(vector, 37));
  CHECK_EQ(1e-1, Strtod(vector, -1));
  CHECK_EQ(1e-2, Strtod(vector, -2));
  CHECK_EQ(1e-5, Strtod(vector, -5));
  CHECK_EQ(1e-20, Strtod(vector, -20));
  CHECK_EQ(1e-22, Strtod(vector, -22));
  CHECK_EQ(1e-23, Strtod(vector, -23));
  CHECK_EQ(1e-25, Strtod(vector, -25));
  CHECK_EQ(1e-39, Strtod(vector, -39));

  OS::StrNCpy(buffer, "2", kBufferSize);
  vector = Vector<char>(buffer_array, strlen(buffer_array));
  CHECK_EQ(2.0, Strtod(vector, 0));
  CHECK_EQ(20.0, Strtod(vector, 1));
  CHECK_EQ(200.0, Strtod(vector, 2));
  CHECK_EQ(2e20, Strtod(vector, 20));
  CHECK_EQ(2e22, Strtod(vector, 22));
  CHECK_EQ(2e23, Strtod(vector, 23));
  CHECK_EQ(2e35, Strtod(vector, 35));
  CHECK_EQ(2e36, Strtod(vector, 36));
  CHECK_EQ(2e37, Strtod(vector, 37));
  CHECK_EQ(2e-1, Strtod(vector, -1));
  CHECK_EQ(2e-2, Strtod(vector, -2));
  CHECK_EQ(2e-5, Strtod(vector, -5));
  CHECK_EQ(2e-20, Strtod(vector, -20));
  CHECK_EQ(2e-22, Strtod(vector, -22));
  CHECK_EQ(2e-23, Strtod(vector, -23));
  CHECK_EQ(2e-25, Strtod(vector, -25));
  CHECK_EQ(2e-39, Strtod(vector, -39));

  OS::StrNCpy(buffer, "9", kBufferSize);
  vector = Vector<char>(buffer_array, strlen(buffer_array));
  CHECK_EQ(9.0, Strtod(vector, 0));
  CHECK_EQ(90.0, Strtod(vector, 1));
  CHECK_EQ(900.0, Strtod(vector, 2));
  CHECK_EQ(9e20, Strtod(vector, 20));
  CHECK_EQ(9e22, Strtod(vector, 22));
  CHECK_EQ(9e23, Strtod(vector, 23));
  CHECK_EQ(9e35, Strtod(vector, 35));
  CHECK_EQ(9e36, Strtod(vector, 36));
  CHECK_EQ(9e37, Strtod(vector, 37));
  CHECK_EQ(9e-1, Strtod(vector, -1));
  CHECK_EQ(9e-2, Strtod(vector, -2));
  CHECK_EQ(9e-5, Strtod(vector, -5));
  CHECK_EQ(9e-20, Strtod(vector, -20));
  CHECK_EQ(9e-22, Strtod(vector, -22));
  CHECK_EQ(9e-23, Strtod(vector, -23));
  CHECK_EQ(9e-25, Strtod(vector, -25));
  CHECK_EQ(9e-39, Strtod(vector, -39));

  OS::StrNCpy(buffer, "12345", kBufferSize);
  vector = Vector<char>(buffer_array, strlen(buffer_array));
  CHECK_EQ(12345.0, Strtod(vector, 0));
  CHECK_EQ(123450.0, Strtod(vector, 1));
  CHECK_EQ(1234500.0, Strtod(vector, 2));
  CHECK_EQ(12345e20, Strtod(vector, 20));
  CHECK_EQ(12345e22, Strtod(vector, 22));
  CHECK_EQ(12345e23, Strtod(vector, 23));
  CHECK_EQ(12345e30, Strtod(vector, 30));
  CHECK_EQ(12345e31, Strtod(vector, 31));
  CHECK_EQ(12345e32, Strtod(vector, 32));
  CHECK_EQ(12345e35, Strtod(vector, 35));
  CHECK_EQ(12345e36, Strtod(vector, 36));
  CHECK_EQ(12345e37, Strtod(vector, 37));
  CHECK_EQ(12345e-1, Strtod(vector, -1));
  CHECK_EQ(12345e-2, Strtod(vector, -2));
  CHECK_EQ(12345e-5, Strtod(vector, -5));
  CHECK_EQ(12345e-20, Strtod(vector, -20));
  CHECK_EQ(12345e-22, Strtod(vector, -22));
  CHECK_EQ(12345e-23, Strtod(vector, -23));
  CHECK_EQ(12345e-25, Strtod(vector, -25));
  CHECK_EQ(12345e-39, Strtod(vector, -39));

  OS::StrNCpy(buffer, "12345678901234", kBufferSize);
  vector = Vector<char>(buffer_array, strlen(buffer_array));
  CHECK_EQ(12345678901234.0, Strtod(vector, 0));
  CHECK_EQ(123456789012340.0, Strtod(vector, 1));
  CHECK_EQ(1234567890123400.0, Strtod(vector, 2));
  CHECK_EQ(12345678901234e20, Strtod(vector, 20));
  CHECK_EQ(12345678901234e22, Strtod(vector, 22));
  CHECK_EQ(12345678901234e23, Strtod(vector, 23));
  CHECK_EQ(12345678901234e30, Strtod(vector, 30));
  CHECK_EQ(12345678901234e31, Strtod(vector, 31));
  CHECK_EQ(12345678901234e32, Strtod(vector, 32));
  CHECK_EQ(12345678901234e35, Strtod(vector, 35));
  CHECK_EQ(12345678901234e36, Strtod(vector, 36));
  CHECK_EQ(12345678901234e37, Strtod(vector, 37));
  CHECK_EQ(12345678901234e-1, Strtod(vector, -1));
  CHECK_EQ(12345678901234e-2, Strtod(vector, -2));
  CHECK_EQ(12345678901234e-5, Strtod(vector, -5));
  CHECK_EQ(12345678901234e-20, Strtod(vector, -20));
  CHECK_EQ(12345678901234e-22, Strtod(vector, -22));
  CHECK_EQ(12345678901234e-23, Strtod(vector, -23));
  CHECK_EQ(12345678901234e-25, Strtod(vector, -25));
  CHECK_EQ(12345678901234e-39, Strtod(vector, -39));

  OS::StrNCpy(buffer, "123456789012345", kBufferSize);
  vector = Vector<char>(buffer_array, strlen(buffer_array));
  CHECK_EQ(123456789012345.0, Strtod(vector, 0));
  CHECK_EQ(1234567890123450.0, Strtod(vector, 1));
  CHECK_EQ(12345678901234500.0, Strtod(vector, 2));
  CHECK_EQ(123456789012345e20, Strtod(vector, 20));
  CHECK_EQ(123456789012345e22, Strtod(vector, 22));
  CHECK_EQ(123456789012345e23, Strtod(vector, 23));
  CHECK_EQ(123456789012345e35, Strtod(vector, 35));
  CHECK_EQ(123456789012345e36, Strtod(vector, 36));
  CHECK_EQ(123456789012345e37, Strtod(vector, 37));
  CHECK_EQ(123456789012345e39, Strtod(vector, 39));
  CHECK_EQ(123456789012345e-1, Strtod(vector, -1));
  CHECK_EQ(123456789012345e-2, Strtod(vector, -2));
  CHECK_EQ(123456789012345e-5, Strtod(vector, -5));
  CHECK_EQ(123456789012345e-20, Strtod(vector, -20));
  CHECK_EQ(123456789012345e-22, Strtod(vector, -22));
  CHECK_EQ(123456789012345e-23, Strtod(vector, -23));
  CHECK_EQ(123456789012345e-25, Strtod(vector, -25));
  CHECK_EQ(123456789012345e-39, Strtod(vector, -39));
}
