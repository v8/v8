// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-temporal-objects.h"

#include <algorithm>
#include <optional>
#include <set>

#include "src/common/globals.h"
#include "src/date/date.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/numbers/conversions-inl.h"
#include "src/objects/js-objects-inl.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-temporal-helpers.h"
#include "src/objects/js-temporal-objects-inl.h"
#include "src/objects/managed-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/option-utils.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/string-set.h"
#include "src/strings/string-builder-inl.h"
#include "src/temporal/temporal-parser.h"
#include "temporal_rs/I128Nanoseconds.hpp"
#include "temporal_rs/Unit.hpp"
#ifdef V8_INTL_SUPPORT
#include "src/objects/intl-objects.h"
#include "src/objects/js-date-time-format.h"
#include "unicode/calendar.h"
#include "unicode/unistr.h"
#endif  // V8_INTL_SUPPORT

namespace v8::internal {

namespace {

// Shorten enums with `using`
using temporal_rs::RoundingMode;
using temporal_rs::Unit;

/**
 * This header declare the Abstract Operations defined in the
 * Temporal spec with the enum and struct for them.
 */

// Struct

template <typename T>
using TemporalResult = diplomat::result<T, temporal_rs::TemporalError>;
template <typename T>
using TemporalAllocatedResult = TemporalResult<std::unique_ptr<T>>;


using temporal::DurationRecord;
using temporal::TimeDurationRecord;

// Options

// https://tc39.es/proposal-temporal/#sec-temporal-totemporaldisambiguation
enum class Disambiguation { kCompatible, kEarlier, kLater, kReject };

// https://tc39.es/proposal-temporal/#sec-temporal-toshowcalendaroption
enum class ShowCalendar { kAuto, kAlways, kNever };

// https://tc39.es/proposal-temporal/#table-temporal-unsigned-rounding-modes
enum class UnsignedRoundingMode {
  kInfinity,
  kZero,
  kHalfInfinity,
  kHalfZero,
  kHalfEven
};

// https://tc39.es/proposal-temporal/#sec-temporal-GetTemporalUnit
enum class UnitGroup {
  kDate,
  kTime,
  kDateTime,
};

// https://tc39.es/proposal-temporal/#sec-temporal-totemporaltimerecord
enum Completeness {
  kComplete,
  kPartial,
};

// For internal temporal_rs errors that should not occur
#define NEW_TEMPORAL_INTERNAL_ERROR() \
  NewTypeError(MessageTemplate::kTemporalRsError)

#define ORDINARY_CREATE_FROM_CONSTRUCTOR(obj, target, new_target, T)           \
  DirectHandle<JSReceiver> new_target_receiver = Cast<JSReceiver>(new_target); \
  DirectHandle<Map> map;                                                       \
  ASSIGN_RETURN_ON_EXCEPTION(                                                  \
      isolate, map,                                                            \
      JSFunction::GetDerivedMap(isolate, target, new_target_receiver));        \
  DirectHandle<T> object =                                                     \
      Cast<T>(isolate->factory()->NewFastOrSlowJSObjectFromMap(map));

#define THROW_INVALID_RANGE(T) \
  THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());

#define CONSTRUCTOR(name)                                                      \
  DirectHandle<JSFunction>(                                                    \
      Cast<JSFunction>(                                                        \
          isolate->context()->native_context()->temporal_##name##_function()), \
      isolate)

// Helper function to split the string into its two constituent encodings
// and call two different functions depending on the encoding
template <typename RetVal>
RetVal HandleStringEncodings(
    Isolate* isolate, DirectHandle<String> string,
    std::function<RetVal(std::string_view)> utf8_fn,
    std::function<RetVal(std::u16string_view)> utf16_fn) {
  string = String::Flatten(isolate, string);
  DisallowGarbageCollection no_gc;
  auto flat = string->GetFlatContent(no_gc);
  if (flat.IsOneByte()) {
    auto content = flat.ToOneByteVector();
    // reinterpret_cast since std string types accept signed chars
    std::string_view view(reinterpret_cast<const char*>(content.data()),
                          content.size());
    return utf8_fn(view);

  } else {
    auto content = flat.ToUC16Vector();
    std::u16string_view view(reinterpret_cast<const char16_t*>(content.data()),
                             content.size());
    return utf16_fn(view);
  }
}

// Take a Rust Result and turn it into a Maybe, suitable for use
// with error handling macros.
//
// Note that a lot of the types returned by Rust code prefer
// move semantics, try using MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE
template <typename ContainedValue>
Maybe<ContainedValue> ExtractRustResult(
    Isolate* isolate, TemporalResult<ContainedValue>&& rust_result) {
  if (rust_result.is_err()) {
    auto err = std::move(rust_result).err().value();
    switch (err.kind) {
      case temporal_rs::ErrorKind::Type:
        THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                     NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                     Nothing<ContainedValue>());
        break;
      case temporal_rs::ErrorKind::Range:
        THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                     NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                     Nothing<ContainedValue>());
        break;
      case temporal_rs::ErrorKind::Syntax:
      case temporal_rs::ErrorKind::Assert:
      case temporal_rs::ErrorKind::Generic:
      default:
        // These cases shouldn't happen; the spec doesn't currently trigger
        // these errors
        THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INTERNAL_ERROR(),
                                     Nothing<ContainedValue>());
    }
    return Nothing<ContainedValue>();
  }
  return Just(std::move(rust_result).ok().value());
}

// Helper function to construct a JSType that wraps a RustType (infallible)
template <typename JSType>
MaybeDirectHandle<JSType> ConstructRustWrappingType(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target,
    std::unique_ptr<typename JSType::RustType>&& rust_value) {
  // Managed requires shared ownership
  std::shared_ptr<typename JSType::RustType> rust_shared =
      std::move(rust_value);

  DirectHandle<Managed<typename JSType::RustType>> managed =
      Managed<typename JSType::RustType>::From(isolate, 0, rust_shared);

  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target, JSType)
  object->initialize_with_wrapped_rust_value(*managed);
  return object;
}

// Helper function to construct a JSType that wraps a RustType (fallible)
template <typename JSType>
MaybeDirectHandle<JSType> ConstructRustWrappingType(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target,
    TemporalResult<std::unique_ptr<typename JSType::RustType>>&& rust_result) {
  std::unique_ptr<typename JSType::RustType> rust_value = nullptr;

  MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
      isolate, rust_value, ExtractRustResult(isolate, std::move(rust_result)),
      MaybeDirectHandle<JSType>());

  return ConstructRustWrappingType<JSType>(isolate, target, new_target,
                                           std::move(rust_value));
}

}  // namespace

// Paired with DECL_ACCESSORS_FOR_RUST_WRAPPER
// Can be omitted and overridden if needed.
#define DEFINE_ACCESSORS_FOR_RUST_WRAPPER(field, JSType)  \
  inline void JSType::initialize_with_wrapped_rust_value( \
      Tagged<Managed<JSType::RustType>> handle) {         \
    this->set_##field(handle);                            \
  }

DEFINE_ACCESSORS_FOR_RUST_WRAPPER(instant, JSTemporalInstant)
DEFINE_ACCESSORS_FOR_RUST_WRAPPER(duration, JSTemporalDuration)
DEFINE_ACCESSORS_FOR_RUST_WRAPPER(date, JSTemporalPlainDate)
DEFINE_ACCESSORS_FOR_RUST_WRAPPER(date_time, JSTemporalPlainDateTime)
DEFINE_ACCESSORS_FOR_RUST_WRAPPER(month_day, JSTemporalPlainMonthDay)
DEFINE_ACCESSORS_FOR_RUST_WRAPPER(time, JSTemporalPlainTime)
DEFINE_ACCESSORS_FOR_RUST_WRAPPER(year_month, JSTemporalPlainYearMonth)
DEFINE_ACCESSORS_FOR_RUST_WRAPPER(zoned_date_time, JSTemporalZonedDateTime)

namespace temporal {

// ====== Numeric conversions ======

// https://tc39.es/proposal-temporal/#sec-temporal-tointegerifintegral
Maybe<double> ToIntegerIfIntegral(Isolate* isolate,
                                  DirectHandle<Object> argument) {
  // 1. Let number be ? ToNumber(argument).
  DirectHandle<Number> number;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, number, Object::ToNumber(isolate, argument), Nothing<double>());
  double number_double = Object::NumberValue(*number);
  // 2. If number is not an integral Number, throw a RangeError exception.
  if (!std::isfinite(number_double) ||
      nearbyint(number_double) != number_double) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(), Nothing<double>());
  }
  // 3. Return ℝ(number).
  return Just(number_double);
}

// https://tc39.es/proposal-temporal/#sec-temporal-tointegerwithtruncation
Maybe<double> ToIntegerWithTruncation(Isolate* isolate,
                                      DirectHandle<Object> argument) {
  // 1. Let number be ? ToNumber(argument).
  double number;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, number, Object::IntegerValue(isolate, argument),
      Nothing<double>());
  // 2. If number is NaN, +∞𝔽 or -∞𝔽, throw a RangeError exception.
  if (!std::isfinite(number)) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(), Nothing<double>());
  }

  // 3. Return truncate(number).
  return Just(number);
}

// https://tc39.es/proposal-temporal/#sec-temporal-topositiveintegerwithtruncation
Maybe<double> ToPositiveIntegerWithTruncation(Isolate* isolate,
                                              DirectHandle<Object> argument) {
  // 1. Let integer be ?ToIntegerWithTruncation(argument).
  double integer;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, integer, ToIntegerWithTruncation(isolate, argument),
      Nothing<double>());
  // 2. If integer is ≤ 0, throw a RangeError exception
  if (integer <= 0) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(), Nothing<double>());
  }

  // 3. Return integer.
  return Just(integer);
}
// temporal_rs currently accepts integer types in cases where
// the spec uses a double (and bounds-checks later). This helper
// allows safely converting objects to some known integer type.
//
// TODO(manishearth) This helper should be removed when it is unnecessary.
// Tracked in https://github.com/boa-dev/temporal/issues/334
template <typename IntegerType>
Maybe<IntegerType> ToIntegerTypeWithTruncation(Isolate* isolate,
                                               DirectHandle<Object> argument) {
  double d;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, d, ToIntegerWithTruncation(isolate, argument),
      Nothing<IntegerType>());
  if (d < std::numeric_limits<IntegerType>::min() ||
      d > std::numeric_limits<IntegerType>::max()) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<IntegerType>());
  }

  return Just(static_cast<IntegerType>(d));
}

// Same as ToIntegerTypeWithTruncation but for ToPositiveIntegerWithTruncation
//
// TODO(manishearth) This helper should be removed when it is unnecessary.
// Tracked in https://github.com/boa-dev/temporal/issues/334
template <typename IntegerType>
Maybe<IntegerType> ToPositiveIntegerTypeWithTruncation(
    Isolate* isolate, DirectHandle<Object> argument) {
  double d;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, d, ToPositiveIntegerWithTruncation(isolate, argument),
      Nothing<IntegerType>());
  if (d < std::numeric_limits<IntegerType>::min() ||
      d > std::numeric_limits<IntegerType>::max()) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<IntegerType>());
  }

  return Just(static_cast<IntegerType>(d));
}

bool IsValidTime(double hour, double minute, double second, double millisecond,
                 double microsecond, double nanosecond) {
  // 1. If hour < 0 or hour > 23, then
  // a. Return false.
  if (hour < 0 || hour > 23) {
    return false;
  }
  // 2. If minute < 0 or minute > 59, then
  // a. Return false.
  if (minute < 0 || minute > 59) {
    return false;
  }
  // 3. If second < 0 or second > 59, then
  // a. Return false.
  if (second < 0 || second > 59) {
    return false;
  }
  // 4. If millisecond < 0 or millisecond > 999, then
  // a. Return false.

  if (millisecond < 0 || millisecond > 999) {
    return false;
  }
  // 5. If microsecond < 0 or microsecond > 999, then
  // a. Return false.
  if (microsecond < 0 || microsecond > 999) {
    return false;
  }
  // 6. If nanosecond < 0 or nanosecond > 999, then
  // a. Return false.
  if (nanosecond < 0 || nanosecond > 999) {
    return false;
  }
  // 7. Return true.
  return true;
}

// https://tc39.es/proposal-temporal/#sec-temporal-isodaysinmonth
int8_t ISODaysInMonth(int32_t year, uint8_t month) {
  switch (month) {
    // 1. If month is 1, 3, 5, 7, 8, 10, or 12, return 31.
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
      return 31;
    // 4. Return 28 + MathematicalInLeapYear(EpochTimeForYear(year)).
    case 2:
      if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
        return 29;
      } else {
        return 28;
      }
    // 2. If month is 4, 6, 9, or 11, return 30.
    default:
      return 30;
  }
}

// https://tc39.es/proposal-temporal/#sec-temporal-isvalidisodate
bool IsValidIsoDate(double year, double month, double day) {
  // 1. If month < 1 or month > 12, then
  if (month < 1 || month > 12) {
    // a. Return false.
    return false;
  }

  // This check is technically needed later when we check if things are in the
  // Temporal range, but we do it now to ensure we can safely cast to int32_t
  // before passing to Rust See https://github.com/boa-dev/temporal/issues/334.
  if (year < std::numeric_limits<int32_t>::min() ||
      year > std::numeric_limits<int32_t>::max()) {
    return false;
  }

  // IsValidIsoDate does not care about years that are "out of Temporal range",
  // that gets handled later.
  int32_t year_int = static_cast<int32_t>(year);
  uint8_t month_int = static_cast<uint8_t>(month);
  // 2. Let daysInMonth be ISODaysInMonth(year, month).
  // 3. If day < 1 or day > daysInMonth, then
  if (day < 1 || day > ISODaysInMonth(year_int, month_int)) {
    // a. Return false.
    return false;
  }

  // 4. Return true.
  return true;
}

// ====== Options getters ======

// https://tc39.es/proposal-temporal/#sec-temporal-tomonthcode
Maybe<std::string> ToMonthCode(Isolate* isolate,
                               DirectHandle<Object> argument) {
  // 1. Let monthCode be ?ToPrimitive(argument, string).
  DirectHandle<Object> mc_prim;
  if (IsJSReceiver(*argument)) {
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, mc_prim,
        JSReceiver::ToPrimitive(isolate, Cast<JSReceiver>(argument),
                                ToPrimitiveHint::kString),
        Nothing<std::string>());
  } else {
    mc_prim = argument;
  }

  // 2. If monthCode is not a String, throw a TypeError exception.
  if (!IsString(*mc_prim)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<std::string>());
  }

  auto month_code = Cast<String>(*mc_prim)->ToStdString();

  // 3. If the length of monthCode is not 3 or 4, throw a RangeError exception.
  if (month_code.size() != 3 && month_code.size() != 4) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<std::string>());
  }

  // 4. If the first code unit of monthCode is not 0x004D (LATIN CAPITAL LETTER
  // M), throw a RangeError exception.
  if (month_code[0] != 'M') {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<std::string>());
  }
  // 5. If the second code unit of monthCode is not in the inclusive interval
  // from 0x0030 (DIGIT ZERO) to 0x0039 (DIGIT NINE), throw a RangeError
  // exception.
  if (month_code[1] < '0' || month_code[1] > '9') {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<std::string>());
  }
  // 6. If the third code unit of monthCode is not in the inclusive interval
  // from 0x0030 (DIGIT ZERO) to 0x0039 (DIGIT NINE), throw a RangeError
  // exception.
  if (month_code[2] < '0' || month_code[2] > '9') {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<std::string>());
  }
  // 7. If the length of monthCode is 4 and the fourth code unit of monthCode is
  // not 0x004C (LATIN CAPITAL LETTER L), throw a RangeError exception.
  if (month_code.size() == 4 && month_code[3] != 'L') {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<std::string>());
  }
  // 8. Let monthCodeDigits be the substring of monthCode from 1 to 3.
  // 9. Let monthCodeInteger be ℝ(StringToNumber(monthCodeDigits)).
  // 10. If monthCodeInteger is 0 and the length of monthCode is not 4, throw a
  // RangeError exception.
  if (month_code[1] == '0' && month_code[2] == '0' && month_code.size() != 4) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<std::string>());
  }
  // 11. Return monthCode.
  return Just(month_code);
}

// https://tc39.es/proposal-temporal/#sec-temporal-totemporaloverflow
// Also handles the undefined check from GetOptionsObject
Maybe<temporal_rs::ArithmeticOverflow> ToTemporalOverflowHandleUndefined(
    Isolate* isolate, DirectHandle<Object> options, const char* method_name) {
  // Default is "constrain"
  if (IsUndefined(*options))
    return Just(temporal_rs::ArithmeticOverflow(
        temporal_rs::ArithmeticOverflow::Constrain));
  if (!IsJSReceiver(*options)) {
    // (GetOptionsObject) 3. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<temporal_rs::ArithmeticOverflow>());
  }
  // 2. Return ? GetOption(options, "overflow", « String », « "constrain",
  // "reject" », "constrain").
  return GetStringOption<temporal_rs::ArithmeticOverflow>(
      isolate, Cast<JSReceiver>(options), "overflow", method_name,
      std::to_array<const std::string_view>({"constrain", "reject"}),
      std::to_array<temporal_rs::ArithmeticOverflow>(
          {temporal_rs::ArithmeticOverflow::Constrain,
           temporal_rs::ArithmeticOverflow::Reject}),
      temporal_rs::ArithmeticOverflow::Constrain);
}

// https://tc39.es/proposal-temporal/#sec-temporal-gettemporaldisambiguationoption
// Also handles the undefined check from GetOptionsObject
Maybe<temporal_rs::Disambiguation>
GetTemporalDisambiguationOptionHandleUndefined(Isolate* isolate,
                                               DirectHandle<Object> options,
                                               const char* method_name) {
  // Default is "compatible"
  if (IsUndefined(*options)) {
    return Just(
        temporal_rs::Disambiguation(temporal_rs::Disambiguation::Reject));
  }
  if (!IsJSReceiver(*options)) {
    // (GetOptionsObject) 3. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<temporal_rs::Disambiguation>());
  }
  // 1. Let stringValue be ?GetOption(options, "disambiguation", string, «
  // "compatible", "earlier", "later", "reject" », "compatible").
  return GetStringOption<temporal_rs::Disambiguation>(
      isolate, Cast<JSReceiver>(options), "overflow", method_name,
      std::to_array<const std::string_view>(
          {"compatible", "earlier", "later", "reject"}),
      std::to_array<temporal_rs::Disambiguation>({
          temporal_rs::Disambiguation::Compatible,
          temporal_rs::Disambiguation::Earlier,
          temporal_rs::Disambiguation::Later,
          temporal_rs::Disambiguation::Reject,
      }),
      temporal_rs::Disambiguation::Compatible);
}

// https://tc39.es/proposal-temporal/#sec-temporal-gettemporaloffsetoption
// Also handles the undefined check from GetOptionsObject
Maybe<temporal_rs::OffsetDisambiguation> GetTemporalOffsetOptionHandleUndefined(
    Isolate* isolate, DirectHandle<Object> options,
    temporal_rs::OffsetDisambiguation fallback, const char* method_name) {
  // Default is fallback
  if (IsUndefined(*options)) {
    return Just(fallback);
  }
  if (!IsJSReceiver(*options)) {
    // (GetOptionsObject) 3. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<temporal_rs::OffsetDisambiguation>());
  }
  // 5. Let stringValue be ? GetOption(options, "offset", string, « "prefer",
  // "use", "ignore", "reject" », stringFallback).
  return GetStringOption<temporal_rs::OffsetDisambiguation>(
      isolate, Cast<JSReceiver>(options), "overflow", method_name,
      std::to_array<const std::string_view>(
          {"prefer", "use", "ignore", "reject"}),
      std::to_array<temporal_rs::OffsetDisambiguation>({
          temporal_rs::OffsetDisambiguation::Prefer,
          temporal_rs::OffsetDisambiguation::Use,
          temporal_rs::OffsetDisambiguation::Ignore,
          temporal_rs::OffsetDisambiguation::Reject,
      }),
      fallback);
}

// https://tc39.es/proposal-temporal/#sec-temporal-gettemporalfractionalseconddigitsoption
Maybe<temporal_rs::Precision> GetTemporalFractionalSecondDigitsOption(
    Isolate* isolate, DirectHandle<JSReceiver> normalized_options,
    const char* method_name) {
  auto auto_val =
      temporal_rs::Precision{.is_minute = false, .precision = std::nullopt};

  Factory* factory = isolate->factory();
  // 1. Let digitsValue be ?Get(options, "fractionalSecondDigits").
  DirectHandle<Object> digits_val;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, digits_val,
      JSReceiver::GetProperty(isolate, normalized_options,
                              factory->fractionalSecondDigits_string()),
      Nothing<temporal_rs::Precision>());

  // 2. If digitsValue is undefined, return auto.
  if (IsUndefined(*digits_val)) {
    return Just(auto_val);
  }

  // 3. If digitsValue is not a Number, then
  if (!IsNumber(*digits_val)) {
    DirectHandle<String> string;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, string,
                                     Object::ToString(isolate, digits_val),
                                     Nothing<temporal_rs::Precision>());
    //  a. If ? ToString(digitsValue) is not "auto", throw a RangeError
    //  exception.
    if (!String::Equals(isolate, string, factory->auto_string())) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                        factory->fractionalSecondDigits_string()),
          Nothing<temporal_rs::Precision>());
    }
    //  b. Return auto.
    return Just(auto_val);
  }
  // 4. If digitsValue is NaN, +∞𝔽, or -∞𝔽, throw a RangeError exception.
  auto digits_num = Cast<Number>(*digits_val);
  auto digits_float = Object::NumberValue(digits_num);
  if (std::isnan(digits_float) || std::isinf(digits_float)) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                      factory->fractionalSecondDigits_string()),
        Nothing<temporal_rs::Precision>());
  }
  // 5. Let digitCount be floor(ℝ(digitsValue)).
  int64_t digit_count = std::floor(Object::NumberValue(digits_num));
  // 6. If digitCount < 0 or digitCount > 9, throw a RangeError exception.
  if (digit_count < 0 || digit_count > 9) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                      factory->fractionalSecondDigits_string()),
        Nothing<temporal_rs::Precision>());
  }

  return Just(
      temporal_rs::Precision{.is_minute = false, .precision = digit_count});
}

