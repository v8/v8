// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test date format in  Islamic and Islamic-rgsa calendars of dates prior
// to 0622-07-18
let dateOK = new Date (Date.UTC(622, 6, 18));
let dateKO = new Date (Date.UTC(622, 6, 17));
let dateDisplay = new Intl.DateTimeFormat (
    'en-GB-u-ca-islamic',
    { timeZone : 'UTC', year : 'numeric', month :'long',
      day : 'numeric', weekday : 'long' });
let dateDisplay2 = new Intl.DateTimeFormat (
    'en-GB-u-ca-islamic-rgsa',
    { timeZone : 'UTC', year : 'numeric', month :'long',
      day : 'numeric', weekday : 'long' });
assertEquals("Thu, 18 Jul 0622 00:00:00 GMT",
    dateOK.toUTCString(), "dateOK.toUTCString()");
assertEquals("Wed, 17 Jul 0622 00:00:00 GMT",
    dateKO.toUTCString(), "dateKO.toUTCString()");
assertEquals("Thursday, Muharram 1, 1 AH",
    dateDisplay.format(dateOK), "dateDisplay.format(dateOK)");
assertEquals("Wednesday, Dhuʻl-Hijjah 30, 0 AH",
    dateDisplay.format(dateKO), "dateDisplay.format(dateKO)");
assertEquals("Thursday 1 Muharram 1",
    dateDisplay2.format(dateOK), "dateDisplay.format(dateOK)");
assertEquals("Wednesday 30 Dhuʻl-Hijjah 0",
    dateDisplay2.format(dateKO), "dateDisplay.format(dateKO)");
