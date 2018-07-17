// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-intl-relative-time-format

// The following test are not part of the comformance. Just some output in
// English to verify the format does return something reasonable for English.
// It may be changed when we update the CLDR data.
// NOTE: These are UNSPECIFIED behavior in
// http://tc39.github.io/proposal-intl-relative-time/

let longAuto = new Intl.RelativeTimeFormat(
    "en", {style: "long", localeMatcher: 'lookup', numeric: 'auto'});

assertEquals('03 seconds ago', longAuto.format(-3, 'second'));
assertEquals('02 seconds ago', longAuto.format(-2, 'second'));
assertEquals('01 second ago', longAuto.format(-1, 'second'));
assertEquals('now', longAuto.format(0, 'second'));
assertEquals('now', longAuto.format(-0, 'second'));
assertEquals('in 01 second', longAuto.format(1, 'second'));
assertEquals('in 02 seconds', longAuto.format(2, 'second'));
assertEquals('in 345 seconds', longAuto.format(345, 'second'));

assertEquals('03 minutes ago', longAuto.format(-3, 'minute'));
assertEquals('02 minutes ago', longAuto.format(-2, 'minute'));
assertEquals('01 minute ago', longAuto.format(-1, 'minute'));
assertEquals('in 00 minutes', longAuto.format(0, 'minute'));
assertEquals('00 minutes ago', longAuto.format(-0, 'minute'));
assertEquals('in 01 minute', longAuto.format(1, 'minute'));
assertEquals('in 02 minutes', longAuto.format(2, 'minute'));
assertEquals('in 345 minutes', longAuto.format(345, 'minute'));

assertEquals('03 hours ago', longAuto.format(-3, 'hour'));
assertEquals('02 hours ago', longAuto.format(-2, 'hour'));
assertEquals('01 hour ago', longAuto.format(-1, 'hour'));
assertEquals('in 00 hours', longAuto.format(0, 'hour'));
assertEquals('00 hours ago', longAuto.format(-0, 'hour'));
assertEquals('in 01 hour', longAuto.format(1, 'hour'));
assertEquals('in 02 hours', longAuto.format(2, 'hour'));
assertEquals('in 345 hours', longAuto.format(345, 'hour'));

assertEquals('03 days ago', longAuto.format(-3, 'day'));
assertEquals('02 days ago', longAuto.format(-2, 'day'));
assertEquals('yesterday', longAuto.format(-1, 'day'));
assertEquals('today', longAuto.format(0, 'day'));
assertEquals('today', longAuto.format(-0, 'day'));
assertEquals('tomorrow', longAuto.format(1, 'day'));
assertEquals('in 02 days', longAuto.format(2, 'day'));
assertEquals('in 345 days', longAuto.format(345, 'day'));

assertEquals('03 weeks ago', longAuto.format(-3, 'week'));
assertEquals('02 weeks ago', longAuto.format(-2, 'week'));
assertEquals('last week', longAuto.format(-1, 'week'));
assertEquals('this week', longAuto.format(0, 'week'));
assertEquals('this week', longAuto.format(-0, 'week'));
assertEquals('next week', longAuto.format(1, 'week'));
assertEquals('in 02 weeks', longAuto.format(2, 'week'));
assertEquals('in 345 weeks', longAuto.format(345, 'week'));

assertEquals('03 months ago', longAuto.format(-3, 'month'));
assertEquals('02 months ago', longAuto.format(-2, 'month'));
assertEquals('last month', longAuto.format(-1, 'month'));
assertEquals('this month', longAuto.format(0, 'month'));
assertEquals('this month', longAuto.format(-0, 'month'));
assertEquals('next month', longAuto.format(1, 'month'));
assertEquals('in 02 months', longAuto.format(2, 'month'));
assertEquals('in 345 months', longAuto.format(345, 'month'));

