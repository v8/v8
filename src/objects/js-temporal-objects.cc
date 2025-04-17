// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-temporal-objects.h"

#include <optional>
#include <set>

#include "src/common/globals.h"
#include "src/date/date.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/numbers/conversions-inl.h"
#include "src/objects/js-objects-inl.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-temporal-objects-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/option-utils.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/string-set.h"
#include "src/strings/string-builder-inl.h"
#include "src/temporal/temporal-parser.h"

#ifdef V8_INTL_SUPPORT
#include "src/objects/intl-objects.h"
#include "src/objects/js-date-time-format.h"
#include "src/objects/managed-inl.h"
#include "unicode/calendar.h"
#include "unicode/unistr.h"
#endif  // V8_INTL_SUPPORT

namespace v8::internal {

namespace {

enum class Unit {
  kNotPresent,
  kAuto,
  kYear,
  kMonth,
  kWeek,
  kDay,
  kHour,
  kMinute,
  kSecond,
  kMillisecond,
  kMicrosecond,
  kNanosecond
};

/**
 * This header declare the Abstract Operations defined in the
 * Temporal spec with the enum and struct for them.
 */

// Struct


using temporal::DateRecord;
using temporal::DateTimeRecord;
using temporal::TimeRecord;

struct DateRecordWithCalendar {
  DateRecord date;
  DirectHandle<Object> calendar;  // String or Undefined
};

struct TimeRecordWithCalendar {
  TimeRecord time;
  DirectHandle<Object> calendar;  // String or Undefined
};

struct TimeZoneRecord {
  bool z;
  DirectHandle<Object> offset_string;  // String or Undefined
  DirectHandle<Object> name;           // String or Undefined
};

struct DateTimeRecordWithCalendar {
  DateRecord date;
  TimeRecord time;
  TimeZoneRecord time_zone;
  DirectHandle<Object> calendar;  // String or Undefined
};

struct InstantRecord {
  DateRecord date;
  TimeRecord time;
  DirectHandle<Object> offset_string;  // String or Undefined
};

using temporal::DurationRecord;
using temporal::IsValidDuration;
using temporal::TimeDurationRecord;

struct DurationRecordWithRemainder {
  DurationRecord record;
  double remainder;
};

// #sec-temporal-date-duration-records
struct DateDurationRecord {
  double years;
  double months;
  double weeks;
  double days;
  // #sec-temporal-createdatedurationrecord
  static Maybe<DateDurationRecord> Create(Isolate* isolate, double years,
                                          double months, double weeks,
                                          double days);
};

// Options


// #sec-temporal-totemporaldisambiguation
enum class Disambiguation { kCompatible, kEarlier, kLater, kReject };

// #sec-temporal-totemporaloverflow
enum class ShowOverflow { kConstrain, kReject };
// #sec-temporal-toshowcalendaroption
enum class ShowCalendar { kAuto, kAlways, kNever };

// sec-temporal-totemporalroundingmode
enum class RoundingMode {
  kCeil,
  kFloor,
  kExpand,
  kTrunc,
  kHalfCeil,
  kHalfFloor,
  kHalfExpand,
  kHalfTrunc,
  kHalfEven
};
// #table-temporal-unsigned-rounding-modes
enum class UnsignedRoundingMode {
  kInfinity,
  kZero,
  kHalfInfinity,
  kHalfZero,
  kHalfEven
};

enum class Precision { k0, k1, k2, k3, k4, k5, k6, k7, k8, k9, kAuto, kMinute };

enum class MatchBehaviour { kMatchExactly, kMatchMinutes };

// #sec-temporal-gettemporalunit
enum class UnitGroup {
  kDate,
  kTime,
  kDateTime,
};


// #sec-temporal-interpretisodatetimeoffset
enum class OffsetBehaviour { kOption, kExact, kWall };

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)

#ifdef DEBUG
#define TEMPORAL_DEBUG_INFO AT
#define TEMPORAL_ENTER_FUNC()
// #define TEMPORAL_ENTER_FUNC()  do { PrintF("Start: %s\n", __func__); } while
// (false)
#else
// #define TEMPORAL_DEBUG_INFO ""
#define TEMPORAL_DEBUG_INFO AT
#define TEMPORAL_ENTER_FUNC()
// #define TEMPORAL_ENTER_FUNC()  do { PrintF("Start: %s\n", __func__); } while
// (false)
#endif  // DEBUG

#define NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR()       \
  NewTypeError(                                     \
      MessageTemplate::kInvalidArgumentForTemporal, \
      isolate->factory()->NewStringFromStaticChars(TEMPORAL_DEBUG_INFO))

#define NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR()       \
  NewRangeError(                                     \
      MessageTemplate::kInvalidTimeValueForTemporal, \
      isolate->factory()->NewStringFromStaticChars(TEMPORAL_DEBUG_INFO))


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




}  // namespace temporal


