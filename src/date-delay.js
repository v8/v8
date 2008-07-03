// Copyright 2006-2007 Google Inc. All Rights Reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


// -------------------------------------------------------------------

// This file contains date support implemented in JavaScript.


// Keep reference to original values of some global properties.  This
// has the added benefit that the code in this file is isolated from
// changes to these properties.
const $Date = global.Date;
const $floor = $Math_floor;
const $abs = $Math_abs;


// ECMA 262 - 15.9.1.2
function Day(time) {
  return $floor(time/msPerDay);
};


// ECMA 262 - 5.2
function Modulo(value, remainder) {
  var mod = value % remainder;
  return mod >= 0 ? mod : mod + remainder;
};


function TimeWithinDay(time) {
  return Modulo(time, msPerDay);
};


// ECMA 262 - 15.9.1.3
function DaysInYear(year) {
  if (year % 4 != 0) return 365;
  if ((year % 100 == 0) && (year % 400 != 0)) return 365;
  return 366;
};


function DayFromYear(year) {
  return 365 * (year-1970)
      + $floor((year-1969)/4)
      - $floor((year-1901)/100)
      + $floor((year-1601)/400);
};


function TimeFromYear(year) {
  return msPerDay * DayFromYear(year);
};


function YearFromTime(time) {
  return FromJulianDay(Day(time) + kDayZeroInJulianDay).year;
};


function InLeapYear(time) {
  return DaysInYear(YearFromTime(time)) == 366 ? 1 : 0;
};


// ECMA 262 - 15.9.1.4
function MonthFromTime(time) {
  return FromJulianDay(Day(time) + kDayZeroInJulianDay).month;
};


function DayWithinYear(time) {
  return Day(time) - DayFromYear(YearFromTime(time));
};


// ECMA 262 - 15.9.1.5
function DateFromTime(time) {
  return FromJulianDay(Day(time) + kDayZeroInJulianDay).date;
};


// ECMA 262 - 15.9.1.9
function EquivalentYear(year) {
  // Returns an equivalent year in the range [1956-2000] matching
  // - leap year.
  // - week day of first day.
  var time = TimeFromYear(year);
  return (InLeapYear(time) == 0 ? 1967 : 1956) + (WeekDay(time) * 12) % 28;
};


function EquivalentTime(t) {
  // The issue here is that some library calls don't work right for dates
  // that cannot be represented using a signed 32 bit integer (measured in
  // whole seconds based on the 1970 epoch).
  // We solve this by mapping the time to a year with same leap-year-ness
  // and same starting day for the year.
  // As an optimization we avoid finding an equivalent year in the common
  // case.  We are measuring in ms here so the 32 bit signed integer range
  // is +-(1<<30)*1000 ie approximately +-2.1e20.
  if (t >= -2.1e12 && t <= 2.1e12) return t;
  var day = MakeDay(EquivalentYear(YearFromTime(t)), MonthFromTime(t), DateFromTime(t));
  return TimeClip(MakeDate(day, TimeWithinDay(t)));
};


var local_time_offset;

function LocalTimeOffset() {
  if (IS_UNDEFINED(local_time_offset)) {
    local_time_offset = %DateLocalTimeOffset(0);
  }
  return local_time_offset;
};


var daylight_cache_time = $NaN;
var daylight_cache_offset;

function DaylightSavingsOffset(t) {
  if (t == daylight_cache_time) {
    return daylight_cache_offset;
  }
  var offset = %DateDaylightSavingsOffset(EquivalentTime(t));
  daylight_cache_time = t;
  daylight_cache_offset = offset;
  return offset;
};


var timezone_cache_time = $NaN;
var timezone_cache_timezone;

function LocalTimezone(t) {
  if(t == timezone_cache_time) {
    return timezone_cache_timezone;
  }
  var timezone = %DateLocalTimezone(EquivalentTime(t));
  timezone_cache_time = t;
  timezone_cache_timezone = timezone;
  return timezone;
};