// https://tc39.es/proposal-temporal/#sec-temporal-GetTemporalUnitvaluedoption
//
// Utility function for getting Unit options off of an object
//
// Temporal distinguishes between unset units and Auto, even when
// "default_is_required=false", so we return a Maybe<optional>, with the outer
// Maybe signaling error states, and the inner optional signaling absence, which
// can directly be consumed by temporal_rs
//
// # extraValues
// In the spec text, the extraValues is defined as an optional argument of
// "a List of ECMAScript language values". Most of the caller does not pass in
// value for extraValues, which is represented by the default
// Unit::NotPresent. For the three places in the spec text calling
// GetTemporalUnit with an extraValues argument:
// << "day" >> is passed in as in the algorithm of
//   Temporal.PlainDateTime.prototype.round() and
//   Temporal.ZonedDateTime.prototype.round();
// << "auto" >> is passed in as in the algorithm of
// Temporal.Duration.prototype.round().
// Therefore we can simply use a Unit of three possible value, the default
// Unit::NotPresent, Unit::Day, and
// Unit::Auto to cover all the possible value for extraValues.
Maybe<std::optional<Unit>> GetTemporalUnit(
    Isolate* isolate, DirectHandle<JSReceiver> normalized_options,
    const char* key, UnitGroup unit_group, std::optional<Unit> default_value,
    bool default_is_required, const char* method_name,
    std::optional<Unit> extra_values = std::nullopt) {
  std::span<const std::string_view> str_values;
  std::span<const std::optional<Unit::Value>> enum_values;
  switch (unit_group) {
    case UnitGroup::kDate:
      if (default_value == Unit::Auto || extra_values == Unit::Auto) {
        static auto strs = std::to_array<const std::string_view>(
            {"year", "month", "week", "day", "auto", "years", "months", "weeks",
             "days"});
        static auto enums = std::to_array<const std::optional<Unit::Value>>(
            {Unit::Year, Unit::Month, Unit::Week, Unit::Day, Unit::Auto,
             Unit::Year, Unit::Month, Unit::Week, Unit::Day});
        str_values = strs;
        enum_values = enums;
      } else {
        DCHECK(default_value == std::nullopt || default_value == Unit::Year ||
               default_value == Unit::Month || default_value == Unit::Week ||
               default_value == Unit::Day);
        static auto strs = std::to_array<const std::string_view>(
            {"year", "month", "week", "day", "years", "months", "weeks",
             "days"});
        static auto enums = std::to_array<const std::optional<Unit::Value>>(
            {Unit::Year, Unit::Month, Unit::Week, Unit::Day, Unit::Year,
             Unit::Month, Unit::Week, Unit::Day});
        str_values = strs;
        enum_values = enums;
      }
      break;
    case UnitGroup::kTime:
      if (default_value == Unit::Auto || extra_values == Unit::Auto) {
        static auto strs = std::to_array<const std::string_view>(
            {"hour", "minute", "second", "millisecond", "microsecond",
             "nanosecond", "auto", "hours", "minutes", "seconds",
             "milliseconds", "microseconds", "nanoseconds"});
        static auto enums = std::to_array<const std::optional<Unit::Value>>(
            {Unit::Hour, Unit::Minute, Unit::Second, Unit::Millisecond,
             Unit::Microsecond, Unit::Nanosecond, Unit::Auto, Unit::Hour,
             Unit::Minute, Unit::Second, Unit::Millisecond, Unit::Microsecond,
             Unit::Nanosecond});
        str_values = strs;
        enum_values = enums;
      } else if (default_value == Unit::Day || extra_values == Unit::Day) {
        static auto strs = std::to_array<const std::string_view>(
            {"hour", "minute", "second", "millisecond", "microsecond",
             "nanosecond", "day", "hours", "minutes", "seconds", "milliseconds",
             "microseconds", "nanoseconds", "days"});
        static auto enums = std::to_array<const std::optional<Unit::Value>>(
            {Unit::Hour, Unit::Minute, Unit::Second, Unit::Millisecond,
             Unit::Microsecond, Unit::Nanosecond, Unit::Day, Unit::Hour,
             Unit::Minute, Unit::Second, Unit::Millisecond, Unit::Microsecond,
             Unit::Nanosecond, Unit::Day});
        str_values = strs;
        enum_values = enums;
      } else {
        DCHECK(default_value == std::nullopt || default_value == Unit::Hour ||
               default_value == Unit::Minute || default_value == Unit::Second ||
               default_value == Unit::Millisecond ||
               default_value == Unit::Microsecond ||
               default_value == Unit::Nanosecond);
        static auto strs = std::to_array<const std::string_view>(
            {"hour", "minute", "second", "millisecond", "microsecond",
             "nanosecond", "hours", "minutes", "seconds", "milliseconds",
             "microseconds", "nanoseconds"});
        static auto enums = std::to_array<const std::optional<Unit::Value>>(
            {Unit::Hour, Unit::Minute, Unit::Second, Unit::Millisecond,
             Unit::Microsecond, Unit::Nanosecond, Unit::Hour, Unit::Minute,
             Unit::Second, Unit::Millisecond, Unit::Microsecond,
             Unit::Nanosecond});
        str_values = strs;
        enum_values = enums;
      }
      break;
    case UnitGroup::kDateTime:
      if (default_value == Unit::Auto || extra_values == Unit::Auto) {
        static auto strs = std::to_array<const std::string_view>(
            {"year",        "month",      "week",         "day",
             "hour",        "minute",     "second",       "millisecond",
             "microsecond", "nanosecond", "auto",         "years",
             "months",      "weeks",      "days",         "hours",
             "minutes",     "seconds",    "milliseconds", "microseconds",
             "nanoseconds"});
        static auto enums = std::to_array<const std::optional<Unit::Value>>(
            {Unit::Year,        Unit::Month,       Unit::Week,
             Unit::Day,         Unit::Hour,        Unit::Minute,
             Unit::Second,      Unit::Millisecond, Unit::Microsecond,
             Unit::Nanosecond,  Unit::Auto,        Unit::Year,
             Unit::Month,       Unit::Week,        Unit::Day,
             Unit::Hour,        Unit::Minute,      Unit::Second,
             Unit::Millisecond, Unit::Microsecond, Unit::Nanosecond});
        str_values = strs;
        enum_values = enums;
      } else {
        static auto strs = std::to_array<const std::string_view>(
            {"year",        "month",        "week",         "day",
             "hour",        "minute",       "second",       "millisecond",
             "microsecond", "nanosecond",   "years",        "months",
             "weeks",       "days",         "hours",        "minutes",
             "seconds",     "milliseconds", "microseconds", "nanoseconds"});
        static auto enums = std::to_array<const std::optional<Unit::Value>>(
            {Unit::Year,        Unit::Month,       Unit::Week,
             Unit::Day,         Unit::Hour,        Unit::Minute,
             Unit::Second,      Unit::Millisecond, Unit::Microsecond,
             Unit::Nanosecond,  Unit::Year,        Unit::Month,
             Unit::Week,        Unit::Day,         Unit::Hour,
             Unit::Minute,      Unit::Second,      Unit::Millisecond,
             Unit::Microsecond, Unit::Nanosecond});
        str_values = strs;
        enum_values = enums;
      }
      break;
  }

  // 4. If default is required, then
  if (default_is_required) default_value = std::nullopt;
  // a. Let defaultValue be undefined.
  // 5. Else,
  // a. Let defaultValue be default.
  // b. If defaultValue is not undefined and singularNames does not contain
  // defaultValue, then i. Append defaultValue to singularNames.

  // 9. Let value be ? GetOption(normalizedOptions, key, "string",
  // allowedValues, defaultValue).
  std::optional<Unit::Value> value;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value,
      GetStringOption<std::optional<Unit::Value>>(isolate, normalized_options,
                                                  key, method_name, str_values,
                                                  enum_values, default_value),
      Nothing<std::optional<Unit>>());

  // 10. If value is undefined and default is required, throw a RangeError
  // exception.
  if (default_is_required && value == std::nullopt) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(
            MessageTemplate::kValueOutOfRange,
            isolate->factory()->undefined_value(),
            isolate->factory()->NewStringFromAsciiChecked(method_name),
            isolate->factory()->NewStringFromAsciiChecked(key)),
        Nothing<std::optional<Unit>>());
  }
  // 12. Return value.
  if (value.has_value()) {
    return Just<std::optional<Unit>>((Unit)value.value());
  } else {
    return Just<std::optional<Unit>>(std::nullopt);
  }
}

// https://tc39.es/proposal-temporal/#sec-temporal-canonicalizecalendar
Maybe<temporal_rs::AnyCalendarKind> CanonicalizeCalendar(
    Isolate* isolate, DirectHandle<String> calendar) {
  std::string s = calendar->ToStdString();
  // 2. If calendars does not contain the ASCII-lowercase of id, throw a
  // RangeError exception.
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);

  auto cal = temporal_rs::AnyCalendarKind::get_for_str(s);

  if (!cal.has_value()) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<temporal_rs::AnyCalendarKind>());
  }
  // Other steps unnecessary, we're not storing these as -u- values but rather
  // as enums.
  return Just(cal.value());
}

// https://tc39.es/proposal-temporal/#sec-temporal-getroundingincrementoption
Maybe<uint32_t> GetRoundingIncrementOption(
    Isolate* isolate, DirectHandle<JSReceiver> normalized_options) {
  DirectHandle<Object> value;

  // 1. Let value be ? Get(options, "roundingIncrement").
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value,
      JSReceiver::GetProperty(isolate, normalized_options,
                              isolate->factory()->roundingIncrement_string()),
      Nothing<uint32_t>());
  // 2. If value is undefined, return 1𝔽.
  if (IsUndefined(*value)) {
    return Just(static_cast<uint32_t>(1));
  }

  // 3. Let integerIncrement be ? ToIntegerWithTruncation(value).
  double integer_increment;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, integer_increment, ToIntegerWithTruncation(isolate, value),
      Nothing<uint32_t>());

  // 4. If integerIncrement < 1 or integerIncrement > 10**9, throw a RangeError
  // exception.
  if (integer_increment < 1 || integer_increment > 1e9) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(), Nothing<uint32_t>());
  }

  return Just(static_cast<uint32_t>(integer_increment));
}

// sec-temporal-getroundingmodeoption
Maybe<RoundingMode> GetRoundingModeOption(Isolate* isolate,
                                          DirectHandle<JSReceiver> options,
                                          RoundingMode fallback,
                                          const char* method_name) {
  // 1. Return ? GetOption(normalizedOptions, "roundingMode", "string", «
  // "ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor", "halfExpand",
  // "halfTrunc", "halfEven" », fallback).

  static const auto values = std::to_array<RoundingMode>(
      {RoundingMode::Ceil, RoundingMode::Floor, RoundingMode::Expand,
       RoundingMode::Trunc, RoundingMode::HalfCeil, RoundingMode::HalfFloor,
       RoundingMode::HalfExpand, RoundingMode::HalfTrunc,
       RoundingMode::HalfEven});
  return GetStringOption<RoundingMode>(
      isolate, options, "roundingMode", method_name,
      std::to_array<const std::string_view>(
          {"ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor",
           "halfExpand", "halfTrunc", "halfEven"}),
      values, fallback);
}

// https://tc39.es/proposal-temporal/#sec-temporal-getdifferencesettings
//
// This does not perform any validity checks, it only does the minimum needed
// to construct a DifferenceSettings object. temporal_rs handles the rest
Maybe<temporal_rs::DifferenceSettings> GetDifferenceSettingsWithoutChecks(
    Isolate* isolate, DirectHandle<Object> options_obj, UnitGroup unit_group,
    std::optional<Unit> fallback_smallest_unit, const char* method_name) {
  DirectHandle<JSReceiver> options;
  // 1. Set options to ? GetOptionsObject(options).
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name),
      Nothing<temporal_rs::DifferenceSettings>());

  // 2. Let largestUnit be ?GetTemporalUnitValuedOption(options, "largestUnit",
  // unitGroup, auto).
  std::optional<Unit> largest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, largest_unit,
      GetTemporalUnit(isolate, options, "largestUnit", unit_group, Unit::Auto,
                      false, method_name),
      Nothing<temporal_rs::DifferenceSettings>());

  // 3. If disallowedUnits contains largestUnit, throw a RangeError exception.
  // (skip, to be validated in Rust code)
  // upstream spec issue on observability:
  // https://github.com/tc39/proposal-temporal/issues/3116

  // 4. Let roundingIncrement be ?GetRoundingIncrementOption(options).
  uint32_t rounding_increment;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_increment,
      temporal::GetRoundingIncrementOption(isolate, options),
      Nothing<temporal_rs::DifferenceSettings>());

  // 5. Let roundingMode be ?GetRoundingModeOption(options, trunc).
  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      temporal::GetRoundingModeOption(isolate, options, RoundingMode::Trunc,
                                      method_name),
      Nothing<temporal_rs::DifferenceSettings>());

  // 7. Let smallestUnit be ?GetTemporalUnitValuedOption(options,
  // "smallestUnit", unitGroup, fallbackSmallestUnit).
  std::optional<Unit> smallest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, largest_unit,
      GetTemporalUnit(isolate, options, "smallestUnit", unit_group,
                      fallback_smallest_unit,
                      !fallback_smallest_unit.has_value(), method_name),
      Nothing<temporal_rs::DifferenceSettings>());

  // remaining steps are validation, to be performed later

  return Just(temporal_rs::DifferenceSettings{.largest_unit = largest_unit,
                                              .smallest_unit = smallest_unit,
                                              .rounding_mode = rounding_mode,
                                              .increment = rounding_increment});
}

// https://tc39.es/proposal-temporal/#sec-temporal-gettemporalshowcalendarnameoption
Maybe<temporal_rs::DisplayCalendar> GetTemporalShowCalendarNameOption(
    Isolate* isolate, DirectHandle<JSReceiver> options,
    const char* method_name) {
  // 1. Return ? GetOption(normalizedOptions, "calendarName", « String », «
  // "auto", "always", "never" », "auto").
  return GetStringOption<temporal_rs::DisplayCalendar>(
      isolate, options, "calendarName", method_name,
      std::to_array<const std::string_view>(
          {"auto", "always", "never", "critical"}),
      std::to_array<temporal_rs::DisplayCalendar>(
          {temporal_rs::DisplayCalendar::Auto,
           temporal_rs::DisplayCalendar::Always,
           temporal_rs::DisplayCalendar::Never,
           temporal_rs::DisplayCalendar::Critical}),
      temporal_rs::DisplayCalendar::Auto);
}

// convenience method for getting the "calendar" field off of a Temporal object
std::optional<temporal_rs::AnyCalendarKind> ExtractCalendarFrom(
    Isolate* isolate, Tagged<HeapObject> calendar_like) {
  InstanceType instance_type = calendar_like->map(isolate)->instance_type();
  // a. If temporalCalendarLike has an [[InitializedTemporalDate]],
  // [[InitializedTemporalDateTime]], [[InitializedTemporalMonthDay]],
  // [[InitializedTemporalYearMonth]], or [[InitializedTemporalZonedDateTime]]
  // internal slot, then
  //
  // i. Return temporalCalendarLike.[[Calendar]].
  if (InstanceTypeChecker::IsJSTemporalPlainDate(instance_type)) {
    return Cast<JSTemporalPlainDate>(*calendar_like)
        ->date()
        ->raw()
        ->calendar()
        .kind();
  } else if (InstanceTypeChecker::IsJSTemporalPlainDateTime(instance_type)) {
    return Cast<JSTemporalPlainDateTime>(*calendar_like)
        ->date_time()
        ->raw()
        ->calendar()
        .kind();
  } else if (InstanceTypeChecker::IsJSTemporalPlainMonthDay(instance_type)) {
    return Cast<JSTemporalPlainMonthDay>(*calendar_like)
        ->month_day()
        ->raw()
        ->calendar()
        .kind();
  } else if (InstanceTypeChecker::IsJSTemporalPlainYearMonth(instance_type)) {
    return Cast<JSTemporalPlainYearMonth>(*calendar_like)
        ->year_month()
        ->raw()
        ->calendar()
        .kind();
  } else if (IsJSTemporalZonedDateTime(*calendar_like)) {
    UNIMPLEMENTED();
  }
  return std::nullopt;
}
// https://tc39.es/proposal-temporal/#sec-temporal-gettemporalcalendarslotvaluewithisodefault
Maybe<temporal_rs::AnyCalendarKind> ToTemporalCalendarIdentifier(
    Isolate* isolate, DirectHandle<Object> calendar_like) {
  // 1. If temporalCalendarLike is an Object, then
  if (IsHeapObject(*calendar_like)) {
    // a. If temporalCalendarLike has an [[InitializedTemporalDate]],
    // [[InitializedTemporalDateTime]], [[InitializedTemporalMonthDay]],
    // [[InitializedTemporalYearMonth]], or [[InitializedTemporalZonedDateTime]]
    // internal slot, then
    //
    //  i. Return temporalCalendarLike.[[Calendar]].
    auto cal_field =
        ExtractCalendarFrom(isolate, Cast<HeapObject>(*calendar_like));
    if (cal_field.has_value()) {
      return Just(cal_field.value());
    }
  }

  // 2. If temporalCalendarLike is not a String, throw a TypeError exception.
  if (!IsString(*calendar_like)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<temporal_rs::AnyCalendarKind>());
  }
  // 3. Let identifier be ?ParseTemporalCalendarString(temporalCalendarLike).
  // 4. Return ?CanonicalizeCalendar(identifier).
  // (CanonicalizeCalendar here takes a string)
  return temporal::CanonicalizeCalendar(isolate, Cast<String>(calendar_like));
}
// https://tc39.es/proposal-temporal/#sec-temporal-gettemporalcalendarslotvaluewithisodefault
Maybe<temporal_rs::AnyCalendarKind> GetTemporalCalendarIdentifierWithISODefault(
    Isolate* isolate, DirectHandle<JSReceiver> options) {
  // 1. If item has an [[InitializedTemporalDate]],
  // [[InitializedTemporalDateTime]], [[InitializedTemporalMonthDay]],
  // [[InitializedTemporalYearMonth]], or [[InitializedTemporalZonedDateTime]]
  // internal slot, then
  //
  // a. Return item.[[Calendar]].
  if (IsHeapObject(*options)) {
    auto cal_field = ExtractCalendarFrom(isolate, Cast<HeapObject>(*options));
    if (cal_field.has_value()) {
      return Just(cal_field.value());
    }
  }
  // 2. Let calendarLike be ?Get(item, "calendar").
  DirectHandle<Object> calendar;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, calendar,
      JSReceiver::GetProperty(isolate, options,
                              isolate->factory()->calendar_string()),
      Nothing<temporal_rs::AnyCalendarKind>());

  // 3. If calendarLike is undefined, then
  if (IsUndefined(*calendar)) {
    // a. Return "iso8601".
    return Just(
        temporal_rs::AnyCalendarKind(temporal_rs::AnyCalendarKind::Iso));
  }
  return ToTemporalCalendarIdentifier(isolate, calendar);
}

constexpr temporal_rs::ToStringRoundingOptions kToStringAuto =
    temporal_rs::ToStringRoundingOptions{
        .precision = temporal_rs::Precision{.is_minute = false,
                                            .precision = std::nullopt},
        .smallest_unit = std::nullopt,
        .rounding_mode = std::nullopt,
    };

// ====== Stringification operations ======

// https://tc39.es/proposal-temporal/#sec-temporal-temporaldurationtostring
MaybeDirectHandle<String> TemporalDurationToString(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    const temporal_rs::ToStringRoundingOptions&& options) {
  std::string output;
  // This is currently inefficient, can be improved after
  // https://github.com/rust-diplomat/diplomat/issues/866 is fixed
  MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
      isolate, output,
      ExtractRustResult(isolate,
                        duration->duration()->raw()->to_string(options)),
      MaybeDirectHandle<String>());
  IncrementalStringBuilder builder(isolate);
  builder.AppendString(output);
  return builder.Finish().ToHandleChecked();
}

// https://tc39.es/proposal-temporal/#sec-temporal-temporalinstanttostring
MaybeDirectHandle<String> TemporalInstantToString(
    Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
    const temporal_rs::TimeZone* time_zone,
    const temporal_rs::ToStringRoundingOptions&& options) {
  std::string output;
  // This is currently inefficient, can be improved after
  // https://github.com/rust-diplomat/diplomat/issues/866 is fixed
  MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
      isolate, output,
      ExtractRustResult(
          isolate,
          instant->instant()->raw()->to_ixdtf_string_with_compiled_data(
              time_zone, options)),
      MaybeDirectHandle<String>());

  IncrementalStringBuilder builder(isolate);
  builder.AppendString(output);
  return builder.Finish().ToHandleChecked();
}

// https://tc39.es/proposal-temporal/#sec-temporal-temporaldatetostring
MaybeDirectHandle<String> TemporalDateToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    temporal_rs::DisplayCalendar show_calendar) {
  // This is currently inefficient, can be improved after
  // https://github.com/rust-diplomat/diplomat/issues/866 is fixed
  auto output = temporal_date->date()->raw()->to_ixdtf_string(show_calendar);

  IncrementalStringBuilder builder(isolate);
  builder.AppendString(output);
  return builder.Finish().ToHandleChecked();
}

// https://tc39.es/proposal-temporal/#sec-temporal-isodatetimetostring
// This automatically operates on the ISO date time within the
// JSTemporalPlainDateTime, you do not need to perform any conversions to
// extract it
MaybeDirectHandle<String> ISODateTimeToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> temporal_date_time,
    const temporal_rs::ToStringRoundingOptions&& options,
    temporal_rs::DisplayCalendar show_calendar) {
  std::string output;
  // This is currently inefficient, can be improved after
  // https://github.com/rust-diplomat/diplomat/issues/866 is fixed
  MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
      isolate, output,
      ExtractRustResult(isolate,
                        temporal_date_time->date_time()->raw()->to_ixdtf_string(
                            std::move(options), show_calendar)),
      MaybeDirectHandle<String>());
  IncrementalStringBuilder builder(isolate);
  builder.AppendString(output);
  return builder.Finish().ToHandleChecked();
}

// https://tc39.es/proposal-temporal/#sec-temporal-timerecordtostring
MaybeDirectHandle<String> TimeRecordToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> time,
    const temporal_rs::ToStringRoundingOptions&& options) {
  std::string output;
  // This is currently inefficient, can be improved after
  // https://github.com/rust-diplomat/diplomat/issues/866 is fixed
  MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
      isolate, output,
      ExtractRustResult(isolate, time->time()->raw()->to_ixdtf_string(options)),
      MaybeDirectHandle<String>());
  IncrementalStringBuilder builder(isolate);
  builder.AppendString(output);
  return builder.Finish().ToHandleChecked();
}

// ====== Record operations ======

constexpr temporal_rs::PartialDate kNullPartialDate = temporal_rs::PartialDate{
    .year = std::nullopt,
    .month = std::nullopt,
    .month_code = "",
    .day = std::nullopt,
    .era = "",
    .era_year = std::nullopt,
    .calendar = temporal_rs::AnyCalendarKind::Iso,
};
constexpr temporal_rs::PartialTime kNullPartialTime = temporal_rs::PartialTime{
    .hour = std::nullopt,
    .minute = std::nullopt,
    .second = std::nullopt,
    .millisecond = std::nullopt,
    .microsecond = std::nullopt,
    .nanosecond = std::nullopt,
};

constexpr temporal_rs::PartialDateTime kNullPartialDateTime =
    temporal_rs::PartialDateTime{.date = kNullPartialDate,
                                 .time = kNullPartialTime};

template <typename RustObject>
temporal_rs::PartialTime GetTimeRecordFromRust(RustObject& rust_object) {
  return temporal_rs::PartialTime{
      .hour = rust_object->hour(),
      .minute = rust_object->minute(),
      .second = rust_object->second(),
      .millisecond = rust_object->millisecond(),
      .microsecond = rust_object->microsecond(),
      .nanosecond = rust_object->nanosecond(),
  };
}
// These can eventually be replaced with methods upstream
temporal_rs::PartialTime GetTimeRecord(
    DirectHandle<JSTemporalPlainTime> plain_time) {
  auto rust_object = plain_time->time()->raw();
  return GetTimeRecordFromRust(rust_object);
}
temporal_rs::PartialTime GetTimeRecord(
    DirectHandle<JSTemporalPlainDateTime> date_time) {
  auto rust_object = date_time->date_time()->raw();
  return GetTimeRecordFromRust(rust_object);
}
temporal_rs::PartialTime GetTimeRecord(
    DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  auto rust_object = zoned_date_time->zoned_date_time()->raw();
  return GetTimeRecordFromRust(rust_object);
}

