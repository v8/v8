// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

// This file relies on the fact that the following declaration has been made
// in runtime.js:
// var $Array = global.Array;

// -------------------------------------------------------------------

// ES6 draft 07-15-13, section 15.4.3.23
function ArrayFind(predicate /* thisArg */) {  // length == 1
  CHECK_OBJECT_COERCIBLE(this, "Array.prototype.find");

  var array = ToObject(this);
  var length = ToInteger(array.length);

  if (!IS_SPEC_FUNCTION(predicate)) {
    throw MakeTypeError('called_non_callable', [predicate]);
  }

  var thisArg;
  if (%_ArgumentsLength() > 1) {
    thisArg = %_Arguments(1);
  }

  if (IS_NULL_OR_UNDEFINED(thisArg)) {
    thisArg = %GetDefaultReceiver(predicate) || thisArg;
  } else if (!IS_SPEC_OBJECT(thisArg) && %IsSloppyModeFunction(predicate)) {
    thisArg = ToObject(thisArg);
  }

  for (var i = 0; i < length; i++) {
    if (i in array) {
      var element = array[i];
      if (%_CallFunction(thisArg, element, i, array, predicate)) {
        return element;
      }
    }
  }

  return;
}


// ES6 draft 07-15-13, section 15.4.3.24
function ArrayFindIndex(predicate /* thisArg */) {  // length == 1
  CHECK_OBJECT_COERCIBLE(this, "Array.prototype.findIndex");

  var array = ToObject(this);
  var length = ToInteger(array.length);

  if (!IS_SPEC_FUNCTION(predicate)) {
    throw MakeTypeError('called_non_callable', [predicate]);
  }

  var thisArg;
  if (%_ArgumentsLength() > 1) {
    thisArg = %_Arguments(1);
  }

  if (IS_NULL_OR_UNDEFINED(thisArg)) {
    thisArg = %GetDefaultReceiver(predicate) || thisArg;
  } else if (!IS_SPEC_OBJECT(thisArg) && %IsSloppyModeFunction(predicate)) {
    thisArg = ToObject(thisArg);
  }

  for (var i = 0; i < length; i++) {
    if (i in array) {
      var element = array[i];
      if (%_CallFunction(thisArg, element, i, array, predicate)) {
        return i;
      }
    }
  }

  return -1;
}


// -------------------------------------------------------------------

function HarmonyArrayExtendArrayPrototype() {
  %CheckIsBootstrapping();

  // Set up the non-enumerable functions on the Array prototype object.
  InstallFunctions($Array.prototype, DONT_ENUM, $Array(
    "find", ArrayFind,
    "findIndex", ArrayFindIndex
  ));
}

HarmonyArrayExtendArrayPrototype();
