// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils) {

"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var GlobalSIMD = global.SIMD;

macro SIMD_TYPES(FUNCTION)
FUNCTION(Float32x4, float32x4, 4)
FUNCTION(Int32x4, int32x4, 4)
FUNCTION(Bool32x4, bool32x4, 4)
FUNCTION(Int16x8, int16x8, 8)
FUNCTION(Bool16x8, bool16x8, 8)
FUNCTION(Int8x16, int8x16, 16)
FUNCTION(Bool8x16, bool8x16, 16)
endmacro

macro DECLARE_GLOBALS(NAME, TYPE, LANES)
var GlobalNAME = GlobalSIMD.NAME;
endmacro

SIMD_TYPES(DECLARE_GLOBALS)

macro DECLARE_COMMON_FUNCTIONS(NAME, TYPE, LANES)
function NAMECheckJS(a) {
  return %NAMECheck(a);
}

function NAMEToString() {
  if (typeof(this) !== 'TYPE' && %_ClassOf(this) !== 'NAME') {
    throw MakeTypeError(kIncompatibleMethodReceiver,
                        "NAME.prototype.toString", this);
  }
  var value = %_ValueOf(this);
  var str = "SIMD.NAME(";
  str += %NAMEExtractLane(value, 0);
  for (var i = 1; i < LANES; i++) {
    str += ", " + %NAMEExtractLane(value, i);
  }
  return str + ")";
}

function NAMEToLocaleString() {
  if (typeof(this) !== 'TYPE' && %_ClassOf(this) !== 'NAME') {
    throw MakeTypeError(kIncompatibleMethodReceiver,
                        "NAME.prototype.toLocaleString", this);
  }
  var value = %_ValueOf(this);
  var str = "SIMD.NAME(";
  str += %NAMEExtractLane(value, 0).toLocaleString();
  for (var i = 1; i < LANES; i++) {
    str += ", " + %NAMEExtractLane(value, i).toLocaleString();
  }
  return str + ")";
}

function NAMEValueOf() {
  if (typeof(this) !== 'TYPE' && %_ClassOf(this) !== 'NAME') {
    throw MakeTypeError(kIncompatibleMethodReceiver,
                        "NAME.prototype.valueOf", this);
  }
  return %_ValueOf(this);
}

function NAMEExtractLaneJS(instance, lane) {
  return %NAMEExtractLane(instance, lane);
}
endmacro

SIMD_TYPES(DECLARE_COMMON_FUNCTIONS)

macro SIMD_NUMERIC_TYPES(FUNCTION)
FUNCTION(Float32x4)
FUNCTION(Int32x4)
FUNCTION(Int16x8)
FUNCTION(Int8x16)
endmacro

macro DECLARE_NUMERIC_FUNCTIONS(NAME)
function NAMEReplaceLaneJS(instance, lane, value) {
  return %NAMEReplaceLane(instance, lane, TO_NUMBER_INLINE(value));
}
endmacro

SIMD_NUMERIC_TYPES(DECLARE_NUMERIC_FUNCTIONS)

macro SIMD_BOOL_TYPES(FUNCTION)
FUNCTION(Bool32x4)
FUNCTION(Bool16x8)
FUNCTION(Bool8x16)
endmacro

macro DECLARE_BOOL_FUNCTIONS(NAME)
function NAMEReplaceLaneJS(instance, lane, value) {
  return %NAMEReplaceLane(instance, lane, value);
}
endmacro

SIMD_BOOL_TYPES(DECLARE_BOOL_FUNCTIONS)

macro SIMD_UNSIGNED_INT_TYPES(FUNCTION)
FUNCTION(Int16x8)
FUNCTION(Int8x16)
endmacro

macro DECLARE_UNSIGNED_INT_FUNCTIONS(NAME)
function NAMEUnsignedExtractLaneJS(instance, lane) {
  return %NAMEUnsignedExtractLane(instance, lane);
}
endmacro

SIMD_UNSIGNED_INT_TYPES(DECLARE_UNSIGNED_INT_FUNCTIONS)

//-------------------------------------------------------------------

function Float32x4Constructor(c0, c1, c2, c3) {
  if (%_IsConstructCall()) throw MakeTypeError(kNotConstructor, "Float32x4");
  return %CreateFloat32x4(TO_NUMBER_INLINE(c0), TO_NUMBER_INLINE(c1),
                          TO_NUMBER_INLINE(c2), TO_NUMBER_INLINE(c3));
}


function Float32x4Splat(s) {
  return %CreateFloat32x4(s, s, s, s);
}


function Int32x4Constructor(c0, c1, c2, c3) {
  if (%_IsConstructCall()) throw MakeTypeError(kNotConstructor, "Int32x4");
  return %CreateInt32x4(TO_NUMBER_INLINE(c0), TO_NUMBER_INLINE(c1),
                        TO_NUMBER_INLINE(c2), TO_NUMBER_INLINE(c3));
}


function Int32x4Splat(s) {
  return %CreateInt32x4(s, s, s, s);
}


function Bool32x4Constructor(c0, c1, c2, c3) {
  if (%_IsConstructCall()) throw MakeTypeError(kNotConstructor, "Bool32x4");
  return %CreateBool32x4(c0, c1, c2, c3);
}


function Bool32x4Splat(s) {
  return %CreateBool32x4(s, s, s, s);
}


