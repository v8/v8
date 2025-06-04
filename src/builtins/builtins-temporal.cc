// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/objects/bigint.h"
#include "src/objects/js-temporal-objects-inl.h"

namespace v8 {
namespace internal {

#define TO_BE_IMPLEMENTED(id)   \
  BUILTIN_NO_RCS(id) {          \
    HandleScope scope(isolate); \
    UNIMPLEMENTED();            \
  }

#define TEMPORAL_NOW0(T)                                            \
  BUILTIN(TemporalNow##T) {                                         \
    HandleScope scope(isolate);                                     \
    RETURN_RESULT_OR_FAILURE(isolate, JSTemporal##T::Now(isolate)); \
  }

#define TEMPORAL_NOW2(T)                                                     \
  BUILTIN(TemporalNow##T) {                                                  \
    HandleScope scope(isolate);                                              \
    RETURN_RESULT_OR_FAILURE(                                                \
        isolate, JSTemporal##T::Now(isolate, args.atOrUndefined(isolate, 1), \
                                    args.atOrUndefined(isolate, 2)));        \
  }

#define TEMPORAL_NOW_ISO1(T)                                             \
  BUILTIN(TemporalNow##T##ISO) {                                         \
    HandleScope scope(isolate);                                          \
    RETURN_RESULT_OR_FAILURE(                                            \
        isolate,                                                         \
        JSTemporal##T::NowISO(isolate, args.atOrUndefined(isolate, 1))); \
  }

#define TEMPORAL_CONSTRUCTOR1(T)                                              \
  BUILTIN(Temporal##T##Constructor) {                                         \
    HandleScope scope(isolate);                                               \
    RETURN_RESULT_OR_FAILURE(                                                 \
        isolate,                                                              \
        JSTemporal##T::Constructor(isolate, args.target(), args.new_target(), \
                                   args.atOrUndefined(isolate, 1)));          \
  }

#define TEMPORAL_PROTOTYPE_METHOD0(T, METHOD, name)                          \
  BUILTIN(Temporal##T##Prototype##METHOD) {                                  \
    HandleScope scope(isolate);                                              \
    CHECK_RECEIVER(JSTemporal##T, obj, "Temporal." #T ".prototype." #name);  \
    RETURN_RESULT_OR_FAILURE(isolate, JSTemporal##T ::METHOD(isolate, obj)); \
  }

#define TEMPORAL_PROTOTYPE_METHOD1(T, METHOD, name)                            \
  BUILTIN(Temporal##T##Prototype##METHOD) {                                    \
    HandleScope scope(isolate);                                                \
    CHECK_RECEIVER(JSTemporal##T, obj, "Temporal." #T ".prototype." #name);    \
    RETURN_RESULT_OR_FAILURE(                                                  \
        isolate,                                                               \
        JSTemporal##T ::METHOD(isolate, obj, args.atOrUndefined(isolate, 1))); \
  }

#define TEMPORAL_PROTOTYPE_METHOD2(T, METHOD, name)                          \
  BUILTIN(Temporal##T##Prototype##METHOD) {                                  \
    HandleScope scope(isolate);                                              \
    CHECK_RECEIVER(JSTemporal##T, obj, "Temporal." #T ".prototype." #name);  \
    RETURN_RESULT_OR_FAILURE(                                                \
        isolate,                                                             \
        JSTemporal##T ::METHOD(isolate, obj, args.atOrUndefined(isolate, 1), \
                               args.atOrUndefined(isolate, 2)));             \
  }

#define TEMPORAL_PROTOTYPE_METHOD3(T, METHOD, name)                          \
  BUILTIN(Temporal##T##Prototype##METHOD) {                                  \
    HandleScope scope(isolate);                                              \
    CHECK_RECEIVER(JSTemporal##T, obj, "Temporal." #T ".prototype." #name);  \
    RETURN_RESULT_OR_FAILURE(                                                \
        isolate,                                                             \
        JSTemporal##T ::METHOD(isolate, obj, args.atOrUndefined(isolate, 1), \
                               args.atOrUndefined(isolate, 2),               \
                               args.atOrUndefined(isolate, 3)));             \
  }

#define TEMPORAL_METHOD1(T, METHOD)                                       \
  BUILTIN(Temporal##T##METHOD) {                                          \
    HandleScope scope(isolate);                                           \
    RETURN_RESULT_OR_FAILURE(                                             \
        isolate,                                                          \
        JSTemporal##T ::METHOD(isolate, args.atOrUndefined(isolate, 1))); \
  }

#define TEMPORAL_METHOD2(T, METHOD)                                     \
  BUILTIN(Temporal##T##METHOD) {                                        \
    HandleScope scope(isolate);                                         \
    RETURN_RESULT_OR_FAILURE(                                           \
        isolate,                                                        \
        JSTemporal##T ::METHOD(isolate, args.atOrUndefined(isolate, 1), \
                               args.atOrUndefined(isolate, 2)));        \
  }

#define TEMPORAL_VALUE_OF(T)                                                 \
  BUILTIN(Temporal##T##PrototypeValueOf) {                                   \
    HandleScope scope(isolate);                                              \
    THROW_NEW_ERROR_RETURN_FAILURE(                                          \
        isolate, NewTypeError(MessageTemplate::kDoNotUse,                    \
                              isolate->factory()->NewStringFromAsciiChecked( \
                                  "Temporal." #T ".prototype.valueOf"),      \
                              isolate->factory()->NewStringFromAsciiChecked( \
                                  "use Temporal." #T                         \
                                  ".prototype.compare for comparison.")));   \
  }

#define TEMPORAL_GET_SMI(T, METHOD, field)                   \
  BUILTIN(Temporal##T##Prototype##METHOD) {                  \
    HandleScope scope(isolate);                              \
    CHECK_RECEIVER(JSTemporal##T, obj,                       \
                   "get Temporal." #T ".prototype." #field); \
    return Smi::FromInt(obj->field());                       \
  }

#define TEMPORAL_METHOD1(T, METHOD)                                       \
  BUILTIN(Temporal##T##METHOD) {                                          \
    HandleScope scope(isolate);                                           \
    RETURN_RESULT_OR_FAILURE(                                             \
        isolate,                                                          \
        JSTemporal##T ::METHOD(isolate, args.atOrUndefined(isolate, 1))); \
  }

#define TEMPORAL_METHOD2(T, METHOD)                                     \
  BUILTIN(Temporal##T##METHOD) {                                        \
    HandleScope scope(isolate);                                         \
    RETURN_RESULT_OR_FAILURE(                                           \
        isolate,                                                        \
        JSTemporal##T ::METHOD(isolate, args.atOrUndefined(isolate, 1), \
                               args.atOrUndefined(isolate, 2)));        \
  }

#define TEMPORAL_GET(T, METHOD, field)                                       \
  BUILTIN(Temporal##T##Prototype##METHOD) {                                  \
    HandleScope scope(isolate);                                              \
    CHECK_RECEIVER(JSTemporal##T, obj, "Temporal." #T ".prototype." #field); \
    return obj->field();                                                     \
  }

// Like TEMPORAL_GET, but gets from an underlying Rust function
// rust_field is the name of the field with the Rust type. rust_getter is the
// name of the getter on the rust side (ideally the same as `field`). cvt is
// conversion code that converts `value` into the final returned JS Handle (use
// one of the macros below)
#define TEMPORAL_GET_RUST(T, rust_field, METHOD, js_field, rust_getter, cvt) \
  BUILTIN(Temporal##T##Prototype##METHOD) {                                  \
    HandleScope scope(isolate);                                              \
    CHECK_RECEIVER(JSTemporal##T, obj,                                       \
                   "Temporal." #T ".prototype." #js_field);                  \
    auto value = obj->rust_field()->raw()->rust_getter();                    \
    cvt                                                                      \
  }

#define CONVERT_INTEGER64 return *isolate->factory()->NewNumberFromInt64(value);
#define CONVERT_SMI return Smi::FromInt(value);
#define CONVERT_BOOLEAN return *isolate->factory()->ToBoolean(value);
#define CONVERT_DOUBLE return *isolate->factory()->NewNumber(value);
#define CONVERT_ASCII_STRING \
  return *isolate->factory()->NewStringFromAsciiChecked(value);

// converts nullopt to undefined
#define CONVERT_NULLABLE_INTEGER                          \
  if (value.has_value()) {                                \
    return *isolate->factory()->NewNumber(value.value()); \
  } else {                                                \
    return *isolate->factory()->undefined_value();        \
  }

// temporal_rs returns errors in a couple spots where it should return
// `undefined`
#define CONVERT_FALLIBLE_INTEGER_AS_NULLABLE                              \
  if (value.is_ok()) {                                                    \
    return *isolate->factory()->NewNumber(std::move(value).ok().value()); \
  } else {                                                                \
    return *isolate->factory()->undefined_value();                        \
  }

#define TEMPORAL_GET_NUMBER_AFTER_DIVID(T, M, field, scale, name)        \
  BUILTIN(Temporal##T##Prototype##M) {                                   \
    HandleScope scope(isolate);                                          \
    CHECK_RECEIVER(JSTemporal##T, handle,                                \
                   "get Temporal." #T ".prototype." #name);              \
    DirectHandle<BigInt> value;                                          \
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(                                  \
        isolate, value,                                                  \
        BigInt::Divide(isolate, direct_handle(handle->field(), isolate), \
                       BigInt::FromUint64(isolate, scale)));             \
    DirectHandle<Object> number = BigInt::ToNumber(isolate, value);      \
    DCHECK(std::isfinite(Object::NumberValue(*number)));                 \
    return *number;                                                      \
  }

#define TEMPORAL_GET_BIGINT_AFTER_DIVID(T, M, field, scale, name)        \
  BUILTIN(Temporal##T##Prototype##M) {                                   \
    HandleScope scope(isolate);                                          \
    CHECK_RECEIVER(JSTemporal##T, handle,                                \
                   "get Temporal." #T ".prototype." #name);              \
    RETURN_RESULT_OR_FAILURE(                                            \
        isolate,                                                         \
        BigInt::Divide(isolate, direct_handle(handle->field(), isolate), \
                       BigInt::FromUint64(isolate, scale)));             \
  }

// Now
TEMPORAL_NOW0(Instant)
TEMPORAL_NOW2(PlainDateTime)
TEMPORAL_NOW_ISO1(PlainDateTime)
TEMPORAL_NOW2(PlainDate)
TEMPORAL_NOW_ISO1(PlainDate)

// There is NO Temporal.now.plainTime
// See https://github.com/tc39/proposal-temporal/issues/1540
TEMPORAL_NOW_ISO1(PlainTime)
TEMPORAL_NOW2(ZonedDateTime)
TEMPORAL_NOW_ISO1(ZonedDateTime)

// PlainDate
BUILTIN(TemporalPlainDateConstructor) {
  HandleScope scope(isolate);
  RETURN_RESULT_OR_FAILURE(
      isolate, JSTemporalPlainDate::Constructor(
                   isolate, args.target(), args.new_target(),
                   args.atOrUndefined(isolate, 1),    // iso_year
                   args.atOrUndefined(isolate, 2),    // iso_month
                   args.atOrUndefined(isolate, 3),    // iso_day
                   args.atOrUndefined(isolate, 4)));  // calendar_like
}
TEMPORAL_METHOD2(PlainDate, From)
TEMPORAL_METHOD2(PlainDate, Compare)
TEMPORAL_GET_RUST(PlainDate, date, Year, year, year, CONVERT_INTEGER64)
TEMPORAL_GET_RUST(PlainDate, date, Era, era, era, CONVERT_ASCII_STRING)
TEMPORAL_GET_RUST(PlainDate, date, EraYear, eraYear, era_year,
                  CONVERT_NULLABLE_INTEGER)
TEMPORAL_GET_RUST(PlainDate, date, Month, month, month, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDate, date, Day, day, day, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDate, date, MonthCode, monthCode, month_code,
                  CONVERT_ASCII_STRING)
TEMPORAL_GET_RUST(PlainDate, date, DayOfWeek, dayOfWeek, day_of_week,
                  CONVERT_FALLIBLE_INTEGER_AS_NULLABLE)
TEMPORAL_GET_RUST(PlainDate, date, DayOfYear, dayOfYear, day_of_year,
                  CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDate, date, WeekOfYear, weekOfYear, week_of_year,
                  CONVERT_NULLABLE_INTEGER)
TEMPORAL_GET_RUST(PlainDate, date, DaysInWeek, daysInWeek, days_in_week,
                  CONVERT_FALLIBLE_INTEGER_AS_NULLABLE)
TEMPORAL_GET_RUST(PlainDate, date, DaysInMonth, daysInMonth, days_in_month,
                  CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDate, date, DaysInYear, daysInYear, days_in_year,
                  CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDate, date, MonthsInYear, monthsInYear, months_in_year,
                  CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDate, date, InLeapYear, inLeapYear, in_leap_year,
                  CONVERT_BOOLEAN)

TEMPORAL_PROTOTYPE_METHOD0(PlainDate, ToPlainYearMonth, toPlainYearMonth)
TEMPORAL_PROTOTYPE_METHOD0(PlainDate, ToPlainMonthDay, toPlainMonthDay)
TEMPORAL_PROTOTYPE_METHOD2(PlainDate, Add, add)
TEMPORAL_PROTOTYPE_METHOD2(PlainDate, Subtract, subtract)
TEMPORAL_PROTOTYPE_METHOD2(PlainDate, With, with)
TEMPORAL_PROTOTYPE_METHOD2(PlainDate, Since, since)
TEMPORAL_PROTOTYPE_METHOD2(PlainDate, Until, until)
TEMPORAL_PROTOTYPE_METHOD1(PlainDate, ToPlainDateTime, toPlainDateTime)
TEMPORAL_PROTOTYPE_METHOD1(PlainDate, ToZonedDateTime, toZonedDateTime)
TEMPORAL_PROTOTYPE_METHOD1(PlainDate, Equals, equals)
TEMPORAL_VALUE_OF(PlainDate)
TEMPORAL_PROTOTYPE_METHOD0(PlainDate, ToJSON, toJSON)
TEMPORAL_PROTOTYPE_METHOD2(PlainDate, ToLocaleString, toLocaleString)
TEMPORAL_PROTOTYPE_METHOD1(PlainDate, ToString, toString)

// PlainTime
BUILTIN(TemporalPlainTimeConstructor) {
  HandleScope scope(isolate);
  RETURN_RESULT_OR_FAILURE(isolate,
                           JSTemporalPlainTime::Constructor(
                               isolate, args.target(), args.new_target(),
                               args.atOrUndefined(isolate, 1),    // hour
                               args.atOrUndefined(isolate, 2),    // minute
                               args.atOrUndefined(isolate, 3),    // second
                               args.atOrUndefined(isolate, 4),    // millisecond
                               args.atOrUndefined(isolate, 5),    // microsecond
                               args.atOrUndefined(isolate, 6)));  // nanosecond
}

TEMPORAL_GET_RUST(PlainTime, time, Hour, hour, hour, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainTime, time, Minute, minute, minute, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainTime, time, Second, second, second, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainTime, time, Millisecond, millisecond, millisecond,
                  CONVERT_SMI)
TEMPORAL_GET_RUST(PlainTime, time, Microsecond, microsecond, microsecond,
                  CONVERT_SMI)
TEMPORAL_GET_RUST(PlainTime, time, Nanosecond, nanosecond, nanosecond,
                  CONVERT_SMI)
TEMPORAL_METHOD2(PlainTime, From)
TEMPORAL_METHOD2(PlainTime, Compare)
TEMPORAL_PROTOTYPE_METHOD1(PlainTime, Equals, equals)
TEMPORAL_PROTOTYPE_METHOD1(PlainTime, Add, add)
TEMPORAL_PROTOTYPE_METHOD1(PlainTime, Subtract, subtract)
TEMPORAL_PROTOTYPE_METHOD1(PlainTime, Round, round)
TEMPORAL_PROTOTYPE_METHOD2(PlainTime, Since, since)
TEMPORAL_PROTOTYPE_METHOD0(PlainTime, ToJSON, toJSON)
TEMPORAL_PROTOTYPE_METHOD2(PlainTime, ToLocaleString, toLocaleString)
TEMPORAL_PROTOTYPE_METHOD1(PlainTime, ToString, toString)
TEMPORAL_PROTOTYPE_METHOD2(PlainTime, Until, until)
TEMPORAL_PROTOTYPE_METHOD2(PlainTime, With, with)
TEMPORAL_VALUE_OF(PlainTime)

// PlainDateTime
BUILTIN(TemporalPlainDateTimeConstructor) {
  HandleScope scope(isolate);
  RETURN_RESULT_OR_FAILURE(
      isolate, JSTemporalPlainDateTime::Constructor(
                   isolate, args.target(), args.new_target(),
                   args.atOrUndefined(isolate, 1),     // iso_year
                   args.atOrUndefined(isolate, 2),     // iso_month
                   args.atOrUndefined(isolate, 3),     // iso_day
                   args.atOrUndefined(isolate, 4),     // hour
                   args.atOrUndefined(isolate, 5),     // minute
                   args.atOrUndefined(isolate, 6),     // second
                   args.atOrUndefined(isolate, 7),     // millisecond
                   args.atOrUndefined(isolate, 8),     // microsecond
                   args.atOrUndefined(isolate, 9),     // nanosecond
                   args.atOrUndefined(isolate, 10)));  // calendar_like
}

TEMPORAL_GET_RUST(PlainDateTime, date_time, Year, year, year, CONVERT_INTEGER64)
TEMPORAL_GET_RUST(PlainDateTime, date_time, Era, era, era, CONVERT_ASCII_STRING)
TEMPORAL_GET_RUST(PlainDateTime, date_time, EraYear, eraYear, era_year,
                  CONVERT_NULLABLE_INTEGER)
TEMPORAL_GET_RUST(PlainDateTime, date_time, Month, month, month, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDateTime, date_time, Day, day, day, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDateTime, date_time, MonthCode, monthCode, month_code,
                  CONVERT_ASCII_STRING)
TEMPORAL_GET_RUST(PlainDateTime, date_time, DayOfWeek, dayOfWeek, day_of_week,
                  CONVERT_FALLIBLE_INTEGER_AS_NULLABLE)
TEMPORAL_GET_RUST(PlainDateTime, date_time, DayOfYear, dayOfYear, day_of_year,
                  CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDateTime, date_time, WeekOfYear, weekOfYear,
                  week_of_year, CONVERT_NULLABLE_INTEGER)
TEMPORAL_GET_RUST(PlainDateTime, date_time, DaysInWeek, daysInWeek,
                  days_in_week, CONVERT_FALLIBLE_INTEGER_AS_NULLABLE)
TEMPORAL_GET_RUST(PlainDateTime, date_time, DaysInMonth, daysInMonth,
                  days_in_month, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDateTime, date_time, DaysInYear, daysInYear,
                  days_in_year, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDateTime, date_time, MonthsInYear, monthsInYear,
                  months_in_year, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDateTime, date_time, InLeapYear, inLeapYear,
                  in_leap_year, CONVERT_BOOLEAN)

TEMPORAL_GET_RUST(PlainDateTime, date_time, Hour, hour, hour, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDateTime, date_time, Minute, minute, minute, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDateTime, date_time, Second, second, second, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDateTime, date_time, Millisecond, millisecond,
                  millisecond, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDateTime, date_time, Microsecond, microsecond,
                  microsecond, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainDateTime, date_time, Nanosecond, nanosecond, nanosecond,
                  CONVERT_SMI)

TEMPORAL_PROTOTYPE_METHOD1(PlainDateTime, WithPlainTime, withPlainTime)

TEMPORAL_METHOD2(PlainDateTime, From)
TEMPORAL_METHOD2(PlainDateTime, Compare)
TEMPORAL_PROTOTYPE_METHOD1(PlainDateTime, Equals, equals)
TEMPORAL_PROTOTYPE_METHOD0(PlainDateTime, ToPlainYearMonth, toPlainYearMonth)
TEMPORAL_PROTOTYPE_METHOD0(PlainDateTime, ToPlainMonthDay, toPlainMonthDay)
TEMPORAL_PROTOTYPE_METHOD2(PlainDateTime, ToZonedDateTime, toZonedDateTime)
TEMPORAL_PROTOTYPE_METHOD1(PlainDateTime, WithPlainDate, withPlainDate)
TEMPORAL_PROTOTYPE_METHOD2(PlainDateTime, With, with)
TEMPORAL_PROTOTYPE_METHOD2(PlainDateTime, Add, add)
TEMPORAL_PROTOTYPE_METHOD1(PlainDateTime, Round, round)
TEMPORAL_PROTOTYPE_METHOD2(PlainDateTime, Since, since)
TEMPORAL_PROTOTYPE_METHOD2(PlainDateTime, Subtract, subtract)
TEMPORAL_PROTOTYPE_METHOD0(PlainDateTime, ToPlainDate, toPlainDate)
TEMPORAL_PROTOTYPE_METHOD0(PlainDateTime, ToPlainTime, toPlainTime)
TEMPORAL_PROTOTYPE_METHOD0(PlainDateTime, ToJSON, toJSON)
TEMPORAL_PROTOTYPE_METHOD2(PlainDateTime, ToLocaleString, toLocaleString)
TEMPORAL_PROTOTYPE_METHOD1(PlainDateTime, ToString, toString)
TEMPORAL_PROTOTYPE_METHOD2(PlainDateTime, Until, until)
TEMPORAL_VALUE_OF(PlainDateTime)

// PlainYearMonth
BUILTIN(TemporalPlainYearMonthConstructor) {
  HandleScope scope(isolate);
  RETURN_RESULT_OR_FAILURE(
      isolate, JSTemporalPlainYearMonth::Constructor(
                   isolate, args.target(), args.new_target(),
                   args.atOrUndefined(isolate, 1),    // iso_year
                   args.atOrUndefined(isolate, 2),    // iso_month
                   args.atOrUndefined(isolate, 3),    // calendar_like
                   args.atOrUndefined(isolate, 4)));  // reference_iso_day
}
TEMPORAL_GET_RUST(PlainYearMonth, year_month, Year, year, year,
                  CONVERT_INTEGER64)
TEMPORAL_GET_RUST(PlainYearMonth, year_month, Era, era, era,
                  CONVERT_ASCII_STRING)
TEMPORAL_GET_RUST(PlainYearMonth, year_month, EraYear, eraYear, era_year,
                  CONVERT_NULLABLE_INTEGER)
TEMPORAL_GET_RUST(PlainYearMonth, year_month, Month, month, month, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainYearMonth, year_month, MonthCode, monthCode, month_code,
                  CONVERT_ASCII_STRING)
TEMPORAL_GET_RUST(PlainYearMonth, year_month, DaysInMonth, daysInMonth,
                  days_in_month, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainYearMonth, year_month, DaysInYear, daysInYear,
                  days_in_year, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainYearMonth, year_month, MonthsInYear, monthsInYear,
                  months_in_year, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainYearMonth, year_month, InLeapYear, inLeapYear,
                  in_leap_year, CONVERT_BOOLEAN)

TEMPORAL_METHOD2(PlainYearMonth, From)
TEMPORAL_METHOD2(PlainYearMonth, Compare)
TEMPORAL_PROTOTYPE_METHOD2(PlainYearMonth, Add, add)
TEMPORAL_PROTOTYPE_METHOD2(PlainYearMonth, Subtract, subtract)
TEMPORAL_PROTOTYPE_METHOD1(PlainYearMonth, Equals, equals)
TEMPORAL_PROTOTYPE_METHOD2(PlainYearMonth, With, with)
TEMPORAL_PROTOTYPE_METHOD1(PlainYearMonth, ToPlainDate, toPlainDate)
TEMPORAL_VALUE_OF(PlainYearMonth)
TEMPORAL_PROTOTYPE_METHOD2(PlainYearMonth, Since, since)
TEMPORAL_PROTOTYPE_METHOD2(PlainYearMonth, ToLocaleString, toLocaleString)
TEMPORAL_PROTOTYPE_METHOD0(PlainYearMonth, ToJSON, toJSON)
TEMPORAL_PROTOTYPE_METHOD1(PlainYearMonth, ToString, toString)
TEMPORAL_PROTOTYPE_METHOD2(PlainYearMonth, Until, until)

// PlainMonthDay
BUILTIN(TemporalPlainMonthDayConstructor) {
  HandleScope scope(isolate);
  RETURN_RESULT_OR_FAILURE(
      isolate, JSTemporalPlainMonthDay::Constructor(
                   isolate, args.target(), args.new_target(),
                   args.atOrUndefined(isolate, 1),    // iso_month
                   args.atOrUndefined(isolate, 2),    // iso_day
                   args.atOrUndefined(isolate, 3),    // calendar_like
                   args.atOrUndefined(isolate, 4)));  // reference_iso_year
}
TEMPORAL_GET_RUST(PlainMonthDay, month_day, Day, day, iso_day, CONVERT_SMI)
TEMPORAL_GET_RUST(PlainMonthDay, month_day, MonthCode, monthCode, month_code,
                  CONVERT_ASCII_STRING)
TEMPORAL_METHOD2(PlainMonthDay, From)
TEMPORAL_PROTOTYPE_METHOD1(PlainMonthDay, Equals, equals)
TEMPORAL_PROTOTYPE_METHOD2(PlainMonthDay, With, with)
TEMPORAL_PROTOTYPE_METHOD1(PlainMonthDay, ToPlainDate, toPlainDate)
TEMPORAL_VALUE_OF(PlainMonthDay)
TEMPORAL_PROTOTYPE_METHOD0(PlainMonthDay, ToJSON, toJSON)
TEMPORAL_PROTOTYPE_METHOD2(PlainMonthDay, ToLocaleString, toLocaleString)
TEMPORAL_PROTOTYPE_METHOD1(PlainMonthDay, ToString, toString)

// ZonedDateTime

#define TEMPORAL_ZONED_DATE_TIME_GET_PREPARE(M)                              \
  HandleScope scope(isolate);                                                \
  const char* method_name = "get Temporal.ZonedDateTime.prototype." #M;      \
  /* 1. Let zonedDateTime be the this value. */                              \
  /* 2. Perform ? RequireInternalSlot(zonedDateTime, */                      \
  /* [[InitializedTemporalZonedDateTime]]). */                               \
  CHECK_RECEIVER(JSTemporalZonedDateTime, zoned_date_time, method_name);     \
  /* 3. Let timeZone be zonedDateTime.[[TimeZone]]. */                       \
  DirectHandle<JSReceiver> time_zone(zoned_date_time->time_zone(), isolate); \
  /* 4. Let instant be ?                                   */                \
  /* CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]). */                \
  DirectHandle<JSTemporalInstant> instant;                                   \
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(                                        \
      isolate, instant,                                                      \
      temporal::CreateTemporalInstant(                                       \
          isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))); \
  /* 5. Let calendar be zonedDateTime.[[Calendar]]. */                       \
  DirectHandle<JSReceiver> calendar(zoned_date_time->calendar(), isolate);   \
  /* 6. Let temporalDateTime be ?                 */                         \
  /* BuiltinTimeZoneGetPlainDateTimeFor(timeZone, */                         \
  /* instant, calendar). */                                                  \
  DirectHandle<JSTemporalPlainDateTime> temporal_date_time;                  \
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(                                        \
      isolate, temporal_date_time,                                           \
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(                          \
          isolate, time_zone, instant, calendar, method_name));

#define TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(M) \
  BUILTIN(TemporalZonedDateTimePrototype##M) { UNIMPLEMENTED(); }

#define TEMPORAL_ZONED_DATE_TIME_GET_INT_BY_FORWARD_TIME_ZONE(M, field) \
  BUILTIN(TemporalZonedDateTimePrototype##M) { UNIMPLEMENTED(); }

BUILTIN(TemporalZonedDateTimeConstructor) {
  HandleScope scope(isolate);
  RETURN_RESULT_OR_FAILURE(
      isolate, JSTemporalZonedDateTime::Constructor(
                   isolate, args.target(), args.new_target(),
                   args.atOrUndefined(isolate, 1),    // epoch_nanoseconds
                   args.atOrUndefined(isolate, 2),    // time_zone_like
                   args.atOrUndefined(isolate, 3)));  // calendar_like
}
TEMPORAL_METHOD2(ZonedDateTime, From)
TEMPORAL_METHOD2(ZonedDateTime, Compare)
TEMPORAL_GET(ZonedDateTime, TimeZone, time_zone)
TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(Year)
TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(Month)
TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(MonthCode)
TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(Day)
TEMPORAL_GET(ZonedDateTime, EpochNanoseconds, nanoseconds)
TEMPORAL_GET_NUMBER_AFTER_DIVID(ZonedDateTime, EpochSeconds, nanoseconds,
                                1000000000, epochSeconds)
TEMPORAL_GET_NUMBER_AFTER_DIVID(ZonedDateTime, EpochMilliseconds, nanoseconds,
                                1000000, epochMilliseconds)
TEMPORAL_GET_BIGINT_AFTER_DIVID(ZonedDateTime, EpochMicroseconds, nanoseconds,
                                1000, epochMicroseconds)
TEMPORAL_ZONED_DATE_TIME_GET_INT_BY_FORWARD_TIME_ZONE(Hour, iso_hour)
TEMPORAL_ZONED_DATE_TIME_GET_INT_BY_FORWARD_TIME_ZONE(Minute, iso_minute)
TEMPORAL_ZONED_DATE_TIME_GET_INT_BY_FORWARD_TIME_ZONE(Second, iso_second)
TEMPORAL_ZONED_DATE_TIME_GET_INT_BY_FORWARD_TIME_ZONE(Millisecond,
                                                      iso_millisecond)
TEMPORAL_ZONED_DATE_TIME_GET_INT_BY_FORWARD_TIME_ZONE(Microsecond,
                                                      iso_microsecond)
TEMPORAL_ZONED_DATE_TIME_GET_INT_BY_FORWARD_TIME_ZONE(Nanosecond,
                                                      iso_nanosecond)
TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(DayOfWeek)
TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(DayOfYear)
TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(WeekOfYear)
TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(DaysInWeek)
TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(DaysInMonth)
TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(DaysInYear)
TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(MonthsInYear)
TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(InLeapYear)
TEMPORAL_PROTOTYPE_METHOD1(ZonedDateTime, Equals, equals)
TEMPORAL_PROTOTYPE_METHOD0(ZonedDateTime, HoursInDay, hoursInDay)
TEMPORAL_PROTOTYPE_METHOD2(ZonedDateTime, With, with)
TEMPORAL_PROTOTYPE_METHOD1(ZonedDateTime, WithPlainDate, withPlainDate)
TEMPORAL_PROTOTYPE_METHOD1(ZonedDateTime, WithPlainTime, withPlainTime)
TEMPORAL_PROTOTYPE_METHOD1(ZonedDateTime, WithTimeZone, withTimeZone)
TEMPORAL_PROTOTYPE_METHOD0(ZonedDateTime, ToPlainYearMonth, toPlainYearMonth)
TEMPORAL_PROTOTYPE_METHOD0(ZonedDateTime, ToPlainMonthDay, toPlainMonthDay)
TEMPORAL_PROTOTYPE_METHOD1(ZonedDateTime, Round, round)
TEMPORAL_PROTOTYPE_METHOD2(ZonedDateTime, Add, add)
TEMPORAL_PROTOTYPE_METHOD2(ZonedDateTime, Subtract, subtract)
TEMPORAL_PROTOTYPE_METHOD0(ZonedDateTime, OffsetNanoseconds, offsetNanoseconds)
TEMPORAL_PROTOTYPE_METHOD0(ZonedDateTime, Offset, offset)
TEMPORAL_PROTOTYPE_METHOD2(ZonedDateTime, Since, since)
TEMPORAL_PROTOTYPE_METHOD0(ZonedDateTime, StartOfDay, startOfDay)
TEMPORAL_PROTOTYPE_METHOD0(ZonedDateTime, ToInstant, toInstant)
TEMPORAL_PROTOTYPE_METHOD0(ZonedDateTime, ToJSON, toJSON)
TEMPORAL_PROTOTYPE_METHOD0(ZonedDateTime, ToPlainDate, toPlainDate)
TEMPORAL_PROTOTYPE_METHOD0(ZonedDateTime, ToPlainTime, toPlainTime)
TEMPORAL_PROTOTYPE_METHOD0(ZonedDateTime, ToPlainDateTime, toPlainDateTime)
TEMPORAL_PROTOTYPE_METHOD2(ZonedDateTime, ToLocaleString, toLocaleString)
TEMPORAL_PROTOTYPE_METHOD1(ZonedDateTime, ToString, toString)
TEMPORAL_PROTOTYPE_METHOD2(ZonedDateTime, Until, until)
TEMPORAL_VALUE_OF(ZonedDateTime)

// Duration
BUILTIN(TemporalDurationConstructor) {
  HandleScope scope(isolate);
  RETURN_RESULT_OR_FAILURE(
      isolate, JSTemporalDuration::Constructor(
                   isolate, args.target(), args.new_target(),
                   args.atOrUndefined(isolate, 1),     // years
                   args.atOrUndefined(isolate, 2),     // months
                   args.atOrUndefined(isolate, 3),     // weeks
                   args.atOrUndefined(isolate, 4),     // days
                   args.atOrUndefined(isolate, 5),     // hours
                   args.atOrUndefined(isolate, 6),     // minutes
                   args.atOrUndefined(isolate, 7),     // seconds
                   args.atOrUndefined(isolate, 8),     // milliseconds
                   args.atOrUndefined(isolate, 9),     // microseconds
                   args.atOrUndefined(isolate, 10)));  // nanoseconds
}

BUILTIN(TemporalDurationCompare) {
  HandleScope scope(isolate);
  RETURN_RESULT_OR_FAILURE(isolate, JSTemporalDuration::Compare(
                                        isolate, args.atOrUndefined(isolate, 1),
                                        args.atOrUndefined(isolate, 2),
                                        args.atOrUndefined(isolate, 3)));
}
TEMPORAL_METHOD1(Duration, From)
TEMPORAL_GET_RUST(Duration, duration, Years, years, years, CONVERT_INTEGER64)
TEMPORAL_GET_RUST(Duration, duration, Months, months, months, CONVERT_INTEGER64)
TEMPORAL_GET_RUST(Duration, duration, Weeks, weeks, weeks, CONVERT_INTEGER64)
TEMPORAL_GET_RUST(Duration, duration, Days, days, days, CONVERT_INTEGER64)
TEMPORAL_GET_RUST(Duration, duration, Hours, hours, hours, CONVERT_INTEGER64)
TEMPORAL_GET_RUST(Duration, duration, Minutes, minutes, minutes,
                  CONVERT_INTEGER64)
TEMPORAL_GET_RUST(Duration, duration, Seconds, seconds, seconds,
                  CONVERT_INTEGER64)
TEMPORAL_GET_RUST(Duration, duration, Milliseconds, milliseconds, milliseconds,
                  CONVERT_INTEGER64)
// In theory the Duration may have millisecond values that are out of range for
// a float (but in range for a BigInt). Spec asks these functions to be
// converted to a Number so we can just produce Infinity when we are out of
// range.
TEMPORAL_GET_RUST(Duration, duration, Microseconds, microseconds, microseconds,
                  CONVERT_DOUBLE)
TEMPORAL_GET_RUST(Duration, duration, Nanoseconds, nanoseconds, nanoseconds,
                  CONVERT_DOUBLE)
TEMPORAL_PROTOTYPE_METHOD1(Duration, Round, round)
TEMPORAL_PROTOTYPE_METHOD1(Duration, Total, total)
TEMPORAL_PROTOTYPE_METHOD1(Duration, With, with)
TEMPORAL_PROTOTYPE_METHOD0(Duration, Sign, sign)
TEMPORAL_PROTOTYPE_METHOD0(Duration, Blank, blank)
TEMPORAL_PROTOTYPE_METHOD0(Duration, Negated, negated)
TEMPORAL_PROTOTYPE_METHOD0(Duration, Abs, abs)
TEMPORAL_PROTOTYPE_METHOD2(Duration, Add, add)
TEMPORAL_PROTOTYPE_METHOD2(Duration, Subtract, subtract)
TEMPORAL_VALUE_OF(Duration)
TEMPORAL_PROTOTYPE_METHOD0(Duration, ToJSON, toJSON)
TEMPORAL_PROTOTYPE_METHOD2(Duration, ToLocaleString, toLocaleString)
TEMPORAL_PROTOTYPE_METHOD1(Duration, ToString, toString)

// Instant
TEMPORAL_CONSTRUCTOR1(Instant)
TEMPORAL_PROTOTYPE_METHOD1(Instant, Equals, equals)
TEMPORAL_VALUE_OF(Instant)
TEMPORAL_METHOD1(Instant, From)
TEMPORAL_PROTOTYPE_METHOD0(Instant, EpochNanoseconds, epochNanoseconds)
TEMPORAL_PROTOTYPE_METHOD0(Instant, EpochMilliseconds, epochMilliseconds)
TEMPORAL_PROTOTYPE_METHOD1(Instant, Add, add)
TEMPORAL_PROTOTYPE_METHOD1(Instant, Round, round)
TEMPORAL_PROTOTYPE_METHOD2(Instant, Since, since)
TEMPORAL_PROTOTYPE_METHOD1(Instant, Subtract, subtract)
TEMPORAL_PROTOTYPE_METHOD0(Instant, ToJSON, toJSON)
TEMPORAL_PROTOTYPE_METHOD2(Instant, ToLocaleString, toLocaleString)
TEMPORAL_PROTOTYPE_METHOD1(Instant, ToString, toString)
TEMPORAL_PROTOTYPE_METHOD1(Instant, ToZonedDateTime, toZonedDateTime)
TEMPORAL_PROTOTYPE_METHOD2(Instant, Until, until)

// get Temporal.*.prototype.era/eraYear
TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(Era)
TEMPORAL_ZONED_DATE_TIME_GET_BY_FORWARD_TIME_ZONE_AND_CALENDAR(EraYear)

}  // namespace internal
}  // namespace v8