namespace temporal {
// #sec-temporal-createtimedurationrecord
Maybe<TimeDurationRecord> TimeDurationRecord::Create(
    Isolate* isolate, double days, double hours, double minutes, double seconds,
    double milliseconds, double microseconds, double nanoseconds) {
  // 1. If ! IsValidDuration(0, 0, 0, days, hours, minutes, seconds,
  // milliseconds, microseconds, nanoseconds) is false, throw a RangeError
  // exception.
  TimeDurationRecord record = {days,         hours,        minutes,    seconds,
                               milliseconds, microseconds, nanoseconds};
  if (!IsValidDuration(isolate, {0, 0, 0, record})) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<TimeDurationRecord>());
  }
  // 2. Return the Record { [[Days]]: ℝ(𝔽(days)), [[Hours]]: ℝ(𝔽(hours)),
  // [[Minutes]]: ℝ(𝔽(minutes)), [[Seconds]]: ℝ(𝔽(seconds)), [[Milliseconds]]:
  // ℝ(𝔽(milliseconds)), [[Microseconds]]: ℝ(𝔽(microseconds)), [[Nanoseconds]]:
  // ℝ(𝔽(nanoseconds)) }.
  return Just(record);
}

// #sec-temporal-createdurationrecord
Maybe<DurationRecord> DurationRecord::Create(
    Isolate* isolate, double years, double months, double weeks, double days,
    double hours, double minutes, double seconds, double milliseconds,
    double microseconds, double nanoseconds) {
  // 1. If ! IsValidDuration(years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds) is false, throw a
  // RangeError exception.
  DurationRecord record = {
      years,
      months,
      weeks,
      {days, hours, minutes, seconds, milliseconds, microseconds, nanoseconds}};
  if (!IsValidDuration(isolate, record)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(),
                                 Nothing<DurationRecord>());
  }
  // 2. Return the Record { [[Years]]: ℝ(𝔽(years)), [[Months]]: ℝ(𝔽(months)),
  // [[Weeks]]: ℝ(𝔽(weeks)), [[Days]]: ℝ(𝔽(days)), [[Hours]]: ℝ(𝔽(hours)),
  // [[Minutes]]: ℝ(𝔽(minutes)), [[Seconds]]: ℝ(𝔽(seconds)), [[Milliseconds]]:
  // ℝ(𝔽(milliseconds)), [[Microseconds]]: ℝ(𝔽(microseconds)), [[Nanoseconds]]:
  // ℝ(𝔽(nanoseconds)) }.
  return Just(record);
}
}  // namespace temporal


namespace temporal {

// #sec-temporal-createtemporalinstant
MaybeDirectHandle<JSTemporalInstant> CreateTemporalInstant(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target,
    DirectHandle<BigInt> epoch_nanoseconds) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

MaybeDirectHandle<JSTemporalInstant> CreateTemporalInstant(
    Isolate* isolate, DirectHandle<BigInt> epoch_nanoseconds) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

}  // namespace temporal


namespace temporal {

MaybeDirectHandle<JSTemporalPlainDateTime> CreateTemporalDateTime(
    Isolate* isolate, const DateTimeRecord& date_time) {
  UNIMPLEMENTED();
}
MaybeDirectHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, DirectHandle<String> identifier) {
  UNIMPLEMENTED();
}
}  // namespace temporal

namespace {

// #sec-isintegralnumber
bool IsIntegralNumber(Isolate* isolate, DirectHandle<Object> argument) {
  // 1. If Type(argument) is not Number, return false.
  if (!IsNumber(*argument)) return false;
  // 2. If argument is NaN, +∞𝔽, or -∞𝔽, return false.
  double number = Object::NumberValue(Cast<Number>(*argument));
  if (!std::isfinite(number)) return false;
  // 3. If floor(abs(ℝ(argument))) ≠ abs(ℝ(argument)), return false.
  if (std::floor(std::abs(number)) != std::abs(number)) return false;
  // 4. Return true.
  return true;
}

// #sec-temporal-tointegerwithoutrounding
Maybe<double> ToIntegerWithoutRounding(Isolate* isolate,
                                       DirectHandle<Object> argument) {
  // 1. Let number be ? ToNumber(argument).
  DirectHandle<Number> number;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, number, Object::ToNumber(isolate, argument), Nothing<double>());
  // 2. If number is NaN, +0𝔽, or −0𝔽 return 0.
  if (IsNaN(*number) || Object::NumberValue(*number) == 0) {
    return Just(static_cast<double>(0));
  }
  // 3. If IsIntegralNumber(number) is false, throw a RangeError exception.
  if (!IsIntegralNumber(isolate, number)) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NEW_TEMPORAL_INVALID_ARG_RANGE_ERROR(), Nothing<double>());
  }
  // 4. Return ℝ(number).
  return Just(Object::NumberValue(*number));
}