function WeekDay(time) {
  return Modulo(Day(time) + 4, 7);
};


function LocalTime(time) {
  if ($isNaN(time)) return time;
  return time + LocalTimeOffset() + DaylightSavingsOffset(time);
};


function UTC(time) {
  if ($isNaN(time)) return time;
  var tmp = time - LocalTimeOffset();
  return tmp - DaylightSavingsOffset(tmp);
};


// ECMA 262 - 15.9.1.10
function HourFromTime(time) {
  return Modulo($floor(time / msPerHour), HoursPerDay);
};


function MinFromTime(time) {
  return Modulo($floor(time / msPerMinute), MinutesPerHour);
};


function SecFromTime(time) {
  return Modulo($floor(time / msPerSecond), SecondsPerMinute);
};


function msFromTime(time) {
  return Modulo(time, msPerSecond);
};


// ECMA 262 - 15.9.1.11
function MakeTime(hour, min, sec, ms) {
  if (!$isFinite(hour)) return $NaN;
  if (!$isFinite(min)) return $NaN;
  if (!$isFinite(sec)) return $NaN;
  if (!$isFinite(ms)) return $NaN;
  return TO_INTEGER(hour) * msPerHour
      + TO_INTEGER(min) * msPerMinute
      + TO_INTEGER(sec) * msPerSecond
      + TO_INTEGER(ms);
};


// ECMA 262 - 15.9.1.12
function TimeInYear(year) {
  return DaysInYear(year) * msPerDay;
};


// Compute modified Julian day from year, month, date.
// The missing days in 1582 are ignored for JavaScript compatibility.
function ToJulianDay(year, month, date) {
  var jy = (month > 1) ? year : year - 1;
  var jm = (month > 1) ? month + 2 : month + 14;
  var ja = $floor(0.01*jy);
  return $floor($floor(365.25*jy) + $floor(30.6001*jm) + date + 1720995) + 2 - ja + $floor(0.25*ja);
};


var four_year_cycle_table;


function CalculateDateTable() {
  var month_lengths = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
  var four_year_cycle_table = new $Array(1461);

  var cumulative = 0;
  var position = 0;
  var leap_position = 0;
  for (var month = 0; month < 12; month++) {
    var length = month_lengths[month];
    for (var day = 1; day <= length; day++) {
      four_year_cycle_table[leap_position] =
        (month << kMonthShift) + day;
      four_year_cycle_table[366 + position] =
        (1 << kYearShift) + (month << kMonthShift) + day;
      four_year_cycle_table[731 + position] =
        (2 << kYearShift) + (month << kMonthShift) + day;
      four_year_cycle_table[1096 + position] =
        (3 << kYearShift) + (month << kMonthShift) + day;
      leap_position++;
      position++;
    }
    if (month == 1) {
      four_year_cycle_table[leap_position++] =
        (month << kMonthShift) + 29;
    }
  }
  return four_year_cycle_table;
};



// Constructor for creating objects holding year, month, and date.
// Introduced to ensure the two return points in FromJulianDay match same map.
function DayTriplet(year, month, date) {
  this.year = year;
  this.month = month;
  this.date = date;
}

