// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


// This file relies on the fact that the following declarations have been made
// in runtime.js:
// const $Object = global.Object;
// const $Boolean = global.Boolean;
// const $Number = global.Number;
// const $Function = global.Function;
// const $Array = global.Array;
// const $NaN = 0/0;


// ECMA 262 - 15.1.1.1.
%AddProperty(global, "NaN", $NaN, DONT_ENUM | DONT_DELETE);


// ECMA-262 - 15.1.1.2.
%AddProperty(global, "Infinity", 1/0, DONT_ENUM | DONT_DELETE);


// ECMA-262 - 15.1.1.3.
%AddProperty(global, "undefined", void 0, DONT_ENUM | DONT_DELETE);


// ECMA 262 - 15.1.4
function $isNaN(number) {
  var n = ToNumber(number);
  return NUMBER_IS_NAN(n);
};
%AddProperty(global, "isNaN", $isNaN, DONT_ENUM);


// ECMA 262 - 15.1.5
function $isFinite(number) {
  return %NumberIsFinite(ToNumber(number));
};
%AddProperty(global, "isFinite", $isFinite, DONT_ENUM);


// ECMA-262 - 15.1.2.2
%AddProperty(global, "parseInt", function(string, radix) {
  if (radix === void 0) {
    radix = 0;
    // Some people use parseInt instead of Math.floor.  This
    // optimization makes parseInt on a Smi 12 times faster (60ns
    // vs 800ns).  The following optimization makes parseInt on a
    // non-Smi number 9 times faster (230ns vs 2070ns).  Together
    // they make parseInt on a string 1.4% slower (274ns vs 270ns).
    if (%_IsSmi(string)) return string;
    if (IS_NUMBER(string)) {
      if (string >= 0.01 && string < 1e9)
        return $Math_floor(string);
      if (string <= -0.01 && string > -1e9)
        return - $Math_floor(-string);
    }
  } else {
    radix = TO_INT32(radix);
    if (!(radix == 0 || (2 <= radix && radix <= 36)))
      return $NaN;
  }
  return %StringParseInt(ToString(string), radix);
}, DONT_ENUM);


// ECMA-262 - 15.1.2.3
%AddProperty(global, "parseFloat", function(string) {
  return %StringParseFloat(ToString(string));
}, DONT_ENUM);


// ----------------------------------------------------------------------------
// Boolean (first part of definition)


%SetCode($Boolean, function(x) {
  if (%IsConstructCall()) {
    %_SetValueOf(this, ToBoolean(x));
  } else {
    return ToBoolean(x);
  }
});

%FunctionSetPrototype($Boolean, new $Boolean(false));

%AddProperty($Boolean.prototype, "constructor", $Boolean, DONT_ENUM);

// ----------------------------------------------------------------------------
// Object

$Object.prototype.constructor = $Object;

%AddProperty($Object.prototype, "toString", function() {
  var c = %ClassOf(this);
  // Hide Arguments from the outside.
  if (c === 'Arguments') c  = 'Object';
  return "[object " + c + "]";
}, DONT_ENUM);


// ECMA-262, section 15.2.4.3, page 84.
%AddProperty($Object.prototype, "toLocaleString", function() {
  return this.toString();
}, DONT_ENUM);


// ECMA-262, section 15.2.4.4, page 85.
%AddProperty($Object.prototype, "valueOf", function() {
  return this;
}, DONT_ENUM);


// ECMA-262, section 15.2.4.5, page 85.
%AddProperty($Object.prototype, "hasOwnProperty", function(V) {
  return %HasLocalProperty(ToObject(this), ToString(V));
}, DONT_ENUM);


// ECMA-262, section 15.2.4.6, page 85.
%AddProperty($Object.prototype, "isPrototypeOf", function(V) {
  if (!IS_OBJECT(V) && !IS_FUNCTION(V)) return false;
  return %IsInPrototypeChain(this, V);
}, DONT_ENUM);


// ECMA-262, section 15.2.4.6, page 85.
%AddProperty($Object.prototype, "propertyIsEnumerable", function(V) {
  if (this == null) return false;
  if (!IS_OBJECT(this) && !IS_FUNCTION(this)) return false;
  return %IsPropertyEnumerable(this, ToString(V));
}, DONT_ENUM);