template <typename RustObject>
temporal_rs::PartialDate GetDateRecordFromRust(RustObject& rust_object) {
  return temporal_rs::PartialDate{
      .year = rust_object->year(),
      .month = rust_object->month(),
      .month_code = "",
      .day = rust_object->day(),
      .era = "",
      .era_year = std::nullopt,
      .calendar = rust_object->calendar().kind(),
  };
}
temporal_rs::PartialDate GetDateRecord(
    DirectHandle<JSTemporalPlainDate> plain_date) {
  auto rust_object = plain_date->date()->raw();
  return GetDateRecordFromRust(rust_object);
}
temporal_rs::PartialDate GetDateRecord(
    DirectHandle<JSTemporalPlainDateTime> date_time) {
  auto rust_object = date_time->date_time()->raw();
  return GetDateRecordFromRust(rust_object);
}
temporal_rs::PartialDate GetDateRecord(
    DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  auto rust_object = zoned_date_time->zoned_date_time()->raw();
  return GetDateRecordFromRust(rust_object);
}

temporal_rs::PartialDateTime GetDateTimeRecord(
    DirectHandle<JSTemporalPlainDate> plain_date) {
  auto rust_object = plain_date->date()->raw();
  return temporal_rs::PartialDateTime{
      .date = GetDateRecordFromRust(rust_object),
      .time = kNullPartialTime,
  };
}
temporal_rs::PartialDateTime GetDateTimeRecord(
    DirectHandle<JSTemporalPlainDateTime> date_time) {
  auto rust_object = date_time->date_time()->raw();
  return temporal_rs::PartialDateTime{
      .date = GetDateRecordFromRust(rust_object),
      .time = GetTimeRecordFromRust(rust_object),
  };
}
temporal_rs::PartialDateTime GetDateTimeRecord(
    DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  auto rust_object = zoned_date_time->zoned_date_time()->raw();
  return temporal_rs::PartialDateTime{
      .date = GetDateRecordFromRust(rust_object),
      .time = GetTimeRecordFromRust(rust_object),
  };
}

// Helper for ToTemporalPartialDurationRecord
// Maybe<std::optional> since the Maybe handles errors and the optional handles
// missing fields
Maybe<std::optional<int64_t>> GetSingleDurationField(
    Isolate* isolate, DirectHandle<JSReceiver> duration_like,
    DirectHandle<String> field_name) {
  DirectHandle<Object> val;
  //  Let val be ? Get(temporalDurationLike, fieldName).
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, val, JSReceiver::GetProperty(isolate, duration_like, field_name),
      Nothing<std::optional<int64_t>>());
  // c. If val is not undefined, then
  if (IsUndefined(*val)) {
    return Just((std::optional<int64_t>)std::nullopt);
  } else {
    double field;
    // 5. If val is not undefined, set result.[[val]] to
    // ?ToIntegerIfIntegral(val).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, field, temporal::ToIntegerIfIntegral(isolate, val),
        Nothing<std::optional<int64_t>>());

    return Just(std::optional(static_cast<int64_t>(field)));
  }
}

// https://tc39.es/proposal-temporal/#sec-temporal-tooffsetstring
Maybe<std::string> ToOffsetString(Isolate* isolate,
                                  DirectHandle<Object> argument) {
  // 1. Let offset be ?ToPrimitive(argument, string).
  DirectHandle<Object> offset_prim;
  if (IsJSReceiver(*argument)) {
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, offset_prim,
        JSReceiver::ToPrimitive(isolate, Cast<JSReceiver>(argument),
                                ToPrimitiveHint::kString),
        Nothing<std::string>());
  } else {
    offset_prim = argument;
  }

  // 2. If offset is not a String, throw a TypeError exception.
  if (!IsString(*offset_prim)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 {});
  }

  // 3. Perform ?ParseDateTimeUTCOffset(offset).
  auto offset = Cast<String>(*offset_prim)->ToStdString();

  // Currently TimeZone::try_from_str parses identifiers and UTC offsets
  // at once. We check to ensure that this is UTC offset-like (not
  // identifier-like) before handing off to Rust.
  // TODO(manishearth) clean up after
  // https://github.com/boa-dev/temporal/pull/348 lands.

  if (offset.size() == 0 || (offset[0] != '+' && offset[0] != '-')) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 {});
  }

  // TODO(manishearth) this has a minor unnecessary cost of allocating a
  // TimeZone, but it can be obviated once
  // https://github.com/boa-dev/temporal/issues/330 is fixed.
  MAYBE_RETURN_ON_EXCEPTION_VALUE(
      isolate,
      ExtractRustResult(isolate, temporal_rs::TimeZone::try_from_str(offset)),
      Nothing<std::string>());

  return Just(std::move(offset));
}

Maybe<std::unique_ptr<temporal_rs::TimeZone>> ToTemporalTimeZoneIdentifier(
    Isolate* isolate, DirectHandle<Object> tz_like) {
  // 1. If temporalTimeZoneLike is an Object, then
  // a. If temporalTimeZoneLike has an [[InitializedTemporalZonedDateTime]]
  // internal slot, then
  if (IsJSTemporalZonedDateTime(*tz_like)) {
    // i. Return temporalTimeZoneLike.[[TimeZone]].
    // TODO(manishearth) we don't currently have a nice way to clone timezones
    // See https://github.com/boa-dev/temporal/pull/344 and
    // https://github.com/boa-dev/temporal/issues/330.

    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
        Nothing<std::unique_ptr<temporal_rs::TimeZone>>());
  }
  // 2. If temporalTimeZoneLike is not a String, throw a TypeError exception.
  if (!IsString(*tz_like)) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
        Nothing<std::unique_ptr<temporal_rs::TimeZone>>());
  }

  DirectHandle<String> str = Cast<String>(tz_like);

  auto std_str = str->ToStdString();

  return ExtractRustResult(isolate,
                           temporal_rs::TimeZone::try_from_str(std_str));
}

// https://tc39.es/proposal-temporal/#sec-temporal-totemporalpartialdurationrecord
Maybe<temporal_rs::PartialDuration> ToTemporalPartialDurationRecord(
    Isolate* isolate, DirectHandle<Object> duration_like_obj) {
  Factory* factory = isolate->factory();

  // 1. If temporalDurationLike is not an Object, then
  if (!IsJSReceiver(*duration_like_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<temporal_rs::PartialDuration>());
  }

  DirectHandle<JSReceiver> duration_like = Cast<JSReceiver>(duration_like_obj);

  // 2. Let result be a new partial Duration Record with each field set to
  // undefined.
  auto result = temporal_rs::PartialDuration{
      .years = std::nullopt,
      .months = std::nullopt,
      .weeks = std::nullopt,
      .days = std::nullopt,
      .hours = std::nullopt,
      .minutes = std::nullopt,
      .seconds = std::nullopt,
      .milliseconds = std::nullopt,
      .microseconds = std::nullopt,
      .nanoseconds = std::nullopt,
  };

  // Steps 3-14: get each field in alphabetical order

  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.days,
      temporal::GetSingleDurationField(isolate, duration_like,
                                       factory->days_string()),
      Nothing<temporal_rs::PartialDuration>());
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.hours,
      temporal::GetSingleDurationField(isolate, duration_like,
                                       factory->hours_string()),
      Nothing<temporal_rs::PartialDuration>());
  std::optional<int64_t> us;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, us,
      temporal::GetSingleDurationField(isolate, duration_like,
                                       factory->microseconds_string()),
      Nothing<temporal_rs::PartialDuration>());
  if (us.has_value()) {
    // This will improve after https://github.com/boa-dev/temporal/issues/189
    result.microseconds = static_cast<double>(us.value());
  }
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.milliseconds,
      temporal::GetSingleDurationField(isolate, duration_like,
                                       factory->milliseconds_string()),
      Nothing<temporal_rs::PartialDuration>());
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.minutes,
      temporal::GetSingleDurationField(isolate, duration_like,
                                       factory->minutes_string()),
      Nothing<temporal_rs::PartialDuration>());
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.months,
      temporal::GetSingleDurationField(isolate, duration_like,
                                       factory->months_string()),
      Nothing<temporal_rs::PartialDuration>());
  std::optional<int64_t> ns;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, ns,
      temporal::GetSingleDurationField(isolate, duration_like,
                                       factory->nanoseconds_string()),
      Nothing<temporal_rs::PartialDuration>());
  if (ns.has_value()) {
    // This will improve after https://github.com/boa-dev/temporal/issues/189
    result.microseconds = static_cast<double>(ns.value());
  }
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.seconds,
      temporal::GetSingleDurationField(isolate, duration_like,
                                       factory->seconds_string()),
      Nothing<temporal_rs::PartialDuration>());
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.weeks,
      temporal::GetSingleDurationField(isolate, duration_like,
                                       factory->weeks_string()),
      Nothing<temporal_rs::PartialDuration>());
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.years,
      temporal::GetSingleDurationField(isolate, duration_like,
                                       factory->years_string()),
      Nothing<temporal_rs::PartialDuration>());

  return Just(result);
}

// Helper for ToTemporalTimeRecord
// Maybe<std::optional> since the Maybe handles errors and the optional handles
// missing fields
template <typename IntegerType>
Maybe<std::optional<IntegerType>> GetSingleTimeRecordField(
    Isolate* isolate, DirectHandle<JSReceiver> time_like,
    DirectHandle<String> field_name, bool* any) {
  DirectHandle<Object> val;
  //  Let v be ?Get(temporalTimeLike, field_name).
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, val, JSReceiver::GetProperty(isolate, time_like, field_name),
      Nothing<std::optional<IntegerType>>());
  // If val is not undefined, then
  if (!IsUndefined(*val)) {
    double field;
    // 5. a. Set result.[[Hour]] to ?ToIntegerWithTruncation(hour).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, field, temporal::ToIntegerWithTruncation(isolate, val),
        Nothing<std::optional<IntegerType>>());
    // b. Set any to true.
    *any = true;

    // TODO(manishearth) We should ideally be casting later, see
    // https://github.com/boa-dev/temporal/issues/334
    return Just(std::optional(static_cast<IntegerType>(field)));
  } else {
    return Just((std::optional<IntegerType>)std::nullopt);
  }
}

// https://tc39.es/proposal-temporal/#sec-temporal-totemporaltimerecord
Maybe<temporal_rs::PartialTime> ToTemporalTimeRecord(
    Isolate* isolate, DirectHandle<JSReceiver> time_like,
    const char* method_name, Completeness completeness = kComplete) {
  TEMPORAL_ENTER_FUNC();
  Factory* factory = isolate->factory();

  // 2. If completeness is complete, then
  // a. Let result be a new TemporalTimeLike Record with each field set to 0.
  // 3. Else,
  // a. Let result be a new TemporalTimeLike Record with each field set to
  // unset.
  auto result = completeness == kPartial ? temporal_rs::PartialTime {
    .hour = std::nullopt,
    .minute = std::nullopt,
    .second = std::nullopt,
    .millisecond = std::nullopt,
    .microsecond = std::nullopt,
    .nanosecond = std::nullopt,
  } : temporal_rs::PartialTime {
    .hour = 0,
    .minute = 0,
    .second = 0,
    .millisecond = 0,
    .microsecond = 0,
    .nanosecond = 0,
  };

  bool any = false;

  // Steps 3-14: get each field in alphabetical order

  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.hour,
      temporal::GetSingleTimeRecordField<uint8_t>(isolate, time_like,
                                                  factory->hour_string(), &any),
      Nothing<temporal_rs::PartialTime>());
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.microsecond,
      temporal::GetSingleTimeRecordField<uint16_t>(
          isolate, time_like, factory->microsecond_string(), &any),
      Nothing<temporal_rs::PartialTime>());
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.millisecond,
      temporal::GetSingleTimeRecordField<uint16_t>(
          isolate, time_like, factory->millisecond_string(), &any),
      Nothing<temporal_rs::PartialTime>());
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.minute,
      temporal::GetSingleTimeRecordField<uint8_t>(
          isolate, time_like, factory->minute_string(), &any),
      Nothing<temporal_rs::PartialTime>());
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.nanosecond,
      temporal::GetSingleTimeRecordField<uint16_t>(
          isolate, time_like, factory->nanosecond_string(), &any),
      Nothing<temporal_rs::PartialTime>());
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result.second,
      temporal::GetSingleTimeRecordField<uint8_t>(
          isolate, time_like, factory->second_string(), &any),
      Nothing<temporal_rs::PartialTime>());

  if (!any) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<temporal_rs::PartialTime>());
  }

  return Just(result);
}

// Returned by PrepareCalendarFields
struct CombinedRecord {
  temporal_rs::PartialDate date;
  temporal_rs::PartialTime time;
  std::optional<std::string> offset;
  std::optional<std::unique_ptr<temporal_rs::TimeZone>> time_zone;
};

// An object that "owns" values borrowed in CombinedRecord,
// to be passed in to PrepareCalendarFields by a caller that
// can make it live longer than the returned CombinedRecord
struct CombinedRecordOwnership {
  std::string era;
  std::string monthCode;
};

enum class CalendarFieldsFlag : uint8_t {
  kDay = 1 << 0,
  // Month and MonthCode
  kMonthFields = 1 << 1,
  // year, era, eraYear
  kYearFields = 1 << 2,
  // hour, minute, second, millisecond, microsecont, nanosecond
  kTimeFields = 1 << 3,
  kOffset = 1 << 4,
  kTimeZone = 1 << 5,
};
using CalendarFieldsFlags = base::Flags<CalendarFieldsFlag>;

DEFINE_OPERATORS_FOR_FLAGS(CalendarFieldsFlags)

constexpr CalendarFieldsFlags kAllDateFlags = CalendarFieldsFlag::kDay |
                                              CalendarFieldsFlag::kMonthFields |
                                              CalendarFieldsFlag::kYearFields;

enum class RequiredFields {
  kNone,
  kPartial,
  kTimeZone,
};

// A single run of the PrepareCalendarFields iteration (Step 9, substeps a-c,
// NOT d) Returns whether or not the field was found. Does not handle the case
// when the field is not found.
template <typename OutType>
Maybe<bool> GetSingleCalendarField(
    Isolate* isolate, DirectHandle<JSReceiver> fields,
    DirectHandle<String> field_name, bool& any, OutType& output,
    Maybe<OutType> (*conversion_func)(Isolate*, DirectHandle<Object>)) {
  // b. Let value be ?Get(fields, property).
  DirectHandle<Object> value;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value, JSReceiver::GetProperty(isolate, fields, field_name),
      Nothing<bool>());

  // c. If value is not undefined, then
  if (!IsUndefined(*value)) {
    // i. Set any to true.
    any = true;
    // ii. Let Conversion be the Conversion value of the same row.
    // (perform conversion)
    // ix. Set result's field whose name is given in the Field Name column of
    // the same row to value.
    MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
        isolate, output, conversion_func(isolate, value), Nothing<bool>());
    return Just(true);
  }

  return Just(false);
}
// Same as above but for DirectHandles
template <typename OutType>
Maybe<bool> GetSingleCalendarField(
    Isolate* isolate, DirectHandle<JSReceiver> fields,
    DirectHandle<String> field_name, bool& any, DirectHandle<OutType>& output,
    MaybeDirectHandle<OutType> (*conversion_func)(Isolate*,
                                                  DirectHandle<Object>)) {
  // b. Let value be ?Get(fields, property).
  DirectHandle<Object> value;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value, JSReceiver::GetProperty(isolate, fields, field_name),
      Nothing<bool>());

  // c. If value is not undefined, then
  if (!IsUndefined(*value)) {
    // i. Set any to true.
    any = true;
    // ii. Let Conversion be the Conversion value of the same row.
    // (perform conversion)
    // ix. Set result's field whose name is given in the Field Name column of
    // the same row to value.
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, output, conversion_func(isolate, value), Nothing<bool>());
    return Just(true);
  }

  return Just(false);
}

// Setters take `resultField` (a field expression of CombinedRecords)
// and `field` (a variable holding a value, and the name of the property),
// and set resultField to field, performing additional work if necessary.
#define SIMPLE_SETTER(resultField, field) resultField = field;
#define MOVING_SETTER(resultField, field) resultField = std::move(field);
#define ANCHORED_STRING_SETTER(resultField, field) \
  anchor.era = field->ToStdString();               \
  resultField = anchor.era;

// Conditions take a boolean expression and wrap it with additional checks
#define SIMPLE_CONDITION(cond) cond
#define ERA_CONDITION(cond) calendarUsesEras && (cond)

#define NOOP_REQUIRED_CHECK
#define TIMEZONE_REQUIRED_CHECK                                         \
  if (!found && required_fields == RequiredFields::kTimeZone) {         \
    THROW_NEW_ERROR_RETURN_VALUE(isolate,                               \
                                 NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(), \
                                 Nothing<CombinedRecord>());            \
  }

// https://tc39.es/proposal-temporal/#sec-temporal-calendar-fields-records
// V(CalendarFieldsFlags, propertyName, resultField, Type, Conversion,
// CONDITION, SETTER, REQUIRED_CHECK, AssignOrMove)
#define CALENDAR_FIELDS(V)                                                     \
  V(kDay, day, result.date.day, uint8_t,                                       \
    ToPositiveIntegerTypeWithTruncation<uint8_t>, SIMPLE_CONDITION,            \
    SIMPLE_SETTER, NOOP_REQUIRED_CHECK, ASSIGN)                                \
  V(kYearFields, era, result.date.era, DirectHandle<String>, Object::ToString, \
    ERA_CONDITION, ANCHORED_STRING_SETTER, NOOP_REQUIRED_CHECK, ASSIGN)        \
  V(kYearFields, eraYear, result.date.era_year, int32_t,                       \
    ToIntegerTypeWithTruncation<int32_t>, ERA_CONDITION, SIMPLE_SETTER,        \
    NOOP_REQUIRED_CHECK, ASSIGN)                                               \
  V(kTimeFields, hour, result.time.hour, uint8_t,                              \
    ToPositiveIntegerTypeWithTruncation<uint8_t>, SIMPLE_CONDITION,            \
    SIMPLE_SETTER, NOOP_REQUIRED_CHECK, ASSIGN)                                \
  V(kTimeFields, microsecond, result.time.microsecond, uint16_t,               \
    ToPositiveIntegerTypeWithTruncation<uint16_t>, SIMPLE_CONDITION,           \
    SIMPLE_SETTER, NOOP_REQUIRED_CHECK, ASSIGN)                                \
  V(kTimeFields, millisecond, result.time.millisecond, uint16_t,               \
    ToPositiveIntegerTypeWithTruncation<uint16_t>, SIMPLE_CONDITION,           \
    SIMPLE_SETTER, NOOP_REQUIRED_CHECK, ASSIGN)                                \
  V(kTimeFields, minute, result.time.minute, uint8_t,                          \
    ToPositiveIntegerTypeWithTruncation<uint8_t>, SIMPLE_CONDITION,            \
    SIMPLE_SETTER, NOOP_REQUIRED_CHECK, ASSIGN)                                \
  V(kMonthFields, month, result.date.month, uint8_t,                           \
    ToPositiveIntegerTypeWithTruncation<uint8_t>, SIMPLE_CONDITION,            \
    SIMPLE_SETTER, NOOP_REQUIRED_CHECK, ASSIGN)                                \
  V(kMonthFields, monthCode, result.date.month_code, DirectHandle<String>,     \
    Object::ToString, SIMPLE_CONDITION, ANCHORED_STRING_SETTER,                \
    NOOP_REQUIRED_CHECK, ASSIGN)                                               \
  V(kTimeFields, nanosecond, result.time.nanosecond, uint16_t,                 \
    ToPositiveIntegerTypeWithTruncation<uint16_t>, SIMPLE_CONDITION,           \
    SIMPLE_SETTER, NOOP_REQUIRED_CHECK, ASSIGN)                                \
  V(kOffset, offset, result.offset, std::string, ToOffsetString,               \
    SIMPLE_CONDITION, MOVING_SETTER, NOOP_REQUIRED_CHECK, ASSIGN)              \
  V(kTimeFields, second, result.time.second, uint8_t,                          \
    ToPositiveIntegerTypeWithTruncation<uint8_t>, SIMPLE_CONDITION,            \
    SIMPLE_SETTER, NOOP_REQUIRED_CHECK, ASSIGN)                                \
  V(kTimeZone, timeZone, result.time_zone,                                     \
    std::unique_ptr<temporal_rs::TimeZone>, ToTemporalTimeZoneIdentifier,      \
    SIMPLE_CONDITION, MOVING_SETTER, NOOP_REQUIRED_CHECK, MOVE)                \
  V(kYearFields, year, result.date.year, int32_t,                              \
    ToIntegerTypeWithTruncation<int32_t>, SIMPLE_CONDITION, SIMPLE_SETTER,     \
    TIMEZONE_REQUIRED_CHECK, ASSIGN)

// https://tc39.es/proposal-temporal/#sec-temporal-preparecalendarfields
Maybe<CombinedRecord> PrepareCalendarFields(Isolate* isolate,
                                            temporal_rs::AnyCalendarKind kind,
                                            DirectHandle<JSReceiver> fields,
                                            CalendarFieldsFlags which_fields,
                                            RequiredFields required_fields,
                                            CombinedRecordOwnership& anchor) {
  // 1. Assert: If requiredFieldNames is a List, requiredFieldNames contains
  // zero or one of each of the elements of calendarFieldNames and
  // nonCalendarFieldNames.
  // 2. Let fieldNames be the list-concatenation of calendarFieldNames and
  // nonCalendarFieldNames.
  // 3. Let extraFieldNames be CalendarExtraFields(calendar,
  // calendarFieldNames).
  // Currently al
  // 4. Set fieldNames to the list-concatenation of fieldNames and
  // extraFieldNames.
  // 5. Assert: fieldNames contains no duplicate elements.

  // All steps handled by RequiredFields/CalendarFieldsFlag being enums, and
  // CalendarExtraFields is handled by calendarUsesEras below.

  // Currently all calendars have a "default" era, except for iso
  // This may change: https://tc39.es/proposal-intl-era-monthcode/
  bool calendarUsesEras = kind != temporal_rs::AnyCalendarKind::Iso;

  // 6. Let result be a Calendar Fields Record with all fields equal to unset.
  CombinedRecord result = CombinedRecord{
      .date = kNullPartialDate,
      .time = kNullPartialTime,
      .offset = std::nullopt,
      .time_zone = std::nullopt,
  };

  // This is not explicitly specced, but CombinedRecord contains the calendar
  // kind unlike the spec, and no caller of PrepareCalendarFields does anything
  // other than pair `fields` with `calendar` when passing to subsequent
  // algorithms.
  result.date.calendar = kind;

  // 7. Let any be false.
  bool any = false;

  // 8. Let sortedPropertyNames be a List whose elements are the values in the
  // Property Key column of Table 19 corresponding to the elements of
  // fieldNames, sorted according to lexicographic code unit order. (handled by
  // sorting)

  // 9. For each property name property of sortedPropertyNames, do
  //  a. Let key be the value in the Enumeration Key column of Table 19
  //  corresponding to the row whose Property Key value is property.
  //
  //  b. Let value be ?Get(fields, property).
  //
  //  c. If value is not undefined, then
  //   i. Set any to true.
  //   ii. Let Conversion be the Conversion value of the same row.
  //   iii. If Conversion is to-integer-with-truncation, then
  //     1. Set value to ? ToIntegerWithTruncation(value).
  //     2. Set value to 𝔽(value).
  //   iv. Else if Conversion is to-positive-integer-with-truncation, then
  //     1. Set value to ? ToPositiveIntegerWithTruncation(value).
  //     2. Set value to 𝔽(value).
  //   v. Else if Conversion is to-string, then
  //     1. Set value to ? ToString(value).
  //   vi. Else if Conversion is to-temporal-time-zone-identifier, then
  //     1. Set value to ? ToTemporalTimeZoneIdentifier(value).
  //   vii. Else if Conversion is to-month-code, then
  //     1. Set value to ? ToMonthCode(value).
  //   viii. Else,
  //     1. Assert: Conversion is to-offset-string.
  //     2. Set value to ? ToOffsetString(value).
  //   ix. Set result's field whose name is given in the Field Name column of
  //   the same row to value.
  //  d. Else if requiredFieldNames is a List, then
  //   i. If requiredFieldNames contains key, then
  //     1. Throw a TypeError exception.
  //   ii. Set result's field whose name is given in the Field Name column of
  //   the same row to the corresponding Default value of the same row.

#define GET_SINGLE_CALENDAR_FIELD(fieldsFlag, propertyName, resultField, Type, \
                                  Conversion, CONDITION, SETTER,               \
                                  REQUIRED_CHECK, AssignOrMove)                \
  if (CONDITION(which_fields & CalendarFieldsFlag::fieldsFlag)) {              \
    Type propertyName;                                                         \
    bool found = 0;                                                            \
    MAYBE_##AssignOrMove##_RETURN_ON_EXCEPTION_VALUE(                          \
        isolate, found,                                                        \
        GetSingleCalendarField(isolate, fields,                                \
                               isolate->factory()->propertyName##_string(),    \
                               any, propertyName, Conversion),                 \
        Nothing<CombinedRecord>());                                            \
    if (found) {                                                               \
      SETTER(resultField, propertyName);                                       \
    }                                                                          \
    REQUIRED_CHECK                                                             \
  }

  CALENDAR_FIELDS(GET_SINGLE_CALENDAR_FIELD);

