// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This files contains runtime support implemented in JavaScript.

// CAUTION: Some of the functions specified in this file are called
// directly from compiled code. These are the functions with names in
// ALL CAPS. The compiled code passes the first argument in 'this'.


// The following declarations are shared with other native JS files.
// They are all declared at this one spot to avoid redeclaration errors.
var $defaultString;
var $NaN;
var $nonNumberToNumber;
var $nonStringToString;
var $sameValue;
var $sameValueZero;
var $toInteger;
var $toLength;
var $toNumber;
var $toPositiveInteger;
var $toPrimitive;
var $toString;

(function(global, utils) {

%CheckIsBootstrapping();

var GlobalArray = global.Array;
var GlobalBoolean = global.Boolean;
var GlobalString = global.String;
var GlobalNumber = global.Number;
var isConcatSpreadableSymbol =
    utils.ImportNow("is_concat_spreadable_symbol");

// ----------------------------------------------------------------------------

/* -----------------------------------
- - -   C o m p a r i s o n   - - -
-----------------------------------
*/

// ECMA-262 Section 11.9.3.
function EQUALS(y) {
  if (IS_STRING(this) && IS_STRING(y)) return %StringEquals(this, y);
  var x = this;

  while (true) {
    if (IS_NUMBER(x)) {
      while (true) {
        if (IS_NUMBER(y)) return %NumberEquals(x, y);
        if (IS_NULL_OR_UNDEFINED(y)) return 1;  // not equal
        if (!IS_SPEC_OBJECT(y)) {
          if (IS_SYMBOL(y) || IS_SIMD_VALUE(y)) return 1;  // not equal
          // String or boolean.
          return %NumberEquals(x, %to_number_fun(y));
        }
        y = %to_primitive(y, NO_HINT);
      }
    } else if (IS_STRING(x)) {
      while (true) {
        if (IS_STRING(y)) return %StringEquals(x, y);
        if (IS_NUMBER(y)) return %NumberEquals(%to_number_fun(x), y);
        if (IS_BOOLEAN(y)) {
          return %NumberEquals(%to_number_fun(x), %to_number_fun(y));
        }
        if (IS_NULL_OR_UNDEFINED(y)) return 1;  // not equal
        if (IS_SYMBOL(y) || IS_SIMD_VALUE(y)) return 1;  // not equal
        y = %to_primitive(y, NO_HINT);
      }
    } else if (IS_SYMBOL(x)) {
      if (IS_SYMBOL(y)) return %_ObjectEquals(x, y) ? 0 : 1;
      return 1; // not equal
    } else if (IS_BOOLEAN(x)) {
      if (IS_BOOLEAN(y)) return %_ObjectEquals(x, y) ? 0 : 1;
      if (IS_NULL_OR_UNDEFINED(y)) return 1;
      if (IS_NUMBER(y)) return %NumberEquals(%to_number_fun(x), y);
      if (IS_STRING(y)) {
        return %NumberEquals(%to_number_fun(x), %to_number_fun(y));
      }
      if (IS_SYMBOL(y) || IS_SIMD_VALUE(y)) return 1;  // not equal
      // y is object.
      x = %to_number_fun(x);
      y = %to_primitive(y, NO_HINT);
    } else if (IS_NULL_OR_UNDEFINED(x)) {
      return IS_NULL_OR_UNDEFINED(y) ? 0 : 1;
    } else if (IS_SIMD_VALUE(x)) {
      if (!IS_SIMD_VALUE(y)) return 1;  // not equal
       return %SimdEquals(x, y);
    } else {
      // x is an object.
      if (IS_SPEC_OBJECT(y)) return %_ObjectEquals(x, y) ? 0 : 1;
      if (IS_NULL_OR_UNDEFINED(y)) return 1;  // not equal
      if (IS_BOOLEAN(y)) {
        y = %to_number_fun(y);
      } else if (IS_SYMBOL(y) || IS_SIMD_VALUE(y)) {
        return 1;  // not equal
      }
      x = %to_primitive(x, NO_HINT);
    }
  }
}


// ECMA-262, section 11.8.5, page 53. The 'ncr' parameter is used as
// the result when either (or both) the operands are NaN.
function COMPARE(x, ncr) {
  var left;
  var right;
  // Fast cases for string, numbers and undefined compares.
  if (IS_STRING(this)) {
    if (IS_STRING(x)) return %_StringCompare(this, x);
    if (IS_UNDEFINED(x)) return ncr;
    left = this;
  } else if (IS_NUMBER(this)) {
    if (IS_NUMBER(x)) return %NumberCompare(this, x, ncr);
    if (IS_UNDEFINED(x)) return ncr;
    left = this;
  } else if (IS_UNDEFINED(this)) {
    if (!IS_UNDEFINED(x)) {
      %to_primitive(x, NUMBER_HINT);
    }
    return ncr;
  } else if (IS_UNDEFINED(x)) {
    %to_primitive(this, NUMBER_HINT);
    return ncr;
  } else {
    left = %to_primitive(this, NUMBER_HINT);
  }

  right = %to_primitive(x, NUMBER_HINT);
  if (IS_STRING(left) && IS_STRING(right)) {
    return %_StringCompare(left, right);
  } else {
    var left_number = %to_number_fun(left);
    var right_number = %to_number_fun(right);
    if (NUMBER_IS_NAN(left_number) || NUMBER_IS_NAN(right_number)) return ncr;
    return %NumberCompare(left_number, right_number, ncr);
  }
}

// Strong mode COMPARE throws if an implicit conversion would be performed
function COMPARE_STRONG(x, ncr) {
  if (IS_STRING(this) && IS_STRING(x)) return %_StringCompare(this, x);
  if (IS_NUMBER(this) && IS_NUMBER(x)) return %NumberCompare(this, x, ncr);

  throw %make_type_error(kStrongImplicitConversion);
}



/* -----------------------------------
   - - -   A r i t h m e t i c   - - -
   -----------------------------------
*/

// ECMA-262, section 11.6.1, page 50.
function ADD(x) {
  // Fast case: Check for number operands and do the addition.
  if (IS_NUMBER(this) && IS_NUMBER(x)) return %NumberAdd(this, x);
  if (IS_STRING(this) && IS_STRING(x)) return %_StringAdd(this, x);

  // Default implementation.
  var a = %to_primitive(this, NO_HINT);
  var b = %to_primitive(x, NO_HINT);

  if (IS_STRING(a)) {
    return %_StringAdd(a, %to_string_fun(b));
  } else if (IS_STRING(b)) {
    return %_StringAdd(%non_string_to_string(a), b);
  } else {
    return %NumberAdd(%to_number_fun(a), %to_number_fun(b));
  }
}


// Strong mode ADD throws if an implicit conversion would be performed
function ADD_STRONG(x) {
  if (IS_NUMBER(this) && IS_NUMBER(x)) return %NumberAdd(this, x);
  if (IS_STRING(this) && IS_STRING(x)) return %_StringAdd(this, x);

  throw %make_type_error(kStrongImplicitConversion);
}


// Left operand (this) is already a string.
function STRING_ADD_LEFT(y) {
  if (!IS_STRING(y)) {
    if (IS_STRING_WRAPPER(y) && %_IsStringWrapperSafeForDefaultValueOf(y)) {
      y = %_ValueOf(y);
    } else {
      y = IS_NUMBER(y)
          ? %_NumberToString(y)
          : %to_string_fun(%to_primitive(y, NO_HINT));
    }
  }
  return %_StringAdd(this, y);
}


// Right operand (y) is already a string.
function STRING_ADD_RIGHT(y) {
  var x = this;
  if (!IS_STRING(x)) {
    if (IS_STRING_WRAPPER(x) && %_IsStringWrapperSafeForDefaultValueOf(x)) {
      x = %_ValueOf(x);
    } else {
      x = IS_NUMBER(x)
          ? %_NumberToString(x)
          : %to_string_fun(%to_primitive(x, NO_HINT));
    }
  }
  return %_StringAdd(x, y);
}


// ECMA-262, section 11.6.2, page 50.
function SUB(y) {
  var x = IS_NUMBER(this) ? this : %non_number_to_number(this);
  if (!IS_NUMBER(y)) y = %non_number_to_number(y);
  return %NumberSub(x, y);
}


// Strong mode SUB throws if an implicit conversion would be performed
function SUB_STRONG(y) {
  if (IS_NUMBER(this) && IS_NUMBER(y)) {
    return %NumberSub(this, y);
  }
  throw %make_type_error(kStrongImplicitConversion);
}


// ECMA-262, section 11.5.1, page 48.
function MUL(y) {
  var x = IS_NUMBER(this) ? this : %non_number_to_number(this);
  if (!IS_NUMBER(y)) y = %non_number_to_number(y);
  return %NumberMul(x, y);
}


// Strong mode MUL throws if an implicit conversion would be performed
function MUL_STRONG(y) {
  if (IS_NUMBER(this) && IS_NUMBER(y)) {
    return %NumberMul(this, y);
  }
  throw %make_type_error(kStrongImplicitConversion);
}


// ECMA-262, section 11.5.2, page 49.
function DIV(y) {
  var x = IS_NUMBER(this) ? this : %non_number_to_number(this);
  if (!IS_NUMBER(y)) y = %non_number_to_number(y);
  return %NumberDiv(x, y);
}


// Strong mode DIV throws if an implicit conversion would be performed
function DIV_STRONG(y) {
  if (IS_NUMBER(this) && IS_NUMBER(y)) {
    return %NumberDiv(this, y);
  }
  throw %make_type_error(kStrongImplicitConversion);
}


// ECMA-262, section 11.5.3, page 49.
function MOD(y) {
  var x = IS_NUMBER(this) ? this : %non_number_to_number(this);
  if (!IS_NUMBER(y)) y = %non_number_to_number(y);
  return %NumberMod(x, y);
}


// Strong mode MOD throws if an implicit conversion would be performed
function MOD_STRONG(y) {
  if (IS_NUMBER(this) && IS_NUMBER(y)) {
    return %NumberMod(this, y);
  }
  throw %make_type_error(kStrongImplicitConversion);
}


/* -------------------------------------------
   - - -   B i t   o p e r a t i o n s   - - -
   -------------------------------------------
*/

// ECMA-262, section 11.10, page 57.
function BIT_OR(y) {
  var x = IS_NUMBER(this) ? this : %non_number_to_number(this);
  if (!IS_NUMBER(y)) y = %non_number_to_number(y);
  return %NumberOr(x, y);
}


// Strong mode BIT_OR throws if an implicit conversion would be performed
function BIT_OR_STRONG(y) {
  if (IS_NUMBER(this) && IS_NUMBER(y)) {
    return %NumberOr(this, y);
  }
  throw %make_type_error(kStrongImplicitConversion);
}


// ECMA-262, section 11.10, page 57.
function BIT_AND(y) {
  var x;
  if (IS_NUMBER(this)) {
    x = this;
    if (!IS_NUMBER(y)) y = %non_number_to_number(y);
  } else {
    x = %non_number_to_number(this);
    // Make sure to convert the right operand to a number before
    // bailing out in the fast case, but after converting the
    // left operand. This ensures that valueOf methods on the right
    // operand are always executed.
    if (!IS_NUMBER(y)) y = %non_number_to_number(y);
    // Optimize for the case where we end up AND'ing a value
    // that doesn't convert to a number. This is common in
    // certain benchmarks.
    if (NUMBER_IS_NAN(x)) return 0;
  }
  return %NumberAnd(x, y);
}


// Strong mode BIT_AND throws if an implicit conversion would be performed
function BIT_AND_STRONG(y) {
  if (IS_NUMBER(this) && IS_NUMBER(y)) {
    return %NumberAnd(this, y);
  }
  throw %make_type_error(kStrongImplicitConversion);
}


// ECMA-262, section 11.10, page 57.
function BIT_XOR(y) {
  var x = IS_NUMBER(this) ? this : %non_number_to_number(this);
  if (!IS_NUMBER(y)) y = %non_number_to_number(y);
  return %NumberXor(x, y);
}


// Strong mode BIT_XOR throws if an implicit conversion would be performed
function BIT_XOR_STRONG(y) {
  if (IS_NUMBER(this) && IS_NUMBER(y)) {
    return %NumberXor(this, y);
  }
  throw %make_type_error(kStrongImplicitConversion);
}


// ECMA-262, section 11.7.1, page 51.
function SHL(y) {
  var x = IS_NUMBER(this) ? this : %non_number_to_number(this);
  if (!IS_NUMBER(y)) y = %non_number_to_number(y);
  return %NumberShl(x, y);
}


// Strong mode SHL throws if an implicit conversion would be performed
function SHL_STRONG(y) {
  if (IS_NUMBER(this) && IS_NUMBER(y)) {
    return %NumberShl(this, y);
  }
  throw %make_type_error(kStrongImplicitConversion);
}


// ECMA-262, section 11.7.2, page 51.
function SAR(y) {
  var x;
  if (IS_NUMBER(this)) {
    x = this;
    if (!IS_NUMBER(y)) y = %non_number_to_number(y);
  } else {
    x = %non_number_to_number(this);
    // Make sure to convert the right operand to a number before
    // bailing out in the fast case, but after converting the
    // left operand. This ensures that valueOf methods on the right
    // operand are always executed.
    if (!IS_NUMBER(y)) y = %non_number_to_number(y);
    // Optimize for the case where we end up shifting a value
    // that doesn't convert to a number. This is common in
    // certain benchmarks.
    if (NUMBER_IS_NAN(x)) return 0;
  }
  return %NumberSar(x, y);
}


// Strong mode SAR throws if an implicit conversion would be performed
function SAR_STRONG(y) {
  if (IS_NUMBER(this) && IS_NUMBER(y)) {
    return %NumberSar(this, y);
  }
  throw %make_type_error(kStrongImplicitConversion);
}


// ECMA-262, section 11.7.3, page 52.
function SHR(y) {
  var x = IS_NUMBER(this) ? this : %non_number_to_number(this);
  if (!IS_NUMBER(y)) y = %non_number_to_number(y);
  return %NumberShr(x, y);
}


// Strong mode SHR throws if an implicit conversion would be performed
function SHR_STRONG(y) {
  if (IS_NUMBER(this) && IS_NUMBER(y)) {
    return %NumberShr(this, y);
  }
  throw %make_type_error(kStrongImplicitConversion);
}


/* -----------------------------
   - - -   H e l p e r s   - - -
   -----------------------------
*/

// ECMA-262, section 11.8.7, page 54.
function IN(x) {
  if (!IS_SPEC_OBJECT(x)) {
    throw %make_type_error(kInvalidInOperatorUse, this, x);
  }
  if (%_IsNonNegativeSmi(this)) {
    if (IS_ARRAY(x) && %_HasFastPackedElements(x)) {
      return this < x.length;
    }
    return %HasElement(x, this);
  }
  return %HasProperty(x, this);
}


function CALL_NON_FUNCTION() {
  var delegate = %GetFunctionDelegate(this);
  if (!IS_FUNCTION(delegate)) {
    var callsite = %RenderCallSite();
    if (callsite == "") callsite = typeof this;
    throw %make_type_error(kCalledNonCallable, callsite);
  }
  return %Apply(delegate, this, arguments, 0, %_ArgumentsLength());
}


function CALL_NON_FUNCTION_AS_CONSTRUCTOR() {
  var delegate = %GetConstructorDelegate(this);
  if (!IS_FUNCTION(delegate)) {
    var callsite = %RenderCallSite();
    if (callsite == "") callsite = typeof this;
    throw %make_type_error(kCalledNonCallable, callsite);
  }
  return %Apply(delegate, this, arguments, 0, %_ArgumentsLength());
}


function CALL_FUNCTION_PROXY() {
  var arity = %_ArgumentsLength() - 1;
  var proxy = %_Arguments(arity);  // The proxy comes in as an additional arg.
  var trap = %GetCallTrap(proxy);
  return %Apply(trap, this, arguments, 0, arity);
}


function CALL_FUNCTION_PROXY_AS_CONSTRUCTOR () {
  var proxy = this;
  var trap = %GetConstructTrap(proxy);
  return %Apply(trap, this, arguments, 0, %_ArgumentsLength());
}


function APPLY_PREPARE(args) {
  var length;
  // First check whether length is a positive Smi and args is an
  // array. This is the fast case. If this fails, we do the slow case
  // that takes care of more eventualities.
  if (IS_ARRAY(args)) {
    length = args.length;
    if (%_IsSmi(length) && length >= 0 && length < kSafeArgumentsLength &&
        IS_SPEC_FUNCTION(this)) {
      return length;
    }
  }

  length = (args == null) ? 0 : TO_UINT32(args.length);

  // We can handle any number of apply arguments if the stack is
  // big enough, but sanity check the value to avoid overflow when
  // multiplying with pointer size.
  if (length > kSafeArgumentsLength) throw %make_range_error(kStackOverflow);

  if (!IS_SPEC_FUNCTION(this)) {
    throw %make_type_error(kApplyNonFunction, %to_string_fun(this), typeof this);
  }

  // Make sure the arguments list has the right type.
  if (args != null && !IS_SPEC_OBJECT(args)) {
    throw %make_type_error(kWrongArgs, "Function.prototype.apply");
  }

  // Return the length which is the number of arguments to copy to the
  // stack. It is guaranteed to be a small integer at this point.
  return length;
}


function REFLECT_APPLY_PREPARE(args) {
  var length;
  // First check whether length is a positive Smi and args is an
  // array. This is the fast case. If this fails, we do the slow case
  // that takes care of more eventualities.
  if (IS_ARRAY(args)) {
    length = args.length;
    if (%_IsSmi(length) && length >= 0 && length < kSafeArgumentsLength &&
        IS_SPEC_FUNCTION(this)) {
      return length;
    }
  }

  if (!IS_SPEC_FUNCTION(this)) {
    throw %make_type_error(kCalledNonCallable, %to_string_fun(this));
  }

  if (!IS_SPEC_OBJECT(args)) {
    throw %make_type_error(kWrongArgs, "Reflect.apply");
  }

  length = %to_length_fun(args.length);

  // We can handle any number of apply arguments if the stack is
  // big enough, but sanity check the value to avoid overflow when
  // multiplying with pointer size.
  if (length > kSafeArgumentsLength) throw %make_range_error(kStackOverflow);

  // Return the length which is the number of arguments to copy to the
  // stack. It is guaranteed to be a small integer at this point.
  return length;
}


function REFLECT_CONSTRUCT_PREPARE(
    args, newTarget) {
  var length;
  var ctorOk = IS_SPEC_FUNCTION(this) && %IsConstructor(this);
  var newTargetOk = IS_SPEC_FUNCTION(newTarget) && %IsConstructor(newTarget);

  // First check whether length is a positive Smi and args is an
  // array. This is the fast case. If this fails, we do the slow case
  // that takes care of more eventualities.
  if (IS_ARRAY(args)) {
    length = args.length;
    if (%_IsSmi(length) && length >= 0 && length < kSafeArgumentsLength &&
        ctorOk && newTargetOk) {
      return length;
    }
  }

  if (!ctorOk) {
    if (!IS_SPEC_FUNCTION(this)) {
      throw %make_type_error(kCalledNonCallable, %to_string_fun(this));
    } else {
      throw %make_type_error(kNotConstructor, %to_string_fun(this));
    }
  }

  if (!newTargetOk) {
    if (!IS_SPEC_FUNCTION(newTarget)) {
      throw %make_type_error(kCalledNonCallable, %to_string_fun(newTarget));
    } else {
      throw %make_type_error(kNotConstructor, %to_string_fun(newTarget));
    }
  }

  if (!IS_SPEC_OBJECT(args)) {
    throw %make_type_error(kWrongArgs, "Reflect.construct");
  }

  length = %to_length_fun(args.length);

  // We can handle any number of apply arguments if the stack is
  // big enough, but sanity check the value to avoid overflow when
  // multiplying with pointer size.
  if (length > kSafeArgumentsLength) throw %make_range_error(kStackOverflow);

  // Return the length which is the number of arguments to copy to the
  // stack. It is guaranteed to be a small integer at this point.
  return length;
}


function CONCAT_ITERABLE_TO_ARRAY(iterable) {
  return %concat_iterable_to_array(this, iterable);
};


function STACK_OVERFLOW(length) {
  throw %make_range_error(kStackOverflow);
}


// Convert the receiver to a number - forward to ToNumber.
function TO_NUMBER() {
  return %to_number_fun(this);
}


// Convert the receiver to a string - forward to ToString.
function TO_STRING() {
  return %to_string_fun(this);
}


/* -------------------------------------
   - - -   C o n v e r s i o n s   - - -
   -------------------------------------
*/

// ECMA-262, section 9.1, page 30. Use null/undefined for no hint,
// (1) for number hint, and (2) for string hint.
function ToPrimitive(x, hint) {
  if (!IS_SPEC_OBJECT(x)) return x;
  if (hint == NO_HINT) hint = (IS_DATE(x)) ? STRING_HINT : NUMBER_HINT;
  return (hint == NUMBER_HINT) ? DefaultNumber(x) : DefaultString(x);
}


// ECMA-262, section 9.2, page 30
function ToBoolean(x) {
  if (IS_BOOLEAN(x)) return x;
  if (IS_STRING(x)) return x.length != 0;
  if (x == null) return false;
  if (IS_NUMBER(x)) return !((x == 0) || NUMBER_IS_NAN(x));
  return true;
}


// ECMA-262, section 9.3, page 31.
function ToNumber(x) {
  if (IS_NUMBER(x)) return x;
  if (IS_STRING(x)) {
    return %_HasCachedArrayIndex(x) ? %_GetCachedArrayIndex(x)
                                    : %StringToNumber(x);
  }
  if (IS_BOOLEAN(x)) return x ? 1 : 0;
  if (IS_UNDEFINED(x)) return NAN;
  // Types that can't be converted to number are caught in DefaultNumber.
  return (IS_NULL(x)) ? 0 : ToNumber(DefaultNumber(x));
}

function NonNumberToNumber(x) {
  if (IS_STRING(x)) {
    return %_HasCachedArrayIndex(x) ? %_GetCachedArrayIndex(x)
                                    : %StringToNumber(x);
  }
  if (IS_BOOLEAN(x)) return x ? 1 : 0;
  if (IS_UNDEFINED(x)) return NAN;
  // Types that can't be converted to number are caught in DefaultNumber.
  return (IS_NULL(x)) ? 0 : ToNumber(DefaultNumber(x));
}


// ECMA-262, section 9.8, page 35.
function ToString(x) {
  if (IS_STRING(x)) return x;
  if (IS_NUMBER(x)) return %_NumberToString(x);
  if (IS_BOOLEAN(x)) return x ? 'true' : 'false';
  if (IS_UNDEFINED(x)) return 'undefined';
  // Types that can't be converted to string are caught in DefaultString.
  return (IS_NULL(x)) ? 'null' : ToString(DefaultString(x));
}

function NonStringToString(x) {
  if (IS_NUMBER(x)) return %_NumberToString(x);
  if (IS_BOOLEAN(x)) return x ? 'true' : 'false';
  if (IS_UNDEFINED(x)) return 'undefined';
  // Types that can't be converted to string are caught in DefaultString.
  return (IS_NULL(x)) ? 'null' : ToString(DefaultString(x));
}


// ECMA-262, section 9.4, page 34.
function ToInteger(x) {
  if (%_IsSmi(x)) return x;
  return %NumberToInteger(ToNumber(x));
}


// ES6, draft 08-24-14, section 7.1.15
function ToLength(arg) {
  arg = ToInteger(arg);
  if (arg < 0) return 0;
  return arg < GlobalNumber.MAX_SAFE_INTEGER ? arg
                                             : GlobalNumber.MAX_SAFE_INTEGER;
}


// ES5, section 9.12
function SameValue(x, y) {
  if (typeof x != typeof y) return false;
  if (IS_NUMBER(x)) {
    if (NUMBER_IS_NAN(x) && NUMBER_IS_NAN(y)) return true;
    // x is +0 and y is -0 or vice versa.
    if (x === 0 && y === 0 && %_IsMinusZero(x) != %_IsMinusZero(y)) {
      return false;
    }
  }
  if (IS_SIMD_VALUE(x)) return %SimdSameValue(x, y);
  return x === y;
}


// ES6, section 7.2.4
function SameValueZero(x, y) {
  if (typeof x != typeof y) return false;
  if (IS_NUMBER(x)) {
    if (NUMBER_IS_NAN(x) && NUMBER_IS_NAN(y)) return true;
  }
  if (IS_SIMD_VALUE(x)) return %SimdSameValueZero(x, y);
  return x === y;
}


function ConcatIterableToArray(target, iterable) {
   var index = target.length;
   for (var element of iterable) {
     %AddElement(target, index++, element);
   }
   return target;
}


/* ---------------------------------
   - - -   U t i l i t i e s   - - -
   ---------------------------------
*/

// Returns if the given x is a primitive value - not an object or a
// function.
function IsPrimitive(x) {
  // Even though the type of null is "object", null is still
  // considered a primitive value. IS_SPEC_OBJECT handles this correctly
  // (i.e., it will return false if x is null).
  return !IS_SPEC_OBJECT(x);
}


// ES6, draft 10-14-14, section 22.1.3.1.1
function IsConcatSpreadable(O) {
  if (!IS_SPEC_OBJECT(O)) return false;
  var spreadable = O[isConcatSpreadableSymbol];
  if (IS_UNDEFINED(spreadable)) return IS_ARRAY(O);
  return ToBoolean(spreadable);
}


// ECMA-262, section 8.6.2.6, page 28.
function DefaultNumber(x) {
  var valueOf = x.valueOf;
  if (IS_SPEC_FUNCTION(valueOf)) {
    var v = %_CallFunction(x, valueOf);
    if (IS_SYMBOL(v)) throw MakeTypeError(kSymbolToNumber);
    if (IS_SIMD_VALUE(x)) throw MakeTypeError(kSimdToNumber);
    if (IsPrimitive(v)) return v;
  }
  var toString = x.toString;
  if (IS_SPEC_FUNCTION(toString)) {
    var s = %_CallFunction(x, toString);
    if (IsPrimitive(s)) return s;
  }
  throw MakeTypeError(kCannotConvertToPrimitive);
}

// ECMA-262, section 8.6.2.6, page 28.
function DefaultString(x) {
  if (!IS_SYMBOL_WRAPPER(x)) {
    if (IS_SYMBOL(x)) throw MakeTypeError(kSymbolToString);
    var toString = x.toString;
    if (IS_SPEC_FUNCTION(toString)) {
      var s = %_CallFunction(x, toString);
      if (IsPrimitive(s)) return s;
    }

    var valueOf = x.valueOf;
    if (IS_SPEC_FUNCTION(valueOf)) {
      var v = %_CallFunction(x, valueOf);
      if (IsPrimitive(v)) return v;
    }
  }
  throw MakeTypeError(kCannotConvertToPrimitive);
}

function ToPositiveInteger(x, rangeErrorIndex) {
  var i = TO_INTEGER_MAP_MINUS_ZERO(x);
  if (i < 0) throw MakeRangeError(rangeErrorIndex);
  return i;
}

//----------------------------------------------------------------------------

// NOTE: Setting the prototype for Array must take place as early as
// possible due to code generation for array literals.  When
// generating code for a array literal a boilerplate array is created
// that is cloned when running the code.  It is essential that the
// boilerplate gets the right prototype.
%FunctionSetPrototype(GlobalArray, new GlobalArray(0));

// ----------------------------------------------------------------------------
// Exports

$defaultString = DefaultString;
$NaN = %GetRootNaN();
$nonNumberToNumber = NonNumberToNumber;
$nonStringToString = NonStringToString;
$sameValue = SameValue;
$sameValueZero = SameValueZero;
$toInteger = ToInteger;
$toLength = ToLength;
$toNumber = ToNumber;
$toPositiveInteger = ToPositiveInteger;
$toPrimitive = ToPrimitive;
$toString = ToString;

%InstallToContext([
  "add_builtin", ADD,
  "add_strong_builtin", ADD_STRONG,
  "apply_prepare_builtin", APPLY_PREPARE,
  "bit_and_builtin", BIT_AND,
  "bit_and_strong_builtin", BIT_AND_STRONG,
  "bit_or_builtin", BIT_OR,
  "bit_or_strong_builtin", BIT_OR_STRONG,
  "bit_xor_builtin", BIT_XOR,
  "bit_xor_strong_builtin", BIT_XOR_STRONG,
  "call_function_proxy_as_constructor_builtin", CALL_FUNCTION_PROXY_AS_CONSTRUCTOR,
  "call_function_proxy_builtin", CALL_FUNCTION_PROXY,
  "call_non_function_as_constructor_builtin", CALL_NON_FUNCTION_AS_CONSTRUCTOR,
  "call_non_function_builtin", CALL_NON_FUNCTION,
  "compare_builtin", COMPARE,
  "compare_strong_builtin", COMPARE_STRONG,
  "concat_iterable_to_array_builtin", CONCAT_ITERABLE_TO_ARRAY,
  "div_builtin", DIV,
  "div_strong_builtin", DIV_STRONG,
  "equals_builtin", EQUALS,
  "in_builtin", IN,
  "mod_builtin", MOD,
  "mod_strong_builtin", MOD_STRONG,
  "mul_builtin", MUL,
  "mul_strong_builtin", MUL_STRONG,
  "reflect_apply_prepare_builtin", REFLECT_APPLY_PREPARE,
  "reflect_construct_prepare_builtin", REFLECT_CONSTRUCT_PREPARE,
  "sar_builtin", SAR,
  "sar_strong_builtin", SAR_STRONG,
  "shl_builtin", SHL,
  "shl_strong_builtin", SHL_STRONG,
  "shr_builtin", SHR,
  "shr_strong_builtin", SHR_STRONG,
  "stack_overflow_builtin", STACK_OVERFLOW,
  "string_add_left_builtin", STRING_ADD_LEFT,
  "string_add_right_builtin", STRING_ADD_RIGHT,
  "sub_builtin", SUB,
  "sub_strong_builtin", SUB_STRONG,
]);

%InstallToContext([
  "concat_iterable_to_array", ConcatIterableToArray,
  "non_number_to_number", NonNumberToNumber,
  "non_string_to_string", NonStringToString,
  "to_integer_fun", ToInteger,
  "to_length_fun", ToLength,
  "to_number_fun", ToNumber,
  "to_primitive", ToPrimitive,
  "to_string_fun", ToString,
]);

utils.Export(function(to) {
  to.ToBoolean = ToBoolean;
  to.ToLength = ToLength;
  to.ToNumber = ToNumber;
  to.ToPrimitive = ToPrimitive;
  to.ToString = ToString;
});

})
