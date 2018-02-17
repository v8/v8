// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils) {

"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

// array.js has to come before typedarray.js for this to work
var ArrayToString = utils.ImportNow("ArrayToString");
var GetIterator;
var GetMethod;
var InnerArrayJoin;
var InnerArraySort;
var InnerArrayToLocaleString;
var InternalArray = utils.InternalArray;
var iteratorSymbol = utils.ImportNow("iterator_symbol");

macro TYPED_ARRAYS(FUNCTION)
FUNCTION(Uint8Array, 1)
FUNCTION(Int8Array, 1)
FUNCTION(Uint16Array, 2)
FUNCTION(Int16Array, 2)
FUNCTION(Uint32Array, 4)
FUNCTION(Int32Array, 4)
FUNCTION(Float32Array, 4)
FUNCTION(Float64Array, 8)
FUNCTION(Uint8ClampedArray, 1)
endmacro

macro DECLARE_GLOBALS(NAME, SIZE)
var GlobalNAME = global.NAME;
endmacro

TYPED_ARRAYS(DECLARE_GLOBALS)

macro IS_TYPEDARRAY(arg)
(%_IsTypedArray(arg))
endmacro

var GlobalTypedArray = %object_get_prototype_of(GlobalUint8Array);

utils.Import(function(from) {
  GetIterator = from.GetIterator;
  GetMethod = from.GetMethod;
  InnerArrayJoin = from.InnerArrayJoin;
  InnerArraySort = from.InnerArraySort;
  InnerArrayToLocaleString = from.InnerArrayToLocaleString;
});

// --------------- Typed Arrays ---------------------

// ES6 section 22.2.3.5.1 ValidateTypedArray ( O )
function ValidateTypedArray(array, methodName) {
  if (!IS_TYPEDARRAY(array)) throw %make_type_error(kNotTypedArray);

  if (%_ArrayBufferViewWasNeutered(array))
    throw %make_type_error(kDetachedOperation, methodName);
}

function TypedArrayCreate(constructor, arg0, arg1, arg2) {
  if (IS_UNDEFINED(arg1)) {
    var newTypedArray = new constructor(arg0);
  } else {
    var newTypedArray = new constructor(arg0, arg1, arg2);
  }
  ValidateTypedArray(newTypedArray, "TypedArrayCreate");
  if (IS_NUMBER(arg0) && %_TypedArrayGetLength(newTypedArray) < arg0) {
    throw %make_type_error(kTypedArrayTooShort);
  }
  return newTypedArray;
}

// ES6 draft 05-18-15, section 22.2.3.25
DEFINE_METHOD(
  GlobalTypedArray.prototype,
  sort(comparefn) {
    ValidateTypedArray(this, "%TypedArray%.prototype.sort");

    if (!IS_UNDEFINED(comparefn) && !IS_CALLABLE(comparefn)) {
      throw %make_type_error(kBadSortComparisonFunction, comparefn);
    }

    var length = %_TypedArrayGetLength(this);

    if (IS_UNDEFINED(comparefn)) {
      return %TypedArraySortFast(this);
    }

    return InnerArraySort(this, length, comparefn);
  }
);


// ES6 section 22.2.3.27
DEFINE_METHOD(
  GlobalTypedArray.prototype,
  toLocaleString() {
    ValidateTypedArray(this, "%TypedArray%.prototype.toLocaleString");

    var length = %_TypedArrayGetLength(this);

    return InnerArrayToLocaleString(this, length);
  }
);


// ES6 section 22.2.3.14
DEFINE_METHOD(
  GlobalTypedArray.prototype,
  join(separator) {
    ValidateTypedArray(this, "%TypedArray%.prototype.join");

    var length = %_TypedArrayGetLength(this);

    return InnerArrayJoin(separator, this, length);
  }
);


// ES#sec-iterabletoarraylike Runtime Semantics: IterableToArrayLike( items )
function IterableToArrayLike(items) {
  var iterable = GetMethod(items, iteratorSymbol);
  if (!IS_UNDEFINED(iterable)) {
    var internal_array = new InternalArray();
    var i = 0;
    for (var value of
         { [iteratorSymbol]() { return GetIterator(items, iterable) } }) {
      internal_array[i] = value;
      i++;
    }
    var array = [];
    %MoveArrayContents(internal_array, array);
    return array;
  }
  return TO_OBJECT(items);
}


// ES#sec-%typedarray%.from
// %TypedArray%.from ( source [ , mapfn [ , thisArg ] ] )
DEFINE_METHOD_LEN(
  GlobalTypedArray,
  'from'(source, mapfn, thisArg) {
    if (!%IsConstructor(this)) throw %make_type_error(kNotConstructor, this);
    var mapping;
    if (!IS_UNDEFINED(mapfn)) {
      if (!IS_CALLABLE(mapfn)) throw %make_type_error(kCalledNonCallable, this);
      mapping = true;
    } else {
      mapping = false;
    }
    var arrayLike = IterableToArrayLike(source);
    var length = TO_LENGTH(arrayLike.length);
    var targetObject = TypedArrayCreate(this, length);
    var value, mappedValue;
    for (var i = 0; i < length; i++) {
      value = arrayLike[i];
      if (mapping) {
        mappedValue = %_Call(mapfn, thisArg, value, i);
      } else {
        mappedValue = value;
      }
      targetObject[i] = mappedValue;
    }
    return targetObject;
  },
  1  /* Set function length. */
);

// TODO(bmeurer): Migrate this to a proper builtin.
function TypedArrayConstructor() {
  throw %make_type_error(kConstructAbstractClass, "TypedArray");
}

// -------------------------------------------------------------------

%SetCode(GlobalTypedArray, TypedArrayConstructor);


%AddNamedProperty(GlobalTypedArray.prototype, "toString", ArrayToString,
                  DONT_ENUM);

})