#undef GET_SINGLE_CALENDAR_FIELD

  // 10. If requiredFieldNames is partial and any is false, then
  if (required_fields == RequiredFields::kPartial && !any) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<CombinedRecord>());
  }

  return Just(std::move(result));
}

#undef SIMPLE_SETTER
#undef MOVING_SETTER
#undef ANCHORED_STRING_SETTER
#undef SIMPLE_CONDITION
#undef ERA_CONDITION
#undef NOOP_REQUIRED_CHECK
#undef TIMEZONE_REQUIRED_CHECK
#undef CALENDAR_FIELDS

// ====== Construction operations ======

// https://tc39.es/proposal-temporal/#sec-temporal-totemporalduration
MaybeDirectHandle<JSTemporalDuration> ToTemporalDuration(
    Isolate* isolate, DirectHandle<Object> item, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 1. If item is an Object and item has an [[InitializedTemporalDuration]]
  // internal slot, then a. a. Return ! CreateTemporalDuration(item.[[Years]],
  // item.[[Months]], item.[[Weeks]], item.[[Days]], item.[[Hours]],
  // item.[[Minutes]], item.[[Seconds]], item.[[Milliseconds]],
  // item.[[Microseconds]], item.[[Nanoseconds]]).
  if (IsJSTemporalDuration(*item)) {
    auto instant = Cast<JSTemporalDuration>(item);
    auto raw = instant->duration()->raw();
    auto years = raw->years();
    auto months = raw->months();
    auto weeks = raw->weeks();
    auto days = raw->days();
    auto hours = raw->hours();
    auto minutes = raw->minutes();
    auto seconds = raw->seconds();
    auto milliseconds = raw->milliseconds();
    auto microseconds = raw->microseconds();
    auto nanoseconds = raw->nanoseconds();
    // i. Return !CreateTemporalInstant(item.[[EpochNanoseconds]]).
    return ConstructRustWrappingType<JSTemporalDuration>(
        isolate, CONSTRUCTOR(duration), CONSTRUCTOR(duration),
        temporal_rs::Duration::create(years, months, weeks, days, hours,
                                      minutes, seconds, milliseconds,
                                      microseconds, nanoseconds));
  }

  // 2. If item is not an Object, then
  if (!IsJSReceiver(*item)) {
    // a. If item is not a String, throw a TypeError exception.
    if (!IsString(*item)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
    }
    DirectHandle<String> str = Cast<String>(item);
    // b. Let result be ? ParseTemporalDurationString(string).
    DirectHandle<JSTemporalInstant> result;

    auto rust_result =
        HandleStringEncodings<TemporalAllocatedResult<temporal_rs::Duration>>(
            isolate, str,
            [](std::string_view view)
                -> TemporalAllocatedResult<temporal_rs::Duration> {
              return temporal_rs::Duration::from_utf8(view);
            },
            [](std::u16string_view view)
                -> TemporalAllocatedResult<temporal_rs::Duration> {
              return temporal_rs::Duration::from_utf16(view);
            });
    return ConstructRustWrappingType<JSTemporalDuration>(
        isolate, CONSTRUCTOR(duration), CONSTRUCTOR(duration),
        std::move(rust_result));
  }

  temporal_rs::PartialDuration partial;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, partial,
      temporal::ToTemporalPartialDurationRecord(isolate, item),
      DirectHandle<JSTemporalDuration>());

  return ConstructRustWrappingType<JSTemporalDuration>(
      isolate, CONSTRUCTOR(duration), CONSTRUCTOR(duration),
      temporal_rs::Duration::from_partial_duration(partial));
}

// https://tc39.es/proposal-temporal/#sec-temporal-totemporalinstant
MaybeDirectHandle<JSTemporalInstant> ToTemporalInstant(
    Isolate* isolate, DirectHandle<Object> item, const char* method_name) {
  TEMPORAL_ENTER_FUNC();

  // 1. If item is an Object, then
  // a. If item has an [[InitializedTemporalInstant]] or
  //    [[InitializedTemporalZonedDateTime]] internal slot, then
  if (IsJSTemporalInstant(*item)) {
    auto instant = Cast<JSTemporalInstant>(item);
    auto ns = instant->instant()->raw()->epoch_nanoseconds();
    // i. Return !CreateTemporalInstant(item.[[EpochNanoseconds]]).
    return ConstructRustWrappingType<JSTemporalInstant>(
        isolate, CONSTRUCTOR(instant), CONSTRUCTOR(instant),
        temporal_rs::Instant::try_new(ns));
  }
  // c. Set item to ?ToPrimitive(item, STRING).
  DirectHandle<Object> item_prim;
  if (IsJSReceiver(*item)) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, item_prim,
        JSReceiver::ToPrimitive(isolate, Cast<JSReceiver>(item),
                                ToPrimitiveHint::kString));
  } else {
    item_prim = item;
  }

  if (!IsString(*item_prim)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }

  DirectHandle<String> item_string = Cast<String>(item_prim);


  auto rust_result =
      HandleStringEncodings<TemporalAllocatedResult<temporal_rs::Instant>>(
          isolate, item_string,
          [](std::string_view view)
              -> TemporalAllocatedResult<temporal_rs::Instant> {
            return temporal_rs::Instant::from_utf8(view);
          },
          [](std::u16string_view view)
              -> TemporalAllocatedResult<temporal_rs::Instant> {
            return temporal_rs::Instant::from_utf16(view);
          });
  return ConstructRustWrappingType<JSTemporalInstant>(
      isolate, CONSTRUCTOR(instant), CONSTRUCTOR(instant),
      std::move(rust_result));
}

// https://tc39.es/proposal-temporal/#sec-temporal-totemporaltime
// Note this skips the options-parsing steps and instead asks the caller to pass
// it in
MaybeDirectHandle<JSTemporalPlainTime> ToTemporalTime(
    Isolate* isolate, DirectHandle<Object> item,
    std::optional<temporal_rs::ArithmeticOverflow> overflow,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. If options is not present, set options to undefined.
  // (handled by caller)

  // This error is eventually thrown by step 3; we perform a check early
  // so that we can optimize with InstanceType. Step 1 and 2 are unobservable.
  if (!IsHeapObject(*item)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  InstanceType instance_type =
      Cast<HeapObject>(*item)->map(isolate)->instance_type();

  // 2. If item is an Object, then
  if (InstanceTypeChecker::IsJSReceiver(instance_type)) {
    auto record = kNullPartialTime;
    // a. If item has an [[InitializedTemporalTime]] internal slot, then
    if (InstanceTypeChecker::IsJSTemporalPlainTime(instance_type)) {
      // iii. Return !CreateTemporalTime(item.[[Time]]).
      record = GetTimeRecord(Cast<JSTemporalPlainTime>(item));
      // b. If item has an [[InitializedTemporalDateTime]] internal slot, then
    } else if (InstanceTypeChecker::IsJSTemporalPlainDateTime(instance_type)) {
      // iii. Return ! CreateTemporalTime(item.[[ISODateTime]].[[Time]]).
      record = GetTimeRecord(Cast<JSTemporalPlainDateTime>(item));
      // c. If item has an [[InitializedTemporalZonedDateTime]] internal slot,
      // then
    } else if (InstanceTypeChecker::IsJSTemporalZonedDateTime(instance_type)) {
      // i. Let isoDateTime be GetISODateTimeFor(item.[[TimeZone]],
      // item.[[EpochNanoseconds]]).
      record = GetTimeRecord(Cast<JSTemporalZonedDateTime>(item));
      // iv. Return !CreateTemporalTime(isoDateTime.[[Time]]).
    } else {
      // d. Let result be ?ToTemporalTimeRecord(item).
      DirectHandle<JSReceiver> item_recvr = Cast<JSReceiver>(item);
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, record,
          temporal::ToTemporalTimeRecord(isolate, item_recvr, method_name),
          DirectHandle<JSTemporalPlainTime>());

      // RegulateTime/etc is handled by temporal_rs
      // caveat: https://github.com/boa-dev/temporal/issues/334
    }

    return ConstructRustWrappingType<JSTemporalPlainTime>(
        isolate, CONSTRUCTOR(plain_time), CONSTRUCTOR(plain_time),
        temporal_rs::PlainTime::from_partial(record, overflow));

    // 3. Else,
  } else {
    // a. If item is not a String, throw a TypeError exception.
    if (!InstanceTypeChecker::IsString(instance_type)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
    }
    DirectHandle<String> str = Cast<String>(item);

    auto rust_result =
        HandleStringEncodings<TemporalAllocatedResult<temporal_rs::PlainTime>>(
            isolate, str,
            [](std::string_view view)
                -> TemporalAllocatedResult<temporal_rs::PlainTime> {
              return temporal_rs::PlainTime::from_utf8(view);
            },
            [](std::u16string_view view)
                -> TemporalAllocatedResult<temporal_rs::PlainTime> {
              return temporal_rs::PlainTime::from_utf16(view);
            });

    return ConstructRustWrappingType<JSTemporalPlainTime>(
        isolate, CONSTRUCTOR(plain_time), CONSTRUCTOR(plain_time),
        std::move(rust_result));
  }
}

// https://tc39.es/proposal-temporal/#sec-temporal-totemporaldate
// Note this skips the options-parsing steps and instead asks the caller to pass
// it in
MaybeDirectHandle<JSTemporalPlainDate> ToTemporalDate(
    Isolate* isolate, DirectHandle<Object> item,
    std::optional<temporal_rs::ArithmeticOverflow> overflow,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. If options is not present, set options to undefined.
  // (handled by caller)

  // This error is eventually thrown by step 3a; we perform a check early
  // so that we can optimize with InstanceType. Step 1 and 2 are unobservable.
  if (!IsHeapObject(*item)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  InstanceType instance_type =
      Cast<HeapObject>(*item)->map(isolate)->instance_type();
  // 2. If item is an Object, then
  if (InstanceTypeChecker::IsJSReceiver(instance_type)) {
    auto record = kNullPartialDate;
    // a. If item has an [[InitializedTemporalDate]] internal slot, then
    if (InstanceTypeChecker::IsJSTemporalPlainDate(instance_type)) {
      // iii. Return !CreateTemporalDate(item.[[Date]], item.[[Calendar]]).
      record = GetDateRecord(Cast<JSTemporalPlainDate>(item));
      // b. If item has an [[InitializedTemporalZonedDateTime]] internal slot,
      // then
    } else if (InstanceTypeChecker::IsJSTemporalZonedDateTime(instance_type)) {
      // i. Let isoDateTime be GetISODateTimeFor(item.[[TimeZone]],
      // item.[[EpochNanoseconds]]).
      //
      // iv. Return !CreateTemporalDate(isoDateTime.[[ISODate]],
      // item.[[Calendar]]).
      record = GetDateRecord(Cast<JSTemporalZonedDateTime>(item));
      // c. If item has an [[InitializedTemporalDateTime]] internal slot, then
    } else if (InstanceTypeChecker::IsJSTemporalPlainDateTime(instance_type)) {
      // iii. Return !CreateTemporalDate(item.[[ISODateTime]].[[ISODate]],
      // item.[[Calendar]]).
      record = GetDateRecord(Cast<JSTemporalPlainDate>(item));
    } else {
      // d. Let calendar be ?GetTemporalCalendarIdentifierWithISODefault(item).
      temporal_rs::AnyCalendarKind kind = temporal_rs::AnyCalendarKind::Iso;
      DirectHandle<JSReceiver> item_recvr = Cast<JSReceiver>(item);
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, kind,
          temporal::GetTemporalCalendarIdentifierWithISODefault(isolate,
                                                                item_recvr),
          DirectHandle<JSTemporalPlainDate>());

      // e. Let fields be ?PrepareCalendarFields(calendar, item, « year, month,
      // month-code, day», «», «»).
      CombinedRecordOwnership owners;
      CombinedRecord fields = CombinedRecord{
          .date = kNullPartialDate,
          .time = kNullPartialTime,
          .offset = std::nullopt,
          .time_zone = std::nullopt,
      };

      MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
          isolate, fields,
          PrepareCalendarFields(isolate, kind, item_recvr, kAllDateFlags,
                                RequiredFields::kNone, owners),
          DirectHandle<JSTemporalPlainDate>());
      return ConstructRustWrappingType<JSTemporalPlainDate>(
          isolate, CONSTRUCTOR(plain_date), CONSTRUCTOR(plain_date),
          temporal_rs::PlainDate::from_partial(fields.date, overflow));
    }

    return ConstructRustWrappingType<JSTemporalPlainDate>(
        isolate, CONSTRUCTOR(plain_date), CONSTRUCTOR(plain_date),
        temporal_rs::PlainDate::from_partial(record, overflow));
    // 3. Else,
  } else {
    // a. If item is not a String, throw a TypeError exception.
    if (!InstanceTypeChecker::IsString(instance_type)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
    }
    DirectHandle<String> str = Cast<String>(item);

    // Rest of the steps handled in Rust

    auto rust_result =
        HandleStringEncodings<TemporalAllocatedResult<temporal_rs::PlainDate>>(
            isolate, str,
            [](std::string_view view)
                -> TemporalAllocatedResult<temporal_rs::PlainDate> {
              return temporal_rs::PlainDate::from_utf8(view);
            },
            [](std::u16string_view view)
                -> TemporalAllocatedResult<temporal_rs::PlainDate> {
              return temporal_rs::PlainDate::from_utf16(view);
            });

    return ConstructRustWrappingType<JSTemporalPlainDate>(
        isolate, CONSTRUCTOR(plain_date), CONSTRUCTOR(plain_date),
        std::move(rust_result));
  }
}

// https://tc39.es/proposal-temporal/#sec-temporal-totemporaldatetime
// Note this skips the options-parsing steps and instead asks the caller to pass
// it in
MaybeDirectHandle<JSTemporalPlainDateTime> ToTemporalDateTime(
    Isolate* isolate, DirectHandle<Object> item,
    std::optional<temporal_rs::ArithmeticOverflow> overflow,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. If options is not present, set options to undefined.
  // (handled by caller)

  // This error is eventually thrown by step 3; we perform a check early
  // so that we can optimize with InstanceType. Step 1 and 2 are unobservable.
  if (!IsHeapObject(*item)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  InstanceType instance_type =
      Cast<HeapObject>(*item)->map(isolate)->instance_type();

  // 2. If item is an Object, then
  if (InstanceTypeChecker::IsJSReceiver(instance_type)) {
    auto record = kNullPartialDateTime;
    // a. If item has an [[InitializedTemporalDateTime]] internal slot, then
    if (InstanceTypeChecker::IsJSTemporalPlainDateTime(instance_type)) {
      // iii. Return !CreateTemporalDate(item.[[Date]], item.[[Calendar]]).
      record = GetDateTimeRecord(Cast<JSTemporalPlainDateTime>(item));
      // b. If item has an [[InitializedTemporalZonedDateTime]] internal slot,
      // then
    } else if (InstanceTypeChecker::IsJSTemporalZonedDateTime(instance_type)) {
      // i. Let isoDateTime be GetISODateTimeFor(item.[[TimeZone]],
      // item.[[EpochNanoseconds]]).
      //
      // iv. Return !CreateTemporalDateTime(isoDateTime, item.[[Calendar]]).
      record = GetDateTimeRecord(Cast<JSTemporalZonedDateTime>(item));
      // c. If item has an [[InitializedTemporalDate]] internal slot, then
    } else if (InstanceTypeChecker::IsJSTemporalPlainDate(instance_type)) {
      // iii. Return !CreateTemporalDate(item.[[ISODateTime]].[[ISODate]],
      // item.[[Calendar]]).
      record = GetDateTimeRecord(Cast<JSTemporalPlainDate>(item));
    } else {
      // d. Let calendar be ?GetTemporalCalendarIdentifierWithISODefault(item).
      temporal_rs::AnyCalendarKind kind = temporal_rs::AnyCalendarKind::Iso;
      DirectHandle<JSReceiver> item_recvr = Cast<JSReceiver>(item);
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, kind,
          temporal::GetTemporalCalendarIdentifierWithISODefault(isolate,
                                                                item_recvr),
          kNullMaybeHandle);

      // e. Let fields be ?PrepareCalendarFields(calendar, item, « year, month,
      // month-code, day», « hour, minute, second, millisecond, microsecond,
      // nanosecond», «»).
      CombinedRecordOwnership owners;
      CombinedRecord fields = CombinedRecord{
          .date = kNullPartialDate,
          .time = kNullPartialTime,
          .offset = std::nullopt,
          .time_zone = std::nullopt,
      };
      using enum CalendarFieldsFlag;
      MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
          isolate, fields,
          PrepareCalendarFields(isolate, kind, item_recvr,
                                kAllDateFlags | kTimeFields,
                                RequiredFields::kNone, owners),
          kNullMaybeHandle);
      record.date = fields.date;
      record.time = fields.time;
    }

    return ConstructRustWrappingType<JSTemporalPlainDateTime>(
        isolate, CONSTRUCTOR(plain_date_time), CONSTRUCTOR(plain_date_time),
        temporal_rs::PlainDateTime::from_partial(record, overflow));

  } else {
    // 3. If item is not a String, throw a TypeError exception.
    if (!InstanceTypeChecker::IsString(instance_type)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
    }
    DirectHandle<String> str = Cast<String>(item);

    // Rest of the steps handled in Rust

    auto rust_result = HandleStringEncodings<
        TemporalAllocatedResult<temporal_rs::PlainDateTime>>(
        isolate, str,
        [](std::string_view view)
            -> TemporalAllocatedResult<temporal_rs::PlainDateTime> {
          return temporal_rs::PlainDateTime::from_utf8(view);
        },
        [](std::u16string_view view)
            -> TemporalAllocatedResult<temporal_rs::PlainDateTime> {
          return temporal_rs::PlainDateTime::from_utf16(view);
        });

    return ConstructRustWrappingType<JSTemporalPlainDateTime>(
        isolate, CONSTRUCTOR(plain_date_time), CONSTRUCTOR(plain_date_time),
        std::move(rust_result));
  }
}

// https://tc39.es/proposal-temporal/#sec-temporal-totemporalyearmonth
// Note this skips the options-parsing steps and instead asks the caller to pass
// it in
MaybeDirectHandle<JSTemporalPlainYearMonth> ToTemporalYearMonth(
    Isolate* isolate, DirectHandle<Object> item,
    std::optional<temporal_rs::ArithmeticOverflow> overflow,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. If options is not present, set options to undefined.
  // (handled by caller)

  // This error is eventually thrown by step 3; we perform a check early
  // so that we can optimize with InstanceType. Step 1 and 2 are unobservable.
  if (!IsHeapObject(*item)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  InstanceType instance_type =
      Cast<HeapObject>(*item)->map(isolate)->instance_type();
  // 2. If item is an Object, then
  if (InstanceTypeChecker::IsJSReceiver(instance_type)) {
    auto year = 0;
    auto month = 1;
    temporal_rs::AnyCalendarKind kind = temporal_rs::AnyCalendarKind::Iso;
    // a. If item has an [[InitializedTemporalYearMonth]] internal slot, then
    if (InstanceTypeChecker::IsJSTemporalPlainYearMonth(instance_type)) {
      auto cast = Cast<JSTemporalPlainYearMonth>(item);
      auto rust_object = cast->year_month()->raw();

      // iii. Return !CreateTemporalYearMonth(item.[[ISODate]],
      // item.[[Calendar]]).
      year = rust_object->year();
      month = rust_object->month();
      kind = rust_object->calendar().kind();
    } else {
      // b. Let calendar be ?GetTemporalCalendarIdentifierWithISODefault(item).
      DirectHandle<JSReceiver> item_recvr = Cast<JSReceiver>(item);
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, kind,
          temporal::GetTemporalCalendarIdentifierWithISODefault(isolate,
                                                                item_recvr),
          kNullMaybeHandle);

      // c. Let fields be ?PrepareCalendarFields(calendar, item, « year, month,
      // month-code», «», «»).
      CombinedRecordOwnership owners;
      CombinedRecord fields = CombinedRecord{
          .date = kNullPartialDate,
          .time = kNullPartialTime,
          .offset = std::nullopt,
          .time_zone = std::nullopt,
      };

      using enum CalendarFieldsFlag;

      MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
          isolate, fields,
          PrepareCalendarFields(isolate, kind, item_recvr,
                                kYearFields | kMonthFields,
                                RequiredFields::kNone, owners),
          kNullMaybeHandle);

      // Remaining steps handled in Rust

      // g. Return !CreateTemporalYearMonth(isoDate, calendar).

      // TODO(manishearth) We can handle this correctly once
      // https://github.com/boa-dev/temporal/pull/351 lands,
      // for now we do something mostly sensible that will not
      // throw errors for missing fields and will not handle month codes.
      year = fields.date.year.value_or(0);
      month = fields.date.month.value_or(1);
      kind = fields.date.calendar;
    }

    // (combined CreateTemporalYearMonth call)
    return ConstructRustWrappingType<JSTemporalPlainYearMonth>(
        isolate, CONSTRUCTOR(plain_year_month), CONSTRUCTOR(plain_year_month),
        temporal_rs::PlainYearMonth::try_new_with_overflow(
            year, month, std::nullopt, kind,
            overflow.value_or(temporal_rs::ArithmeticOverflow::Reject)));
  } else {
    // 3. If item is not a String, throw a TypeError exception.
    if (!InstanceTypeChecker::IsString(instance_type)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
    }
    DirectHandle<String> str = Cast<String>(item);

    // Rest of the steps handled in Rust

    auto rust_result = HandleStringEncodings<
        TemporalAllocatedResult<temporal_rs::PlainYearMonth>>(
        isolate, str,
        [](std::string_view view)
            -> TemporalAllocatedResult<temporal_rs::PlainYearMonth> {
          return temporal_rs::PlainYearMonth::from_utf8(view);
        },
        [](std::u16string_view view)
            -> TemporalAllocatedResult<temporal_rs::PlainYearMonth> {
          return temporal_rs::PlainYearMonth::from_utf16(view);
        });

    return ConstructRustWrappingType<JSTemporalPlainYearMonth>(
        isolate, CONSTRUCTOR(plain_year_month), CONSTRUCTOR(plain_year_month),
        std::move(rust_result));
  }
}