// Helper function to loop through Table 8 Duration Record Fields
// This function implement
// "For each row of Table 8, except the header row, in table order, do"
// loop. It is designed to be used to implement the common part of
// ToPartialDuration, ToTemporalDurationRecord
Maybe<bool> IterateDurationRecordFieldsTable(
    Isolate* isolate, DirectHandle<JSReceiver> temporal_duration_like,
    Maybe<bool> (*RowFunction)(Isolate*,
                               DirectHandle<JSReceiver> temporal_duration_like,
                               DirectHandle<String>, double*),
    DurationRecord* record) {
  Factory* factory = isolate->factory();
  std::array<std::pair<DirectHandle<String>, double*>, 10> table8 = {
      {{factory->days_string(), &record->time_duration.days},
       {factory->hours_string(), &record->time_duration.hours},
       {factory->microseconds_string(), &record->time_duration.microseconds},
       {factory->milliseconds_string(), &record->time_duration.milliseconds},
       {factory->minutes_string(), &record->time_duration.minutes},
       {factory->months_string(), &record->months},
       {factory->nanoseconds_string(), &record->time_duration.nanoseconds},
       {factory->seconds_string(), &record->time_duration.seconds},
       {factory->weeks_string(), &record->weeks},
       {factory->years_string(), &record->years}}};

  // x. Let any be false.
  bool any = false;
  // x+1. For each row of Table 8, except the header row, in table order, do
  for (const auto& row : table8) {
    bool result;
    // row.first is prop: the Property Name value of the current row
    // row.second is the address of result's field whose name is the Field Name
    // value of the current row
    MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, result,
        RowFunction(isolate, temporal_duration_like, row.first, row.second),
        Nothing<bool>());
    any |= result;
  }
  return Just(any);
}

}  // namespace

namespace temporal {

// #sec-temporal-durationsign
int32_t DurationRecord::Sign(const DurationRecord& dur) {
  TEMPORAL_ENTER_FUNC();

  // 1. For each value v of « years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds », do a. If v < 0, return
  // −1. b. If v > 0, return 1.
  // 2. Return 0.
  if (dur.years < 0) return -1;
  if (dur.years > 0) return 1;
  if (dur.months < 0) return -1;
  if (dur.months > 0) return 1;
  if (dur.weeks < 0) return -1;
  if (dur.weeks > 0) return 1;
  const TimeDurationRecord& time = dur.time_duration;
  if (time.days < 0) return -1;
  if (time.days > 0) return 1;
  if (time.hours < 0) return -1;
  if (time.hours > 0) return 1;
  if (time.minutes < 0) return -1;
  if (time.minutes > 0) return 1;
  if (time.seconds < 0) return -1;
  if (time.seconds > 0) return 1;
  if (time.milliseconds < 0) return -1;
  if (time.milliseconds > 0) return 1;
  if (time.microseconds < 0) return -1;
  if (time.microseconds > 0) return 1;
  if (time.nanoseconds < 0) return -1;
  if (time.nanoseconds > 0) return 1;
  return 0;
}

// #sec-temporal-isvalidduration
bool IsValidDuration(Isolate* isolate, const DurationRecord& dur) {
  TEMPORAL_ENTER_FUNC();

  // 1. Let sign be ! DurationSign(years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds).
  int32_t sign = DurationRecord::Sign(dur);
  // 2. For each value v of « years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds », do a. If v is not
  // finite, return false. b. If v < 0 and sign > 0, return false. c. If v > 0
  // and sign < 0, return false.
  // 3. Return true.
  const TimeDurationRecord& time = dur.time_duration;

  if (!(std::isfinite(dur.years) && std::isfinite(dur.months) &&
        std::isfinite(dur.weeks) && std::isfinite(time.days) &&
        std::isfinite(time.hours) && std::isfinite(time.minutes) &&
        std::isfinite(time.seconds) && std::isfinite(time.milliseconds) &&
        std::isfinite(time.microseconds) && std::isfinite(time.nanoseconds))) {
    return false;
  }
  if ((sign > 0 && (dur.years < 0 || dur.months < 0 || dur.weeks < 0 ||
                    time.days < 0 || time.hours < 0 || time.minutes < 0 ||
                    time.seconds < 0 || time.milliseconds < 0 ||
                    time.microseconds < 0 || time.nanoseconds < 0)) ||
      (sign < 0 && (dur.years > 0 || dur.months > 0 || dur.weeks > 0 ||
                    time.days > 0 || time.hours > 0 || time.minutes > 0 ||
                    time.seconds > 0 || time.milliseconds > 0 ||
                    time.microseconds > 0 || time.nanoseconds > 0))) {
    return false;
  }
  static const double kPower32Of2 = static_cast<double>(int64_t(1) << 32);
  static const int64_t kPower53Of2 = int64_t(1) << 53;
  // 3. If abs(years) ≥ 2**32, return false.
  if (std::abs(dur.years) >= kPower32Of2) {
    return false;
  }
  // 4. If abs(months) ≥ 2**32, return false.
  if (std::abs(dur.months) >= kPower32Of2) {
    return false;
  }
  // 5. If abs(weeks) ≥ 2**32, return false.
  if (std::abs(dur.weeks) >= kPower32Of2) {
    return false;
  }
  // 6. Let normalizedSeconds be days × 86,400 + hours × 3600 + minutes × 60 +
  // seconds + ℝ(𝔽(milliseconds)) × 10**-3 + ℝ(𝔽(microseconds)) × 10**-6 +
  // ℝ(𝔽(nanoseconds)) × 10**-9.
  // 7. NOTE: The above step cannot be implemented directly using floating-point
  // arithmetic. Multiplying by 10**-3, 10**-6, and 10**-9 respectively may be
  // imprecise when milliseconds, microseconds, or nanoseconds is an unsafe
  // integer. This multiplication can be implemented in C++ with an
  // implementation of std::remquo() with sufficient bits in the quotient.
  // String manipulation will also give an exact result, since the
  // multiplication is by a power of 10.
  // 8. If abs(normalizedSeconds) ≥ 2**53, return false.

  int64_t allowed = kPower53Of2;
  double in_seconds = std::abs(time.days * 86400.0 + time.hours * 3600.0 +
                               time.minutes * 60.0 + time.seconds);

  if (in_seconds >= allowed) {
    return false;
  }
  allowed -= in_seconds;

  // Check the part > 1 seconds.
  in_seconds = std::floor(std::abs(time.milliseconds / 1e3)) +
               std::floor(std::abs(time.microseconds / 1e6)) +
               std::floor(std::abs(time.nanoseconds / 1e9));
  if (in_seconds >= allowed) {
    return false;
  }
  allowed -= in_seconds;

  // Sum of the three remainings will surely < 3
  if (allowed > 3) {
    return true;
  }

  allowed *= 1000000000;  // convert to ns
  int64_t remainders = std::abs(fmod(time.milliseconds, 1e3)) * 1000000 +
                       std::abs(fmod(time.microseconds, 1e6)) * 1000 +
                       std::abs(fmod(time.nanoseconds, 1e9));
  if (remainders >= allowed) {
    return false;
  }
  return true;
}


// #sec-temporal-topartialduration
Maybe<DurationRecord> ToPartialDuration(
    Isolate* isolate, DirectHandle<Object> temporal_duration_like_obj,
    const DurationRecord& input) {
  // 1. If Type(temporalDurationLike) is not Object, then
  if (!IsJSReceiver(*temporal_duration_like_obj)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<DurationRecord>());
  }
  DirectHandle<JSReceiver> temporal_duration_like =
      Cast<JSReceiver>(temporal_duration_like_obj);