// Extensions for providing property getters and setters.
%AddProperty($Object.prototype, "__defineGetter__", function(name, fun) {
  if (this == null) throw new $TypeError('Object.prototype.__defineGetter__: this is Null');
  if (!IS_FUNCTION(fun)) throw new $TypeError('Object.prototype.__defineGetter__: Expecting function');
  return %DefineAccessor(ToObject(this), ToString(name), GETTER, fun);
}, DONT_ENUM);



%AddProperty($Object.prototype, "__lookupGetter__", function(name) {
  if (this == null) throw new $TypeError('Object.prototype.__lookupGetter__: this is Null');
  return %LookupAccessor(ToObject(this), ToString(name), GETTER);
}, DONT_ENUM);


%AddProperty($Object.prototype, "__defineSetter__", function(name, fun) {
  if (this == null) throw new $TypeError('Object.prototype.__defineSetter__: this is Null');
  if (!IS_FUNCTION(fun)) throw new $TypeError('Object.prototype.__defineSetter__: Expecting function');
  return %DefineAccessor(ToObject(this), ToString(name), SETTER, fun);
}, DONT_ENUM);


%AddProperty($Object.prototype, "__lookupSetter__", function(name) {
  if (this == null) throw new $TypeError('Object.prototype.__lookupSetter__: this is Null');
  return %LookupAccessor(ToObject(this), ToString(name), SETTER);
}, DONT_ENUM);


%SetCode($Object, function(x) {
  if (%IsConstructCall()) {
    if (x == null) return this;
    return ToObject(x);
  } else {
    if (x == null) return { };
    return ToObject(x);
  }
});


// ----------------------------------------------------------------------------
// Global stuff...

%AddProperty(global, "eval", function(x) {
  if (!IS_STRING(x)) return x;

  var f = %CompileString(x, true);
  if (!IS_FUNCTION(f)) return f;

  return f.call(%EvalReceiver(this));
}, DONT_ENUM);


// execScript for IE compatibility.
%AddProperty(global, "execScript", function(expr, lang) {
  // NOTE: We don't care about the character casing.
  if (!lang || /javascript/i.test(lang)) {
    var f = %CompileString(ToString(expr), false);
    f.call(global);
  }
  return null;
}, DONT_ENUM);


// ----------------------------------------------------------------------------
// Boolean

%AddProperty($Boolean.prototype, "toString", function() {
  // NOTE: Both Boolean objects and values can enter here as
  // 'this'. This is not as dictated by ECMA-262.
  if (!IS_BOOLEAN(this) && %ClassOf(this) !== 'Boolean')
    throw new $TypeError('Boolean.prototype.toString is not generic');
  return ToString(%_ValueOf(this));
}, DONT_ENUM);


%AddProperty($Boolean.prototype, "valueOf", function() {
  // NOTE: Both Boolean objects and values can enter here as
  // 'this'. This is not as dictated by ECMA-262.
  if (!IS_BOOLEAN(this) && %ClassOf(this) !== 'Boolean')
    throw new $TypeError('Boolean.prototype.valueOf is not generic');
  return %_ValueOf(this);
}, DONT_ENUM);


// ----------------------------------------------------------------------------
// Number

// Set the Number function and constructor.
%SetCode($Number, function(x) {
  var value = %_ArgumentsLength() == 0 ? 0 : ToNumber(x);
  if (%IsConstructCall()) {
    %_SetValueOf(this, value);
  } else {
    return value;
  }
});

%FunctionSetPrototype($Number, new $Number(0));

%AddProperty($Number.prototype, "constructor", $Number, DONT_ENUM);

// ECMA-262 section 15.7.3.1.
%AddProperty($Number, "MAX_VALUE", 1.7976931348623157e+308, DONT_ENUM | DONT_DELETE | READ_ONLY);

// ECMA-262 section 15.7.3.2.
%AddProperty($Number, "MIN_VALUE", 5e-324, DONT_ENUM | DONT_DELETE | READ_ONLY);

// ECMA-262 section 15.7.3.3.
%AddProperty($Number, "NaN", $NaN, DONT_ENUM | DONT_DELETE | READ_ONLY);

// ECMA-262 section 15.7.3.4.
%AddProperty($Number, "NEGATIVE_INFINITY", -1/0,  DONT_ENUM | DONT_DELETE | READ_ONLY);

// ECMA-262 section 15.7.3.5.
%AddProperty($Number, "POSITIVE_INFINITY", 1/0,  DONT_ENUM | DONT_DELETE | READ_ONLY);