// https://tc39.es/proposal-temporal/#sec-temporal-totemporalzoneddatetime
// Note this skips the options-parsing steps and instead asks the caller to pass
// it in
MaybeDirectHandle<JSTemporalZonedDateTime> ToTemporalZonedDateTime(
    Isolate* isolate, DirectHandle<Object> item,
    std::optional<temporal_rs::Disambiguation> disambiguation,
    std::optional<temporal_rs::OffsetDisambiguation> offset_option,
    std::optional<temporal_rs::ArithmeticOverflow> overflow,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. If options is not present, set options to undefined.
  // (handled by caller)

  // This error is eventually thrown by step 3; we perform a check early
  // so that we can optimize with InstanceType. Step 1 and 2 are unobservable.
  if (!IsHeapObject(*item)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  InstanceType instance_type =
      Cast<HeapObject>(*item)->map(isolate)->instance_type();

  // 2. Let offsetBehaviour be option.
  // 3. Let matchBehaviour be match-exactly.
  // (handled in Rust)

  // 4. If item is an Object, then
  if (InstanceTypeChecker::IsJSReceiver(instance_type)) {
    // a. If item has an [[InitializedTemporalZonedDateTime]] internal slot,
    // then
    if (InstanceTypeChecker::IsJSTemporalZonedDateTime(instance_type)) {
      auto cast = Cast<JSTemporalZonedDateTime>(item);
      auto rust_object = cast->zoned_date_time()->raw();

      // vi. Return !CreateTemporalZonedDateTime(item.[[EpochNanoseconds]],
      // item.[[TimeZone]], item.[[Calendar]]).
      return ConstructRustWrappingType<JSTemporalZonedDateTime>(
          isolate, CONSTRUCTOR(zoned_date_time), CONSTRUCTOR(zoned_date_time),
          temporal_rs::ZonedDateTime::try_new(rust_object->epoch_nanoseconds(),
                                              rust_object->calendar().kind(),
                                              rust_object->timezone()));

    } else {
      // b. Let calendar be ?GetTemporalCalendarIdentifierWithISODefault(item).
      temporal_rs::AnyCalendarKind kind = temporal_rs::AnyCalendarKind::Iso;
      DirectHandle<JSReceiver> item_recvr = Cast<JSReceiver>(item);
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, kind,
          temporal::GetTemporalCalendarIdentifierWithISODefault(isolate,
                                                                item_recvr),
          kNullMaybeHandle);

      // e. c. Let fields be ? PrepareCalendarFields(calendar, item, « year,
      // month, month-code, day», « hour, minute, second, millisecond,
      // microsecond, nanosecond, offset, time-zone», « time-zone»).
      CombinedRecordOwnership owners;
      CombinedRecord fields = CombinedRecord{
          .date = kNullPartialDate,
          .time = kNullPartialTime,
          .offset = std::nullopt,
          .time_zone = std::nullopt,
      };
      using enum CalendarFieldsFlag;
      MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
          isolate, fields,
          PrepareCalendarFields(
              isolate, kind, item_recvr,
              kAllDateFlags | kTimeFields | kOffset | kTimeZone,
              RequiredFields::kTimeZone, owners),
          kNullMaybeHandle);

      auto record = temporal_rs::PartialZonedDateTime{
          .date = fields.date,
          .time = fields.time,
          .offset = std::nullopt,
          .timezone = nullptr,
      };
      if (fields.time_zone.has_value()) {
        record.timezone = fields.time_zone.value().get();
      }
      if (fields.offset.has_value()) {
        record.offset = fields.offset.value();
      }

      return ConstructRustWrappingType<JSTemporalZonedDateTime>(
          isolate, CONSTRUCTOR(zoned_date_time), CONSTRUCTOR(zoned_date_time),
          temporal_rs::ZonedDateTime::from_partial(
              record, overflow, disambiguation, offset_option));
    }

  } else {
    // 3. If item is not a String, throw a TypeError exception.
    if (!InstanceTypeChecker::IsString(instance_type)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
    }
    DirectHandle<String> str = Cast<String>(item);

    // Default value from GetTemporalDisambiguationOption
    auto disambiguation_defaulted =
        disambiguation.value_or(temporal_rs::Disambiguation::Compatible);
    auto offset_defaulted =
        offset_option.value_or(temporal_rs::OffsetDisambiguation::Reject);

    // Rest of the steps handled in Rust

    auto rust_result = HandleStringEncodings<
        TemporalAllocatedResult<temporal_rs::ZonedDateTime>>(
        isolate, str,
        [&disambiguation_defaulted, &offset_defaulted](std::string_view view)
            -> TemporalAllocatedResult<temporal_rs::ZonedDateTime> {
          return temporal_rs::ZonedDateTime::from_utf8(
              view, disambiguation_defaulted, offset_defaulted);
        },
        [&disambiguation_defaulted, &offset_defaulted](std::u16string_view view)
            -> TemporalAllocatedResult<temporal_rs::ZonedDateTime> {
          return temporal_rs::ZonedDateTime::from_utf16(
              view, disambiguation_defaulted, offset_defaulted);
        });

    return ConstructRustWrappingType<JSTemporalZonedDateTime>(
        isolate, CONSTRUCTOR(zoned_date_time), CONSTRUCTOR(zoned_date_time),
        std::move(rust_result));
  }
}

// https://tc39.es/proposal-temporal/#sec-temporal-totemporalmonthday
// Note this skips the options-parsing steps and instead asks the caller to pass
// it in
MaybeDirectHandle<JSTemporalPlainMonthDay> ToTemporalMonthDay(
    Isolate* isolate, DirectHandle<Object> item,
    std::optional<temporal_rs::ArithmeticOverflow> overflow,
    const char* method_name) {
  TEMPORAL_ENTER_FUNC();
  // 1. If options is not present, set options to undefined.
  // (handled by caller)

  // This error is eventually thrown by step 3; we perform a check early
  // so that we can optimize with InstanceType. Step 1 and 2 are unobservable.
  if (!IsHeapObject(*item)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  InstanceType instance_type =
      Cast<HeapObject>(*item)->map(isolate)->instance_type();
  // 2. If item is an Object, then
  if (InstanceTypeChecker::IsJSReceiver(instance_type)) {
    std::optional<int32_t> year = std::nullopt;
    auto month = 1;
    auto day = 1;
    temporal_rs::AnyCalendarKind kind = temporal_rs::AnyCalendarKind::Iso;
    // a. If item has an [[InitializedTemporalMonthDay]] internal slot, then
    if (InstanceTypeChecker::IsJSTemporalPlainMonthDay(instance_type)) {
      auto cast = Cast<JSTemporalPlainMonthDay>(item);
      auto rust_object = cast->month_day()->raw();

      // iii. Return !CreateTemporalMonthDay(item.[[ISODate]],
      // item.[[Calendar]]).

      // TODO(manishearth) This only works for ISO, we can fix
      // it after https://github.com/boa-dev/temporal/pull/351 lands
      day = rust_object->iso_day();
      month = rust_object->iso_month();
      kind = rust_object->calendar().kind();
    } else {
      // b. Let calendar be ?GetTemporalCalendarIdentifierWithISODefault(item).
      DirectHandle<JSReceiver> item_recvr = Cast<JSReceiver>(item);
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, kind,
          temporal::GetTemporalCalendarIdentifierWithISODefault(isolate,
                                                                item_recvr),
          kNullMaybeHandle);

      // c. Let fields be ?PrepareCalendarFields(calendar, item, « year, month,
      // month-code, day», «», «»).
      CombinedRecordOwnership owners;
      CombinedRecord fields = CombinedRecord{
          .date = kNullPartialDate,
          .time = kNullPartialTime,
          .offset = std::nullopt,
          .time_zone = std::nullopt,
      };

      using enum CalendarFieldsFlag;

      MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
          isolate, fields,
          PrepareCalendarFields(isolate, kind, item_recvr,
                                kYearFields | kMonthFields,
                                RequiredFields::kNone, owners),
          kNullMaybeHandle);

      // Remaining steps handled in Rust

      // g. Return !CreateTemporalMonthDay(isoDate, calendar).

      // TODO(manishearth) We can handle this correctly once
      // https://github.com/boa-dev/temporal/pull/351 lands,
      // for now we do something mostly sensible that will not
      // throw errors for missing fields and will not handle month codes.
      year = fields.date.year;
      month = fields.date.month.value_or(1);
      day = fields.date.day.value_or(1);
      kind = fields.date.calendar;
    }

    // (combined CreateTemporalMonthDay call)
    return ConstructRustWrappingType<JSTemporalPlainMonthDay>(
        isolate, CONSTRUCTOR(plain_month_day), CONSTRUCTOR(plain_month_day),
        temporal_rs::PlainMonthDay::try_new_with_overflow(
            month, day, kind,
            overflow.value_or(temporal_rs::ArithmeticOverflow::Reject), year));
  } else {
    // 3. If item is not a String, throw a TypeError exception.
    if (!InstanceTypeChecker::IsString(instance_type)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
    }
    DirectHandle<String> str = Cast<String>(item);
    DirectHandle<JSTemporalPlainDate> result;

    // Rest of the steps handled in Rust

    auto rust_result = HandleStringEncodings<
        TemporalAllocatedResult<temporal_rs::PlainMonthDay>>(
        isolate, str,
        [](std::string_view view)
            -> TemporalAllocatedResult<temporal_rs::PlainMonthDay> {
          return temporal_rs::PlainMonthDay::from_utf8(view);
        },
        [](std::u16string_view view)
            -> TemporalAllocatedResult<temporal_rs::PlainMonthDay> {
          return temporal_rs::PlainMonthDay::from_utf16(view);
        });

    return ConstructRustWrappingType<JSTemporalPlainMonthDay>(
        isolate, CONSTRUCTOR(plain_month_day), CONSTRUCTOR(plain_month_day),
        std::move(rust_result));
  }
}

// A class that wraps a PlainDate or a ZonedDateTime that allows
// them to be either owned in a unique_ptr or borrowed.
//
// Setters should only be called once (this is not a safety
// invariant, but the spec should not be setting things multiple
// times)
class OwnedRelativeTo {
 public:
  OwnedRelativeTo()
      : date_(std::nullopt),
        zoned_(std::nullopt),
        date_ptr_(nullptr),
        zoned_ptr_(nullptr) {}

  // These methods are not constructors so that they can be explicitly invoked,
  // to avoid e.g. passing in an owned type as a pointer.
  static OwnedRelativeTo Owned(std::unique_ptr<temporal_rs::PlainDate>&& val) {
    OwnedRelativeTo ret;
    ret.date_ = std::move(val);
    ret.date_ptr_ = val.get();
    return ret;
  }
  static OwnedRelativeTo Owned(
      std::unique_ptr<temporal_rs::ZonedDateTime>&& val) {
    OwnedRelativeTo ret;
    ret.zoned_ = std::move(val);
    ret.zoned_ptr_ = val.get();
    return ret;
  }

  static OwnedRelativeTo Borrowed(temporal_rs::PlainDate const* val) {
    OwnedRelativeTo ret;
    ret.date_ptr_ = val;
    return ret;
  }

  static OwnedRelativeTo Borrowed(temporal_rs::ZonedDateTime const* val) {
    OwnedRelativeTo ret;
    ret.zoned_ptr_ = val;
    return ret;
  }
  temporal_rs::RelativeTo ToRust() const {
    return temporal_rs::RelativeTo{
        .date = date_ptr_,
        .zoned = zoned_ptr_,
    };
  }

 private:
  std::optional<std::unique_ptr<temporal_rs::PlainDate>> date_;
  std::optional<std::unique_ptr<temporal_rs::ZonedDateTime>> zoned_;

  temporal_rs::PlainDate const* date_ptr_;
  temporal_rs::ZonedDateTime const* zoned_ptr_;
};

// https://tc39.es/proposal-temporal/#sec-temporal-gettemporalrelativetooption
// Also handles the undefined case from GetOptionsObject
Maybe<OwnedRelativeTo> GetTemporalRelativeToOptionHandleUndefined(
    Isolate* isolate, DirectHandle<Object> options) {
  OwnedRelativeTo ret;

  // Default is empty
  if (IsUndefined(*options)) {
    return Just(OwnedRelativeTo());
  }

  if (!IsJSReceiver(*options)) {
    // (GetOptionsObject) 3. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<OwnedRelativeTo>());
  }

  // 1. Let value be ?Get(options, "relativeTo").
  DirectHandle<Object> value;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value,
      JSReceiver::GetProperty(isolate, Cast<JSReceiver>(options),
                              isolate->factory()->relativeTo_string()),
      Nothing<OwnedRelativeTo>());

  // 2. If value is undefined, return the Record { [[PlainRelativeTo]]:
  // undefined, [[ZonedRelativeTo]]: undefined }.
  if (IsUndefined(*value)) {
    return Just(OwnedRelativeTo());
  }

  // 3. Let offsetBehaviour be option.
  // 4. Let matchBehaviour be match-exactly.

  // This error is eventually thrown by step 6a; we perform a check early
  // so that we can optimize with InstanceType. Step 5-6 are unobservable
  // in this case.
  if (!IsHeapObject(*value)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 {});
  }
  InstanceType instance_type =
      Cast<HeapObject>(*value)->map(isolate)->instance_type();

  // 5. If value is an Object, then
  if (InstanceTypeChecker::IsJSReceiver(instance_type)) {
    // a. If value has an [[InitializedTemporalZonedDateTime]] internal slot,
    // then
    if (InstanceTypeChecker::IsJSTemporalZonedDateTime(instance_type)) {
      // i. Return the Record { [[PlainRelativeTo]]: undefined,
      // [[ZonedRelativeTo]]: value }.
      return Just(OwnedRelativeTo::Borrowed(
          Cast<JSTemporalZonedDateTime>(value)->zoned_date_time()->raw()));
    }

    // b. If value has an [[InitializedTemporalDate]] internal slot, then
    if (InstanceTypeChecker::IsJSTemporalPlainDate(instance_type)) {
      // i. Return the Record { [[PlainRelativeTo]]: value, [[ZonedRelativeTo]]:
      // undefined }.
      return Just(OwnedRelativeTo::Borrowed(
          Cast<JSTemporalPlainDate>(value)->date()->raw()));
    }

    // c. If value has an [[InitializedTemporalDateTime]] internal slot, then
    if (InstanceTypeChecker::IsJSTemporalPlainDateTime(instance_type)) {
      // i. Let plainDate be
      // !CreateTemporalDate(value.[[ISODateTime]].[[ISODate]],
      // value.[[Calendar]]).
      auto date_record = GetDateRecord(Cast<JSTemporalPlainDate>(value));
      std::unique_ptr<temporal_rs::PlainDate> plain_date = nullptr;

      MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
          isolate, plain_date,
          ExtractRustResult(isolate, temporal_rs::PlainDate::from_partial(
                                         date_record, std::nullopt)),
          {});
      // ii. Return the Record { [[PlainRelativeTo]]: plainDate,
      // [[ZonedRelativeTo]]: undefined }.
      return Just(OwnedRelativeTo::Owned(std::move(plain_date)));
    }
    // d. Let calendar be ? GetTemporalCalendarIdentifierWithISODefault(value).
    temporal_rs::AnyCalendarKind kind = temporal_rs::AnyCalendarKind::Iso;
    auto value_recvr = Cast<JSReceiver>(value);
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, kind,
        temporal::GetTemporalCalendarIdentifierWithISODefault(isolate,
                                                              value_recvr),
        {});
    // e. Let fields be ? PrepareCalendarFields(calendar, value, « year, month,
    // month-code, day », « hour, minute, second, millisecond, microsecond,
    // nanosecond, offset, time-zone », «»).
    CombinedRecordOwnership owners;
    CombinedRecord fields = CombinedRecord{
        .date = kNullPartialDate,
        .time = kNullPartialTime,
        .offset = std::nullopt,
        .time_zone = std::nullopt,
    };

    using enum CalendarFieldsFlag;
    MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
        isolate, fields,
        PrepareCalendarFields(isolate, kind, value_recvr,
                              kAllDateFlags | kTimeFields | kOffset | kTimeZone,
                              RequiredFields::kNone, owners),
        {});

    // f. Let result be ? InterpretTemporalDateTimeFields(calendar, fields,
    // constrain).
    auto overflow = temporal_rs::ArithmeticOverflow::Constrain;

    // (handled by the Constrain argument further down)

    auto record = temporal_rs::PartialZonedDateTime{
        .date = kNullPartialDate,
        .time = kNullPartialTime,
        .offset = std::nullopt,
        .timezone = nullptr,
    };

    // g. Let timeZone be fields.[[TimeZone]].
    if (fields.time_zone.has_value()) {
      record.timezone = fields.time_zone.value().get();
    }
    // h. Let offsetString be fields.[[OffsetString]].
    if (fields.offset.has_value()) {
      record.offset = fields.offset.value();
    }
    // j. Let isoDate be result.[[ISODate]].
    record.date = fields.date;
    // k. Let time be result.[[Time]].
    record.time = fields.time;

    // We use different construction methods for ZonedDateTime in these two
    // branches, so we've pulled steps 10-12 into this branch

    // 10. Let epochNanoseconds be ? InterpretISODateTimeOffset(isoDate, time,
    // offsetBehaviour, offsetNs, timeZone, compatible, reject, matchBehaviour).

    // 11. Let zonedRelativeTo be !
    // CreateTemporalZonedDateTime(epochNanoseconds, timeZone, calendar).

    std::unique_ptr<temporal_rs::ZonedDateTime> zoned_relative_to;
    MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
        isolate, zoned_relative_to,
        ExtractRustResult(
            isolate,
            temporal_rs::ZonedDateTime::from_partial(
                record, overflow, temporal_rs::Disambiguation::Compatible,
                temporal_rs::OffsetDisambiguation::Reject)),
        {});
    // 12. Return the Record { [[PlainRelativeTo]]: undefined,
    // [[ZonedRelativeTo]]: zonedRelativeTo }.
    return Just(OwnedRelativeTo::Owned(std::move(zoned_relative_to)));

    // 6. Else,
  } else {
    // a. If value is not a String, throw a TypeError exception.
    if (!InstanceTypeChecker::IsString(instance_type)) {
      THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                   NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(), {});
    }

    DirectHandle<String> str = Cast<String>(value);

    // 10. Let epochNanoseconds be ? InterpretISODateTimeOffset(isoDate, time,
    // offsetBehaviour, offsetNs, timeZone, compatible, reject, matchBehaviour).

    // 11. Let zonedRelativeTo be !
    // CreateTemporalZonedDateTime(epochNanoseconds, timeZone, calendar).

    auto disambiguation = temporal_rs::Disambiguation::Compatible;
    auto offset = temporal_rs::OffsetDisambiguation::Reject;

    // Rest of the steps handled in Rust

    auto rust_result = HandleStringEncodings<
        TemporalAllocatedResult<temporal_rs::ZonedDateTime>>(
        isolate, str,
        [&disambiguation, &offset](std::string_view view)
            -> TemporalAllocatedResult<temporal_rs::ZonedDateTime> {
          return temporal_rs::ZonedDateTime::from_utf8(view, disambiguation,
                                                       offset);
        },
        [&disambiguation, &offset](std::u16string_view view)
            -> TemporalAllocatedResult<temporal_rs::ZonedDateTime> {
          return temporal_rs::ZonedDateTime::from_utf16(view, disambiguation,
                                                        offset);
        });

    std::unique_ptr<temporal_rs::ZonedDateTime> zoned_relative_to;

    MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
        isolate, zoned_relative_to,
        ExtractRustResult(isolate, std::move(rust_result)), {});

    // 12. Return the Record { [[PlainRelativeTo]]: undefined,
    // [[ZonedRelativeTo]]: zonedRelativeTo }.
    return Just(OwnedRelativeTo::Owned(std::move(zoned_relative_to)));
  }
}

// ====== Difference operations ======

enum DifferenceOperation {
  kSince,
  kUntil,
};

MaybeDirectHandle<JSTemporalDuration> DifferenceTemporalInstant(
    Isolate* isolate, DifferenceOperation operation,
    DirectHandle<JSTemporalInstant> handle, DirectHandle<Object> other_obj,
    DirectHandle<Object> options, const char* method_name) {
  // 1. Set other to ?ToTemporalInstant(other).
  DirectHandle<JSTemporalInstant> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalInstant(isolate, other_obj, method_name));

  auto settings = temporal_rs::DifferenceSettings{.largest_unit = std::nullopt,
                                                  .smallest_unit = std::nullopt,
                                                  .rounding_mode = std::nullopt,
                                                  .increment = std::nullopt};

  // 2. Let resolvedOptions be ? GetOptionsObject(options).
  // 3. Let settings be ? GetDifferenceSettings(operation, resolvedOptions,
  // time, « », nanosecond, second).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, settings,
      temporal::GetDifferenceSettingsWithoutChecks(
          isolate, options, UnitGroup::kTime, Unit::Nanosecond, method_name),
      DirectHandle<JSTemporalDuration>());

  // Remaining steps handled by temporal_rs
  // operation negation (step 6) is also handled in temporal_rs
  auto this_rust = handle->instant()->raw();
  auto other_rust = other->instant()->raw();

  auto diff = operation == kUntil ? this_rust->until(*other_rust, settings)
                                  : this_rust->since(*other_rust, settings);

  return ConstructRustWrappingType<JSTemporalDuration>(
      isolate, CONSTRUCTOR(duration), CONSTRUCTOR(duration), std::move(diff));
}

MaybeDirectHandle<JSTemporalDuration> DifferenceTemporalPlainTime(
    Isolate* isolate, DifferenceOperation operation,
    DirectHandle<JSTemporalPlainTime> handle, DirectHandle<Object> other_obj,
    DirectHandle<Object> options, const char* method_name) {
  // 1. Set other to ?ToTemporalInstant(other).
  DirectHandle<JSTemporalPlainTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalTime(isolate, other_obj, std::nullopt, method_name));

  auto settings = temporal_rs::DifferenceSettings{.largest_unit = std::nullopt,
                                                  .smallest_unit = std::nullopt,
                                                  .rounding_mode = std::nullopt,
                                                  .increment = std::nullopt};

  // 2. Let resolvedOptions be ? GetOptionsObject(options).
  // 3. Let settings be ? GetDifferenceSettings(operation, resolvedOptions,
  // time, « », nanosecond, second).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, settings,
      temporal::GetDifferenceSettingsWithoutChecks(
          isolate, options, UnitGroup::kTime, Unit::Nanosecond, method_name),
      DirectHandle<JSTemporalDuration>());

  // Remaining steps handled by temporal_rs
  // operation negation (step 6) is also handled in temporal_rs
  auto this_rust = handle->time()->raw();
  auto other_rust = other->time()->raw();

  auto diff = operation == kUntil ? this_rust->until(*other_rust, settings)
                                  : this_rust->since(*other_rust, settings);

  return ConstructRustWrappingType<JSTemporalDuration>(
      isolate, CONSTRUCTOR(duration), CONSTRUCTOR(duration), std::move(diff));
}

// https://tc39.es/proposal-temporal/#sec-temporal-differencetemporalplaindate
MaybeDirectHandle<JSTemporalDuration> DifferenceTemporalPlainDate(
    Isolate* isolate, DifferenceOperation operation,
    DirectHandle<JSTemporalPlainDate> handle, DirectHandle<Object> other_obj,
    DirectHandle<Object> options, const char* method_name) {
  // 1. Set other to ?ToTemporalDate(other).
  DirectHandle<JSTemporalPlainDate> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalDate(isolate, other_obj, std::nullopt, method_name));

  auto settings = temporal_rs::DifferenceSettings{.largest_unit = std::nullopt,
                                                  .smallest_unit = std::nullopt,
                                                  .rounding_mode = std::nullopt,
                                                  .increment = std::nullopt};

  auto this_rust = handle->date()->raw();
  auto other_rust = other->date()->raw();

  // 2. If CalendarEquals(temporalDate.[[Calendar]], other.[[Calendar]]) is
  // false, throw a RangeError exception.
  if (this_rust->calendar().kind() != other_rust->calendar().kind()) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }

  // 3. Let resolvedOptions be ?GetOptionsObject(options).
  // 4. Let settings be ? GetDifferenceSettings(operation, resolvedOptions,
  // date, « », day, day).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, settings,
      temporal::GetDifferenceSettingsWithoutChecks(
          isolate, options, UnitGroup::kDate, Unit::Day, method_name),
      DirectHandle<JSTemporalDuration>());

  // Remaining steps handled by temporal_rs
  // operation negation (step 6) is also handled in temporal_rs

  auto diff = operation == kUntil ? this_rust->until(*other_rust, settings)
                                  : this_rust->since(*other_rust, settings);

  return ConstructRustWrappingType<JSTemporalDuration>(
      isolate, CONSTRUCTOR(duration), CONSTRUCTOR(duration), std::move(diff));
}