  // 2. Let result be a new partial Duration Record with each field set to
  // undefined.
  DurationRecord result = input;

  // 3. Let any be false.
  bool any = false;

  // Table 8: Duration Record Fields
  // #table-temporal-duration-record-fields
  // 4. For each row of Table 8, except the header row, in table order, do
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, any,
      IterateDurationRecordFieldsTable(
          isolate, temporal_duration_like,
          [](Isolate* isolate, DirectHandle<JSReceiver> temporal_duration_like,
             DirectHandle<String> prop, double* field) -> Maybe<bool> {
            bool not_undefined = false;
            // a. Let prop be the Property value of the current row.
            DirectHandle<Object> val;
            // b. Let val be ? Get(temporalDurationLike, prop).
            ASSIGN_RETURN_ON_EXCEPTION_VALUE(
                isolate, val,
                JSReceiver::GetProperty(isolate, temporal_duration_like, prop),
                Nothing<bool>());
            // c. If val is not undefined, then
            if (!IsUndefined(*val)) {
              // i. Set any to true.
              not_undefined = true;
              // ii. Let val be 𝔽(? ToIntegerWithoutRounding(val)).
              // iii. Set result's field whose name is the Field Name value of
              // the current row to val.
              MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
                  isolate, *field, ToIntegerWithoutRounding(isolate, val),
                  Nothing<bool>());
            }
            return Just(not_undefined);
          },
          &result),
      Nothing<DurationRecord>());

  // 5. If any is false, then
  if (!any) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALID_ARG_TYPE_ERROR(),
                                 Nothing<DurationRecord>());
  }
  // 6. Return result.
  return Just(result);
}

}  // namespace temporal

// #sec-temporal.duration
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> years,
    DirectHandle<Object> months, DirectHandle<Object> weeks,
    DirectHandle<Object> days, DirectHandle<Object> hours,
    DirectHandle<Object> minutes, DirectHandle<Object> seconds,
    DirectHandle<Object> milliseconds, DirectHandle<Object> microseconds,
    DirectHandle<Object> nanoseconds) {
  UNIMPLEMENTED();
}

// #sec-temporal.duration.compare
MaybeDirectHandle<Smi> JSTemporalDuration::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj, DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.duration.from
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::From(
    Isolate* isolate, DirectHandle<Object> item) {
  UNIMPLEMENTED();
}

