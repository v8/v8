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

#define CONVERT_SIMD_LANE_ARG_CHECKED(name, index, lanes) \
  CONVERT_INT32_ARG_CHECKED(name, index);                 \
  RUNTIME_ASSERT(name >= 0 && name < lanes);

#define SIMD_CREATE_NUMERIC_FUNCTION(type, lane_type, lane_count) \
  RUNTIME_FUNCTION(Runtime_Create##type) {                        \
    static const int kLaneCount = lane_count;                     \
    HandleScope scope(isolate);                                   \
    DCHECK(args.length() == kLaneCount);                          \
    lane_type lanes[kLaneCount];                                  \
    for (int i = 0; i < kLaneCount; i++) {                        \
      CONVERT_NUMBER_ARG_HANDLE_CHECKED(number, i);               \
      lanes[i] = ConvertNumber<lane_type>(number->Number());      \
    }                                                             \
    return *isolate->factory()->New##type(lanes);                 \
  }

#define SIMD_CREATE_BOOLEAN_FUNCTION(type, lane_count) \
  RUNTIME_FUNCTION(Runtime_Create##type) {             \
    static const int kLaneCount = lane_count;          \
    HandleScope scope(isolate);                        \
    DCHECK(args.length() == kLaneCount);               \
    bool lanes[kLaneCount];                            \
    for (int i = 0; i < kLaneCount; i++) {             \
      lanes[i] = args[i]->BooleanValue();              \
    }                                                  \
    return *isolate->factory()->New##type(lanes);      \
  }

#define SIMD_CHECK_FUNCTION(type)           \
  RUNTIME_FUNCTION(Runtime_##type##Check) { \
    HandleScope scope(isolate);             \
    CONVERT_ARG_HANDLE_CHECKED(type, a, 0); \
    return *a;                              \
  }

#define SIMD_EXTRACT_LANE_FUNCTION(type, lanes, extract_fn)    \
  RUNTIME_FUNCTION(Runtime_##type##ExtractLane) {              \
    HandleScope scope(isolate);                                \
    DCHECK(args.length() == 2);                                \
    CONVERT_ARG_HANDLE_CHECKED(type, a, 0);                    \
    CONVERT_SIMD_LANE_ARG_CHECKED(lane, 1, lanes);             \
    return *isolate->factory()->extract_fn(a->get_lane(lane)); \
  }

#define SIMD_REPLACE_NUMERIC_LANE_FUNCTION(type, lane_type, lane_count) \
  RUNTIME_FUNCTION(Runtime_##type##ReplaceLane) {                       \
    static const int kLaneCount = lane_count;                           \
    HandleScope scope(isolate);                                         \
    DCHECK(args.length() == 3);                                         \
    CONVERT_ARG_HANDLE_CHECKED(type, simd, 0);                          \
    CONVERT_SIMD_LANE_ARG_CHECKED(lane, 1, kLaneCount);                 \
    CONVERT_NUMBER_ARG_HANDLE_CHECKED(number, 2);                       \
    lane_type lanes[kLaneCount];                                        \
    for (int i = 0; i < kLaneCount; i++) {                              \
      lanes[i] = simd->get_lane(i);                                     \
    }                                                                   \
    lanes[lane] = ConvertNumber<lane_type>(number->Number());           \
    Handle<type> result = isolate->factory()->New##type(lanes);         \
    return *result;                                                     \
  }

#define SIMD_REPLACE_BOOLEAN_LANE_FUNCTION(type, lane_count)    \
  RUNTIME_FUNCTION(Runtime_##type##ReplaceLane) {               \
    static const int kLaneCount = lane_count;                   \
    HandleScope scope(isolate);                                 \
    DCHECK(args.length() == 3);                                 \
    CONVERT_ARG_HANDLE_CHECKED(type, simd, 0);                  \
    CONVERT_SIMD_LANE_ARG_CHECKED(lane, 1, kLaneCount);         \
    bool lanes[kLaneCount];                                     \
    for (int i = 0; i < kLaneCount; i++) {                      \
      lanes[i] = simd->get_lane(i);                             \
    }                                                           \
    lanes[lane] = args[2]->BooleanValue();                      \
    Handle<type> result = isolate->factory()->New##type(lanes); \
    return *result;                                             \
  }


namespace v8 {
namespace internal {

namespace {

// Functions to convert Numbers to SIMD component types.

template <typename T>
static T ConvertNumber(double number);


template <>
float ConvertNumber<float>(double number) {
  return DoubleToFloat32(number);
}


template <>
int32_t ConvertNumber<int32_t>(double number) {
  return DoubleToInt32(number);
}


template <>
int16_t ConvertNumber<int16_t>(double number) {
  return static_cast<int16_t>(DoubleToInt32(number));
}


template <>
int8_t ConvertNumber<int8_t>(double number) {
  return static_cast<int8_t>(DoubleToInt32(number));
}


bool Equals(Float32x4* a, Float32x4* b) {
  for (int i = 0; i < 4; i++) {
    if (a->get_lane(i) != b->get_lane(i)) return false;
  }
  return true;
}

}  // namespace


RUNTIME_FUNCTION(Runtime_IsSimdValue) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 1);
  return isolate->heap()->ToBoolean(args[0]->IsSimd128Value());
}


RUNTIME_FUNCTION(Runtime_SimdToObject) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 1);
  CONVERT_ARG_HANDLE_CHECKED(Simd128Value, value, 0);
  return *Object::ToObject(isolate, value).ToHandleChecked();
}


