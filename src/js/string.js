// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils) {

"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var GlobalString = global.String;

// -------------------------------------------------------------------
// String methods related to templates

// Set up the non-enumerable functions on the String object.
DEFINE_METHOD(
  GlobalString,

  /* ES#sec-string.raw */
  raw(callSite) {
    var numberOfSubstitutions = arguments.length;
    var cooked = TO_OBJECT(callSite);
    var raw = TO_OBJECT(cooked.raw);
    var literalSegments = TO_LENGTH(raw.length);
    if (literalSegments <= 0) return "";

    var result = TO_STRING(raw[0]);

    for (var i = 1; i < literalSegments; ++i) {
      if (i < numberOfSubstitutions) {
        result += TO_STRING(arguments[i]);
      }
      result += TO_STRING(raw[i]);
    }

    return result;
  }
);

// -------------------------------------------------------------------

})