// #sec-temporal.duration.prototype.round
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Round(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> round_to_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.duration.prototype.total
MaybeDirectHandle<Object> JSTemporalDuration::Total(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> total_of_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.duration.prototype.with
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::With(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> temporal_duration_like) {
  UNIMPLEMENTED();
}

// #sec-get-temporal.duration.prototype.sign
MaybeDirectHandle<Smi> JSTemporalDuration::Sign(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  UNIMPLEMENTED();
}

// #sec-get-temporal.duration.prototype.blank
MaybeDirectHandle<Oddball> JSTemporalDuration::Blank(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  UNIMPLEMENTED();
}

// #sec-temporal.duration.prototype.negated
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Negated(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  UNIMPLEMENTED();
}

// #sec-temporal.duration.prototype.abs
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Abs(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  UNIMPLEMENTED();
}

// #sec-temporal.duration.prototype.add
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Add(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal.duration.prototype.subtract
MaybeDirectHandle<JSTemporalDuration> JSTemporalDuration::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal.duration.prototype.tojson
MaybeDirectHandle<String> JSTemporalDuration::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration) {
  UNIMPLEMENTED();
}

// #sec-temporal.duration.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalDuration::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal.duration.prototype.tostring
MaybeDirectHandle<String> JSTemporalDuration::ToString(
    Isolate* isolate, DirectHandle<JSTemporalDuration> duration,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.timezone
MaybeDirectHandle<JSTemporalTimeZone> JSTemporalTimeZone::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> identifier_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.now.timezone
MaybeDirectHandle<JSTemporalTimeZone> JSTemporalTimeZone::Now(
    Isolate* isolate) {
  UNIMPLEMENTED();
}

// #sec-temporal.timezone.prototype.getinstantfor
MaybeDirectHandle<JSTemporalInstant> JSTemporalTimeZone::GetInstantFor(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> date_time_obj, DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.timezone.prototype.getplaindatetimefor
MaybeDirectHandle<JSTemporalPlainDateTime>
JSTemporalTimeZone::GetPlainDateTimeFor(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> instant_obj, DirectHandle<Object> calendar_like) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.timezone.prototype.getnexttransition
MaybeDirectHandle<Object> JSTemporalTimeZone::GetNextTransition(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> starting_point_obj) {
  UNIMPLEMENTED();
}
// #sec-temporal.timezone.prototype.getprevioustransition
MaybeDirectHandle<Object> JSTemporalTimeZone::GetPreviousTransition(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> starting_point_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.timezone.prototype.getpossibleinstantsfor
MaybeDirectHandle<JSArray> JSTemporalTimeZone::GetPossibleInstantsFor(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> date_time_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.timezone.prototype.getoffsetnanosecondsfor
MaybeDirectHandle<Object> JSTemporalTimeZone::GetOffsetNanosecondsFor(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> instant_obj) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.timezone.prototype.getoffsetstringfor
MaybeDirectHandle<String> JSTemporalTimeZone::GetOffsetStringFor(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    DirectHandle<Object> instant_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.timezone.prototype.tostring
MaybeDirectHandle<Object> JSTemporalTimeZone::ToString(
    Isolate* isolate, DirectHandle<JSTemporalTimeZone> time_zone,
    const char* method_name) {
  UNIMPLEMENTED();
}

int32_t JSTemporalTimeZone::time_zone_index() const {
  DCHECK(is_offset() == false);
  return offset_milliseconds_or_time_zone_index();
}

int64_t JSTemporalTimeZone::offset_nanoseconds() const {
  TEMPORAL_ENTER_FUNC();
  DCHECK(is_offset());
  return static_cast<int64_t>(offset_milliseconds()) * 1000000 +
         static_cast<int64_t>(offset_sub_milliseconds());
}

void JSTemporalTimeZone::set_offset_nanoseconds(int64_t ns) {
  this->set_offset_milliseconds(static_cast<int32_t>(ns / 1000000));
  this->set_offset_sub_milliseconds(static_cast<int32_t>(ns % 1000000));
}

MaybeDirectHandle<String> JSTemporalTimeZone::id(Isolate* isolate) const {
  UNIMPLEMENTED();
}

MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> iso_year_obj,
    DirectHandle<Object> iso_month_obj, DirectHandle<Object> iso_day_obj,
    DirectHandle<Object> calendar_like) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindate.compare
MaybeDirectHandle<Smi> JSTemporalPlainDate::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindate.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainDate::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> other_obj) {
  UNIMPLEMENTED();
}


// #sec-temporal.plaindate.prototype.toplainyearmonth
MaybeDirectHandle<JSTemporalPlainYearMonth>
JSTemporalPlainDate::ToPlainYearMonth(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindate.prototype.toplainmonthday
MaybeDirectHandle<JSTemporalPlainMonthDay> JSTemporalPlainDate::ToPlainMonthDay(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindate.prototype.toplaindatetime
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDate::ToPlainDateTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> temporal_time_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindate.prototype.with
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::With(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> temporal_date_like_obj,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindate.prototype.tozoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalPlainDate::ToZonedDateTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> item_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindate.prototype.add
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::Add(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> temporal_duration_like,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindate.prototype.subtract
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> temporal_duration_like,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindate.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainDate::Until(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.plaindate.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainDate::Since(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.now.plaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::Now(
    Isolate* isolate, DirectHandle<Object> calendar_like,
    DirectHandle<Object> temporal_time_zone_like) {
  UNIMPLEMENTED();
}

// #sec-temporal.now.plaindateiso
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::NowISO(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindate.from
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDate::From(
    Isolate* isolate, DirectHandle<Object> item,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}


// #sec-temporal.plaindate.prototype.getisofields
MaybeDirectHandle<JSReceiver> JSTemporalPlainDate::GetISOFields(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindate.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainDate::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindate.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainDate::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sup-temporal.plaindate.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalPlainDate::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDate> temporal_date,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal-createtemporaldatetime
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> iso_year_obj,
    DirectHandle<Object> iso_month_obj, DirectHandle<Object> iso_day_obj,
    DirectHandle<Object> hour_obj, DirectHandle<Object> minute_obj,
    DirectHandle<Object> second_obj, DirectHandle<Object> millisecond_obj,
    DirectHandle<Object> microsecond_obj, DirectHandle<Object> nanosecond_obj,
    DirectHandle<Object> calendar_like) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.from
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::From(
    Isolate* isolate, DirectHandle<Object> item,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.compare
MaybeDirectHandle<Smi> JSTemporalPlainDateTime::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainDateTime::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> other_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.with
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::With(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_date_time_like_obj,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.withplaintime
MaybeDirectHandle<JSTemporalPlainDateTime>
JSTemporalPlainDateTime::WithPlainTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> plain_time_like) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.toplainyearmonth
MaybeDirectHandle<JSTemporalPlainYearMonth>
JSTemporalPlainDateTime::ToPlainYearMonth(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.toplainmonthday
MaybeDirectHandle<JSTemporalPlainMonthDay>
JSTemporalPlainDateTime::ToPlainMonthDay(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.tozoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalPlainDateTime::ToZonedDateTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_time_zone_like,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}


// #sec-temporal.plaindatetime.prototype.withplaindate
MaybeDirectHandle<JSTemporalPlainDateTime>
JSTemporalPlainDateTime::WithPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_date_like) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainDateTime::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalPlainDateTime::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainDateTime::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.now.plaindatetime
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Now(
    Isolate* isolate, DirectHandle<Object> calendar_like,
    DirectHandle<Object> temporal_time_zone_like) {
  UNIMPLEMENTED();
}

// #sec-temporal.now.plaindatetimeiso
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::NowISO(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like) {
  UNIMPLEMENTED();
}


// #sec-temporal.plaindatetime.prototype.round
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Round(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> round_to_obj) {
  UNIMPLEMENTED();
}


// #sec-temporal.plaindatetime.prototype.add
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Add(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.subtract
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainDateTime::Until(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainDateTime::Since(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.getisofields
MaybeDirectHandle<JSReceiver> JSTemporalPlainDateTime::GetISOFields(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.toplaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainDateTime::ToPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaindatetime.prototype.toplaintime
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainDateTime::ToPlainTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainDateTime> date_time) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainmonthday
MaybeDirectHandle<JSTemporalPlainMonthDay> JSTemporalPlainMonthDay::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> iso_month_obj,
    DirectHandle<Object> iso_day_obj, DirectHandle<Object> calendar_like,
    DirectHandle<Object> reference_iso_year_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainmonthday.from
MaybeDirectHandle<JSTemporalPlainMonthDay> JSTemporalPlainMonthDay::From(
    Isolate* isolate, DirectHandle<Object> item,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainyearmonth.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainMonthDay::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
    DirectHandle<Object> other_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainmonthday.prototype.with
MaybeDirectHandle<JSTemporalPlainMonthDay> JSTemporalPlainMonthDay::With(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> temporal_month_day,
    DirectHandle<Object> temporal_month_day_like_obj,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainmonthday.prototype.toplaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainMonthDay::ToPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
    DirectHandle<Object> item_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainmonthday.prototype.getisofields
MaybeDirectHandle<JSReceiver> JSTemporalPlainMonthDay::GetISOFields(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainmonthday.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainMonthDay::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainmonthday.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainMonthDay::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainMonthDay> month_day,
    DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainmonthday.prototype.tolocalestring
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
  UNIMPLEMENTED();
}

// #sec-temporal.plainyearmonth.from
MaybeDirectHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::From(
    Isolate* isolate, DirectHandle<Object> item,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainyearmonth.compare
MaybeDirectHandle<Smi> JSTemporalPlainYearMonth::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainyearmonth.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainYearMonth::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> other_obj) {
  UNIMPLEMENTED();
}


// #sec-temporal.plainyearmonth.prototype.add
MaybeDirectHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::Add(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainyearmonth.prototype.subtract
MaybeDirectHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainyearmonth.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainYearMonth::Until(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.plainyearmonth.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainYearMonth::Since(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.plainyearmonth.prototype.with
MaybeDirectHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::With(
    Isolate* isolate,
    DirectHandle<JSTemporalPlainYearMonth> temporal_year_month,
    DirectHandle<Object> temporal_year_month_like_obj,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainyearmonth.prototype.toplaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalPlainYearMonth::ToPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> item_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainyearmonth.prototype.getisofields
MaybeDirectHandle<JSReceiver> JSTemporalPlainYearMonth::GetISOFields(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainyearmonth.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainYearMonth::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainyearmonth.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainYearMonth::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal.plainyearmonth.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalPlainYearMonth::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalPlainYearMonth> year_month,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal-plaintime-constructor
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target, DirectHandle<Object> hour_obj,
    DirectHandle<Object> minute_obj, DirectHandle<Object> second_obj,
    DirectHandle<Object> millisecond_obj, DirectHandle<Object> microsecond_obj,
    DirectHandle<Object> nanosecond_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaintime.prototype.tozoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalPlainTime::ToZonedDateTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> item_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaintime.compare
MaybeDirectHandle<Smi> JSTemporalPlainTime::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaintime.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalPlainTime::Equals(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> other_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaintime.prototype.round
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::Round(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> round_to_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaintime.prototype.with
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::With(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> temporal_time_like_obj,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.now.plaintimeiso
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::NowISO(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaintime.from
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::From(
    Isolate* isolate, DirectHandle<Object> item_obj,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaintime.prototype.toplaindatetime
MaybeDirectHandle<JSTemporalPlainDateTime> JSTemporalPlainTime::ToPlainDateTime(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> temporal_date_like) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaintime.prototype.add
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::Add(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> temporal_duration_like) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaintime.prototype.subtract
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalPlainTime::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> temporal_duration_like) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaintime.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainTime::Until(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.plaintime.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalPlainTime::Since(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.plaintime.prototype.getisofields
MaybeDirectHandle<JSReceiver> JSTemporalPlainTime::GetISOFields(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaintime.prototype.tojson
MaybeDirectHandle<String> JSTemporalPlainTime::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time) {
  UNIMPLEMENTED();
}

// #sup-temporal.plaintime.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalPlainTime::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal.plaintime.prototype.tostring
MaybeDirectHandle<String> JSTemporalPlainTime::ToString(
    Isolate* isolate, DirectHandle<JSTemporalPlainTime> temporal_time,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target,
    DirectHandle<Object> epoch_nanoseconds_obj,
    DirectHandle<Object> time_zone_like, DirectHandle<Object> calendar_like) {
  UNIMPLEMENTED();
}

// #sec-get-temporal.zoneddatetime.prototype.hoursinday
MaybeDirectHandle<Object> JSTemporalZonedDateTime::HoursInDay(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.from
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::From(
    Isolate* isolate, DirectHandle<Object> item,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.compare
MaybeDirectHandle<Smi> JSTemporalZonedDateTime::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalZonedDateTime::Equals(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> other_obj) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}


// #sec-temporal.zoneddatetime.prototype.with
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::With(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> temporal_zoned_date_time_like_obj,
    DirectHandle<Object> options_obj) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}


// #sec-temporal.zoneddatetime.prototype.withplaindate
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalZonedDateTime::WithPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> plain_date_like) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.prototype.withplaintime
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalZonedDateTime::WithPlainTime(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> plain_time_like) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.prototype.withtimezone
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalZonedDateTime::WithTimeZone(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> time_zone_like) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}


// #sec-temporal.zoneddatetime.prototype.toplainyearmonth
MaybeDirectHandle<JSTemporalPlainYearMonth>
JSTemporalZonedDateTime::ToPlainYearMonth(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.prototype.toplainmonthday
MaybeDirectHandle<JSTemporalPlainMonthDay>
JSTemporalZonedDateTime::ToPlainMonthDay(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.prototype.tojson
MaybeDirectHandle<String> JSTemporalZonedDateTime::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalZonedDateTime::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.prototype.tostring
MaybeDirectHandle<String> JSTemporalZonedDateTime::ToString(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.now.zoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Now(
    Isolate* isolate, DirectHandle<Object> calendar_like,
    DirectHandle<Object> temporal_time_zone_like) {
  UNIMPLEMENTED();
}

// #sec-temporal.now.zoneddatetimeiso
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::NowISO(
    Isolate* isolate, DirectHandle<Object> temporal_time_zone_like) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.prototype.round
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Round(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> round_to_obj) {
  UNIMPLEMENTED();
}


// #sec-temporal.zoneddatetime.prototype.add
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Add(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}
// #sec-temporal.zoneddatetime.prototype.subtract
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    DirectHandle<Object> temporal_duration_like, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}



// #sec-temporal.zoneddatetime.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalZonedDateTime::Until(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalZonedDateTime::Since(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.prototype.getisofields
MaybeDirectHandle<JSReceiver> JSTemporalZonedDateTime::GetISOFields(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.now.instant
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Now(Isolate* isolate) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-get-temporal.zoneddatetime.prototype.offsetnanoseconds
MaybeDirectHandle<Object> JSTemporalZonedDateTime::OffsetNanoseconds(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-get-temporal.zoneddatetime.prototype.offset
MaybeDirectHandle<String> JSTemporalZonedDateTime::Offset(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.prototype.startofday
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::StartOfDay(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.prototype.toinstant
MaybeDirectHandle<JSTemporalInstant> JSTemporalZonedDateTime::ToInstant(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}


// #sec-temporal.zoneddatetime.prototype.toplaindate
MaybeDirectHandle<JSTemporalPlainDate> JSTemporalZonedDateTime::ToPlainDate(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.prototype.toplaintime
MaybeDirectHandle<JSTemporalPlainTime> JSTemporalZonedDateTime::ToPlainTime(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  UNIMPLEMENTED();
}

// #sec-temporal.zoneddatetime.prototype.toplaindatetime
MaybeDirectHandle<JSTemporalPlainDateTime>
JSTemporalZonedDateTime::ToPlainDateTime(
    Isolate* isolate, DirectHandle<JSTemporalZonedDateTime> zoned_date_time) {
  UNIMPLEMENTED();
}

// #sec-temporal.instant
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Constructor(
    Isolate* isolate, DirectHandle<JSFunction> target,
    DirectHandle<HeapObject> new_target,
    DirectHandle<Object> epoch_nanoseconds_obj) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.instant.fromepochseconds
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::FromEpochSeconds(
    Isolate* isolate, DirectHandle<Object> epoch_seconds) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.instant.fromepochmilliseconds
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::FromEpochMilliseconds(
    Isolate* isolate, DirectHandle<Object> epoch_milliseconds) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.instant.fromepochmicroseconds
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::FromEpochMicroseconds(
    Isolate* isolate, DirectHandle<Object> epoch_microseconds) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.instant.fromepochnanoeconds
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::FromEpochNanoseconds(
    Isolate* isolate, DirectHandle<Object> epoch_nanoseconds) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.instant.compare
MaybeDirectHandle<Smi> JSTemporalInstant::Compare(
    Isolate* isolate, DirectHandle<Object> one_obj,
    DirectHandle<Object> two_obj) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.instant.prototype.equals
MaybeDirectHandle<Oddball> JSTemporalInstant::Equals(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> other_obj) {
  TEMPORAL_ENTER_FUNC();

  UNIMPLEMENTED();
}

// #sec-temporal.instant.prototype.round
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Round(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> round_to_obj) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.instant.from
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::From(
    Isolate* isolate, DirectHandle<Object> item) {
  TEMPORAL_ENTER_FUNC();

  UNIMPLEMENTED();
}

// #sec-temporal.instant.prototype.tozoneddatetime
MaybeDirectHandle<JSTemporalZonedDateTime> JSTemporalInstant::ToZonedDateTime(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> item_obj) {
  TEMPORAL_ENTER_FUNC();

  UNIMPLEMENTED();
}


// #sec-temporal.instant.prototype.tojson
MaybeDirectHandle<String> JSTemporalInstant::ToJSON(
    Isolate* isolate, DirectHandle<JSTemporalInstant> instant) {
  TEMPORAL_ENTER_FUNC();

  UNIMPLEMENTED();
}

// #sec-temporal.instant.prototype.tolocalestring
MaybeDirectHandle<String> JSTemporalInstant::ToLocaleString(
    Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
    DirectHandle<Object> locales, DirectHandle<Object> options) {
  UNIMPLEMENTED();
}

// #sec-temporal.instant.prototype.tostring
MaybeDirectHandle<String> JSTemporalInstant::ToString(
    Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
    DirectHandle<Object> options_obj) {
  UNIMPLEMENTED();
}

// #sec-temporal.instant.prototype.tozoneddatetimeiso
MaybeDirectHandle<JSTemporalZonedDateTime>
JSTemporalInstant::ToZonedDateTimeISO(Isolate* isolate,
                                      DirectHandle<JSTemporalInstant> handle,
                                      DirectHandle<Object> item_obj) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.instant.prototype.add
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Add(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> temporal_duration_like) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.instant.prototype.subtract
MaybeDirectHandle<JSTemporalInstant> JSTemporalInstant::Subtract(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> temporal_duration_like) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.instant.prototype.until
MaybeDirectHandle<JSTemporalDuration> JSTemporalInstant::Until(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
  UNIMPLEMENTED();
}

// #sec-temporal.instant.prototype.since
MaybeDirectHandle<JSTemporalDuration> JSTemporalInstant::Since(
    Isolate* isolate, DirectHandle<JSTemporalInstant> handle,
    DirectHandle<Object> other, DirectHandle<Object> options) {
  TEMPORAL_ENTER_FUNC();
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