// Compute year, month, and day from modified Julian day.
// The missing days in 1582 are ignored for JavaScript compatibility.
function FromJulianDay(julian) {
  // Avoid floating point and non-Smi maths in common case.  This is also a period of
  // time where leap years are very regular.  The range is not too large to avoid overflow
  // when doing the multiply-to-divide trick.
  if (julian > kDayZeroInJulianDay &&
      (julian - kDayZeroInJulianDay) < 40177) { // 1970 - 2080
    if (!four_year_cycle_table)
      four_year_cycle_table = CalculateDateTable();
    var jsimple = (julian - kDayZeroInJulianDay) + 731; // Day 0 is 1st January 1968
    var y = 1968;
    // Divide by 1461 by multiplying with 22967 and shifting down by 25!
    var after_1968 = (jsimple * 22967) >> 25;
    y += after_1968 << 2;
    jsimple -= 1461 * after_1968;
    var four_year_cycle = four_year_cycle_table[jsimple];
    return new DayTriplet(y + (four_year_cycle >> kYearShift),
                           (four_year_cycle & kMonthMask) >> kMonthShift,
                           four_year_cycle & kDayMask);
  }
  var jalpha = $floor((julian - 1867216.25) / 36524.25);
  var jb = julian + 1 + jalpha - $floor(0.25 * jalpha) + 1524;
  var jc = $floor(6680.0 + ((jb-2439870) - 122.1)/365.25);
  var jd = $floor(365 * jc + (0.25 * jc));
  var je = $floor((jb - jd)/30.6001);
  var m = je - 1;
  if (m > 12) m -= 13;
  var y = jc - 4715;
  if (m > 2) { --y; --m; }
  var d = jb - jd - $floor(30.6001 * je);
  return new DayTriplet(y, m, d);
};

// Compute number of days given a year, month, date.
// Note that month and date can lie outside the normal range.
//   For example:
//     MakeDay(2007, -4, 20) --> MakeDay(2006, 8, 20)
//     MakeDay(2007, -33, 1) --> MakeDay(2004, 3, 1)
//     MakeDay(2007, 14, -50) --> MakeDay(2007, 8, 11)
function MakeDay(year, month, date) {
  if (!$isFinite(year) || !$isFinite(month) || !$isFinite(date)) return $NaN;

  // Conversion to integers.
  year = TO_INTEGER(year);
  month = TO_INTEGER(month);
  date = TO_INTEGER(date);

  // Overflow months into year.
  year = year + $floor(month/12);
  month = month % 12;
  if (month < 0) {
    month += 12;
  }

  // Return days relative to Jan 1 1970.
  return ToJulianDay(year, month, date) - kDayZeroInJulianDay;
};


// ECMA 262 - 15.9.1.13
function MakeDate(day, time) {
  if (!$isFinite(day)) return $NaN;
  if (!$isFinite(time)) return $NaN;
  return day * msPerDay + time;
};


// ECMA 262 - 15.9.1.14
function TimeClip(time) {
  if (!$isFinite(time)) return $NaN;
  if ($abs(time) > 8.64E15) return $NaN;
  return TO_INTEGER(time);
};


%SetCode($Date, function(year, month, date, hours, minutes, seconds, ms) {
  if (%IsConstructCall(this)) {
    // ECMA 262 - 15.9.3
    var argc = %_ArgumentsLength();
    if (argc == 0) {
      %_SetValueOf(this, %DateCurrentTime(argc));
      return;
    }
    if (argc == 1) {
      // According to ECMA 262, no hint should be given for this
      // conversion.  However, ToPrimitive defaults to String Hint
      // for Date objects which will lose precision when the Date
      // constructor is called with another Date object as its
      // argument.  We therefore use Number Hint for the conversion
      // (which is the default for everything else than Date
      // objects).  This makes us behave like KJS and SpiderMonkey.
      var time = ToPrimitive(year, NUMBER_HINT);
      if (IS_STRING(time)) {
        %_SetValueOf(this, DateParse(time));
      } else {
        %_SetValueOf(this, TimeClip(ToNumber(time)));
      }
      return;
    }
    year = ToNumber(year);
    month = ToNumber(month);
    date = argc > 2 ? ToNumber(date) : 1;
    hours = argc > 3 ? ToNumber(hours) : 0;
    minutes = argc > 4 ? ToNumber(minutes) : 0;
    seconds = argc > 5 ? ToNumber(seconds) : 0;
    ms = argc > 6 ? ToNumber(ms) : 0;
    year = (!$isNaN(year) && 0 <= TO_INTEGER(year) && TO_INTEGER(year) <= 99)
        ? 1900 + TO_INTEGER(year) : year;
    var day = MakeDay(year, month, date);
    var time = MakeTime(hours, minutes, seconds, ms);
    %_SetValueOf(this, TimeClip(UTC(MakeDate(day, time))));
  } else {
    // ECMA 262 - 15.9.2
    return (new $Date()).toString();
  }
});