// https://tc39.es/proposal-temporal/#sec-temporal-differencetemporalplaindatetime
MaybeDirectHandle<JSTemporalDuration> DifferenceTemporalPlainDateTime(
    Isolate* isolate, DifferenceOperation operation,
    DirectHandle<JSTemporalPlainDateTime> handle,
    DirectHandle<Object> other_obj, DirectHandle<Object> options,
    const char* method_name) {
  // 1. Set other to ?ToTemporalDate(other).
  DirectHandle<JSTemporalPlainDateTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalDateTime(isolate, other_obj, std::nullopt,
                                   method_name));

  auto settings = temporal_rs::DifferenceSettings{.largest_unit = std::nullopt,
                                                  .smallest_unit = std::nullopt,
                                                  .rounding_mode = std::nullopt,
                                                  .increment = std::nullopt};

  auto this_rust = handle->date_time()->raw();
  auto other_rust = other->date_time()->raw();

  // 2. If CalendarEquals(dateTime.[[Calendar]], other.[[Calendar]]) is false,
  // throw a RangeError exception.
  if (this_rust->calendar().kind() != other_rust->calendar().kind()) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }

  // 3. Let resolvedOptions be ? GetOptionsObject(options).
  // 4. Let settings be ? GetDifferenceSettings(operation, resolvedOptions,
  // date, « », day, day).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, settings,
      temporal::GetDifferenceSettingsWithoutChecks(
          isolate, options, UnitGroup::kDateTime, Unit::Nanosecond,
          method_name),
      kNullMaybeHandle);

  // Remaining steps handled by temporal_rs
  // operation negation (step 6) is also handled in temporal_rs

  auto diff = operation == kUntil ? this_rust->until(*other_rust, settings)
                                  : this_rust->since(*other_rust, settings);

  return ConstructRustWrappingType<JSTemporalDuration>(
      isolate, CONSTRUCTOR(duration), CONSTRUCTOR(duration), std::move(diff));
}

// https://tc39.es/proposal-temporal/#sec-temporal-differencetemporalplainyearmonth
MaybeDirectHandle<JSTemporalDuration> DifferenceTemporalPlainYearMonth(
    Isolate* isolate, DifferenceOperation operation,
    DirectHandle<JSTemporalPlainYearMonth> handle,
    DirectHandle<Object> other_obj, DirectHandle<Object> options,
    const char* method_name) {
  // 1. Set other to ?ToTemporalYearMonth(other).
  DirectHandle<JSTemporalPlainYearMonth> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalYearMonth(isolate, other_obj, std::nullopt,
                                    method_name));

  auto settings = temporal_rs::DifferenceSettings{.largest_unit = std::nullopt,
                                                  .smallest_unit = std::nullopt,
                                                  .rounding_mode = std::nullopt,
                                                  .increment = std::nullopt};

  auto this_rust = handle->year_month()->raw();
  auto other_rust = other->year_month()->raw();
  // 2. Let calendar be yearMonth.[[Calendar]].
  // 3. If CalendarEquals(temporalDate.[[Calendar]], other.[[Calendar]]) is
  // false, throw a RangeError exception.
  if (this_rust->calendar().kind() != other_rust->calendar().kind()) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 4. Let resolvedOptions be ? GetOptionsObject(options).
  // 5. Let settings be ? GetDifferenceSettings(operation, resolvedOptions,
  // date, « », day, day).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, settings,
      temporal::GetDifferenceSettingsWithoutChecks(
          isolate, options, UnitGroup::kDate, Unit::Day, method_name),
      DirectHandle<JSTemporalDuration>());

  // Remaining steps handled by temporal_rs
  // operation negation (step 6) is also handled in temporal_rs

  auto diff = operation == kUntil ? this_rust->until(*other_rust, settings)
                                  : this_rust->since(*other_rust, settings);

  return ConstructRustWrappingType<JSTemporalDuration>(
      isolate, CONSTRUCTOR(duration), CONSTRUCTOR(duration), std::move(diff));
}

}  // namespace temporal

// https://tc39.es/proposal-temporal/#sec-temporal.duration
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> years,
    DirectHandle<Object> months, DirectHandle<Object> weeks,
    DirectHandle<Object> days, DirectHandle<Object> hours,
    DirectHandle<Object> minutes, DirectHandle<Object> seconds,
    DirectHandle<Object> milliseconds, DirectHandle<Object> microseconds,
    DirectHandle<Object> nanoseconds) {
  // 1. If NewTarget is undefined, then
  if (IsUndefined(*new_target)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "Temporal.Duration")));
  }
  // 2. Let y be ? ToIntegerIfIntegral(years).
  double y;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, y, temporal::ToIntegerIfIntegral(isolate, years),
      DirectHandle<JSTemporalDuration>());

  // 3. Let mo be ? ToIntegerIfIntegral(months).
  double mo;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, mo, temporal::ToIntegerIfIntegral(isolate, months),
      DirectHandle<JSTemporalDuration>());

  // 4. Let w be ? ToIntegerIfIntegral(weeks).
  double w;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, w, temporal::ToIntegerIfIntegral(isolate, weeks),
      DirectHandle<JSTemporalDuration>());

  // 5. Let d be ? ToIntegerIfIntegral(days).
  double d;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, d, temporal::ToIntegerIfIntegral(isolate, days),
      DirectHandle<JSTemporalDuration>());

  // 6. Let h be ? ToIntegerIfIntegral(hours).
  double h;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, h, temporal::ToIntegerIfIntegral(isolate, hours),
      DirectHandle<JSTemporalDuration>());

  // 7. Let m be ? ToIntegerIfIntegral(minutes).
  double m;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, m, temporal::ToIntegerIfIntegral(isolate, minutes),
      DirectHandle<JSTemporalDuration>());

  // 8. Let s be ? ToIntegerIfIntegral(seconds).
  double s;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, s, temporal::ToIntegerIfIntegral(isolate, seconds),
      DirectHandle<JSTemporalDuration>());

  // 9. Let ms be ? ToIntegerIfIntegral(milliseconds).
  double ms;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, ms, temporal::ToIntegerIfIntegral(isolate, milliseconds),
      DirectHandle<JSTemporalDuration>());

  // 10. Let mis be ? ToIntegerIfIntegral(microseconds).
  double mis;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, mis, temporal::ToIntegerIfIntegral(isolate, microseconds),
      DirectHandle<JSTemporalDuration>());

  // 11. Let ns be ? ToIntegerIfIntegral(nanoseconds).
  double ns;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, ns, temporal::ToIntegerIfIntegral(isolate, nanoseconds),
      DirectHandle<JSTemporalDuration>());

  // 12. Return ?CreateTemporalDuration(y, mo, w, d, h, m, s, ms, mis, ns,
  // NewTarget).
  return ConstructRustWrappingType<JSTemporalDuration>(
      isolate, CONSTRUCTOR(duration), CONSTRUCTOR(duration),
      temporal_rs::Duration::create(y, mo, w, d, h, m, s, ms, mis, ms));
}

// https://tc39.es/proposal-temporal/#sec-temporal.duration.compare
MaybeDirectHandle<Smi> JSTemporalDuration::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj, DirectHandle<Object> options_obj) {
  const char method_name[] = "Temporal.Duration.compare";
  DirectHandle<JSTemporalDuration> one;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, one,
      temporal::ToTemporalDuration(isolate, one_obj, method_name));
  DirectHandle<JSTemporalDuration> two;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, two,
      temporal::ToTemporalDuration(isolate, two_obj, method_name));

  temporal::OwnedRelativeTo relative_to;

  MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
      isolate, relative_to,
      temporal::GetTemporalRelativeToOptionHandleUndefined(isolate,
                                                           options_obj),
      kNullMaybeHandle);

  int8_t comparison = 0;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, comparison,
      ExtractRustResult(isolate,
                        one->duration()->raw()->compare(*two->duration()->raw(),
                                                        relative_to.ToRust())),
      kNullMaybeHandle);

  return direct_handle(Smi::FromInt(comparison), isolate);
}

// https://tc39.es/proposal-temporal/#sec-temporal.duration.from
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::From(
    Isolate* isolate, DirectHandle<Object> item) {
  const char method_name[] = "Temporal.Duration.from";

  return temporal::ToTemporalDuration(isolate, item, method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.duration.prototype.round
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Round(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> round_to_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.duration.prototype.total
// #sec-temporal.duration.prototype.total
MaybeDirectHandle<Number> JSTemporalDuration::Total(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> total_of_obj) {
  const char method_name[] = "Temporal.Duration.prototype.total";
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).

  // (handled by type system)

  // 3. If totalOf is undefined, throw a TypeError exception.
  if (IsUndefined(*total_of_obj)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }

  DirectHandle<JSReceiver> total_of;
  auto factory = isolate->factory();

  // 4. If totalOf is a String, then
  if (IsString(*total_of_obj)) {
    // a. Let paramString be totalOf.
    DirectHandle<String> param_string = Cast<String>(total_of_obj);
    // b. Set totalOf to ! OrdinaryObjectCreate(null).
    total_of = factory->NewJSObjectWithNullProto();
    // c. Perform ! CreateDataPropertyOrThrow(total_of, "unit", paramString).
    CHECK(JSReceiver::CreateDataProperty(isolate, total_of,
                                         factory->unit_string(), param_string,
                                         Just(kThrowOnError))
              .FromJust());
    // 5. Else,
  } else {
    // a. Set totalOf to ? GetOptionsObject(totalOf).
    // We have already checked for undefined, we can hoist the JSReceiver
    // check out and just cast
    if (!IsJSReceiver(*total_of_obj)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
    }
    total_of = Cast<JSReceiver>(total_of_obj);
  }

  // 6. NOTE (...)

  // 7. Let relativeToRecord be ? GetTemporalRelativeToOption(totalOf).
  // 8. Let zonedRelativeTo be relativeToRecord.[[ZonedRelativeTo]].
  // 9. Let plainRelativeTo be relativeToRecord.[[PlainRelativeTo]].

  temporal::OwnedRelativeTo relative_to;

  MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
      isolate, relative_to,
      temporal::GetTemporalRelativeToOptionHandleUndefined(isolate,
                                                           total_of_obj),
      kNullMaybeHandle);

  // 10. Let unit be ? GetTemporalUnitValuedOption(totalOf, "unit", datetime,
  // required).
  std::optional<Unit> unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, unit,
      temporal::GetTemporalUnit(isolate, total_of, "unit", UnitGroup::kDateTime,
                                std::nullopt, true, method_name),
      kNullMaybeHandle);
  // We set required to true.
  DCHECK(unit.has_value());

  // Remaining steps handled in Rust
  double ret;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, ret,
      ExtractRustResult(isolate, duration->duration()->raw()->total(
                                     unit.value(), relative_to.ToRust())),
      kNullMaybeHandle);

  return factory->NewNumber(ret);
}

// https://tc39.es/proposal-temporal/#sec-temporal.duration.prototype.with
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::With(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> temporal_duration_like) {
  temporal_rs::PartialDuration partial;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, partial,
      temporal::ToTemporalPartialDurationRecord(isolate,
                                                temporal_duration_like),
      DirectHandle<JSTemporalDuration>());
  if (!partial.years.has_value()) {
    partial.years = duration->duration()->raw()->years();
  }
  if (!partial.months.has_value()) {
    partial.months = duration->duration()->raw()->months();
  }
  if (!partial.months.has_value()) {
    partial.months = duration->duration()->raw()->months();
  }
  if (!partial.weeks.has_value()) {
    partial.weeks = duration->duration()->raw()->weeks();
  }
  if (!partial.days.has_value()) {
    partial.days = duration->duration()->raw()->days();
  }
  if (!partial.hours.has_value()) {
    partial.hours = duration->duration()->raw()->hours();
  }
  if (!partial.minutes.has_value()) {
    partial.minutes = duration->duration()->raw()->minutes();
  }
  if (!partial.seconds.has_value()) {
    partial.seconds = duration->duration()->raw()->seconds();
  }
  if (!partial.milliseconds.has_value()) {
    partial.milliseconds = duration->duration()->raw()->milliseconds();
  }
  if (!partial.microseconds.has_value()) {
    partial.microseconds = duration->duration()->raw()->microseconds();
  }
  if (!partial.nanoseconds.has_value()) {
    partial.nanoseconds = duration->duration()->raw()->nanoseconds();
  }
  return ConstructRustWrappingType<JSTemporalDuration>(
      isolate, CONSTRUCTOR(duration), CONSTRUCTOR(duration),
      temporal_rs::Duration::from_partial_duration(partial));
}

// https://tc39.es/proposal-temporal/#sec-get-temporal.duration.prototype.sign
MaybeDirectHandle<Smi> JSTemporalDuration::Sign(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  auto sign = duration->duration()->raw()->sign();
  return direct_handle(Smi::FromInt((temporal_rs::Sign::Value)sign), isolate);
}

// https://tc39.es/proposal-temporal/#sec-get-temporal.duration.prototype.blank
MaybeDirectHandle<Oddball> JSTemporalDuration::Blank(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  return isolate->factory()->ToBoolean(duration->duration()->raw()->sign() ==
                                       temporal_rs::Sign::Zero);
}

// https://tc39.es/proposal-temporal/#sec-temporal.duration.prototype.negated
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Negated(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  return ConstructRustWrappingType<JSTemporalDuration>(
      isolate, CONSTRUCTOR(duration), CONSTRUCTOR(duration),
      duration->duration()->raw()->negated());
}

// https://tc39.es/proposal-temporal/#sec-temporal.duration.prototype.abs
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Abs(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  return ConstructRustWrappingType<JSTemporalDuration>(
      isolate, CONSTRUCTOR(duration), CONSTRUCTOR(duration),
      duration->duration()->raw()->abs());
}

// https://tc39.es/proposal-temporal/#sec-temporal.duration.prototype.add
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Add(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  const char method_name[] = "Temporal.Duration.prototype.add";

  DirectHandle<JSTemporalDuration> other_duration;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other_duration,
      temporal::ToTemporalDuration(isolate, other, method_name));

  auto result =
      duration->duration()->raw()->add(*other_duration->duration()->raw());
  return ConstructRustWrappingType<JSTemporalDuration>(
      isolate, CONSTRUCTOR(duration), CONSTRUCTOR(duration), std::move(result));
}

// https://tc39.es/proposal-temporal/#sec-temporal.duration.prototype.subtract
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  const char method_name[] = "Temporal.Duration.prototype.subtract";

  DirectHandle<JSTemporalDuration> other_duration;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other_duration,
      temporal::ToTemporalDuration(isolate, other, method_name));

  auto result =
      duration->duration()->raw()->subtract(*other_duration->duration()->raw());
  return ConstructRustWrappingType<JSTemporalDuration>(
      isolate, CONSTRUCTOR(duration), CONSTRUCTOR(duration), std::move(result));
}

// https://tc39.es/proposal-temporal/#sec-temporal.duration.prototype.tojson
MaybeDirectHandle<String> JSTemporalDuration::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  return temporal::TemporalDurationToString(isolate, duration,
                                            std::move(temporal::kToStringAuto));
}

// https://tc39.es/proposal-temporal/#sec-temporal.duration.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalDuration::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  return temporal::TemporalDurationToString(isolate, duration,
                                            std::move(temporal::kToStringAuto));
}

// https://tc39.es/proposal-temporal/#sec-temporal.duration.prototype.tostring
MaybeDirectHandle<String> JSTemporalDuration::ToString(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> options_obj) {
  const char method_name[] = "Temporal.Duration.prototype.toString";

  // 3. Let resolvedOptions be ?GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 5. Let digits be ?GetTemporalFractionalSecondDigitsOption(resolvedOptions).

  temporal_rs::Precision digits;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, digits,
      temporal::GetTemporalFractionalSecondDigitsOption(isolate, options,
                                                        method_name),
      DirectHandle<String>());

  // 6. Let roundingMode be ? GetRoundingModeOption(resolvedOptions, trunc).

  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      temporal::GetRoundingModeOption(isolate, options, RoundingMode::Trunc,
                                      method_name),
      DirectHandle<String>());

  // 7. Let smallestUnit be ? GetTemporalUnitValuedOption(resolvedOptions,
  // "smallestUnit", time, unset).

  std::optional<Unit> smallest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, smallest_unit,
      temporal::GetTemporalUnit(isolate, options, "smallestUnit",
                                UnitGroup::kTime, std::nullopt, false,
                                method_name),
      DirectHandle<String>());

  // 8-17 performed by Rust
  auto rust_options = temporal_rs::ToStringRoundingOptions{
      .precision = digits,
      .smallest_unit = smallest_unit,
      .rounding_mode = rounding_mode,
  };

  return temporal::TemporalDurationToString(isolate, duration,
                                            std::move(rust_options));
}

MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> iso_year_obj,
    DirectHandle<Object> iso_month_obj, DirectHandle<Object> iso_day_obj,
    DirectHandle<Object> calendar_like) {
  // 1. If NewTarget is undefined, then
  if (IsUndefined(*new_target)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "Temporal.PlainDate")));
  }
  // 2. Let y be ? ToIntegerWithTruncation(isoYear).
  double y = 0;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, y, temporal::ToIntegerWithTruncation(isolate, iso_year_obj),
      kNullMaybeHandle);
  // 3. Let m be ? ToIntegerWithTruncation(isoMonth).
  double m = 0;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, m, temporal::ToIntegerWithTruncation(isolate, iso_month_obj),
      kNullMaybeHandle);
  // 4. Let d be ? ToIntegerWithTruncation(isoDay).
  double d = 0;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, d, temporal::ToIntegerWithTruncation(isolate, iso_day_obj),
      kNullMaybeHandle);

  // 5. If calendar is undefined, set calendar to "iso8601".
  temporal_rs::AnyCalendarKind calendar = temporal_rs::AnyCalendarKind::Iso;

  if (!IsUndefined(*calendar_like)) {
    // 6. If calendar is not a String, throw a TypeError exception.
    if (!IsString(*calendar_like)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
    }

    // 7. Set calendar to ?CanonicalizeCalendar(calendar).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, calendar,
        temporal::CanonicalizeCalendar(isolate, Cast<String>(calendar_like)),
        kNullMaybeHandle);
  }
  // 8. If IsValidISODate(y, m, d) is false, throw a RangeError exception.
  if (!temporal::IsValidIsoDate(y, m, d)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }

  // Rest of the steps handled in Rust

  // These static_casts are fine to perform since IsValid* will have constrained
  // these values to range already.
  // https://github.com/boa-dev/temporal/issues/334 for moving this logic into
  // the Rust code.
  auto rust_object = temporal_rs::PlainDate::try_new(
      static_cast<int32_t>(y), static_cast<uint8_t>(m), static_cast<uint8_t>(d),
      calendar);
  return ConstructRustWrappingType<JSTemporalPlainDate>(
      isolate, target, new_target, std::move(rust_object));
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.compare
MaybeDirectHandle<Smi> JSTemporalPlainDate::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  const char method_name[] = "Temporal.PlainDate.compare";
  DirectHandle<JSTemporalPlainDate> one;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, one,
      temporal::ToTemporalDate(isolate, one_obj, std::nullopt, method_name));
  DirectHandle<JSTemporalPlainDate> two;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, two,
      temporal::ToTemporalDate(isolate, two_obj, std::nullopt, method_name));

  return direct_handle(Smi::FromInt(temporal_rs::PlainDate::compare(
                           *one->date()->raw(), *two->date()->raw())),
                       isolate);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainDate::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> other_obj) {
  const char method_name[] = "Temporal.PlainDate.prototype.equals";

  DirectHandle<JSTemporalPlainDate> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalDate(isolate, other_obj, std::nullopt, method_name));

  auto equals = temporal_date->date()->raw()->equals(*other->date()->raw());

  return isolate->factory()->ToBoolean(equals);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.prototype.toplainyearmonth
