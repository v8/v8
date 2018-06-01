// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-locale

// Locale constructor can't be called as function.
assertThrows(() => Intl.Locale('sr'), TypeError);

// Non-string locale.
assertThrows(() => new Intl.Locale(5), TypeError);

// Invalid locale string.
assertThrows(() => new Intl.Locale('abcdefghi'), RangeError);

// Options will be force converted into Object.
assertDoesNotThrow(() => new Intl.Locale('sr', 5));

// ICU problem - locale length is limited.
// http://bugs.icu-project.org/trac/ticket/13417.
assertThrows(
    () => new Intl.Locale(
        'sr-cyrl-rs-t-ja-u-ca-islamic-cu-rsd-tz-uslax-x-whatever', {
          calendar: 'buddhist',
          caseFirst: 'true',
          collation: 'phonebk',
          hourCycle: 'h23',
          caseFirst: 'upper',
          numeric: 'true',
          numberingSystem: 'roman',
        }),
    RangeError);

// Throws only once during construction.
// Check for all getters to prevent regression.
assertThrows(
    () => new Intl.Locale('en-US', {
      get calendar() {
        throw new Error('foo');
      }
    }),
    Error);

assertThrows(
    () => new Intl.Locale('en-US', {
      get caseFirst() {
        throw new Error('foo');
      }
    }),
    Error);

assertThrows(
    () => new Intl.Locale('en-US', {
      get collation() {
        throw new Error('foo');
      }
    }),
    Error);

assertThrows(
    () => new Intl.Locale('en-US', {
      get hourCycle() {
        throw new Error('foo');
      }
    }),
    Error);

assertThrows(
    () => new Intl.Locale('en-US', {
      get numeric() {
        throw new Error('foo');
      }
    }),
    Error);

assertThrows(
    () => new Intl.Locale('en-US', {
      get numberingSystem() {
        throw new Error('foo');
      }
    }),
    Error);

// These don't throw yet, we need to implement language/script/region
// override logic first.
assertDoesNotThrow(
    () => new Intl.Locale('en-US', {
      get language() {
        throw new Error('foo');
      }
    }),
    Error);

assertDoesNotThrow(
    () => new Intl.Locale('en-US', {
      get script() {
        throw new Error('foo');
      }
    }),
    Error);

assertDoesNotThrow(
    () => new Intl.Locale('en-US', {
      get region() {
        throw new Error('foo');
      }
    }),
    Error);

// There won't be an override for baseName so we don't expect it to throw.
assertDoesNotThrow(
    () => new Intl.Locale('en-US', {
      get baseName() {
        throw new Error('foo');
      }
    }),
    Error);
