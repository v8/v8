// Copyright 2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils) {

"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var GlobalDate = global.Date;
var GlobalJSON = global.JSON;
var GlobalSet = global.Set;
var InternalArray = utils.InternalArray;
var toStringTagSymbol = utils.ImportNow("to_string_tag_symbol");

// -------------------------------------------------------------------

function JSONParse(text, reviver) {
  return %ParseJson(text, reviver);
}

// -------------------------------------------------------------------

%AddNamedProperty(GlobalJSON, toStringTagSymbol, "JSON", READ_ONLY | DONT_ENUM);

// Set up non-enumerable properties of the JSON object.
utils.InstallFunctions(GlobalJSON, DONT_ENUM, [
  "parse", JSONParse,
]);

// -------------------------------------------------------------------
// Date.toJSON

// 20.3.4.37 Date.prototype.toJSON ( key )
function DateToJSON(key) {
  var o = TO_OBJECT(this);
  var tv = TO_PRIMITIVE_NUMBER(o);
  if (IS_NUMBER(tv) && !NUMBER_IS_FINITE(tv)) {
    return null;
  }
  return o.toISOString();
}

// Set up non-enumerable functions of the Date prototype object.
utils.InstallFunctions(GlobalDate.prototype, DONT_ENUM, [
  "toJSON", DateToJSON
]);

})