// "quarter" is not working in ICU now
// Tracked by ICU bug in http://bugs.icu-project.org/trac/ticket/12171
/*
assertEquals('03 quarters ago', longAuto.format(-3, 'quarter'));
assertEquals('02 quarters ago', longAuto.format(-2, 'quarter'));
assertEquals('last quarter', longAuto.format(-1, 'quarter'));
assertEquals('this quarter', longAuto.format(0, 'quarter'));
assertEquals('this quarter', longAuto.format(-0, 'quarter'));
assertEquals('next quarter', longAuto.format(1, 'quarter'));
assertEquals('in 02 quarters', longAuto.format(2, 'quarter'));
assertEquals('in 345 quarters', longAuto.format(345, 'quarter'));
*/

assertEquals('03 years ago', longAuto.format(-3, 'year'));
assertEquals('02 years ago', longAuto.format(-2, 'year'));
assertEquals('last year', longAuto.format(-1, 'year'));
assertEquals('this year', longAuto.format(0, 'year'));
assertEquals('this year', longAuto.format(-0, 'year'));
assertEquals('next year', longAuto.format(1, 'year'));
assertEquals('in 02 years', longAuto.format(2, 'year'));
assertEquals('in 345 years', longAuto.format(345, 'year'));

let shortAuto = new Intl.RelativeTimeFormat(
    "en", {style: "short", localeMatcher: 'lookup', numeric: 'auto'});

assertEquals('03 sec. ago', shortAuto.format(-3, 'second'));
assertEquals('02 sec. ago', shortAuto.format(-2, 'second'));
assertEquals('01 sec. ago', shortAuto.format(-1, 'second'));
assertEquals('now', shortAuto.format(0, 'second'));
assertEquals('now', shortAuto.format(-0, 'second'));
assertEquals('in 01 sec.', shortAuto.format(1, 'second'));
assertEquals('in 02 sec.', shortAuto.format(2, 'second'));
assertEquals('in 345 sec.', shortAuto.format(345, 'second'));

assertEquals('03 min. ago', shortAuto.format(-3, 'minute'));
assertEquals('02 min. ago', shortAuto.format(-2, 'minute'));
assertEquals('01 min. ago', shortAuto.format(-1, 'minute'));
assertEquals('in 00 min.', shortAuto.format(0, 'minute'));
assertEquals('00 min. ago', shortAuto.format(-0, 'minute'));
assertEquals('in 01 min.', shortAuto.format(1, 'minute'));
assertEquals('in 02 min.', shortAuto.format(2, 'minute'));
assertEquals('in 345 min.', shortAuto.format(345, 'minute'));

assertEquals('03 hr. ago', shortAuto.format(-3, 'hour'));
assertEquals('02 hr. ago', shortAuto.format(-2, 'hour'));
assertEquals('01 hr. ago', shortAuto.format(-1, 'hour'));
assertEquals('in 00 hr.', shortAuto.format(0, 'hour'));
assertEquals('00 hr. ago', shortAuto.format(-0, 'hour'));
assertEquals('in 01 hr.', shortAuto.format(1, 'hour'));
assertEquals('in 02 hr.', shortAuto.format(2, 'hour'));
assertEquals('in 345 hr.', shortAuto.format(345, 'hour'));

assertEquals('03 days ago', shortAuto.format(-3, 'day'));
assertEquals('02 days ago', shortAuto.format(-2, 'day'));
assertEquals('yesterday', shortAuto.format(-1, 'day'));
assertEquals('today', shortAuto.format(0, 'day'));
assertEquals('today', shortAuto.format(-0, 'day'));
assertEquals('tomorrow', shortAuto.format(1, 'day'));
assertEquals('in 02 days', shortAuto.format(2, 'day'));
assertEquals('in 345 days', shortAuto.format(345, 'day'));

