// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test date format in islamic-umalqura calendar of date prior to
// -195366-07-23
let dateOK = new Date (Date.UTC(-195366, 6, 23));
let dateKO = new Date (Date.UTC(-195366, 6, 22));
let dateDisplay = new Intl.DateTimeFormat (
    'en-GB-u-ca-islamic-umalqura',
    { timeZone : 'UTC', year : 'numeric', month :'long',
      day : 'numeric', weekday : 'long' });
assertEquals("Wed, 23 Jul -195366 00:00:00 GMT",
    dateOK.toUTCString(), "dateOK.toUTCString()");
assertEquals("Tue, 22 Jul -195366 00:00:00 GMT",
    dateKO.toUTCString(), "dateKO.toUTCString()");
assertEquals("Wednesday 17 Dhuʻl-Hijjah -202003",
    dateDisplay.format(dateOK), "dateDisplay.format(dateOK)");
assertEquals("Tuesday 16 Dhuʻl-Hijjah -202003",
    dateDisplay.format(dateKO), "dateDisplay.format(dateKO)");