function Int16x8Constructor(c0, c1, c2, c3, c4, c5, c6, c7) {
  if (%_IsConstructCall()) throw MakeTypeError(kNotConstructor, "Int16x8");
  return %CreateInt16x8(TO_NUMBER_INLINE(c0), TO_NUMBER_INLINE(c1),
                        TO_NUMBER_INLINE(c2), TO_NUMBER_INLINE(c3),
                        TO_NUMBER_INLINE(c4), TO_NUMBER_INLINE(c5),
                        TO_NUMBER_INLINE(c6), TO_NUMBER_INLINE(c7));
}


function Int16x8Splat(s) {
  return %CreateInt16x8(s, s, s, s, s, s, s, s);
}


function Bool16x8Constructor(c0, c1, c2, c3, c4, c5, c6, c7) {
  if (%_IsConstructCall()) throw MakeTypeError(kNotConstructor, "Bool16x8");
  return %CreateBool16x8(c0, c1, c2, c3, c4, c5, c6, c7);
}


function Bool16x8Splat(s) {
  return %CreateBool16x8(s, s, s, s, s, s, s, s);
}


function Int8x16Constructor(c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11,
                            c12, c13, c14, c15) {
  if (%_IsConstructCall()) throw MakeTypeError(kNotConstructor, "Int8x16");
  return %CreateInt8x16(TO_NUMBER_INLINE(c0), TO_NUMBER_INLINE(c1),
                        TO_NUMBER_INLINE(c2), TO_NUMBER_INLINE(c3),
                        TO_NUMBER_INLINE(c4), TO_NUMBER_INLINE(c5),
                        TO_NUMBER_INLINE(c6), TO_NUMBER_INLINE(c7),
                        TO_NUMBER_INLINE(c8), TO_NUMBER_INLINE(c9),
                        TO_NUMBER_INLINE(c10), TO_NUMBER_INLINE(c11),
                        TO_NUMBER_INLINE(c12), TO_NUMBER_INLINE(c13),
                        TO_NUMBER_INLINE(c14), TO_NUMBER_INLINE(c15));
}


function Int8x16Splat(s) {
  return %CreateInt8x16(s, s, s, s, s, s, s, s, s, s, s, s, s, s, s, s);
}


function Bool8x16Constructor(c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11,
                             c12, c13, c14, c15) {
  if (%_IsConstructCall()) throw MakeTypeError(kNotConstructor, "Bool8x16");
  return %CreateBool8x16(c0, c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12,
                         c13, c14, c15);
}


function Bool8x16Splat(s) {
  return %CreateBool8x16(s, s, s, s, s, s, s, s, s, s, s, s, s, s, s, s);
}


%AddNamedProperty(GlobalSIMD, symbolToStringTag, 'SIMD', READ_ONLY | DONT_ENUM);

macro SETUP_SIMD_TYPE(NAME, TYPE, LANES)
%SetCode(GlobalNAME, NAMEConstructor);
%FunctionSetPrototype(GlobalNAME, {});
%AddNamedProperty(GlobalNAME.prototype, 'constructor', GlobalNAME,
    DONT_ENUM);
%AddNamedProperty(GlobalNAME.prototype, symbolToStringTag, 'NAME',
    DONT_ENUM | READ_ONLY);
utils.InstallFunctions(GlobalNAME.prototype, DONT_ENUM, [
  'toLocaleString', NAMEToLocaleString,
  'toString', NAMEToString,
  'valueOf', NAMEValueOf,
]);
endmacro

SIMD_TYPES(SETUP_SIMD_TYPE)

//-------------------------------------------------------------------

utils.InstallFunctions(GlobalFloat32x4, DONT_ENUM, [
  'splat', Float32x4Splat,
  'check', Float32x4CheckJS,
  'extractLane', Float32x4ExtractLaneJS,
  'replaceLane', Float32x4ReplaceLaneJS,
]);

utils.InstallFunctions(GlobalInt32x4, DONT_ENUM, [
  'splat', Int32x4Splat,
  'check', Int32x4CheckJS,
  'extractLane', Int32x4ExtractLaneJS,
  'replaceLane', Int32x4ReplaceLaneJS,
]);

utils.InstallFunctions(GlobalBool32x4, DONT_ENUM, [
  'splat', Bool32x4Splat,
  'check', Bool32x4CheckJS,
  'extractLane', Bool32x4ExtractLaneJS,
  'replaceLane', Bool32x4ReplaceLaneJS,
]);

utils.InstallFunctions(GlobalInt16x8, DONT_ENUM, [
  'splat', Int16x8Splat,
  'check', Int16x8CheckJS,
  'extractLane', Int16x8ExtractLaneJS,
  'unsignedExtractLane', Int16x8UnsignedExtractLaneJS,
  'replaceLane', Int16x8ReplaceLaneJS,
]);

utils.InstallFunctions(GlobalBool16x8, DONT_ENUM, [
  'splat', Bool16x8Splat,
  'check', Bool16x8CheckJS,
  'extractLane', Bool16x8ExtractLaneJS,
  'replaceLane', Bool16x8ReplaceLaneJS,
]);

utils.InstallFunctions(GlobalInt8x16, DONT_ENUM, [
  'splat', Int8x16Splat,
  'check', Int8x16CheckJS,
  'extractLane', Int8x16ExtractLaneJS,
  'unsignedExtractLane', Int8x16UnsignedExtractLaneJS,
  'replaceLane', Int8x16ReplaceLaneJS,
]);

utils.InstallFunctions(GlobalBool8x16, DONT_ENUM, [
  'splat', Bool8x16Splat,
  'check', Bool8x16CheckJS,
  'extractLane', Bool8x16ExtractLaneJS,
  'replaceLane', Bool8x16ReplaceLaneJS,
]);

})
