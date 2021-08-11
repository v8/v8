// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

assertThrows(
    () => new Intl.DateTimeFormat('en', {calendar: 'abc-'.repeat(100000)}),
    RangeError,
    "Invalid calendar : " + ('abc-'.repeat(100000)));

assertThrows(
    () => new Intl.DateTimeFormat('en', {calendar: 'abc_'.repeat(100000)}),
    RangeError,
    "Invalid calendar : " + ('abc_'.repeat(100000)));

assertThrows(
    () => new Intl.DateTimeFormat('en', {calendar: 'abc_efgh-'.repeat(100000)}),
    RangeError,
    "Invalid calendar : " + ('abc_efgh-'.repeat(100000)));