// Helper functions.
function GetTimeFrom(aDate) {
  if (IS_DATE(aDate)) return %_ValueOf(aDate);
  throw new $TypeError('this is not a Date object.');
};


function GetMillisecondsFrom(aDate) {
  var t = GetTimeFrom(aDate);
  if ($isNaN(t)) return t;
  return msFromTime(LocalTime(t));
};


function GetUTCMillisecondsFrom(aDate) {
  var t = GetTimeFrom(aDate);
  if ($isNaN(t)) return t;
  return msFromTime(t);
};


function GetSecondsFrom(aDate) {
  var t = GetTimeFrom(aDate);
  if ($isNaN(t)) return t;
  return SecFromTime(LocalTime(t));
};


function GetUTCSecondsFrom(aDate) {
  var t = GetTimeFrom(aDate);
  if ($isNaN(t)) return t;
  return SecFromTime(t);
};


function GetMinutesFrom(aDate) {
  var t = GetTimeFrom(aDate);
  if ($isNaN(t)) return t;
  return MinFromTime(LocalTime(t));
};


function GetUTCMinutesFrom(aDate) {
  var t = GetTimeFrom(aDate);
  if ($isNaN(t)) return t;
  return MinFromTime(t);
};


function GetHoursFrom(aDate) {
  var t = GetTimeFrom(aDate);
  if ($isNaN(t)) return t;
  return HourFromTime(LocalTime(t));
};


function GetUTCHoursFrom(aDate) {
  var t = GetTimeFrom(aDate);
  if ($isNaN(t)) return t;
  return HourFromTime(t);
};


function GetFullYearFrom(aDate) {
  var t = GetTimeFrom(aDate);
  if ($isNaN(t)) return t;
  return YearFromTime(LocalTime(t));
};


function GetUTCFullYearFrom(aDate) {
  var t = GetTimeFrom(aDate);
  if ($isNaN(t)) return t;
  return YearFromTime(t);
};


function GetMonthFrom(aDate) {
  var t = GetTimeFrom(aDate);
  if ($isNaN(t)) return t;
  return MonthFromTime(LocalTime(t));
};


function GetUTCMonthFrom(aDate) {
  var t = GetTimeFrom(aDate);
  if ($isNaN(t)) return t;
  return MonthFromTime(t);
};


function GetDateFrom(aDate) {
  var t = GetTimeFrom(aDate);
  if ($isNaN(t)) return t;
  return DateFromTime(LocalTime(t));
};


function GetUTCDateFrom(aDate) {
  var t = GetTimeFrom(aDate);
  if ($isNaN(t)) return t;
  return DateFromTime(t);
};


%FunctionSetPrototype($Date, new $Date($NaN));


var WeekDays = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
var Months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];


function TwoDigitString(value) {
  return value < 10 ? "0" + value : "" + value;
};


function DateString(time) {
  var YMD = FromJulianDay(Day(time) + kDayZeroInJulianDay);
  return WeekDays[WeekDay(time)] + ' '
      + Months[YMD.month] + ' '
      + TwoDigitString(YMD.date) + ' '
      + YMD.year;
};


function TimeString(time) {
  return TwoDigitString(HourFromTime(time)) + ':'
      + TwoDigitString(MinFromTime(time)) + ':'
      + TwoDigitString(SecFromTime(time));
};


function LocalTimezoneString(time) {
  var timezoneOffset = (LocalTimeOffset() + DaylightSavingsOffset(time)) / msPerMinute;
  var sign = (timezoneOffset >= 0) ? 1 : -1;
  var hours = $floor((sign * timezoneOffset)/60);
  var min   = $floor((sign * timezoneOffset)%60);
  var gmt = ' GMT' + ((sign == 1) ? '+' : '-') + TwoDigitString(hours) + TwoDigitString(min);
  return gmt + ' (' +  LocalTimezone(time) + ')';
};