MaybeDirectHandle<JSTemporalPlainYearMonth>
JSTemporalPlainDate::ToPlainYearMonth(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date) {
  return ConstructRustWrappingType<JSTemporalPlainYearMonth>(
      isolate, CONSTRUCTOR(plain_year_month), CONSTRUCTOR(plain_year_month),
      temporal_date->date()->raw()->to_plain_year_month());
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.prototype.toplainmonthday
MaybeDirectHandle<JSTemporalPlainMonthDay> JSTemporalPlainDate::ToPlainMonthDay(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date) {
  return ConstructRustWrappingType<JSTemporalPlainMonthDay>(
      isolate, CONSTRUCTOR(plain_year_month), CONSTRUCTOR(plain_year_month),
      temporal_date->date()->raw()->to_plain_month_day());
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.prototype.toplaindatetime
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDate::ToPlainDateTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> temporal_time_obj) {
  const char method_name[] = "Temporal.PlainDate.toPlainDateTime";
  const temporal_rs::PlainTime* maybe_time = nullptr;
  DirectHandle<JSTemporalPlainTime> time;
  if (!IsUndefined(*temporal_time_obj)) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time,
        temporal::ToTemporalTime(isolate, temporal_time_obj, std::nullopt,
                                 method_name));
    maybe_time = time->time()->raw();
  }
  return ConstructRustWrappingType<JSTemporalPlainDateTime>(
      isolate, CONSTRUCTOR(plain_date_time), CONSTRUCTOR(plain_date_time),
      temporal_date->date()->raw()->to_plain_date_time(maybe_time));
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.prototype.with
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::With(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> temporal_date_like_obj,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.prototype.withcalendar
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::WithCalendar(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> calendar_id) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.prototype.tozoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalPlainDate::ToZonedDateTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> item_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.prototype.add
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::Add(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> temporal_duration_like,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.prototype.subtract
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> temporal_duration_like,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainDate::Until(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.PlainDate.prototype.until";

  return temporal::DifferenceTemporalPlainDate(
      isolate, temporal::DifferenceOperation::kUntil, handle, other, options,
      method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainDate::Since(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.PlainDate.prototype.since";

  return temporal::DifferenceTemporalPlainDate(
      isolate, temporal::DifferenceOperation::kSince, handle, other, options,
      method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.now.plaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::Now(
    Isolate* isolate, DirectHandle<Object> calendar_like,
    DirectHandle<Object> temporal_time_zone_like) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.now.plaindateiso
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::NowISO(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.from
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::From(
    Isolate* isolate, DirectHandle<Object> item_obj,
    DirectHandle<Object> options_obj) {
  const char method_name[] = "Temporal.PlainDate.from";
  DirectHandle<JSTemporalPlainDate> item;

  // Options parsing hoisted out of ToTemporalTime
  // https://github.com/tc39/proposal-temporal/issues/3116
  temporal_rs::ArithmeticOverflow overflow;
  // (ToTemporalDate) i. Let resolvedOptions be ?GetOptionsObject(options).
  // (ToTemporalDate) ii. Perform ?GetTemporalOverflowOption(resolvedOptions).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, overflow,
      temporal::ToTemporalOverflowHandleUndefined(isolate, options_obj,
                                                  method_name),
      DirectHandle<JSTemporalPlainDate>());

  return temporal::ToTemporalDate(isolate, item_obj, overflow, method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainDate::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date) {
  return temporal::TemporalDateToString(isolate, temporal_date,
                                        temporal_rs::DisplayCalendar::Auto);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindate.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainDate::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> options_obj) {
  const char method_name[] = "Temporal.PlainDate.prototype.toString";

  // 3. Let resolvedOptions be ?GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 4. Let showCalendar be ?GetTemporalShowCalendarNameOption(resolvedOptions).
  temporal_rs::DisplayCalendar show_calendar;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, show_calendar,
      temporal::GetTemporalShowCalendarNameOption(isolate, options,
                                                  method_name),
      DirectHandle<String>());

  // 5. Return TemporalDateToString(temporalDate, showCalendar).
  return temporal::TemporalDateToString(isolate, temporal_date, show_calendar);
}

// https://tc39.es/proposal-temporal/#sup-temporal.plaindate.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalPlainDate::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  return temporal::TemporalDateToString(isolate, temporal_date,
                                        temporal_rs::DisplayCalendar::Auto);
}

// https://tc39.es/proposal-temporal/#sec-temporal-createtemporaldatetime
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> iso_year_obj,
    DirectHandle<Object> iso_month_obj, DirectHandle<Object> iso_day_obj,
    DirectHandle<Object> hour_obj, DirectHandle<Object> minute_obj,
    DirectHandle<Object> second_obj, DirectHandle<Object> millisecond_obj,
    DirectHandle<Object> microsecond_obj, DirectHandle<Object> nanosecond_obj,
    DirectHandle<Object> calendar_like) {
  // 1. If NewTarget is undefined, then
  if (IsUndefined(*new_target)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "Temporal.PlainDateTime")));
  }
  // 2. Set isoYear to ?ToIntegerWithTruncation(isoYear).
  double y = 0;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, y, temporal::ToIntegerWithTruncation(isolate, iso_year_obj),
      kNullMaybeHandle);
  // 3. Set isoMonth to ?ToIntegerWithTruncation(isoMonth).
  double m = 0;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, m, temporal::ToIntegerWithTruncation(isolate, iso_month_obj),
      kNullMaybeHandle);
  // 4. Set isoDay to ?ToIntegerWithTruncation(isoDay).
  double d = 0;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, d, temporal::ToIntegerWithTruncation(isolate, iso_day_obj),
      kNullMaybeHandle);

  // 5. If hour is undefined, set hour to 0; else set hour to ?
  // ToIntegerWithTruncation(hour).
  double hour = 0;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, hour, temporal::ToIntegerWithTruncation(isolate, hour_obj),
      kNullMaybeHandle);
  // 6. If minute is undefined, set minute to 0; else set minute to ?
  // ToIntegerWithTruncation(minute).
  double minute = 0;
  if (!IsUndefined(*minute_obj)) {
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, minute, temporal::ToIntegerWithTruncation(isolate, minute_obj),
        kNullMaybeHandle);
  }
  // 7. If second is undefined, set second to 0; else set second to ?
  // ToIntegerWithTruncation(second).
  double second = 0;
  if (!IsUndefined(*second_obj)) {
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, second, temporal::ToIntegerWithTruncation(isolate, second_obj),
        kNullMaybeHandle);
  }
  // 8. If millisecond is undefined, set millisecond to 0; else set millisecond
  // to ? ToIntegerWithTruncation(millisecond).
  double millisecond = 0;
  if (!IsUndefined(*millisecond_obj)) {
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, millisecond,
        temporal::ToIntegerWithTruncation(isolate, millisecond_obj),
        kNullMaybeHandle);
  }
  // 9. If microsecond is undefined, set microsecond to 0; else set microsecond
  // to ? ToIntegerWithTruncation(microsecond).

  double microsecond = 0;
  if (!IsUndefined(*microsecond_obj)) {
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, microsecond,
        temporal::ToIntegerWithTruncation(isolate, microsecond_obj),
        kNullMaybeHandle);
  }
  // 10. If nanosecond is undefined, set nanosecond to 0; else set nanosecond to
  // ? ToIntegerWithTruncation(nanosecond).
  double nanosecond = 0;
  if (!IsUndefined(*nanosecond_obj)) {
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, nanosecond,
        temporal::ToIntegerWithTruncation(isolate, nanosecond_obj),
        kNullMaybeHandle);
  }

  // 11. If calendar is undefined, set calendar to "iso8601".
  temporal_rs::AnyCalendarKind calendar = temporal_rs::AnyCalendarKind::Iso;

  if (!IsUndefined(*calendar_like)) {
    // 12. If calendar is not a String, throw a TypeError exception.
    if (!IsString(*calendar_like)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
    }

    // 13. Set calendar to ?CanonicalizeCalendar(calendar).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, calendar,
        temporal::CanonicalizeCalendar(isolate, Cast<String>(calendar_like)),
        kNullMaybeHandle);
  }
  // 14. If IsValidISODate(isoYear, isoMonth, isoDay) is false, throw a
  // RangeError exception.
  if (!temporal::IsValidIsoDate(y, m, d)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // 16. If IsValidTime(hour, minute, second, millisecond, microsecond,
  // nanosecond) is false, throw a RangeError exception.
  if (!temporal::IsValidTime(hour, minute, second, millisecond, microsecond,
                             nanosecond)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }

  // Rest of the steps handled in Rust.

  // These static_casts are fine to perform since IsValid* will have constrained
  // these values to range already.
  // https://github.com/boa-dev/temporal/issues/334 for moving this logic into
  // the Rust code.
  auto rust_object = temporal_rs::PlainDateTime::try_new(
      static_cast<int32_t>(y), static_cast<uint8_t>(m), static_cast<uint8_t>(d),
      static_cast<uint8_t>(hour), static_cast<uint8_t>(minute),
      static_cast<uint8_t>(second), static_cast<uint16_t>(millisecond),
      static_cast<uint16_t>(microsecond), static_cast<uint16_t>(nanosecond),
      calendar);
  return ConstructRustWrappingType<JSTemporalPlainDateTime>(
      isolate, target, new_target, std::move(rust_object));
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.from
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::From(
    Isolate* isolate, DirectHandle<Object> item_obj,
    DirectHandle<Object> options_obj) {
  const char method_name[] = "Temporal.PlainDateTime.from";
  DirectHandle<JSTemporalPlainDateTime> item;

  // Options parsing hoisted out of ToTemporalDateTime
  // https://github.com/tc39/proposal-temporal/issues/3116
  temporal_rs::ArithmeticOverflow overflow;
  // (ToTemporalDateTime) i. Let resolvedOptions be ?GetOptionsObject(options).
  // (ToTemporalDateTime) ii. Perform
  // ?GetTemporalOverflowOption(resolvedOptions).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, overflow,
      temporal::ToTemporalOverflowHandleUndefined(isolate, options_obj,
                                                  method_name),
      kNullMaybeHandle);

  return temporal::ToTemporalDateTime(isolate, item_obj, overflow, method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.compare
MaybeDirectHandle<Smi> JSTemporalPlainDateTime::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  const char method_name[] = "Temporal.PlainDateTime.compare";
  DirectHandle<JSTemporalPlainDateTime> one;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, one,
                             temporal::ToTemporalDateTime(
                                 isolate, one_obj, std::nullopt, method_name));
  DirectHandle<JSTemporalPlainDateTime> two;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, two,
                             temporal::ToTemporalDateTime(
                                 isolate, two_obj, std::nullopt, method_name));

  return direct_handle(Smi::FromInt(temporal_rs::PlainDateTime::compare(
                           *one->date_time()->raw(), *two->date_time()->raw())),
                       isolate);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainDateTime::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> other_obj) {
  const char method_name[] = "Temporal.PlainDateTime.prototype.equals";

  DirectHandle<JSTemporalPlainDateTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalDateTime(isolate, other_obj, std::nullopt,
                                   method_name));

  auto equals =
      date_time->date_time()->raw()->equals(*other->date_time()->raw());

  return isolate->factory()->ToBoolean(equals);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.with
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::With(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_date_time_like_obj,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.withcalendar
MaybeDirectHandle<JSTemporalPlainDateTime>
JSTemporalPlainDateTime::WithCalendar(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> temporal_date,
    DirectHandle<Object> calendar_id) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.withplaintime
MaybeDirectHandle<JSTemporalPlainDateTime>
JSTemporalPlainDateTime::WithPlainTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> plain_time_like) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.toplainyearmonth
MaybeDirectHandle<JSTemporalPlainYearMonth>
JSTemporalPlainDateTime::ToPlainYearMonth(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.toplainmonthday
MaybeDirectHandle<JSTemporalPlainMonthDay>
JSTemporalPlainDateTime::ToPlainMonthDay(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.tozoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalPlainDateTime::ToZonedDateTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_time_zone_like,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainDateTime::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  return temporal::ISODateTimeToString(isolate, date_time,
                                       std::move(temporal::kToStringAuto),
                                       temporal_rs::DisplayCalendar::Auto);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalPlainDateTime::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  return temporal::ISODateTimeToString(isolate, date_time,
                                       std::move(temporal::kToStringAuto),
                                       temporal_rs::DisplayCalendar::Auto);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainDateTime::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> options_obj) {
  const char method_name[] = "Temporal.DateTime.prototype.toString";

  // 3. Let resolvedOptions be ?GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 5. Let showCalendar be ?GetTemporalShowCalendarNameOption(resolvedOptions).
  temporal_rs::DisplayCalendar show_calendar;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, show_calendar,
      temporal::GetTemporalShowCalendarNameOption(isolate, options,
                                                  method_name),
      DirectHandle<String>());

  // 5. Let digits be ?GetTemporalFractionalSecondDigitsOption(resolvedOptions).
  temporal_rs::Precision digits;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, digits,
      temporal::GetTemporalFractionalSecondDigitsOption(isolate, options,
                                                        method_name),
      DirectHandle<String>());

  // 6. Let roundingMode be ? GetRoundingModeOption(resolvedOptions, trunc).
  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      temporal::GetRoundingModeOption(isolate, options, RoundingMode::Trunc,
                                      method_name),
      DirectHandle<String>());

  // 7. Let smallestUnit be ? GetTemporalUnitValuedOption(resolvedOptions,
  // "smallestUnit", time, unset).
  std::optional<Unit> smallest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, smallest_unit,
      temporal::GetTemporalUnit(isolate, options, "smallestUnit",
                                UnitGroup::kTime, std::nullopt, false,
                                method_name),
      DirectHandle<String>());

  // Rest of the steps handled in Rust
  auto rust_options = temporal_rs::ToStringRoundingOptions{
      .precision = digits,
      .smallest_unit = smallest_unit,
      .rounding_mode = rounding_mode,
  };
  return temporal::ISODateTimeToString(isolate, date_time,
                                       std::move(rust_options), show_calendar);
}

// https://tc39.es/proposal-temporal/#sec-temporal.now.plaindatetime
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Now(
    Isolate* isolate, DirectHandle<Object> calendar_like,
    DirectHandle<Object> temporal_time_zone_like) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.now.plaindatetimeiso
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::NowISO(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.round
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Round(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> round_to_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.add
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Add(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.subtract
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainDateTime::Until(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.PlainDateTime.prototype.until";

  return temporal::DifferenceTemporalPlainDateTime(
      isolate, temporal::DifferenceOperation::kUntil, handle, other, options,
      method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainDateTime::Since(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.PlainDateTime.prototype.since";

  return temporal::DifferenceTemporalPlainDateTime(
      isolate, temporal::DifferenceOperation::kSince, handle, other, options,
      method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.toplaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDateTime::ToPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaindatetime.prototype.toplaintime
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainDateTime::ToPlainTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainmonthday
MaybeDirectHandle<JSTemporalPlainMonthDay> JSTemporalPlainMonthDay::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> iso_month_obj,
    DirectHandle<Object> iso_day_obj, DirectHandle<Object> calendar_like,
    DirectHandle<Object> reference_iso_year_obj) {
  // 1. If NewTarget is undefined, then
  if (IsUndefined(*new_target)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "Temporal.PlainYearMonth")));
  }

  // 2. If referenceISOYear is undefined, then
  // a. Set referenceISOYear to 1𝔽.
  double reference_iso_year = 1972.0;

  // 3. Let m be ? ToIntegerWithTruncation(isoMonth).
  double m = 0;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, m, temporal::ToIntegerWithTruncation(isolate, iso_month_obj),
      kNullMaybeHandle);
  // 4. Let d be ? ToIntegerWithTruncation(isoYear).
  double d = 0;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, d, temporal::ToIntegerWithTruncation(isolate, iso_day_obj),
      kNullMaybeHandle);

  // 5. If calendar is undefined, set calendar to "iso8601".
  temporal_rs::AnyCalendarKind calendar = temporal_rs::AnyCalendarKind::Iso;

  if (!IsUndefined(*calendar_like)) {
    // 6. If calendar is not a String, throw a TypeError exception.
    if (!IsString(*calendar_like)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
    }

    // 7. Set calendar to ?CanonicalizeCalendar(calendar).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, calendar,
        temporal::CanonicalizeCalendar(isolate, Cast<String>(calendar_like)),
        kNullMaybeHandle);
  }

  if (!IsUndefined(*reference_iso_year_obj)) {
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, reference_iso_year,
        temporal::ToIntegerWithTruncation(isolate, reference_iso_year_obj),
        kNullMaybeHandle);
  }

  // 9. If IsValidISODate(ref, m, d) is false, throw a RangeError exception.
  if (!temporal::IsValidIsoDate(reference_iso_year, m, d)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // Rest of the steps handled in Rust.

  // These static_casts are fine to perform since IsValid* will have constrained
  // these values to range already.
  // https://github.com/boa-dev/temporal/issues/334 for moving this logic into
  // the Rust code.
  auto rust_object = temporal_rs::PlainMonthDay::try_new_with_overflow(
      static_cast<uint8_t>(m), static_cast<uint8_t>(d), calendar,
      temporal_rs::ArithmeticOverflow::Reject,
      static_cast<int32_t>(reference_iso_year));
  return ConstructRustWrappingType<JSTemporalPlainMonthDay>(
      isolate, target, new_target, std::move(rust_object));
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainmonthday.from
MaybeDirectHandle<JSTemporalPlainMonthDay> JSTemporalPlainMonthDay::From(
    Isolate* isolate, DirectHandle<Object> item_obj,
    DirectHandle<Object> options_obj) {
  const char method_name[] = "Temporal.PlainMonthDay.from";
  DirectHandle<JSTemporalPlainMonthDay> item;

  // Options parsing hoisted out of ToTemporalYearMonth
  // https://github.com/tc39/proposal-temporal/issues/3116
  temporal_rs::ArithmeticOverflow overflow;
  // (ToTemporalDate) i. Let resolvedOptions be ?GetOptionsObject(options).
  // (ToTemporalDate) ii. Perform ?GetTemporalOverflowOption(resolvedOptions).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, overflow,
      temporal::ToTemporalOverflowHandleUndefined(isolate, options_obj,
                                                  method_name),
      DirectHandle<JSTemporalPlainMonthDay>());

  return temporal::ToTemporalMonthDay(isolate, item_obj, overflow, method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainyearmonth.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainMonthDay::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
    DirectHandle<Object> other_obj) {
  const char method_name[] = "Temporal.PlainMonthDay.prototype.equals";

  DirectHandle<JSTemporalPlainMonthDay> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalMonthDay(isolate, other_obj, std::nullopt,
                                   method_name));

  auto equals =
      month_day->month_day()->raw()->equals(*other->month_day()->raw());

  return isolate->factory()->ToBoolean(equals);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainmonthday.prototype.with
MaybeDirectHandle<JSTemporalPlainMonthDay> JSTemporalPlainMonthDay::With(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> temporal_month_day,
    DirectHandle<Object> temporal_month_day_like_obj,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainmonthday.prototype.toplaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainMonthDay::ToPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
    DirectHandle<Object> item_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainmonthday.prototype.getisofields
MaybeDirectHandle<JSReceiver> JSTemporalPlainMonthDay::GetISOFields(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainmonthday.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainMonthDay::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainmonthday.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainMonthDay::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
    DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainmonthday.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalPlainMonthDay::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

MaybeDirectHandle<JSTemporalPlainYearMonth>
JSTemporalPlainYearMonth::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> iso_year_obj,
    DirectHandle<Object> iso_month_obj, DirectHandle<Object> calendar_like,
    DirectHandle<Object> reference_iso_day_obj) {
  // 1. If NewTarget is undefined, then
  if (IsUndefined(*new_target)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "Temporal.PlainYearMonth")));
  }

  // 2. If referenceISODay is undefined, then
  // a. Set referenceISODay to 1𝔽.
  double reference_iso_day = 1.0;

  // 3. Let y be ? ToIntegerWithTruncation(isoYear).
  double y = 0;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, y, temporal::ToIntegerWithTruncation(isolate, iso_year_obj),
      kNullMaybeHandle);
  // 4. Let m be ? ToIntegerWithTruncation(isoMonth).
  double m = 0;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, m, temporal::ToIntegerWithTruncation(isolate, iso_month_obj),
      kNullMaybeHandle);

  // 5. If calendar is undefined, set calendar to "iso8601".
  temporal_rs::AnyCalendarKind calendar = temporal_rs::AnyCalendarKind::Iso;

  if (!IsUndefined(*calendar_like)) {
    // 6. If calendar is not a String, throw a TypeError exception.
    if (!IsString(*calendar_like)) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
    }

    // 7. Set calendar to ?CanonicalizeCalendar(calendar).
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, calendar,
        temporal::CanonicalizeCalendar(isolate, Cast<String>(calendar_like)),
        kNullMaybeHandle);
  }

  if (!IsUndefined(*reference_iso_day_obj)) {
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, reference_iso_day,
        temporal::ToIntegerWithTruncation(isolate, reference_iso_day_obj),
        kNullMaybeHandle);
  }

  // 9. If IsValidISODate(y, m, ref) is false, throw a RangeError exception.
  if (!temporal::IsValidIsoDate(y, m, reference_iso_day)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }
  // Rest of the steps handled in Rust.

  // These static_casts are fine to perform since IsValid* will have constrained
  // these values to range already.
  // https://github.com/boa-dev/temporal/issues/334 for moving this logic into
  // the Rust code.
  auto rust_object = temporal_rs::PlainYearMonth::try_new_with_overflow(
      static_cast<int32_t>(y), static_cast<uint8_t>(m),
      static_cast<uint8_t>(reference_iso_day), calendar,
      temporal_rs::ArithmeticOverflow::Reject);
  return ConstructRustWrappingType<JSTemporalPlainYearMonth>(
      isolate, target, new_target, std::move(rust_object));
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainyearmonth.from
MaybeDirectHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::From(
    Isolate* isolate, DirectHandle<Object> item_obj,
    DirectHandle<Object> options_obj) {
  const char method_name[] = "Temporal.PlainYearMonth.from";
  DirectHandle<JSTemporalPlainYearMonth> item;

  // Options parsing hoisted out of ToTemporalYearMonth
  // https://github.com/tc39/proposal-temporal/issues/3116
  temporal_rs::ArithmeticOverflow overflow;
  // (ToTemporalDate) i. Let resolvedOptions be ?GetOptionsObject(options).
  // (ToTemporalDate) ii. Perform ?GetTemporalOverflowOption(resolvedOptions).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, overflow,
      temporal::ToTemporalOverflowHandleUndefined(isolate, options_obj,
                                                  method_name),
      DirectHandle<JSTemporalPlainYearMonth>());

  return temporal::ToTemporalYearMonth(isolate, item_obj, overflow,
                                       method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainyearmonth.compare
MaybeDirectHandle<Smi> JSTemporalPlainYearMonth::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  const char method_name[] = "Temporal.PlainYearMonth.compare";
  DirectHandle<JSTemporalPlainYearMonth> one;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, one,
                             temporal::ToTemporalYearMonth(
                                 isolate, one_obj, std::nullopt, method_name));
  DirectHandle<JSTemporalPlainYearMonth> two;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, two,
                             temporal::ToTemporalYearMonth(
                                 isolate, two_obj, std::nullopt, method_name));

  return direct_handle(
      Smi::FromInt(temporal_rs::PlainYearMonth::compare(
          *one->year_month()->raw(), *two->year_month()->raw())),
      isolate);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainyearmonth.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainYearMonth::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> other_obj) {
  const char method_name[] = "Temporal.PlainYearMonth.prototype.equals";

  DirectHandle<JSTemporalPlainYearMonth> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalYearMonth(isolate, other_obj, std::nullopt,
                                    method_name));

  auto equals =
      year_month->year_month()->raw()->equals(*other->year_month()->raw());

  return isolate->factory()->ToBoolean(equals);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainyearmonth.prototype.add
MaybeDirectHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::Add(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainyearmonth.prototype.subtract
MaybeDirectHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainyearmonth.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainYearMonth::Until(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.PlainYearMonth.prototype.until";

  return temporal::DifferenceTemporalPlainYearMonth(
      isolate, temporal::DifferenceOperation::kUntil, handle, other, options,
      method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainyearmonth.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainYearMonth::Since(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.PlainYearMonth.prototype.since";

  return temporal::DifferenceTemporalPlainYearMonth(
      isolate, temporal::DifferenceOperation::kSince, handle, other, options,
      method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainyearmonth.prototype.with
MaybeDirectHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::With(
    Isolate* isolate,
    DirectHandle<JSTemporalPlainYearMonth> temporal_year_month,
    DirectHandle<Object> temporal_year_month_like_obj,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainyearmonth.prototype.toplaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainYearMonth::ToPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> item_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainyearmonth.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainYearMonth::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainyearmonth.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainYearMonth::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plainyearmonth.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalPlainYearMonth::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal-plaintime-constructor
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> hour_obj,
    DirectHandle<Object> minute_obj, DirectHandle<Object> second_obj,
    DirectHandle<Object> millisecond_obj, DirectHandle<Object> microsecond_obj,
    DirectHandle<Object> nanosecond_obj) {
  // 1. If NewTarget is undefined, then
  if (IsUndefined(*new_target)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "Temporal.PlainTime")));
  }
  // 2. If hour is undefined, set hour to 0; else set hour to ?
  // ToIntegerWithTruncation(hour).
  double hour = 0;
  if (!IsUndefined(*hour_obj)) {
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, hour, temporal::ToIntegerWithTruncation(isolate, hour_obj),
        kNullMaybeHandle);
  }
  // 3. If minute is undefined, set minute to 0; else set minute to ?
  // ToIntegerWithTruncation(minute).
  double minute = 0;
  if (!IsUndefined(*minute_obj)) {
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, minute, temporal::ToIntegerWithTruncation(isolate, minute_obj),
        kNullMaybeHandle);
  }
  // 4. If second is undefined, set second to 0; else set second to ?
  // ToIntegerWithTruncation(second).
  double second = 0;
  if (!IsUndefined(*second_obj)) {
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, second, temporal::ToIntegerWithTruncation(isolate, second_obj),
        kNullMaybeHandle);
  }
  // 5. If millisecond is undefined, set millisecond to 0; else set millisecond
  // to ? ToIntegerWithTruncation(millisecond).
  double millisecond = 0;
  if (!IsUndefined(*millisecond_obj)) {
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, millisecond,
        temporal::ToIntegerWithTruncation(isolate, millisecond_obj),
        kNullMaybeHandle);
  }
  // 6. If microsecond is undefined, set microsecond to 0; else set microsecond
  // to ? ToIntegerWithTruncation(microsecond).

  double microsecond = 0;
  if (!IsUndefined(*microsecond_obj)) {
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, microsecond,
        temporal::ToIntegerWithTruncation(isolate, microsecond_obj),
        kNullMaybeHandle);
  }
  // 7. If nanosecond is undefined, set nanosecond to 0; else set nanosecond to
  // ? ToIntegerWithTruncation(nanosecond).

  double nanosecond = 0;
  if (!IsUndefined(*nanosecond_obj)) {
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, nanosecond,
        temporal::ToIntegerWithTruncation(isolate, nanosecond_obj),
        kNullMaybeHandle);
  }

  // 8. If IsValidTime(hour, minute, second, millisecond, microsecond,
  // nanosecond) is false, throw a RangeError exception.
  if (!temporal::IsValidTime(hour, minute, second, millisecond, microsecond,
                             nanosecond)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }

  // Rest of the steps handled in Rust

  // These static_casts are fine to perform since IsValid* will have constrained
  // these values to range already.
  // https://github.com/boa-dev/temporal/issues/334 for moving this logic into
  // the Rust code
  auto rust_object = temporal_rs::PlainTime::try_new(
      static_cast<uint8_t>(hour), static_cast<uint8_t>(minute),
      static_cast<uint8_t>(second), static_cast<uint16_t>(millisecond),
      static_cast<uint16_t>(microsecond), static_cast<uint16_t>(nanosecond));
  return ConstructRustWrappingType<JSTemporalPlainTime>(
      isolate, CONSTRUCTOR(plain_time), CONSTRUCTOR(plain_time),
      std::move(rust_object));
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaintime.compare
MaybeDirectHandle<Smi> JSTemporalPlainTime::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  const char method_name[] = "Temporal.PlainTime.compare";
  DirectHandle<JSTemporalPlainTime> one;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, one,
      temporal::ToTemporalTime(isolate, one_obj, std::nullopt, method_name));
  DirectHandle<JSTemporalPlainTime> two;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, two,
      temporal::ToTemporalTime(isolate, two_obj, std::nullopt, method_name));

  return direct_handle(Smi::FromInt(temporal_rs::PlainTime::compare(
                           *one->time()->raw(), *two->time()->raw())),
                       isolate);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaintime.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainTime::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> other_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaintime.prototype.round
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::Round(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> round_to_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaintime.prototype.with
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::With(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> temporal_time_like_obj,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.now.plaintimeiso
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::NowISO(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaintime.from
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::From(
    Isolate* isolate, DirectHandle<Object> item_obj,
    DirectHandle<Object> options_obj) {
  const char method_name[] = "Temporal.PlainTime.from";
  DirectHandle<JSTemporalPlainTime> item;

  // Options parsing hoisted out of ToTemporalTime
  // https://github.com/tc39/proposal-temporal/issues/3116
  temporal_rs::ArithmeticOverflow overflow;
  // (ToTemporalTime) i. Let resolvedOptions be ?GetOptionsObject(options).
  // (ToTemporalTime) ii. Perform ?GetTemporalOverflowOption(resolvedOptions).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, overflow,
      temporal::ToTemporalOverflowHandleUndefined(isolate, options_obj,
                                                  method_name),
      DirectHandle<JSTemporalPlainTime>());

  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, item,
      temporal::ToTemporalTime(isolate, item_obj, overflow, method_name));

  return item;
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaintime.prototype.add
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::Add(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> temporal_duration_like) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.PlainTime.prototype.add";

  DirectHandle<JSTemporalDuration> other_duration;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, other_duration,
                             temporal::ToTemporalDuration(
                                 isolate, temporal_duration_like, method_name));

  auto added =
      temporal_time->time()->raw()->add(*other_duration->duration()->raw());

  return ConstructRustWrappingType<JSTemporalPlainTime>(
      isolate, CONSTRUCTOR(plain_time), CONSTRUCTOR(plain_time),
      std::move(added));
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaintime.prototype.subtract
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> temporal_duration_like) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.PlainTime.prototype.subtract";

  DirectHandle<JSTemporalDuration> other_duration;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, other_duration,
                             temporal::ToTemporalDuration(
                                 isolate, temporal_duration_like, method_name));

  auto subtracted = temporal_time->time()->raw()->subtract(
      *other_duration->duration()->raw());

  return ConstructRustWrappingType<JSTemporalPlainTime>(
      isolate, CONSTRUCTOR(plain_time), CONSTRUCTOR(plain_time),
      std::move(subtracted));
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaintime.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainTime::Until(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.PlainTime.prototype.until";

  return temporal::DifferenceTemporalPlainTime(
      isolate, temporal::DifferenceOperation::kUntil, handle, other, options,
      method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaintime.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainTime::Since(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.PlainTime.prototype.since";

  return temporal::DifferenceTemporalPlainTime(
      isolate, temporal::DifferenceOperation::kSince, handle, other, options,
      method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaintime.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainTime::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time) {
  return temporal::TimeRecordToString(isolate, temporal_time,
                                      std::move(temporal::kToStringAuto));
}

// https://tc39.es/proposal-temporal/#sup-temporal.plaintime.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalPlainTime::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  return temporal::TimeRecordToString(isolate, temporal_time,
                                      std::move(temporal::kToStringAuto));
}

// https://tc39.es/proposal-temporal/#sec-temporal.plaintime.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainTime::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> options_obj) {
  const char method_name[] = "Temporal.PlainTime.prototype.toString";
  // 3. Let resolvedOptions be ?GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 5. Let digits be ?GetTemporalFractionalSecondDigitsOption(resolvedOptions).

  temporal_rs::Precision digits;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, digits,
      temporal::GetTemporalFractionalSecondDigitsOption(isolate, options,
                                                        method_name),
      DirectHandle<String>());

  // 6. Let roundingMode be ? GetRoundingModeOption(resolvedOptions, trunc).

  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      temporal::GetRoundingModeOption(isolate, options, RoundingMode::Trunc,
                                      method_name),
      DirectHandle<String>());

  // 7. Let smallestUnit be ? GetTemporalUnitValuedOption(resolvedOptions,
  // "smallestUnit", time, unset).

  std::optional<Unit> smallest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, smallest_unit,
      temporal::GetTemporalUnit(isolate, options, "smallestUnit",
                                UnitGroup::kTime, std::nullopt, false,
                                method_name),
      DirectHandle<String>());

  // 8-10 performed by Rust
  auto rust_options = temporal_rs::ToStringRoundingOptions{
      .precision = digits,
      .smallest_unit = smallest_unit,
      .rounding_mode = rounding_mode,
  };

  return temporal::TimeRecordToString(isolate, temporal_time,
                                      std::move(rust_options));
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target,
    DirectHandle<Object> epoch_nanoseconds_obj,
    DirectHandle<Object> time_zone_like, DirectHandle<Object> calendar_like) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-get-temporal.zoneddatetime.prototype.hoursinday
MaybeDirectHandle<Object> JSTemporalZonedDateTime::HoursInDay(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.from
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::From(
    Isolate* isolate, DirectHandle<Object> item_obj,
    DirectHandle<Object> options_obj) {
  const char method_name[] = "Temporal.ZonedDateTime.from";
  DirectHandle<JSTemporalPlainDateTime> item;

  // Options parsing hoisted out of ToTemporalZonedDateTime
  // https://github.com/tc39/proposal-temporal/issues/3116
  temporal_rs::Disambiguation disambiguation;
  temporal_rs::OffsetDisambiguation offset_option;
  temporal_rs::ArithmeticOverflow overflow;
  // (ToTemporalZonedDateTime) g. Let resolvedOptions be
  // ?GetOptionsObject(options).
  //
  // (ToTemporalZonedDateTime) h. Let disambiguation be
  // ?GetTemporalDisambiguationOption(resolvedOptions).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, disambiguation,
      temporal::GetTemporalDisambiguationOptionHandleUndefined(
          isolate, options_obj, method_name),
      kNullMaybeHandle);

  // (ToTemporalZonedDateTime) i. Let offsetOption be
  // ?GetTemporalOffsetOption(resolvedOptions, reject).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, offset_option,
      temporal::GetTemporalOffsetOptionHandleUndefined(
          isolate, options_obj, temporal_rs::OffsetDisambiguation::Reject,
          method_name),
      kNullMaybeHandle);
  // (ToTemporalZonedDateTime) ii. Perform
  // ?GetTemporalOverflowOption(resolvedOptions).
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, overflow,
      temporal::ToTemporalOverflowHandleUndefined(isolate, options_obj,
                                                  method_name),
      kNullMaybeHandle);

  return temporal::ToTemporalZonedDateTime(
      isolate, item_obj, disambiguation, offset_option, overflow, method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.compare
MaybeDirectHandle<Smi> JSTemporalZonedDateTime::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.ZonedDateTime.compare";
  DirectHandle<JSTemporalZonedDateTime> one;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, one,
                             temporal::ToTemporalZonedDateTime(
                                 isolate, one_obj, std::nullopt, std::nullopt,
                                 std::nullopt, method_name));
  DirectHandle<JSTemporalZonedDateTime> two;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, two,
                             temporal::ToTemporalZonedDateTime(
                                 isolate, two_obj, std::nullopt, std::nullopt,
                                 std::nullopt, method_name));

  return direct_handle(
      Smi::FromInt(one->zoned_date_time()->raw()->compare_instant(
          *two->zoned_date_time()->raw())),
      isolate);
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalZonedDateTime::Equals(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> other_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.with
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::With(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> temporal_zoned_date_time_like_obj,
    DirectHandle<Object> options_obj) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.withcalendar
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalZonedDateTime::WithCalendar(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> temporal_date,
    DirectHandle<Object> calendar_id) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.withplaintime
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalZonedDateTime::WithPlainTime(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> plain_time_like) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.withtimezone
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalZonedDateTime::WithTimeZone(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> time_zone_like) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.tojson
MaybeDirectHandle<String> JSTemporalZonedDateTime::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalZonedDateTime::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.tostring
MaybeDirectHandle<String> JSTemporalZonedDateTime::ToString(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.now.zoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Now(
    Isolate* isolate, DirectHandle<Object> calendar_like,
    DirectHandle<Object> temporal_time_zone_like) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.now.zoneddatetimeiso
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::NowISO(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.round
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Round(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> round_to_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.add
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Add(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}
// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.subtract
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalZonedDateTime::Until(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalZonedDateTime::Since(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.now.instant
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Now(Isolate* isolate) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-get-temporal.zoneddatetime.prototype.offsetnanoseconds
MaybeDirectHandle<Object> JSTemporalZonedDateTime::OffsetNanoseconds(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-get-temporal.zoneddatetime.prototype.epochnanoseconds
MaybeDirectHandle<BigInt> JSTemporalZonedDateTime::EpochNanoseconds(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-get-temporal.zoneddatetime.prototype.timezoneid
MaybeDirectHandle<String> JSTemporalZonedDateTime::TimeZoneId(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-get-temporal.zoneddatetime.prototype.offset
MaybeDirectHandle<String> JSTemporalZonedDateTime::Offset(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.startofday
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::StartOfDay(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}
// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.gettimezonetransition
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalZonedDateTime::GetTimeZoneTransition(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> direction_param) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.toinstant
MaybeDirectHandle<JSTemporalInstant> JSTemporalZonedDateTime::ToInstant(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.toplaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalZonedDateTime::ToPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.toplaintime
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalZonedDateTime::ToPlainTime(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.zoneddatetime.prototype.toplaindatetime
MaybeDirectHandle<JSTemporalPlainDateTime>
JSTemporalZonedDateTime::ToPlainDateTime(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  UNIMPLEMENTED();
}

namespace temporal {

// https://tc39.es/proposal-temporal/#sec-temporal-createtemporalinstant, but
// this also performs the validity check
MaybeDirectHandle<JSTemporalInstant> CreateTemporalInstantWithValidityCheck(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target,
    DirectHandle<BigInt> epoch_nanoseconds) {
  TEMPORAL_ENTER_FUNC();
  if (epoch_nanoseconds->Words64Count() > 2) {
    // 3. If ! IsValidEpochNanoseconds(epochNanoseconds) is false, throw a
    // RangeError exception.
    // Most validation is performed by the Instant ctor.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }

  uint64_t words[2] = {0, 0};
  uint32_t word_count = 2;
  int sign_bit = 0;
  epoch_nanoseconds->ToWordsArray64(&sign_bit, &word_count, words);

  if (words[1] > std::numeric_limits<int64_t>::max()) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR());
  }

  int64_t high = static_cast<int64_t>(words[1]);
  if (sign_bit == 1) {
    high = -high;
  }

  auto ns = temporal_rs::I128Nanoseconds{.high = high, .low = words[0]};

  return ConstructRustWrappingType<JSTemporalInstant>(
      isolate, target, new_target, temporal_rs::Instant::try_new(ns));
}

MaybeDirectHandle<JSTemporalInstant> CreateTemporalInstantWithValidityCheck(
    Isolate* isolate, DirectHandle<BigInt> epoch_nanoseconds) {
  TEMPORAL_ENTER_FUNC();
  return CreateTemporalInstantWithValidityCheck(
      isolate, CONSTRUCTOR(instant), CONSTRUCTOR(instant), epoch_nanoseconds);
}

}  // namespace temporal

// https://tc39.es/proposal-temporal/#sec-temporal.instant
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target,
    DirectHandle<Object> epoch_nanoseconds_obj) {
  TEMPORAL_ENTER_FUNC();
  // 1. If NewTarget is undefined, then
  if (IsUndefined(*new_target)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "Temporal.Instant")));
  }
  // 2. Let epochNanoseconds be ? ToBigInt(epochNanoseconds).
  DirectHandle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, epoch_nanoseconds,
      BigInt::FromObject(isolate, epoch_nanoseconds_obj));

  return temporal::CreateTemporalInstantWithValidityCheck(
      isolate, target, new_target, epoch_nanoseconds);
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.from
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::From(
    Isolate* isolate, DirectHandle<Object> item) {
  const char method_name[] = "Temporal.Instant.from";
  DirectHandle<JSTemporalInstant> item_instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, item_instant,
      temporal::ToTemporalInstant(isolate, item, method_name));

  return item_instant;
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.fromepochmilliseconds
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::FromEpochMilliseconds(
    Isolate* isolate, DirectHandle<Object> item) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.fromepochnanoseconds
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::FromEpochNanoseconds(
    Isolate* isolate, DirectHandle<Object> item) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.compare
MaybeDirectHandle<Smi> JSTemporalInstant::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalInstant::Equals(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> other_obj) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.Instant.prototype.equals";

  DirectHandle<JSTemporalInstant> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalInstant(isolate, other_obj, method_name));

  auto this_ns = handle->instant()->raw()->epoch_nanoseconds();
  auto other_ns = other->instant()->raw()->epoch_nanoseconds();

  // equals() isn't exposed over FFI, but it's easy enough to do here
  // in the future we can use https://github.com/boa-dev/temporal/pull/311
  return isolate->factory()->ToBoolean(this_ns.high == other_ns.high &&
                                       this_ns.low == other_ns.low);
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.prototype.round
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Round(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> round_to_obj) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.Instant.prototype.round";
  Factory* factory = isolate->factory();
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. If roundTo is undefined, then
  if (IsUndefined(*round_to_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR());
  }
  DirectHandle<JSReceiver> round_to;
  // 4. If Type(roundTo) is String, then
  if (IsString(*round_to_obj)) {
    // TODO(415359720) This could be done more efficiently, if we had better
    // GetStringOption APIs
    // a. Let paramString be roundTo.
    DirectHandle<String> param_string = Cast<String>(round_to_obj);
    // b. Set roundTo to ! OrdinaryObjectCreate(null).
    round_to = factory->NewJSObjectWithNullProto();
    // c. Perform ! CreateDataPropertyOrThrow(roundTo, "smallestUnit",
    // paramString).
    CHECK(JSReceiver::CreateDataProperty(isolate, round_to,
                                         factory->smallestUnit_string(),
                                         param_string, Just(kThrowOnError))
              .FromJust());
  } else {
    // a. Set roundTo to ? GetOptionsObject(roundTo).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, round_to,
        GetOptionsObject(isolate, round_to_obj, method_name));
  }

  // 7. Let roundingIncrement be ? GetRoundingIncrementOption(roundTo).
  uint32_t rounding_increment;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_increment,
      temporal::GetRoundingIncrementOption(isolate, round_to),
      DirectHandle<JSTemporalInstant>());

  // 8. Let roundingMode be ? GetRoundingModeOption(roundTo, half-expand).
  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      temporal::GetRoundingModeOption(isolate, round_to,
                                      RoundingMode::HalfExpand, method_name),
      DirectHandle<JSTemporalInstant>());

  // 9. Let smallestUnit be ? GetTemporalUnitValuedOption(roundTo,
  // "smallestUnit", time, required
  std::optional<Unit> smallest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, smallest_unit,
      temporal::GetTemporalUnit(isolate, round_to, "smallestUnit",
                                UnitGroup::kTime, std::nullopt, true,
                                method_name),
      DirectHandle<JSTemporalInstant>());

  auto options = temporal_rs::RoundingOptions{.largest_unit = std::nullopt,
                                              .smallest_unit = smallest_unit,
                                              .rounding_mode = rounding_mode,
                                              .increment = rounding_increment};

  auto rounded = handle->instant()->raw()->round(options);
  return ConstructRustWrappingType<JSTemporalInstant>(
      isolate, CONSTRUCTOR(instant), CONSTRUCTOR(instant), std::move(rounded));
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.prototype.epochmilliseconds
MaybeDirectHandle<Number> JSTemporalInstant::EpochMilliseconds(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle) {
  TEMPORAL_ENTER_FUNC();
  int64_t ms = handle->instant()->raw()->epoch_milliseconds();

  return isolate->factory()->NewNumberFromInt64(ms);
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.prototype.epochnanoseconds
MaybeDirectHandle<BigInt> JSTemporalInstant::EpochNanoseconds(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle) {
  TEMPORAL_ENTER_FUNC();
  temporal_rs::I128Nanoseconds ns =
      handle->instant()->raw()->epoch_nanoseconds();
  uint64_t words[2];
  bool sign_bit;
  if (ns.high < 0) {
    sign_bit = true;
    words[1] = static_cast<uint64_t>(-ns.high);
  } else {
    sign_bit = false;
    words[1] = static_cast<uint64_t>(ns.high);
  }
  words[0] = ns.low;
  return BigInt::FromWords64(isolate, sign_bit, 2, words);
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.prototype.tozoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalInstant::ToZonedDateTimeISO(Isolate* isolate,
                                      DirectHandle<JSTemporalInstant> handle,
                                      DirectHandle<Object> item_obj) {
  TEMPORAL_ENTER_FUNC();

  UNIMPLEMENTED();
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.prototype.tojson
MaybeDirectHandle<String> JSTemporalInstant::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalInstant> instant) {
  TEMPORAL_ENTER_FUNC();

  return temporal::TemporalInstantToString(isolate, instant, nullptr,
                                           std::move(temporal::kToStringAuto));
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalInstant::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  return temporal::TemporalInstantToString(isolate, instant, nullptr,
                                           std::move(temporal::kToStringAuto));
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.prototype.tostring
MaybeDirectHandle<String> JSTemporalInstant::ToString(
    Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
    DirectHandle<Object> options_obj) {
  const char method_name[] = "Temporal.Instant.prototype.toString";

  // 3. Set options to ? GetOptionsObject(options).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method_name));

  // 5. Let digits be ?
  // GetTemporalFractionalSecondDigitsOption(resolvedOptions).

  temporal_rs::Precision digits;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, digits,
      temporal::GetTemporalFractionalSecondDigitsOption(isolate, options,
                                                        method_name),
      DirectHandle<String>());

  // 6. Let roundingMode be ? GetRoundingModeOption(resolvedOptions, trunc).

  RoundingMode rounding_mode;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, rounding_mode,
      temporal::GetRoundingModeOption(isolate, options, RoundingMode::Trunc,
                                      method_name),
      DirectHandle<String>());

  // 7. Let smallestUnit be ? GetTemporalUnitValuedOption(resolvedOptions,
  // "smallestUnit", time, unset).
  std::optional<Unit> smallest_unit;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, smallest_unit,
      temporal::GetTemporalUnit(isolate, options, "smallestUnit",
                                UnitGroup::kTime, std::nullopt, false,
                                method_name),
      DirectHandle<String>());

  // 8. If smallestUnit is hour, throw a RangeError exception.
  if (smallest_unit == Unit::Hour) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                      isolate->factory()->smallestUnit_string()),
        DirectHandle<String>());
  }

  // 9. Let timeZone be ? Get(resolvedOptions, "timeZone").
  DirectHandle<Object> time_zone;
  //  Let val be ? Get(temporalDurationLike, fieldName).
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, time_zone,
      JSReceiver::GetProperty(isolate, options,
                              isolate->factory()->timeZone_string()),
      DirectHandle<String>());
  std::unique_ptr<temporal_rs::TimeZone> rust_time_zone;
  // 10. If timeZone is not undefined, then
  if (!IsUndefined(*time_zone)) {
    // a. Set timeZone to ? ToTemporalTimeZoneIdentifier(timeZone).
    MAYBE_MOVE_RETURN_ON_EXCEPTION_VALUE(
        isolate, rust_time_zone,
        temporal::ToTemporalTimeZoneIdentifier(isolate, time_zone),
        DirectHandle<String>());
  }

  auto rust_options = temporal_rs::ToStringRoundingOptions{
      .precision = digits,
      .smallest_unit = smallest_unit,
      .rounding_mode = rounding_mode,
  };

  return temporal::TemporalInstantToString(
      isolate, instant, rust_time_zone.get(), std::move(rust_options));
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.prototype.add
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Add(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> temporal_duration_like) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.Duration.prototype.add";

  DirectHandle<JSTemporalDuration> other_duration;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, other_duration,
                             temporal::ToTemporalDuration(
                                 isolate, temporal_duration_like, method_name));

  auto added =
      handle->instant()->raw()->add(*other_duration->duration()->raw());

  return ConstructRustWrappingType<JSTemporalInstant>(
      isolate, CONSTRUCTOR(instant), CONSTRUCTOR(instant), std::move(added));
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.prototype.subtract
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> temporal_duration_like) {
  TEMPORAL_ENTER_FUNC();

  const char method_name[] = "Temporal.Duration.prototype.subtract";

  DirectHandle<JSTemporalDuration> other_duration;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, other_duration,
                             temporal::ToTemporalDuration(
                                 isolate, temporal_duration_like, method_name));

  auto subtracted =
      handle->instant()->raw()->subtract(*other_duration->duration()->raw());

  return ConstructRustWrappingType<JSTemporalInstant>(
      isolate, CONSTRUCTOR(instant), CONSTRUCTOR(instant),
      std::move(subtracted));
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalInstant::Until(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> other_obj, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.Instant.prototype.until";

  return temporal::DifferenceTemporalInstant(
      isolate, temporal::DifferenceOperation::kUntil, handle, other_obj,
      options, method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.instant.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalInstant::Since(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> other_obj, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  const char method_name[] = "Temporal.Instant.prototype.since";

  return temporal::DifferenceTemporalInstant(
      isolate, temporal::DifferenceOperation::kSince, handle, other_obj,
      options, method_name);
}

// https://tc39.es/proposal-temporal/#sec-temporal.now.timezoneid
V8_WARN_UNUSED_RESULT MaybeDirectHandle<String> JSTemporalNowTimeZoneId(
    Isolate* isolate) {
  UNIMPLEMENTED();
}

namespace temporal {

// A simple convenient function to avoid the need to unnecessarily exposing
// the definition of enum Disambiguation.
MaybeDirectHandle<JSTemporalInstant> BuiltinTimeZoneGetInstantForCompatible(
    Isolate* isolate, DirectHandle<JSReceiver> time_zone,
    DirectHandle<JSTemporalPlainDateTime> date_time, const char* method_name) {
  UNIMPLEMENTED();
}

}  // namespace temporal
}  // namespace v8::internal
