// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-locale
//
// Test NumberFormat will accept Intl.Locale as first parameter, or
// as in the array.

let tag = "zh-Hant-TW-u-nu-thai"
let l = new Intl.Locale(tag);

var nf;
// Test with String
assertDoesNotThrow(() => nf = new Intl.NumberFormat(tag));
assertEquals(tag, nf.resolvedOptions().locale);

// Test with Array with one String
assertDoesNotThrow(() => nf = new Intl.NumberFormat([tag]));
assertEquals(tag, nf.resolvedOptions().locale);

// Test with Array with two String
assertDoesNotThrow(() => nf = new Intl.NumberFormat([tag, "en"]));
assertEquals(tag, nf.resolvedOptions().locale);

// Test with a Locale
assertDoesNotThrow(() => nf = new Intl.NumberFormat(l));
assertEquals(tag, nf.resolvedOptions().locale);

// Test with a Array of one Locale
assertDoesNotThrow(() => nf = new Intl.NumberFormat([l]));
assertEquals(tag, nf.resolvedOptions().locale);

// Test with a Array of one Locale and a Sring
assertDoesNotThrow(() => nf = new Intl.NumberFormat([l, "en"]));
assertEquals(tag, nf.resolvedOptions().locale);

// Test DateTimeFormat
var df;
assertDoesNotThrow(() => df = new Intl.DateTimeFormat(tag));
assertEquals(tag, df.resolvedOptions().locale);
assertDoesNotThrow(() => df = new Intl.DateTimeFormat([tag]));
assertEquals(tag, df.resolvedOptions().locale);

// Test RelativeTimeFormat
var rtf;
assertDoesNotThrow(() => rtf = new Intl.RelativeTimeFormat(tag));
assertEquals(tag, rtf.resolvedOptions().locale);
assertDoesNotThrow(() => rtf = new Intl.RelativeTimeFormat([tag]));
assertEquals(tag, rtf.resolvedOptions().locale);

// Test ListFormat
tag = "zh-Hant-TW"
var lf;
assertDoesNotThrow(() => lf = new Intl.ListFormat(tag));
assertEquals(tag, lf.resolvedOptions().locale);
assertDoesNotThrow(() => lf = new Intl.ListFormat([tag]));
assertEquals(tag, lf.resolvedOptions().locale);

// Test Collator
var col;
assertDoesNotThrow(() => col = new Intl.Collator(tag));
assertEquals(tag, lf.resolvedOptions().locale);
assertDoesNotThrow(() => col = new Intl.Collator([tag]));
assertEquals(tag, lf.resolvedOptions().locale);
