// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils) {

"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var GlobalArrayBuffer = global.ArrayBuffer;
var MakeTypeError;
var MaxSimple;
var MinSimple;

utils.Import(function(from) {
  MakeTypeError = from.MakeTypeError;
  MaxSimple = from.MaxSimple;
  MinSimple = from.MinSimple;
});

// -------------------------------------------------------------------

function ArrayBufferGetByteLen() {
  if (!IS_ARRAYBUFFER(this)) {
    throw MakeTypeError(kIncompatibleMethodReceiver,
                        'ArrayBuffer.prototype.byteLength', this);
  }
  return %_ArrayBufferGetByteLength(this);
}

// ES6 Draft 15.13.5.5.3
function ArrayBufferSlice(start, end) {
  if (!IS_ARRAYBUFFER(this)) {
    throw MakeTypeError(kIncompatibleMethodReceiver,
                        'ArrayBuffer.prototype.slice', this);
  }

  var relativeStart = TO_INTEGER(start);
  if (!IS_UNDEFINED(end)) {
    end = TO_INTEGER(end);
  }
  var first;
  var byte_length = %_ArrayBufferGetByteLength(this);
  if (relativeStart < 0) {
    first = MaxSimple(byte_length + relativeStart, 0);
  } else {
    first = MinSimple(relativeStart, byte_length);
  }
  var relativeEnd = IS_UNDEFINED(end) ? byte_length : end;
  var fin;
  if (relativeEnd < 0) {
    fin = MaxSimple(byte_length + relativeEnd, 0);
  } else {
    fin = MinSimple(relativeEnd, byte_length);
  }

  if (fin < first) {
    fin = first;
  }
  var newLen = fin - first;
  // TODO(dslomov): implement inheritance
  var result = new GlobalArrayBuffer(newLen);

  %ArrayBufferSliceImpl(this, result, first);
  return result;
}

utils.InstallGetter(GlobalArrayBuffer.prototype, "byteLength",
                    ArrayBufferGetByteLen);

utils.InstallFunctions(GlobalArrayBuffer.prototype, DONT_ENUM, [
  "slice", ArrayBufferSlice
]);

})