assertEquals('03 wk. ago', shortAuto.format(-3, 'week'));
assertEquals('02 wk. ago', shortAuto.format(-2, 'week'));
assertEquals('last wk.', shortAuto.format(-1, 'week'));
assertEquals('this wk.', shortAuto.format(0, 'week'));
assertEquals('this wk.', shortAuto.format(-0, 'week'));
assertEquals('next wk.', shortAuto.format(1, 'week'));
assertEquals('in 02 wk.', shortAuto.format(2, 'week'));
assertEquals('in 345 wk.', shortAuto.format(345, 'week'));

assertEquals('03 mo. ago', shortAuto.format(-3, 'month'));
assertEquals('02 mo. ago', shortAuto.format(-2, 'month'));
assertEquals('last mo.', shortAuto.format(-1, 'month'));
assertEquals('this mo.', shortAuto.format(0, 'month'));
assertEquals('this mo.', shortAuto.format(-0, 'month'));
assertEquals('next mo.', shortAuto.format(1, 'month'));
assertEquals('in 02 mo.', shortAuto.format(2, 'month'));
assertEquals('in 345 mo.', shortAuto.format(345, 'month'));

// "quarter" is not working in ICU now
/*
assertEquals('03 qtrs. ago', shortAuto.format(-3, 'quarter'));
assertEquals('02 qtrs. ago', shortAuto.format(-2, 'quarter'));
assertEquals('last qtr.', shortAuto.format(-1, 'quarter'));
assertEquals('this qtr.', shortAuto.format(0, 'quarter'));
assertEquals('this qtr.', shortAuto.format(-0, 'quarter'));
assertEquals('next qtr.', shortAuto.format(1, 'quarter'));
assertEquals('in 02 qtrs.', shortAuto.format(2, 'quarter'));
assertEquals('in 345 qtrs.', shortAuto.format(345, 'quarter'));
*/

assertEquals('03 yr. ago', shortAuto.format(-3, 'year'));
assertEquals('02 yr. ago', shortAuto.format(-2, 'year'));
assertEquals('last yr.', shortAuto.format(-1, 'year'));
assertEquals('this yr.', shortAuto.format(0, 'year'));
assertEquals('this yr.', shortAuto.format(-0, 'year'));
assertEquals('next yr.', shortAuto.format(1, 'year'));
assertEquals('in 02 yr.', shortAuto.format(2, 'year'));
assertEquals('in 345 yr.', shortAuto.format(345, 'year'));

// Somehow in the 'en' locale, there are no valeu for -narrow
let narrowAuto = new Intl.RelativeTimeFormat(
    "en", {style: "narrow", localeMatcher: 'lookup', numeric: 'auto'});

assertEquals('03 sec. ago', narrowAuto.format(-3, 'second'));
assertEquals('02 sec. ago', narrowAuto.format(-2, 'second'));
assertEquals('01 sec. ago', narrowAuto.format(-1, 'second'));
assertEquals('now', narrowAuto.format(0, 'second'));
assertEquals('now', narrowAuto.format(-0, 'second'));
assertEquals('in 01 sec.', narrowAuto.format(1, 'second'));
assertEquals('in 02 sec.', narrowAuto.format(2, 'second'));
assertEquals('in 345 sec.', narrowAuto.format(345, 'second'));

assertEquals('03 min. ago', narrowAuto.format(-3, 'minute'));
assertEquals('02 min. ago', narrowAuto.format(-2, 'minute'));
assertEquals('01 min. ago', narrowAuto.format(-1, 'minute'));
assertEquals('in 00 min.', narrowAuto.format(0, 'minute'));
assertEquals('00 min. ago', narrowAuto.format(-0, 'minute'));
assertEquals('in 01 min.', narrowAuto.format(1, 'minute'));
assertEquals('in 02 min.', narrowAuto.format(2, 'minute'));
assertEquals('in 345 min.', narrowAuto.format(345, 'minute'));

