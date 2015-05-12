// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, exports) {

"use strict";

%CheckIsBootstrapping();

macro TYPED_ARRAYS(FUNCTION)
// arrayIds below should be synchronized with Runtime_TypedArrayInitialize.
FUNCTION(Uint8Array)
FUNCTION(Int8Array)
FUNCTION(Uint16Array)
FUNCTION(Int16Array)
FUNCTION(Uint32Array)
FUNCTION(Int32Array)
FUNCTION(Float32Array)
FUNCTION(Float64Array)
FUNCTION(Uint8ClampedArray)
endmacro

macro DECLARE_GLOBALS(NAME)
var GlobalNAME = global.NAME;
endmacro

TYPED_ARRAYS(DECLARE_GLOBALS)

// -------------------------------------------------------------------

function TypedArrayCopyWithin(target, start, end) {
  if (!%IsTypedArray(this)) throw MakeTypeError(kNotTypedArray);

  var length = %_TypedArrayGetLength(this);

  // TODO(dehrenberg): Replace with a memcpy for better performance
  return $innerArrayCopyWithin(target, start, end, this, length);
}
%FunctionSetLength(TypedArrayCopyWithin, 2);

// ES6 draft 05-05-15, section 22.2.3.7
function TypedArrayEvery(f, receiver) {
  if (!%IsTypedArray(this)) throw MakeTypeError(kNotTypedArray);

  var length = %_TypedArrayGetLength(this);

  return $innerArrayEvery(f, receiver, this, length);
}
%FunctionSetLength(TypedArrayEvery, 1);

// ES6 draft 08-24-14, section 22.2.3.12
function TypedArrayForEach(f, receiver) {
  if (!%IsTypedArray(this)) throw MakeTypeError(kNotTypedArray);

  var length = %_TypedArrayGetLength(this);

  $innerArrayForEach(f, receiver, this, length);
}
%FunctionSetLength(TypedArrayForEach, 1);

// ES6 draft 08-24-14, section 22.2.2.2
function TypedArrayOf() {
  var length = %_ArgumentsLength();
  var array = new this(length);
  for (var i = 0; i < length; i++) {
    array[i] = %_Arguments(i);
  }
  return array;
}

macro EXTEND_TYPED_ARRAY(NAME)
  // Set up non-enumerable functions on the object.
  $installFunctions(GlobalNAME, DONT_ENUM | DONT_DELETE | READ_ONLY, [
    "of", TypedArrayOf
  ]);

  // Set up non-enumerable functions on the prototype object.
  $installFunctions(GlobalNAME.prototype, DONT_ENUM, [
    "copyWithin", TypedArrayCopyWithin,
    "every", TypedArrayEvery,
    "forEach", TypedArrayForEach
  ]);
endmacro

TYPED_ARRAYS(EXTEND_TYPED_ARRAY)

})
