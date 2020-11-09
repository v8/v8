// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test date format in Indian calendar of date prior to 0001-01-01
let dateOK = new Date (Date.UTC(0, 0, 1));
dateOK.setFullYear(1);
let dateKO = new Date (Date.UTC(0, 11, 31));
dateKO.setFullYear(0);
let dateDisplay = new Intl.DateTimeFormat (
    'en-GB-u-ca-indian',
    { timeZone : 'UTC', year : 'numeric', month :'long',
      day : 'numeric', weekday : 'long' });
assertEquals("Mon, 31 Dec 0001 23:52:58 GMT",
    dateOK.toUTCString(), "dateOK.toUTCString()");
assertEquals("Sat, 30 Dec 0000 23:52:58 GMT",
    dateKO.toUTCString(), "dateKO.toUTCString()");
assertEquals("Monday 10 Pausa -77",
    dateDisplay.format(dateOK), "dateDisplay.format(dateOK)");
assertEquals("Saturday 9 Pausa -78",
    dateDisplay.format(dateKO), "dateDisplay.format(dateKO)");
