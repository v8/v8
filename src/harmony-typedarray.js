// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function() {

"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------

macro TYPED_ARRAYS(FUNCTION)
// arrayIds below should be synchronized with Runtime_TypedArrayInitialize.
FUNCTION(1, Uint8Array, 1)
FUNCTION(2, Int8Array, 1)
FUNCTION(3, Uint16Array, 2)
FUNCTION(4, Int16Array, 2)
FUNCTION(5, Uint32Array, 4)
FUNCTION(6, Int32Array, 4)
FUNCTION(7, Float32Array, 4)
FUNCTION(8, Float64Array, 8)
FUNCTION(9, Uint8ClampedArray, 1)
endmacro


macro TYPED_ARRAY_HARMONY_ADDITIONS(ARRAY_ID, NAME, ELEMENT_SIZE)

// ES6 draft 08-24-14, section 22.2.3.12
function NAMEForEach(f /* thisArg */) {  // length == 1
  if (!%IsTypedArray(this)) throw MakeTypeError(kNotTypedArray);
  if (!IS_SPEC_FUNCTION(f)) throw MakeTypeError(kCalledNonCallable, f);

  var length = %_TypedArrayGetLength(this);
  var receiver;

  if (%_ArgumentsLength() > 1) {
    receiver = %_Arguments(1);
  }

  var needs_wrapper = false;
  if (IS_NULL_OR_UNDEFINED(receiver)) {
    receiver = %GetDefaultReceiver(f) || receiver;
  } else {
    needs_wrapper = SHOULD_CREATE_WRAPPER(f, receiver);
  }

  var stepping = DEBUG_IS_ACTIVE && %DebugCallbackSupportsStepping(f);
  for (var i = 0; i < length; i++) {
    var element = this[i];
    // Prepare break slots for debugger step in.
    if (stepping) %DebugPrepareStepInIfStepping(f);
    var new_receiver = needs_wrapper ? ToObject(receiver) : receiver;
    %_CallFunction(new_receiver, TO_OBJECT_INLINE(element), i, this, f);
  }
}

// ES6 draft 08-24-14, section 22.2.2.2
function NAMEOf() {  // length == 0
  var length = %_ArgumentsLength();
  var array = new this(length);
  for (var i = 0; i < length; i++) {
    array[i] = %_Arguments(i);
  }
  return array;
}

endmacro

TYPED_ARRAYS(TYPED_ARRAY_HARMONY_ADDITIONS)


macro EXTEND_TYPED_ARRAY(ARRAY_ID, NAME, ELEMENT_SIZE)
  // Set up non-enumerable functions on the object.
  InstallFunctions(global.NAME, DONT_ENUM | DONT_DELETE | READ_ONLY, [
    "of", NAMEOf
  ]);

  // Set up non-enumerable functions on the prototype object.
  InstallFunctions(global.NAME.prototype, DONT_ENUM, [
    "forEach", NAMEForEach
  ]);
endmacro

TYPED_ARRAYS(EXTEND_TYPED_ARRAY)

})();