assertEquals('03 hr. ago', narrowAuto.format(-3, 'hour'));
assertEquals('02 hr. ago', narrowAuto.format(-2, 'hour'));
assertEquals('01 hr. ago', narrowAuto.format(-1, 'hour'));
assertEquals('in 00 hr.', narrowAuto.format(0, 'hour'));
assertEquals('00 hr. ago', narrowAuto.format(-0, 'hour'));
assertEquals('in 01 hr.', narrowAuto.format(1, 'hour'));
assertEquals('in 02 hr.', narrowAuto.format(2, 'hour'));
assertEquals('in 345 hr.', narrowAuto.format(345, 'hour'));

assertEquals('03 days ago', narrowAuto.format(-3, 'day'));
assertEquals('02 days ago', narrowAuto.format(-2, 'day'));
assertEquals('yesterday', narrowAuto.format(-1, 'day'));
assertEquals('today', narrowAuto.format(0, 'day'));
assertEquals('today', narrowAuto.format(-0, 'day'));
assertEquals('tomorrow', narrowAuto.format(1, 'day'));
assertEquals('in 02 days', narrowAuto.format(2, 'day'));
assertEquals('in 345 days', narrowAuto.format(345, 'day'));

assertEquals('03 wk. ago', narrowAuto.format(-3, 'week'));
assertEquals('02 wk. ago', narrowAuto.format(-2, 'week'));
assertEquals('last wk.', narrowAuto.format(-1, 'week'));
assertEquals('this wk.', narrowAuto.format(0, 'week'));
assertEquals('this wk.', narrowAuto.format(-0, 'week'));
assertEquals('next wk.', narrowAuto.format(1, 'week'));
assertEquals('in 02 wk.', narrowAuto.format(2, 'week'));
assertEquals('in 345 wk.', narrowAuto.format(345, 'week'));

assertEquals('03 mo. ago', narrowAuto.format(-3, 'month'));
assertEquals('02 mo. ago', narrowAuto.format(-2, 'month'));
assertEquals('last mo.', narrowAuto.format(-1, 'month'));
assertEquals('this mo.', narrowAuto.format(0, 'month'));
assertEquals('this mo.', narrowAuto.format(-0, 'month'));
assertEquals('next mo.', narrowAuto.format(1, 'month'));
assertEquals('in 02 mo.', narrowAuto.format(2, 'month'));
assertEquals('in 345 mo.', narrowAuto.format(345, 'month'));

// "quarter" is not working in ICU now
/*
assertEquals('03 qtrs. ago', narrowAuto.format(-3, 'quarter'));
assertEquals('02 qtrs. ago', narrowAuto.format(-2, 'quarter'));
assertEquals('last qtr.', narrowAuto.format(-1, 'quarter'));
assertEquals('this qtr.', narrowAuto.format(0, 'quarter'));
assertEquals('this qtr.', narrowAuto.format(-0, 'quarter'));
assertEquals('next qtr.', narrowAuto.format(1, 'quarter'));
assertEquals('in 02 qtrs.', narrowAuto.format(2, 'quarter'));
assertEquals('in 345 qtrs.', narrowAuto.format(345, 'quarter'));
*/

assertEquals('03 yr. ago', narrowAuto.format(-3, 'year'));
assertEquals('02 yr. ago', narrowAuto.format(-2, 'year'));
assertEquals('last yr.', narrowAuto.format(-1, 'year'));
assertEquals('this yr.', narrowAuto.format(0, 'year'));
assertEquals('this yr.', narrowAuto.format(-0, 'year'));
assertEquals('next yr.', narrowAuto.format(1, 'year'));
assertEquals('in 02 yr.', narrowAuto.format(2, 'year'));
assertEquals('in 345 yr.', narrowAuto.format(345, 'year'));

let longAlways = new Intl.RelativeTimeFormat(
    "en", {style: "long", localeMatcher: 'lookup', numeric: 'always'});

assertEquals('03 seconds ago', longAlways.format(-3, 'second'));
assertEquals('02 seconds ago', longAlways.format(-2, 'second'));
assertEquals('01 second ago', longAlways.format(-1, 'second'));
assertEquals('in 00 seconds', longAlways.format(0, 'second'));
assertEquals('00 seconds ago', longAlways.format(-0, 'second'));
assertEquals('in 01 second', longAlways.format(1, 'second'));
assertEquals('in 02 seconds', longAlways.format(2, 'second'));
assertEquals('in 345 seconds', longAlways.format(345, 'second'));

