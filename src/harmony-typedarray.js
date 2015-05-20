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
DECLARE_GLOBALS(Array)

// -------------------------------------------------------------------

function TypedArrayCopyWithin(target, start, end) {
  if (!%IsTypedArray(this)) throw MakeTypeError(kNotTypedArray);

  var length = %_TypedArrayGetLength(this);

  // TODO(littledan): Replace with a memcpy for better performance
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

// ES6 draft 04-05-14 section 22.2.3.8
function TypedArrayFill(value, start , end) {
  if (!%IsTypedArray(this)) throw MakeTypeError(kNotTypedArray);

  var length = %_TypedArrayGetLength(this);

  return $innerArrayFill(value, start, end, this, length);
}
%FunctionSetLength(TypedArrayFill, 1);

// ES6 draft 07-15-13, section 22.2.3.10
function TypedArrayFind(predicate, thisArg) {
  if (!%IsTypedArray(this)) throw MakeTypeError(kNotTypedArray);

  var length = %_TypedArrayGetLength(this);

  return $innerArrayFind(predicate, thisArg, this, length);
}
%FunctionSetLength(TypedArrayFind, 1);

// ES6 draft 07-15-13, section 22.2.3.11
function TypedArrayFindIndex(predicate, thisArg) {
  if (!%IsTypedArray(this)) throw MakeTypeError(kNotTypedArray);

  var length = %_TypedArrayGetLength(this);

  return $innerArrayFindIndex(predicate, thisArg, this, length);
}
%FunctionSetLength(TypedArrayFindIndex, 1);

// ES6 draft 05-18-15, section 22.2.3.21
function TypedArrayReverse() {
  if (!%IsTypedArray(this)) throw MakeTypeError(kNotTypedArray);

  var length = %_TypedArrayGetLength(this);

  return $innerArrayReverse(this, length);
}


function TypedArrayComparefn(x, y) {
  if ($isNaN(x) && $isNaN(y)) {
    return $isNaN(y) ? 0 : 1;
  }
  if ($isNaN(x)) {
    return 1;
  }
  if (x === 0 && x === y) {
    if (%_IsMinusZero(x)) {
      if (!%_IsMinusZero(y)) {
        return -1;
      }
    } else if (%_IsMinusZero(y)) {
      return 1;
    }
  }
  return x - y;
}


// ES6 draft 05-18-15, section 22.2.3.25
function TypedArraySort(comparefn) {
  if (!%IsTypedArray(this)) throw MakeTypeError(kNotTypedArray);

  var length = %_TypedArrayGetLength(this);

  if (IS_UNDEFINED(comparefn)) {
    comparefn = TypedArrayComparefn;
  }

  return %_CallFunction(this, length, comparefn, $innerArraySort);
}


// ES6 section 22.2.3.13
function TypedArrayIndexOf(element, index) {
  if (!%IsTypedArray(this)) throw MakeTypeError(kNotTypedArray);

  var length = %_TypedArrayGetLength(this);

  return %_CallFunction(this, element, index, length, $innerArrayIndexOf);
}
%FunctionSetLength(TypedArrayIndexOf, 1);


// ES6 section 22.2.3.16
function TypedArrayLastIndexOf(element, index) {
  if (!%IsTypedArray(this)) throw MakeTypeError(kNotTypedArray);

  var length = %_TypedArrayGetLength(this);

  return %_CallFunction(this, element, index, length,
                        %_ArgumentsLength(), $innerArrayLastIndexOf);
}
%FunctionSetLength(TypedArrayLastIndexOf, 1);


// ES6 draft 08-24-14, section 22.2.2.2
function TypedArrayOf() {
  var length = %_ArgumentsLength();
  var array = new this(length);
  for (var i = 0; i < length; i++) {
    array[i] = %_Arguments(i);
  }
  return array;
}

function ConstructTypedArray(constructor, array) {
  // TODO(littledan): This is an approximation of the spec, which requires
  // that only real TypedArray classes should be accepted (22.2.2.1.1)
  if (!IS_SPEC_OBJECT(constructor) || IS_UNDEFINED(constructor.prototype) ||
      !%HasOwnProperty(constructor.prototype, "BYTES_PER_ELEMENT")) {
    throw MakeTypeError(kNotTypedArray);
  }

  // TODO(littledan): The spec requires that, rather than directly calling
  // the constructor, a TypedArray is created with the proper proto and
  // underlying size and element size, and elements are put in one by one.
  // By contrast, this would allow subclasses to make a radically different
  // constructor with different semantics.
  return new constructor(array);
}

function TypedArrayFrom(source, mapfn, thisArg) {
  // TODO(littledan): Investigate if there is a receiver which could be
  // faster to accumulate on than Array, e.g., a TypedVector.
  var array = %_CallFunction(GlobalArray, source, mapfn, thisArg, $arrayFrom);
  return ConstructTypedArray(this, array);
}
%FunctionSetLength(TypedArrayFrom, 1);

// TODO(littledan): Fix the TypedArray proto chain (bug v8:4085).
macro EXTEND_TYPED_ARRAY(NAME)
  // Set up non-enumerable functions on the object.
  $installFunctions(GlobalNAME, DONT_ENUM | DONT_DELETE | READ_ONLY, [
    "from", TypedArrayFrom,
    "of", TypedArrayOf
  ]);

  // Set up non-enumerable functions on the prototype object.
  $installFunctions(GlobalNAME.prototype, DONT_ENUM, [
    "copyWithin", TypedArrayCopyWithin,
    "every", TypedArrayEvery,
    "forEach", TypedArrayForEach,
    "find", TypedArrayFind,
    "findIndex", TypedArrayFindIndex,
    "fill", TypedArrayFill,
    "indexOf", TypedArrayIndexOf,
    "lastIndexOf", TypedArrayLastIndexOf,
    "reverse", TypedArrayReverse,
    "sort", TypedArraySort
  ]);
endmacro

TYPED_ARRAYS(EXTEND_TYPED_ARRAY)

})
