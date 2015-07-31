// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/arguments.h"
#include "src/base/macros.h"
#include "src/conversions.h"
#include "src/runtime/runtime-utils.h"

// Implement Single Instruction Multiple Data (SIMD) operations as defined in
// the SIMD.js draft spec:
// http://littledan.github.io/simd.html

#define NumberToFloat32x4Component NumberToFloat

#define CONVERT_SIMD_LANE_ARG_CHECKED(name, index, lanes) \
  RUNTIME_ASSERT(args[index]->IsSmi());                   \
  int name = args.smi_at(index);                          \
  RUNTIME_ASSERT(name >= 0 && name < lanes);

#define SIMD4_CREATE_FUNCTION(type)                                    \
  RUNTIME_FUNCTION(Runtime_Create##type) {                             \
    HandleScope scope(isolate);                                        \
    DCHECK(args.length() == 4);                                        \
    CONVERT_NUMBER_ARG_HANDLE_CHECKED(w, 0);                           \
    CONVERT_NUMBER_ARG_HANDLE_CHECKED(x, 1);                           \
    CONVERT_NUMBER_ARG_HANDLE_CHECKED(y, 2);                           \
    CONVERT_NUMBER_ARG_HANDLE_CHECKED(z, 3);                           \
    return *isolate->factory()->NewFloat32x4(                          \
        NumberTo##type##Component(*w), NumberTo##type##Component(*x),  \
        NumberTo##type##Component(*y), NumberTo##type##Component(*z)); \
  }

#define SIMD_CHECK_FUNCTION(type)           \
  RUNTIME_FUNCTION(Runtime_##type##Check) { \
    HandleScope scope(isolate);             \
    CONVERT_ARG_HANDLE_CHECKED(type, a, 0); \
    return *a;                              \
  }

#define SIMD_EXTRACT_LANE_FUNCTION(type, lanes)               \
  RUNTIME_FUNCTION(Runtime_##type##ExtractLane) {             \
    HandleScope scope(isolate);                               \
    DCHECK(args.length() == 2);                               \
    CONVERT_ARG_HANDLE_CHECKED(type, a, 0);                   \
    CONVERT_SIMD_LANE_ARG_CHECKED(lane, 1, lanes);            \
    return *isolate->factory()->NewNumber(a->get_lane(lane)); \
  }

#define SIMD4_EQUALS_FUNCTION(type)                          \
  RUNTIME_FUNCTION(Runtime_##type##Equals) {                 \
    HandleScope scope(isolate);                              \
    DCHECK(args.length() == 2);                              \
    CONVERT_ARG_HANDLE_CHECKED(type, a, 0);                  \
    CONVERT_ARG_HANDLE_CHECKED(type, b, 1);                  \
    return Equals(a->get_lane(0), b->get_lane(0)) &&         \
                   Equals(a->get_lane(1), b->get_lane(1)) && \
                   Equals(a->get_lane(2), b->get_lane(2)) && \
                   Equals(a->get_lane(3), b->get_lane(3))    \
               ? Smi::FromInt(EQUAL)                         \
               : Smi::FromInt(NOT_EQUAL);                    \
  }

#define SIMD4_SAME_VALUE_FUNCTION(type)              \
  RUNTIME_FUNCTION(Runtime_##type##SameValue) {      \
    HandleScope scope(isolate);                      \
    DCHECK(args.length() == 2);                      \
    CONVERT_ARG_HANDLE_CHECKED(type, a, 0);          \
    CONVERT_ARG_HANDLE_CHECKED(type, b, 1);          \
    return isolate->heap()->ToBoolean(               \
        SameValue(a->get_lane(0), b->get_lane(0)) && \
        SameValue(a->get_lane(1), b->get_lane(1)) && \
        SameValue(a->get_lane(2), b->get_lane(2)) && \
        SameValue(a->get_lane(3), b->get_lane(3)));  \
  }

#define SIMD4_SAME_VALUE_ZERO_FUNCTION(type)             \
  RUNTIME_FUNCTION(Runtime_##type##SameValueZero) {      \
    HandleScope scope(isolate);                          \
    DCHECK(args.length() == 2);                          \
    CONVERT_ARG_HANDLE_CHECKED(type, a, 0);              \
    CONVERT_ARG_HANDLE_CHECKED(type, b, 1);              \
    return isolate->heap()->ToBoolean(                   \
        SameValueZero(a->get_lane(0), b->get_lane(0)) && \
        SameValueZero(a->get_lane(1), b->get_lane(1)) && \
        SameValueZero(a->get_lane(2), b->get_lane(2)) && \
        SameValueZero(a->get_lane(3), b->get_lane(3)));  \
  }

#define SIMD4_EXTRACT_LANE_FUNCTION(type) SIMD_EXTRACT_LANE_FUNCTION(type, 4)

#define SIMD4_FUNCTIONS(type)        \
  SIMD4_CREATE_FUNCTION(type)        \
  SIMD_CHECK_FUNCTION(type)          \
  SIMD4_EXTRACT_LANE_FUNCTION(type)  \
  SIMD4_EQUALS_FUNCTION(type)        \
  SIMD4_SAME_VALUE_FUNCTION(type)    \
  SIMD4_SAME_VALUE_ZERO_FUNCTION(type)


namespace v8 {
namespace internal {

namespace {

// Convert from Number object to float.
inline float NumberToFloat(Object* number) {
  return DoubleToFloat32(number->Number());
}


inline bool Equals(float x, float y) { return x == y; }

}  // namespace

SIMD4_FUNCTIONS(Float32x4)

}  // namespace internal
}  // namespace v8