// ECMA-262 section 15.7.4.2.
%AddProperty($Number.prototype, "toString", function(radix) {
  // NOTE: Both Number objects and values can enter here as
  // 'this'. This is not as dictated by ECMA-262.
  var number = this;
  if (!IS_NUMBER(this)) {
    if (%ClassOf(this) !== 'Number')
      throw new $TypeError('Number.prototype.toString is not generic');
    // Get the value of this number in case it's an object.
    number = %_ValueOf(this);
  }
  // Fast case: Convert number in radix 10.
  if (IS_UNDEFINED(radix) || radix === 10) {
    return ToString(number);
  }

  // Convert the radix to an integer and check the range.
  radix = TO_INTEGER(radix);
  if (radix < 2 || radix > 36) {
    throw new $RangeError('toString() radix argument must be between 2 and 36');
  }
  // Convert the number to a string in the given radix.
  return %NumberToRadixString(number, radix);
}, DONT_ENUM);


// ECMA-262 section 15.7.4.3
%AddProperty($Number.prototype, "toLocaleString", function() {
  return this.toString();
}, DONT_ENUM);


// ECMA-262 section 15.7.4.4
%AddProperty($Number.prototype, "valueOf", function() {
  // NOTE: Both Number objects and values can enter here as
  // 'this'. This is not as dictated by ECMA-262.
  if (!IS_NUMBER(this) && %ClassOf(this) !== 'Number')
    throw new $TypeError('Number.prototype.valueOf is not generic');
  return %_ValueOf(this);
}, DONT_ENUM);


// ECMA-262 section 15.7.4.5
%AddProperty($Number.prototype, "toFixed", function(fractionDigits) {
  var f = TO_INTEGER(fractionDigits);
  if (f < 0 || f > 20) {
    throw new $RangeError("toFixed() digits argument must be between 0 and 20");
  }
  var x = ToNumber(this);
  return %NumberToFixed(x, f);
}, DONT_ENUM);


// ECMA-262 section 15.7.4.6
%AddProperty($Number.prototype, "toExponential", function(fractionDigits) {
  var f = -1;
  if (!IS_UNDEFINED(fractionDigits)) {
    f = TO_INTEGER(fractionDigits);
    if (f < 0 || f > 20) {
      throw new $RangeError("toExponential() argument must be between 0 and 20");
    }
  }
  var x = ToNumber(this);
  return %NumberToExponential(x, f);
}, DONT_ENUM);


// ECMA-262 section 15.7.4.7
%AddProperty($Number.prototype, "toPrecision", function(precision) {
  if (IS_UNDEFINED(precision)) return ToString(%_ValueOf(this));
  var p = TO_INTEGER(precision);
  if (p < 1 || p > 21) {
    throw new $RangeError("toPrecision() argument must be between 1 and 21");
  }
  var x = ToNumber(this);
  return %NumberToPrecision(x, p);
}, DONT_ENUM);


// ----------------------------------------------------------------------------
// Function

$Function.prototype.constructor = $Function;


function FunctionSourceString(func) {
  // NOTE: Both Function objects and values can enter here as
  // 'func'. This is not as dictated by ECMA-262.
  if (!IS_FUNCTION(func) && %ClassOf(func) != 'Function')
    throw new $TypeError('Function.prototype.toString is not generic');

  var source = %FunctionGetSourceCode(func);
  if (!IS_STRING(source)) {
    var name = %FunctionGetName(func);
    if (name) {
      // Mimic what KJS does.
      return 'function ' + name + '() { [native code] }';
    } else {
      return 'function () { [native code] }';
    }
  }

  // Censor occurrences of internal calls.  We do that for all
  // functions and don't cache under the assumption that people rarly
  // convert functions to strings.  Note that we (apparently) can't
  // use regular expression literals in natives files.
  var regexp = ORIGINAL_REGEXP("%(\\w+\\()", "gm");
  if (source.match(regexp)) source = source.replace(regexp, "$1");
  var name = %FunctionGetName(func);
  return 'function ' + name + source;
};


%AddProperty($Function.prototype, "toString", function() {
  return FunctionSourceString(this);
}, DONT_ENUM);