function DatePrintString(time) {
  return DateString(time) + ' ' + TimeString(time);
};

// -------------------------------------------------------------------


// ECMA 262 - 15.9.4.2
function DateParse(string) {
  var arr = %DateParseString(ToString(string));
  if (IS_NULL(arr)) return $NaN;

  var day = MakeDay(arr[0], arr[1], arr[2]);
  var time = MakeTime(arr[3], arr[4], arr[5], 0);
  var date = MakeDate(day, time);
  
  if (IS_NULL(arr[6])) {
    return TimeClip(UTC(date));
  } else {
    return TimeClip(date - arr[6] * 1000);
  }
};


// ECMA 262 - 15.9.4.3
function DateUTC(year, month, date, hours, minutes, seconds, ms) {
  year = ToNumber(year);
  month = ToNumber(month);
  var argc = %_ArgumentsLength();
  date = argc > 2 ? ToNumber(date) : 1;
  hours = argc > 3 ? ToNumber(hours) : 0;
  minutes = argc > 4 ? ToNumber(minutes) : 0;
  seconds = argc > 5 ? ToNumber(seconds) : 0;
  ms = argc > 6 ? ToNumber(ms) : 0;
  year = (!$isNaN(year) && 0 <= TO_INTEGER(year) && TO_INTEGER(year) <= 99)
      ? 1900 + TO_INTEGER(year) : year;
  var day = MakeDay(year, month, date);
  var time = MakeTime(hours, minutes, seconds, ms);
  return %_SetValueOf(this, TimeClip(MakeDate(day, time)));
};


// Mozilla-specific extension. Returns the number of milliseconds
// elapsed since 1 January 1970 00:00:00 UTC.
function DateNow() {
  return %DateCurrentTime(0);
};


// ECMA 262 - 15.9.5.2
function DateToString() {
  var t = GetTimeFrom(this);
  if ($isNaN(t)) return kInvalidDate;
  return DatePrintString(LocalTime(t)) + LocalTimezoneString(t);
};


// ECMA 262 - 15.9.5.3
function DateToDateString() {
  var t = GetTimeFrom(this);
  if ($isNaN(t)) return kInvalidDate;
  return DateString(LocalTime(t));
};


// ECMA 262 - 15.9.5.4
function DateToTimeString() {
  var t = GetTimeFrom(this);
  if ($isNaN(t)) return kInvalidDate;
  var lt = LocalTime(t);
  return TimeString(lt) + LocalTimezoneString(lt);
};


// ECMA 262 - 15.9.5.9
function DateGetTime() {
  return GetTimeFrom(this);
}


// ECMA 262 - 15.9.5.10
function DateGetFullYear() {
  return GetFullYearFrom(this)
};


// ECMA 262 - 15.9.5.11
function DateGetUTCFullYear() {
  return GetUTCFullYearFrom(this)
};


// ECMA 262 - 15.9.5.12
function DateGetMonth() {
  return GetMonthFrom(this);
};


// ECMA 262 - 15.9.5.13
function DateGetUTCMonth() {
  return GetUTCMonthFrom(this);
};


// ECMA 262 - 15.9.5.14
function DateGetDate() {
  return GetDateFrom(this);
};


// ECMA 262 - 15.9.5.15
function DateGetUTCDate() {
  return GetUTCDateFrom(this);
};


// ECMA 262 - 15.9.5.16
function DateGetDay() {
  var t = GetTimeFrom(this);
  if ($isNaN(t)) return t;
  return WeekDay(LocalTime(t));
};


// ECMA 262 - 15.9.5.17
function DateGetUTCDay() {
  var t = GetTimeFrom(this);
  if ($isNaN(t)) return t;
  return WeekDay(t);
};


// ECMA 262 - 15.9.5.18
function DateGetHours() {
  return GetHoursFrom(this);
};


// ECMA 262 - 15.9.5.19
function DateGetUTCHours() {
  return GetUTCHoursFrom(this);
};


