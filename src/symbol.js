// Copyright 2013 the V8 project authors. All rights reserved.
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

"use strict";

// This file relies on the fact that the following declaration has been made
// in runtime.js:
// var $Array = global.Array;

var $Symbol = global.Symbol;

// -------------------------------------------------------------------

function SymbolConstructor(x) {
  if (%_IsConstructCall()) {
    throw MakeTypeError('not_constructor', ["Symbol"]);
  }
  // NOTE: Passing in a Symbol value will throw on ToString().
  return %CreateSymbol(IS_UNDEFINED(x) ? x : ToString(x));
}


function SymbolToString() {
  if (!(IS_SYMBOL(this) || IS_SYMBOL_WRAPPER(this))) {
    throw MakeTypeError(
      'incompatible_method_receiver', ["Symbol.prototype.toString", this]);
  }
  var description = %SymbolDescription(%_ValueOf(this));
  return "Symbol(" + (IS_UNDEFINED(description) ? "" : description) + ")";
}


function SymbolValueOf() {
  if (!(IS_SYMBOL(this) || IS_SYMBOL_WRAPPER(this))) {
    throw MakeTypeError(
      'incompatible_method_receiver', ["Symbol.prototype.valueOf", this]);
  }
  return %_ValueOf(this);
}


function GetSymbolRegistry() {
  var registry = %SymbolRegistry();
  if (!('internal' in registry)) {
    registry.internal = {__proto__: null};
    registry.for = {__proto__: null};
    registry.keyFor = {__proto__: null};
  }
  return registry;
}


function InternalSymbol(key) {
  var registry = GetSymbolRegistry();
  if (!(key in registry.internal)) {
    registry.internal[key] = %CreateSymbol(key);
  }
  return registry.internal[key];
}


function SymbolFor(key) {
  key = TO_STRING_INLINE(key);
  var registry = GetSymbolRegistry();
  if (!(key in registry.for)) {
    var symbol = %CreateSymbol(key);
    registry.for[key] = symbol;
    registry.keyFor[symbol] = key;
  }
  return registry.for[key];
}


function SymbolKeyFor(symbol) {
  if (!IS_SYMBOL(symbol)) {
    throw MakeTypeError("not_a_symbol", [symbol]);
  }
  return GetSymbolRegistry().keyFor[symbol];
}


// ES6 19.1.2.8
function ObjectGetOwnPropertySymbols(obj) {
  if (!IS_SPEC_OBJECT(obj)) {
    throw MakeTypeError("called_on_non_object",
                        ["Object.getOwnPropertySymbols"]);
  }

  // TODO(arv): Proxies use a shared trap for String and Symbol keys.

  return ObjectGetOwnPropertyKeys(obj, true);
}


//-------------------------------------------------------------------

var symbolCreate = InternalSymbol("@@create");
var symbolHasInstance = InternalSymbol("@@hasInstance");
var symbolIsConcatSpreadable = InternalSymbol("@@isConcatSpreadable");
var symbolIsRegExp = InternalSymbol("@@isRegExp");
var symbolIterator = InternalSymbol("@@iterator");
var symbolToStringTag = InternalSymbol("@@toStringTag");
var symbolUnscopables = InternalSymbol("@@unscopables");


//-------------------------------------------------------------------

function SetUpSymbol() {
  %CheckIsBootstrapping();

  %SetCode($Symbol, SymbolConstructor);
  %FunctionSetPrototype($Symbol, new $Object());

  %SetProperty($Symbol, "create", symbolCreate, DONT_ENUM);
  %SetProperty($Symbol, "hasInstance", symbolHasInstance, DONT_ENUM);
  %SetProperty($Symbol, "isConcatSpreadable",
      symbolIsConcatSpreadable, DONT_ENUM);
  %SetProperty($Symbol, "isRegExp", symbolIsRegExp, DONT_ENUM);
  %SetProperty($Symbol, "iterator", symbolIterator, DONT_ENUM);
  %SetProperty($Symbol, "toStringTag", symbolToStringTag, DONT_ENUM);
  %SetProperty($Symbol, "unscopables", symbolUnscopables, DONT_ENUM);
  InstallFunctions($Symbol, DONT_ENUM, $Array(
    "for", SymbolFor,
    "keyFor", SymbolKeyFor
  ));

  %SetProperty($Symbol.prototype, "constructor", $Symbol, DONT_ENUM);
  InstallFunctions($Symbol.prototype, DONT_ENUM, $Array(
    "toString", SymbolToString,
    "valueOf", SymbolValueOf
  ));
}

SetUpSymbol();


function ExtendObject() {
  %CheckIsBootstrapping();

  InstallFunctions($Object, DONT_ENUM, $Array(
    "getOwnPropertySymbols", ObjectGetOwnPropertySymbols
  ));
}

ExtendObject();