function NewFunction(arg1) {  // length == 1
  var n = %_ArgumentsLength();
  var p = '';
  if (n > 1) {
    p = new $Array(n - 1);
    // Explicitly convert all parameters to strings.
    // Array.prototype.join replaces null with empty strings which is
    // not appropriate.
    for (var i = 0; i < n - 1; i++) p[i] = ToString(%_Arguments(i));
    p = p.join(',');
    // If the formal parameters string include ) - an illegal
    // character - it may make the combined function expression
    // compile. We avoid this problem by checking for this early on.
    if (p.indexOf(')') != -1) throw MakeSyntaxError('unable_to_parse',[]);
  }
  var body = (n > 0) ? ToString(%_Arguments(n - 1)) : '';
  var source = '(function anonymous(' + p + ') { ' + body + ' })';

  // The call to SetNewFunctionAttributes will ensure the prototype
  // property of the resulting function is enumerable (ECMA262, 15.3.5.2).
  return %SetNewFunctionAttributes(%CompileString(source, false)());
};

%SetCode($Function, NewFunction);


// NOTE: The following functions (call and apply) are only used in this
// form on the ARM platform. On IA-32 they are handled through specialized
// builtins; see builtins-ia32.cc.

%AddProperty($Function.prototype, "call", function(receiver) {
  // Make sure the receiver of this call is a function. If it isn't
  // we "fake" a call of it (without the right arguments) to force
  // an exception to be thrown.
  if (!IS_FUNCTION(this)) this();

  // If receiver is null or undefined set the receiver to the global
  // object. If the receiver isn't an object, we convert the
  // receiver to an object.
  if (receiver == null) receiver = global;
  else if (!IS_OBJECT(receiver)) receiver = ToObject(receiver);

  %_SetThisFunction(this);
  %_SetThis(receiver);

  var len = %_GetArgumentsLength(1);
  return %_ShiftDownAndTailCall(len ? len - 1 : 0);
}, DONT_ENUM);


// This implementation of Function.prototype.apply replaces the stack frame
// of the apply call with the new stack frame containing the arguments from
// the args array.
%AddProperty($Function.prototype, "apply", function(receiver, args) {
  var length = (args == null) ? 0 : ToUint32(args.length);

  // We can handle any number of apply arguments if the stack is
  // big enough, but sanity check the value to avoid overflow when
  // multiplying with pointer size.
  if (length > 0x800000) {
    throw new $RangeError(
        "Function.prototype.apply cannot support " + length + " arguments.");
  }

  if (!IS_FUNCTION(this)) {
    throw new $TypeError('Function.prototype.apply was called on ' + this.toString() + ', which is a ' + (typeof this) + ' and not a function');
  }

  // Make sure args has the right type.
  if (args != null && %ClassOf(args) !== 'Array' && %ClassOf(args) !== 'Arguments') {
    throw new $TypeError('Function.prototype.apply: args has wrong type');
  }

  // If receiver is null or undefined set the receiver to the global
  // object. If the receiver isn't an object, we convert the
  // receiver to an object.
  if (receiver == null) receiver = global;
  else if (!IS_OBJECT(receiver)) receiver = ToObject(receiver);

  %_SetThisFunction(this);
  %_SetThis(receiver);

  var arguments_length = %_GetArgumentsLength(2);

  // This method has 2 formal arguments so if less are passed, then space has
  // been made.
  if (arguments_length < 2)
    arguments_length = 2;

  // Move some stuff to locals so they don't get overwritten when we start
  // expanding the args array.
  var saved_args = args;

  if (arguments_length > length) {
    // We have too many arguments - we need to squash the frame.
    %_SquashFrame(arguments_length, length);
  } else if (arguments_length != length) {
    // We have too few spaces for arguments - we need to expand the frame.
    if (!%_ExpandFrame(arguments_length, length)) {
      throw new $RangeError(
          "Function.prototype.apply cannot find stack space for " + length + " arguments.");
    }
    // GC doesn't like junk in the arguments!
    for (var i = 0; i < length; i++) {
      %_SetArgument(i, 0, length);
    }
  }

  // Update-number-of-arguments field to keep things looking consistent for
  // stack traces, and uses of arguments or arguments.length.
  %_SetArgumentsLength(length);

  // NOTE: For the fast case this should be implemented in assembler,
  // which would allow us to omit bounds and class checks galore.  The
  // assembler version could fall back to this implementation if
  // tricky stuff is found, like arrays implemented as dictionaries or
  // holes in arrays.
  for (var i = 0; i < length; i++) {
    %_SetArgument(i, saved_args[i], length);
  }

  // Replaces the current frame with the new call.  This has the added effect
  // of removing apply from the stack trace entirely, which matches the
  // behaviour of Firefox.
  return %_TailCallWithArguments(length);
}, DONT_ENUM);