// ECMA 262 - 15.9.5.20
function DateGetMinutes() {
  return GetMinutesFrom(this);
};


// ECMA 262 - 15.9.5.21
function DateGetUTCMinutes() {
  return GetUTCMinutesFrom(this);
};


// ECMA 262 - 15.9.5.22
function DateGetSeconds() {
  return GetSecondsFrom(this);
};


// ECMA 262 - 15.9.5.23
function DateGetUTCSeconds() {
  return GetUTCSecondsFrom(this);
};


// ECMA 262 - 15.9.5.24
function DateGetMilliseconds() {
  return GetMillisecondsFrom(this);
};


// ECMA 262 - 15.9.5.25
function DateGetUTCMilliseconds() {
  return GetUTCMillisecondsFrom(this);
};


// ECMA 262 - 15.9.5.26
function DateGetTimezoneOffset() {
  var t = GetTimeFrom(this);
  if ($isNaN(t)) return t;
  return (t - LocalTime(t)) / msPerMinute;
};


// ECMA 262 - 15.9.5.27
function DateSetTime(ms) {
  if (!IS_DATE(this)) throw new $TypeError('this is not a Date object.');
  return %_SetValueOf(this, TimeClip(ToNumber(ms)));
};


// ECMA 262 - 15.9.5.28
function DateSetMilliseconds(ms) {
  var t = LocalTime(GetTimeFrom(this));
  ms = ToNumber(ms);
  var time = MakeTime(HourFromTime(t), MinFromTime(t), SecFromTime(t), ms);
  return %_SetValueOf(this, TimeClip(UTC(MakeDate(Day(t), time))));
};


// ECMA 262 - 15.9.5.29
function DateSetUTCMilliseconds(ms) {
  var t = GetTimeFrom(this);
  ms = ToNumber(ms);
  var time = MakeTime(HourFromTime(t), MinFromTime(t), SecFromTime(t), ms);
  return %_SetValueOf(this, TimeClip(MakeDate(Day(t), time)));
};


// ECMA 262 - 15.9.5.30
function DateSetSeconds(sec, ms) {
  var t = LocalTime(GetTimeFrom(this));
  sec = ToNumber(sec);
  ms = %_ArgumentsLength() < 2 ? GetMillisecondsFrom(this) : ToNumber(ms);
  var time = MakeTime(HourFromTime(t), MinFromTime(t), sec, ms);
  return %_SetValueOf(this, TimeClip(UTC(MakeDate(Day(t), time))));
};


// ECMA 262 - 15.9.5.31
function DateSetUTCSeconds(sec, ms) {
  var t = GetTimeFrom(this);
  sec = ToNumber(sec);
  ms = %_ArgumentsLength() < 2 ? GetUTCMillisecondsFrom(this) : ToNumber(ms);
  var time = MakeTime(HourFromTime(t), MinFromTime(t), sec, ms);
  return %_SetValueOf(this, TimeClip(MakeDate(Day(t), time)));
};


// ECMA 262 - 15.9.5.33
function DateSetMinutes(min, sec, ms) {
  var t = LocalTime(GetTimeFrom(this));
  min = ToNumber(min);
  var argc = %_ArgumentsLength();
  sec = argc < 2 ? GetSecondsFrom(this) : ToNumber(sec);
  ms = argc < 3 ? GetMillisecondsFrom(this) : ToNumber(ms);
  var time = MakeTime(HourFromTime(t), min, sec, ms);
  return %_SetValueOf(this, TimeClip(UTC(MakeDate(Day(t), time))));
};


// ECMA 262 - 15.9.5.34
function DateSetUTCMinutes(min, sec, ms) {
  var t = GetTimeFrom(this);
  min = ToNumber(min);
  var argc = %_ArgumentsLength();
  sec = argc < 2 ? GetUTCSecondsFrom(this) : ToNumber(sec);
  ms = argc < 3 ? GetUTCMillisecondsFrom(this) : ToNumber(ms);
  var time = MakeTime(HourFromTime(t), min, sec, ms);
  return %_SetValueOf(this, TimeClip(MakeDate(Day(t), time)));
};


