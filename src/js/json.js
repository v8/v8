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

function CreateDataProperty(o, p, v) {
  var desc = {value: v, enumerable: true, writable: true, configurable: true};
  return %reflect_define_property(o, p, desc);
}


function InternalizeJSONProperty(holder, name, reviver) {
  var val = holder[name];
  if (IS_RECEIVER(val)) {
    if (%is_arraylike(val)) {
      var length = TO_LENGTH(val.length);
      for (var i = 0; i < length; i++) {
        var newElement =
            InternalizeJSONProperty(val, %_NumberToString(i), reviver);
        if (IS_UNDEFINED(newElement)) {
          %reflect_delete_property(val, i);
        } else {
          CreateDataProperty(val, i, newElement);
        }
      }
    } else {
      var keys = %object_keys(val);
      for (var i = 0; i < keys.length; i++) {
        var p = keys[i];
        var newElement = InternalizeJSONProperty(val, p, reviver);
        if (IS_UNDEFINED(newElement)) {
          %reflect_delete_property(val, p);
        } else {
          CreateDataProperty(val, p, newElement);
        }
      }
    }
  }
  return %_Call(reviver, holder, name, val);
}


function JSONParse(text, reviver) {
  var unfiltered = %ParseJson(text);
  if (IS_CALLABLE(reviver)) {
    return InternalizeJSONProperty({'': unfiltered}, '', reviver);
  } else {
    return unfiltered;
  }
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