RUNTIME_FUNCTION(Runtime_SimdEquals) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 2);
  CONVERT_ARG_HANDLE_CHECKED(Simd128Value, a, 0);
  bool result = false;
  // args[1] is of unknown type.
  if (args[1]->IsSimd128Value()) {
    Simd128Value* b = Simd128Value::cast(args[1]);
    if (a->map()->instance_type() == b->map()->instance_type()) {
      if (a->IsFloat32x4()) {
        result = Equals(Float32x4::cast(*a), Float32x4::cast(b));
      } else {
        result = a->BitwiseEquals(b);
      }
    }
  }
  return Smi::FromInt(result ? EQUAL : NOT_EQUAL);
}


RUNTIME_FUNCTION(Runtime_SimdSameValue) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 2);
  CONVERT_ARG_HANDLE_CHECKED(Simd128Value, a, 0);
  bool result = false;
  // args[1] is of unknown type.
  if (args[1]->IsSimd128Value()) {
    Simd128Value* b = Simd128Value::cast(args[1]);
    if (a->map()->instance_type() == b->map()->instance_type()) {
      if (a->IsFloat32x4()) {
        result = Float32x4::cast(*a)->SameValue(Float32x4::cast(b));
      } else {
        result = a->BitwiseEquals(b);
      }
    }
  }
  return isolate->heap()->ToBoolean(result);
}


RUNTIME_FUNCTION(Runtime_SimdSameValueZero) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 2);
  CONVERT_ARG_HANDLE_CHECKED(Simd128Value, a, 0);
  bool result = false;
  // args[1] is of unknown type.
  if (args[1]->IsSimd128Value()) {
    Simd128Value* b = Simd128Value::cast(args[1]);
    if (a->map()->instance_type() == b->map()->instance_type()) {
      if (a->IsFloat32x4()) {
        result = Float32x4::cast(*a)->SameValueZero(Float32x4::cast(b));
      } else {
        result = a->BitwiseEquals(b);
      }
    }
  }
  return isolate->heap()->ToBoolean(result);
}


SIMD_CREATE_NUMERIC_FUNCTION(Float32x4, float, 4)
SIMD_CREATE_NUMERIC_FUNCTION(Int32x4, int32_t, 4)
SIMD_CREATE_BOOLEAN_FUNCTION(Bool32x4, 4)
SIMD_CREATE_NUMERIC_FUNCTION(Int16x8, int16_t, 8)
SIMD_CREATE_BOOLEAN_FUNCTION(Bool16x8, 8)
SIMD_CREATE_NUMERIC_FUNCTION(Int8x16, int8_t, 16)
SIMD_CREATE_BOOLEAN_FUNCTION(Bool8x16, 16)


SIMD_CHECK_FUNCTION(Float32x4)
SIMD_CHECK_FUNCTION(Int32x4)
SIMD_CHECK_FUNCTION(Bool32x4)
SIMD_CHECK_FUNCTION(Int16x8)
SIMD_CHECK_FUNCTION(Bool16x8)
SIMD_CHECK_FUNCTION(Int8x16)
SIMD_CHECK_FUNCTION(Bool8x16)


SIMD_EXTRACT_LANE_FUNCTION(Float32x4, 4, NewNumber)
SIMD_EXTRACT_LANE_FUNCTION(Int32x4, 4, NewNumber)
SIMD_EXTRACT_LANE_FUNCTION(Bool32x4, 4, ToBoolean)
SIMD_EXTRACT_LANE_FUNCTION(Int16x8, 8, NewNumber)
SIMD_EXTRACT_LANE_FUNCTION(Bool16x8, 8, ToBoolean)
SIMD_EXTRACT_LANE_FUNCTION(Int8x16, 16, NewNumber)
SIMD_EXTRACT_LANE_FUNCTION(Bool8x16, 16, ToBoolean)


RUNTIME_FUNCTION(Runtime_Int16x8UnsignedExtractLane) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 2);
  CONVERT_ARG_HANDLE_CHECKED(Int16x8, a, 0);
  CONVERT_SIMD_LANE_ARG_CHECKED(lane, 1, 8);
  return *isolate->factory()->NewNumber(bit_cast<uint16_t>(a->get_lane(lane)));
}


RUNTIME_FUNCTION(Runtime_Int8x16UnsignedExtractLane) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 2);
  CONVERT_ARG_HANDLE_CHECKED(Int8x16, a, 0);
  CONVERT_SIMD_LANE_ARG_CHECKED(lane, 1, 16);
  return *isolate->factory()->NewNumber(bit_cast<uint8_t>(a->get_lane(lane)));
}


SIMD_REPLACE_NUMERIC_LANE_FUNCTION(Float32x4, float, 4)
SIMD_REPLACE_NUMERIC_LANE_FUNCTION(Int32x4, int32_t, 4)
SIMD_REPLACE_BOOLEAN_LANE_FUNCTION(Bool32x4, 4)
SIMD_REPLACE_NUMERIC_LANE_FUNCTION(Int16x8, int16_t, 8)
SIMD_REPLACE_BOOLEAN_LANE_FUNCTION(Bool16x8, 8)
SIMD_REPLACE_NUMERIC_LANE_FUNCTION(Int8x16, int8_t, 16)
SIMD_REPLACE_BOOLEAN_LANE_FUNCTION(Bool8x16, 16)
}  // namespace internal
}  // namespace v8
