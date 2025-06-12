// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_TEMPORAL_OBJECTS_INL_H_
#define V8_OBJECTS_JS_TEMPORAL_OBJECTS_INL_H_

#include "src/objects/js-temporal-objects.h"
// Include the non-inl header before the rest of the headers.

#include "src/api/api-inl.h"
#include "src/objects/objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

// Rust includes to transitively include
#include "temporal_rs/Duration.hpp"
#include "temporal_rs/Instant.hpp"
#include "temporal_rs/PlainDate.hpp"
#include "temporal_rs/PlainDateTime.hpp"
#include "temporal_rs/PlainMonthDay.hpp"
#include "temporal_rs/PlainTime.hpp"
#include "temporal_rs/PlainYearMonth.hpp"
#include "temporal_rs/ZonedDateTime.hpp"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-temporal-objects-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSTemporalDuration)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSTemporalInstant)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSTemporalPlainDate)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSTemporalPlainDateTime)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSTemporalPlainMonthDay)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSTemporalPlainTime)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSTemporalPlainYearMonth)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSTemporalZonedDateTime)


// temporal_rs object getters
ACCESSORS(JSTemporalInstant, instant, Tagged<Managed<temporal_rs::Instant>>,
          kInstantOffset)
ACCESSORS(JSTemporalDuration, duration, Tagged<Managed<temporal_rs::Duration>>,
          kDurationOffset)
ACCESSORS(JSTemporalPlainDate, date, Tagged<Managed<temporal_rs::PlainDate>>,
          kDateOffset)
ACCESSORS(JSTemporalPlainDateTime, date_time,
          Tagged<Managed<temporal_rs::PlainDateTime>>, kDateTimeOffset)
ACCESSORS(JSTemporalPlainMonthDay, month_day,
          Tagged<Managed<temporal_rs::PlainMonthDay>>, kMonthDayOffset)
ACCESSORS(JSTemporalPlainTime, time, Tagged<Managed<temporal_rs::PlainTime>>,
          kTimeOffset)
ACCESSORS(JSTemporalPlainYearMonth, year_month,
          Tagged<Managed<temporal_rs::PlainYearMonth>>, kYearMonthOffset)
ACCESSORS(JSTemporalZonedDateTime, zoned_date_time,
          Tagged<Managed<temporal_rs::ZonedDateTime>>, kZonedDateTimeOffset)

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_TEMPORAL_OBJECTS_INL_H_