assertEquals('03 minutes ago', longAlways.format(-3, 'minute'));
assertEquals('02 minutes ago', longAlways.format(-2, 'minute'));
assertEquals('01 minute ago', longAlways.format(-1, 'minute'));
assertEquals('in 00 minutes', longAlways.format(0, 'minute'));
assertEquals('00 minutes ago', longAlways.format(-0, 'minute'));
assertEquals('in 01 minute', longAlways.format(1, 'minute'));
assertEquals('in 02 minutes', longAlways.format(2, 'minute'));
assertEquals('in 345 minutes', longAlways.format(345, 'minute'));

assertEquals('03 hours ago', longAlways.format(-3, 'hour'));
assertEquals('02 hours ago', longAlways.format(-2, 'hour'));
assertEquals('01 hour ago', longAlways.format(-1, 'hour'));
assertEquals('in 00 hours', longAlways.format(0, 'hour'));
assertEquals('00 hours ago', longAlways.format(-0, 'hour'));
assertEquals('in 01 hour', longAlways.format(1, 'hour'));
assertEquals('in 02 hours', longAlways.format(2, 'hour'));
assertEquals('in 345 hours', longAlways.format(345, 'hour'));

assertEquals('03 days ago', longAlways.format(-3, 'day'));
assertEquals('02 days ago', longAlways.format(-2, 'day'));
assertEquals('01 day ago', longAlways.format(-1, 'day'));
assertEquals('in 00 days', longAlways.format(0, 'day'));
assertEquals('00 days ago', longAlways.format(-0, 'day'));
assertEquals('in 01 day', longAlways.format(1, 'day'));
assertEquals('in 02 days', longAlways.format(2, 'day'));
assertEquals('in 345 days', longAlways.format(345, 'day'));

assertEquals('03 weeks ago', longAlways.format(-3, 'week'));
assertEquals('02 weeks ago', longAlways.format(-2, 'week'));
assertEquals('01 week ago', longAlways.format(-1, 'week'));
assertEquals('in 00 weeks', longAlways.format(0, 'week'));
assertEquals('00 weeks ago', longAlways.format(-0, 'week'));
assertEquals('in 01 week', longAlways.format(1, 'week'));
assertEquals('in 02 weeks', longAlways.format(2, 'week'));
assertEquals('in 345 weeks', longAlways.format(345, 'week'));

assertEquals('03 months ago', longAlways.format(-3, 'month'));
assertEquals('02 months ago', longAlways.format(-2, 'month'));
assertEquals('01 month ago', longAlways.format(-1, 'month'));
assertEquals('in 00 months', longAlways.format(0, 'month'));
assertEquals('00 months ago', longAlways.format(-0, 'month'));
assertEquals('in 01 month', longAlways.format(1, 'month'));
assertEquals('in 02 months', longAlways.format(2, 'month'));
assertEquals('in 345 months', longAlways.format(345, 'month'));

// "quarter" is not working in ICU now
/*
assertEquals('03 quarters ago', longAlways.format(-3, 'quarter'));
assertEquals('02 quarters ago', longAlways.format(-2, 'quarter'));
assertEquals('01 quarter ago', longAlways.format(-1, 'quarter'));
assertEquals('in 00 quarters', longAlways.format(0, 'quarter'));
assertEquals('00 quarters ago', longAlways.format(-0, 'quarter'));
assertEquals('in 01 quarter', longAlways.format(1, 'quarter'));
assertEquals('in 02 quarters', longAlways.format(2, 'quarter'));
assertEquals('in 345 quarters', longAlways.format(345, 'quarter'));
*/