// ECMA 262 - 15.9.5.35
function DateSetHours(hour, min, sec, ms) {
  var t = LocalTime(GetTimeFrom(this));
  hour = ToNumber(hour);
  var argc = %_ArgumentsLength();
  min = argc < 2 ? GetMinutesFrom(this) : ToNumber(min);
  sec = argc < 3 ? GetSecondsFrom(this) : ToNumber(sec);
  ms = argc < 4 ? GetMillisecondsFrom(this) : ToNumber(ms);
  var time = MakeTime(hour, min, sec, ms);
  return %_SetValueOf(this, TimeClip(UTC(MakeDate(Day(t), time))));
};


// ECMA 262 - 15.9.5.34
function DateSetUTCHours(hour, min, sec, ms) {
  var t = GetTimeFrom(this);
  hour = ToNumber(hour);
  var argc = %_ArgumentsLength();
  min = argc < 2 ? GetUTCMinutesFrom(this) : ToNumber(min);
  sec = argc < 3 ? GetUTCSecondsFrom(this) : ToNumber(sec);
  ms = argc < 4 ? GetUTCMillisecondsFrom(this) : ToNumber(ms);
  var time = MakeTime(hour, min, sec, ms);
  return %_SetValueOf(this, TimeClip(MakeDate(Day(t), time)));
};


// ECMA 262 - 15.9.5.36
function DateSetDate(date) {
  var t = LocalTime(GetTimeFrom(this));
  date = ToNumber(date);
  var day = MakeDay(YearFromTime(t), MonthFromTime(t), date);
  return %_SetValueOf(this, TimeClip(UTC(MakeDate(day, TimeWithinDay(t)))));
};


// ECMA 262 - 15.9.5.37
function DateSetUTCDate(date) {
  var t = GetTimeFrom(this);
  date = ToNumber(date);
  var day = MakeDay(YearFromTime(t), MonthFromTime(t), date);
  return %_SetValueOf(this, TimeClip(MakeDate(day, TimeWithinDay(t))));
};


// ECMA 262 - 15.9.5.38
function DateSetMonth(month, date) {
  var t = LocalTime(GetTimeFrom(this));
  month = ToNumber(month);
  date = %_ArgumentsLength() < 2 ? GetDateFrom(this) : ToNumber(date);
  var day = MakeDay(YearFromTime(t), month, date);
  return %_SetValueOf(this, TimeClip(UTC(MakeDate(day, TimeWithinDay(t)))));
};


// ECMA 262 - 15.9.5.39
function DateSetUTCMonth(month, date) {
  var t = GetTimeFrom(this);
  month = ToNumber(month);
  date = %_ArgumentsLength() < 2 ? GetUTCDateFrom(this) : ToNumber(date);
  var day = MakeDay(YearFromTime(t), month, date);
  return %_SetValueOf(this, TimeClip(MakeDate(day, TimeWithinDay(t))));
};


// ECMA 262 - 15.9.5.40
function DateSetFullYear(year, month, date) {
  var t = GetTimeFrom(this);
  t = $isNaN(t) ? 0 : LocalTime(t);
  year = ToNumber(year);
  var argc = %_ArgumentsLength();
  month = argc < 2 ? MonthFromTime(t) : ToNumber(month);
  date = argc < 3 ? DateFromTime(t) : ToNumber(date);
  var day = MakeDay(year, month, date);
  return %_SetValueOf(this, TimeClip(UTC(MakeDate(day, TimeWithinDay(t)))));
};


// ECMA 262 - 15.9.5.41
function DateSetUTCFullYear(year, month, date) {
  var t = GetTimeFrom(this);
  if ($isNaN(t)) t = 0;
  var argc = %_ArgumentsLength();
  year = ToNumber(year);
  month = argc < 2 ? MonthFromTime(t) : ToNumber(month);
  date = argc < 3 ? DateFromTime(t) : ToNumber(date);
  var day = MakeDay(year, month, date);
  return %_SetValueOf(this, TimeClip(MakeDate(day, TimeWithinDay(t))));
};


