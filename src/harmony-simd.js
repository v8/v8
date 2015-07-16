// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var $float32x4ToString;

(function(global, utils) {

"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var GlobalSIMD = global.SIMD;
var GlobalFloat32x4 = GlobalSIMD.Float32x4;

//-------------------------------------------------------------------

function Float32x4Constructor(x, y, z, w) {
  if (%_IsConstructCall()) throw MakeTypeError(kNotConstructor, "Float32x4");
  if (!IS_NUMBER(x) || !IS_NUMBER(y) || !IS_NUMBER(z) || !IS_NUMBER(w)) {
    throw MakeTypeError(kInvalidArgument);
  }
  return %CreateFloat32x4(x, y, z, w);
}

function Float32x4Splat(s) {
  return %CreateFloat32x4(s, s, s, s);
}

function Float32x4CheckJS(a) {
  return %Float32x4Check(a);
}

function Float32x4ToString() {
  if (!(IS_FLOAT32X4(this) || IS_FLOAT32X4_WRAPPER(this))) {
    throw MakeTypeError(kIncompatibleMethodReceiver,
                        "Float32x4.prototype.toString", this);
  }
  var value = %_ValueOf(this);
  var w = GlobalFloat32x4.extractLane(value, 0),
      x = GlobalFloat32x4.extractLane(value, 1),
      y = GlobalFloat32x4.extractLane(value, 2),
      z = GlobalFloat32x4.extractLane(value, 3);
  return "Float32x4(" + w + ", " + x + ", " + y + ", " + z + ")";
}

function Float32x4ValueOf() {
  if (!(IS_FLOAT32X4(this) || IS_FLOAT32X4_WRAPPER(this))) {
    throw MakeTypeError(kIncompatibleMethodReceiver,
                        "Float32x4.prototype.valueOf", this);
  }
  return %_ValueOf(this);
}

//-------------------------------------------------------------------

function Float32x4ExtractLaneJS(value, lane) {
  return %Float32x4ExtractLane(value, lane);
}

// -------------------------------------------------------------------

%AddNamedProperty(GlobalSIMD, symbolToStringTag, 'SIMD', READ_ONLY | DONT_ENUM);
%AddNamedProperty(GlobalSIMD, 'float32x4', GlobalFloat32x4, DONT_ENUM);

%SetCode(GlobalFloat32x4, Float32x4Constructor);
%FunctionSetPrototype(GlobalFloat32x4, {});
%AddNamedProperty(
    GlobalFloat32x4.prototype, 'constructor', GlobalFloat32x4, DONT_ENUM);
%AddNamedProperty(
    GlobalFloat32x4, symbolToStringTag, 'Float32x4', DONT_ENUM | READ_ONLY);

utils.InstallFunctions(GlobalFloat32x4.prototype, DONT_ENUM, [
  'valueOf', Float32x4ValueOf,
  'toString', Float32x4ToString,
]);

utils.InstallFunctions(GlobalFloat32x4, DONT_ENUM, [
  'splat', Float32x4Splat,
  'check', Float32x4CheckJS,
  'extractLane', Float32x4ExtractLaneJS,
]);

$float32x4ToString = Float32x4ToString;

})