assertEquals('03 years ago', longAlways.format(-3, 'year'));
assertEquals('02 years ago', longAlways.format(-2, 'year'));
assertEquals('01 year ago', longAlways.format(-1, 'year'));
assertEquals('in 00 years', longAlways.format(0, 'year'));
assertEquals('00 years ago', longAlways.format(-0, 'year'));
assertEquals('in 01 year', longAlways.format(1, 'year'));
assertEquals('in 02 years', longAlways.format(2, 'year'));
assertEquals('in 345 years', longAlways.format(345, 'year'));

let shortAlways = new Intl.RelativeTimeFormat(
    "en", {style: "short", localeMatcher: 'lookup', numeric: 'always'});

assertEquals('03 sec. ago', shortAlways.format(-3, 'second'));
assertEquals('02 sec. ago', shortAlways.format(-2, 'second'));
assertEquals('01 sec. ago', shortAlways.format(-1, 'second'));
assertEquals('in 00 sec.', shortAlways.format(0, 'second'));
assertEquals('00 sec. ago', shortAlways.format(-0, 'second'));
assertEquals('in 01 sec.', shortAlways.format(1, 'second'));
assertEquals('in 02 sec.', shortAlways.format(2, 'second'));
assertEquals('in 345 sec.', shortAlways.format(345, 'second'));

assertEquals('03 min. ago', shortAlways.format(-3, 'minute'));
assertEquals('02 min. ago', shortAlways.format(-2, 'minute'));
assertEquals('01 min. ago', shortAlways.format(-1, 'minute'));
assertEquals('in 00 min.', shortAlways.format(0, 'minute'));
assertEquals('00 min. ago', shortAlways.format(-0, 'minute'));
assertEquals('in 01 min.', shortAlways.format(1, 'minute'));
assertEquals('in 02 min.', shortAlways.format(2, 'minute'));
assertEquals('in 345 min.', shortAlways.format(345, 'minute'));

assertEquals('03 hr. ago', shortAlways.format(-3, 'hour'));
assertEquals('02 hr. ago', shortAlways.format(-2, 'hour'));
assertEquals('01 hr. ago', shortAlways.format(-1, 'hour'));
assertEquals('in 00 hr.', shortAlways.format(0, 'hour'));
assertEquals('00 hr. ago', shortAlways.format(-0, 'hour'));
assertEquals('in 01 hr.', shortAlways.format(1, 'hour'));
assertEquals('in 02 hr.', shortAlways.format(2, 'hour'));
assertEquals('in 345 hr.', shortAlways.format(345, 'hour'));

assertEquals('03 days ago', shortAlways.format(-3, 'day'));
assertEquals('02 days ago', shortAlways.format(-2, 'day'));
assertEquals('01 day ago', shortAlways.format(-1, 'day'));
assertEquals('in 00 days', shortAlways.format(0, 'day'));
assertEquals('00 days ago', shortAlways.format(-0, 'day'));
assertEquals('in 01 day', shortAlways.format(1, 'day'));
assertEquals('in 02 days', shortAlways.format(2, 'day'));
assertEquals('in 345 days', shortAlways.format(345, 'day'));

assertEquals('03 wk. ago', shortAlways.format(-3, 'week'));
assertEquals('02 wk. ago', shortAlways.format(-2, 'week'));
assertEquals('01 wk. ago', shortAlways.format(-1, 'week'));
assertEquals('in 00 wk.', shortAlways.format(0, 'week'));
assertEquals('00 wk. ago', shortAlways.format(-0, 'week'));
assertEquals('in 01 wk.', shortAlways.format(1, 'week'));
assertEquals('in 02 wk.', shortAlways.format(2, 'week'));
assertEquals('in 345 wk.', shortAlways.format(345, 'week'));

assertEquals('03 mo. ago', shortAlways.format(-3, 'month'));
assertEquals('02 mo. ago', shortAlways.format(-2, 'month'));
assertEquals('01 mo. ago', shortAlways.format(-1, 'month'));
assertEquals('in 00 mo.', shortAlways.format(0, 'month'));
assertEquals('00 mo. ago', shortAlways.format(-0, 'month'));
assertEquals('in 01 mo.', shortAlways.format(1, 'month'));
assertEquals('in 02 mo.', shortAlways.format(2, 'month'));
assertEquals('in 345 mo.', shortAlways.format(345, 'month'));