// ECMA 262 - 15.9.5.42
function DateToUTCString() {
  var t = GetTimeFrom(this);
  if ($isNaN(t)) return kInvalidDate;
  // Return UTC string of the form: Sat, 31 Jan 1970 23:00:00 GMT
  return WeekDays[WeekDay(t)] + ', '
      + TwoDigitString(DateFromTime(t)) + ' '
      + Months[MonthFromTime(t)] + ' '
      + YearFromTime(t) + ' '
      + TimeString(t) + ' GMT';
};


// ECMA 262 - B.2.4
function DateGetYear() {
  var t = GetTimeFrom(this);
  if ($isNaN(t)) return $NaN;
  return YearFromTime(LocalTime(t)) - 1900;
};


// ECMA 262 - B.2.5
function DateSetYear(year) {
  var t = LocalTime(GetTimeFrom(this));
  if ($isNaN(t)) t = 0;
  year = ToNumber(year);
  if ($isNaN(year)) return %_SetValueOf(this, $NaN);
  year = (0 <= TO_INTEGER(year) && TO_INTEGER(year) <= 99)
      ? 1900 + TO_INTEGER(year) : year;
  var day = MakeDay(year, GetMonthFrom(this), GetDateFrom(this));
  return %_SetValueOf(this, TimeClip(UTC(MakeDate(day, TimeWithinDay(t)))));
};


// -------------------------------------------------------------------

function SetupDate() {
  // Setup non-enumerable properties of the Date object itself.
  InstallProperties($Date, DONT_ENUM, {
    UTC: DateUTC,
    parse: DateParse,
    now: DateNow
  });

  // Setup non-enumerable properties of the Date prototype object.
  InstallProperties($Date.prototype, DONT_ENUM, {
    constructor: $Date,
    toString: DateToString,
    toDateString: DateToDateString,
    toTimeString: DateToTimeString,
    toLocaleString: DateToString,
    toLocaleDateString: DateToDateString,
    toLocaleTimeString: DateToTimeString,
    valueOf: DateGetTime,
    getTime: DateGetTime,
    getFullYear: DateGetFullYear,
    getUTCFullYear: DateGetUTCFullYear,
    getMonth: DateGetMonth,
    getUTCMonth: DateGetUTCMonth,
    getDate: DateGetDate,
    getUTCDate: DateGetUTCDate,
    getDay: DateGetDay,
    getUTCDay: DateGetUTCDay,
    getHours: DateGetHours,
    getUTCHours: DateGetUTCHours,
    getMinutes: DateGetMinutes,
    getUTCMinutes: DateGetUTCMinutes,
    getSeconds: DateGetSeconds,
    getUTCSeconds: DateGetUTCSeconds,
    getMilliseconds: DateGetMilliseconds,
    getUTCMilliseconds: DateGetUTCMilliseconds,
    getTimezoneOffset: DateGetTimezoneOffset,
    setTime: DateSetTime,
    setMilliseconds: DateSetMilliseconds,
    setUTCMilliseconds: DateSetUTCMilliseconds,
    setSeconds: DateSetSeconds,
    setUTCSeconds: DateSetUTCSeconds,
    setMinutes: DateSetMinutes,
    setUTCMinutes: DateSetUTCMinutes,
    setHours: DateSetHours,
    setUTCHours: DateSetUTCHours,
    setDate: DateSetDate,
    setUTCDate: DateSetUTCDate,
    setMonth: DateSetMonth,
    setUTCMonth: DateSetUTCMonth,
    setFullYear: DateSetFullYear,
    setUTCFullYear: DateSetUTCFullYear,
    toUTCString: DateToUTCString,
    toGMTString: DateToUTCString,
    getYear: DateGetYear,
    setYear: DateSetYear
  });
};

SetupDate();
