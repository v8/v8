// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

function GetTemplateCallSite(siteObj, rawStrings) {
  // TODO(caitp): ensure same template callsite is used for subsequent tag calls

  %AddNamedProperty(siteObj, "raw", %ObjectFreeze(rawStrings),
      READ_ONLY | DONT_ENUM | DONT_DELETE);

  return %ObjectFreeze(siteObj);
}


// ES6 Draft 10-14-2014, section 21.1.2.4
function StringRaw(callSite) {
  // TODO(caitp): Use rest parameters when implemented
  var numberOfSubstitutions = %_ArgumentsLength();
  var cooked = ToObject(callSite);
  var raw = ToObject(cooked.raw);
  var literalSegments = ToLength(raw.length);
  if (literalSegments <= 0) return "";

  var result = ToString(raw[0]);

  for (var i = 1; i < literalSegments; ++i) {
    if (i < numberOfSubstitutions) {
      result += ToString(%_Arguments(i));
    }
    result += ToString(raw[i]);
  }

  return result;
}


function ExtendStringForTemplates() {
  %CheckIsBootstrapping();

  // Set up the non-enumerable functions on the String object.
  InstallFunctions($String, DONT_ENUM, $Array(
    "raw", StringRaw
  ));
}

ExtendStringForTemplates();