// "quarter" is not working in ICU now
/*
assertEquals('03 qtrs. ago', shortAlways.format(-3, 'quarter'));
assertEquals('02 qtrs. ago', shortAlways.format(-2, 'quarter'));
assertEquals('01 qtr. ago', shortAlways.format(-1, 'quarter'));
assertEquals('in 00 qtrs.', shortAlways.format(0, 'quarter'));
assertEquals('00 qtr. ago', shortAlways.format(-0, 'quarter'));
assertEquals('in 01 qtr.', shortAlways.format(1, 'quarter'));
assertEquals('in 02 qtrs.', shortAlways.format(2, 'quarter'));
assertEquals('in 345 qtrs.', shortAlways.format(345, 'quarter'));
*/

assertEquals('03 yr. ago', shortAlways.format(-3, 'year'));
assertEquals('02 yr. ago', shortAlways.format(-2, 'year'));
assertEquals('01 yr. ago', shortAlways.format(-1, 'year'));
assertEquals('in 00 yr.', shortAlways.format(0, 'year'));
assertEquals('00 yr. ago', shortAlways.format(-0, 'year'));
assertEquals('in 01 yr.', shortAlways.format(1, 'year'));
assertEquals('in 02 yr.', shortAlways.format(2, 'year'));
assertEquals('in 345 yr.', shortAlways.format(345, 'year'));

// Somehow in the 'en' locale, there are no valeu for -narrow
let narrowAlways = new Intl.RelativeTimeFormat(
    "en", {style: "narrow", localeMatcher: 'lookup', numeric: 'always'});

assertEquals('03 sec. ago', narrowAlways.format(-3, 'second'));
assertEquals('02 sec. ago', narrowAlways.format(-2, 'second'));
assertEquals('01 sec. ago', narrowAlways.format(-1, 'second'));
assertEquals('in 00 sec.', narrowAlways.format(0, 'second'));
assertEquals('00 sec. ago', narrowAlways.format(-0, 'second'));
assertEquals('in 01 sec.', narrowAlways.format(1, 'second'));
assertEquals('in 02 sec.', narrowAlways.format(2, 'second'));
assertEquals('in 345 sec.', narrowAlways.format(345, 'second'));

assertEquals('03 min. ago', narrowAlways.format(-3, 'minute'));
assertEquals('02 min. ago', narrowAlways.format(-2, 'minute'));
assertEquals('01 min. ago', narrowAlways.format(-1, 'minute'));
assertEquals('in 00 min.', narrowAlways.format(0, 'minute'));
assertEquals('00 min. ago', narrowAlways.format(-0, 'minute'));
assertEquals('in 01 min.', narrowAlways.format(1, 'minute'));
assertEquals('in 02 min.', narrowAlways.format(2, 'minute'));
assertEquals('in 345 min.', narrowAlways.format(345, 'minute'));

assertEquals('03 hr. ago', narrowAlways.format(-3, 'hour'));
assertEquals('02 hr. ago', narrowAlways.format(-2, 'hour'));
assertEquals('01 hr. ago', narrowAlways.format(-1, 'hour'));
assertEquals('in 00 hr.', narrowAlways.format(0, 'hour'));
assertEquals('00 hr. ago', narrowAlways.format(-0, 'hour'));
assertEquals('in 01 hr.', narrowAlways.format(1, 'hour'));
assertEquals('in 02 hr.', narrowAlways.format(2, 'hour'));
assertEquals('in 345 hr.', narrowAlways.format(345, 'hour'));

assertEquals('03 days ago', narrowAlways.format(-3, 'day'));
assertEquals('02 days ago', narrowAlways.format(-2, 'day'));
assertEquals('01 day ago', narrowAlways.format(-1, 'day'));
assertEquals('in 00 days', narrowAlways.format(0, 'day'));
assertEquals('00 days ago', narrowAlways.format(-0, 'day'));
assertEquals('in 01 day', narrowAlways.format(1, 'day'));
assertEquals('in 02 days', narrowAlways.format(2, 'day'));
assertEquals('in 345 days', narrowAlways.format(345, 'day'));

assertEquals('03 wk. ago', narrowAlways.format(-3, 'week'));
assertEquals('02 wk. ago', narrowAlways.format(-2, 'week'));
assertEquals('01 wk. ago', narrowAlways.format(-1, 'week'));
assertEquals('in 00 wk.', narrowAlways.format(0, 'week'));
assertEquals('00 wk. ago', narrowAlways.format(-0, 'week'));
assertEquals('in 01 wk.', narrowAlways.format(1, 'week'));
assertEquals('in 02 wk.', narrowAlways.format(2, 'week'));
assertEquals('in 345 wk.', narrowAlways.format(345, 'week'));

assertEquals('03 mo. ago', narrowAlways.format(-3, 'month'));
assertEquals('02 mo. ago', narrowAlways.format(-2, 'month'));
assertEquals('01 mo. ago', narrowAlways.format(-1, 'month'));
assertEquals('in 00 mo.', narrowAlways.format(0, 'month'));
assertEquals('00 mo. ago', narrowAlways.format(-0, 'month'));
assertEquals('in 01 mo.', narrowAlways.format(1, 'month'));
assertEquals('in 02 mo.', narrowAlways.format(2, 'month'));
assertEquals('in 345 mo.', narrowAlways.format(345, 'month'));

// "quarter" is not working in ICU now
/*
assertEquals('03 qtrs. ago', narrowAlways.format(-3, 'quarter'));
assertEquals('02 qtrs. ago', narrowAlways.format(-2, 'quarter'));
assertEquals('01 qtr. ago', narrowAlways.format(-1, 'quarter'));
assertEquals('in 00 qtrs.', narrowAlways.format(0, 'quarter'));
assertEquals('00 qtr. ago', narrowAlways.format(-0, 'quarter'));
assertEquals('in 01 qtr.', narrowAlways.format(1, 'quarter'));
assertEquals('in 02 qtrs.', narrowAlways.format(2, 'quarter'));
assertEquals('in 345 qtrs.', narrowAlways.format(345, 'quarter'));
*/

assertEquals('03 yr. ago', narrowAlways.format(-3, 'year'));
assertEquals('02 yr. ago', narrowAlways.format(-2, 'year'));
assertEquals('01 yr. ago', narrowAlways.format(-1, 'year'));
assertEquals('in 00 yr.', narrowAlways.format(0, 'year'));
assertEquals('00 yr. ago', narrowAlways.format(-0, 'year'));
assertEquals('in 01 yr.', narrowAlways.format(1, 'year'));
assertEquals('in 02 yr.', narrowAlways.format(2, 'year'));
assertEquals('in 345 yr.', narrowAlways.format(345, 'year'));

var styleNumericCombinations = [
    longAuto, shortAuto, narrowAuto, longAlways,
    shortAlways, narrowAlways ];
var validUnits = [
    'second', 'minute', 'hour', 'day', 'week', 'month', 'quarter', 'year'];

// Test these all throw RangeError
for (var i = 0; i < styleNumericCombinations.length; i++) {
  for (var j = 0; j < validUnits.length; j++) {
    assertThrows(() => styleNumericCombinations[i].format(NaN, validUnits[j]),
        RangeError);
    assertThrows(() => styleNumericCombinations[i].format(NaN, validUnits[j] + 's'),
        RangeError);
    assertThrows(() => styleNumericCombinations[i].format(NaN, validUnits[j]),
        RangeError);
    assertThrows(() => styleNumericCombinations[i].format(NaN, validUnits[j] + 's'),
        RangeError);
  }
}
