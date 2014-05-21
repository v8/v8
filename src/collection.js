// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

// This file relies on the fact that the following declaration has been made
// in runtime.js:
// var $Array = global.Array;

var $Set = global.Set;
var $Map = global.Map;


// -------------------------------------------------------------------
// Harmony Set

function SetConstructor() {
  if (%_IsConstructCall()) {
    %SetInitialize(this);
  } else {
    throw MakeTypeError('constructor_not_function', ['Set']);
  }
}


function SetAddJS(key) {
  if (!IS_SET(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Set.prototype.add', this]);
  }
  return %SetAdd(this, key);
}


function SetHasJS(key) {
  if (!IS_SET(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Set.prototype.has', this]);
  }
  return %SetHas(this, key);
}


function SetDeleteJS(key) {
  if (!IS_SET(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Set.prototype.delete', this]);
  }
  return %SetDelete(this, key);
}


function SetGetSizeJS() {
  if (!IS_SET(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Set.prototype.size', this]);
  }
  return %SetGetSize(this);
}


function SetClearJS() {
  if (!IS_SET(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Set.prototype.clear', this]);
  }
  %SetClear(this);
}


function SetForEach(f, receiver) {
  if (!IS_SET(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Set.prototype.forEach', this]);
  }

  if (!IS_SPEC_FUNCTION(f)) {
    throw MakeTypeError('called_non_callable', [f]);
  }

  var iterator = %SetCreateIterator(this, ITERATOR_KIND_VALUES);
  var entry;
  var stepping = %_DebugCallbackSupportsStepping(f);
  while (!(entry = %SetIteratorNext(iterator)).done) {
    if (stepping) %DebugPrepareStepInIfStepping(f);
    %_CallFunction(receiver, entry.value, entry.value, this, f);
  }
}


// -------------------------------------------------------------------

function SetUpSet() {
  %CheckIsBootstrapping();

  %SetCode($Set, SetConstructor);
  %FunctionSetPrototype($Set, new $Object());
  %SetProperty($Set.prototype, "constructor", $Set, DONT_ENUM);

  %FunctionSetLength(SetForEach, 1);

  // Set up the non-enumerable functions on the Set prototype object.
  InstallGetter($Set.prototype, "size", SetGetSizeJS);
  InstallFunctions($Set.prototype, DONT_ENUM, $Array(
    "add", SetAddJS,
    "has", SetHasJS,
    "delete", SetDeleteJS,
    "clear", SetClearJS,
    "forEach", SetForEach
  ));
}

SetUpSet();


// -------------------------------------------------------------------
// Harmony Map

function MapConstructor() {
  if (%_IsConstructCall()) {
    %MapInitialize(this);
  } else {
    throw MakeTypeError('constructor_not_function', ['Map']);
  }
}


function MapGetJS(key) {
  if (!IS_MAP(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Map.prototype.get', this]);
  }
  return %MapGet(this, key);
}


function MapSetJS(key, value) {
  if (!IS_MAP(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Map.prototype.set', this]);
  }
  return %MapSet(this, key, value);
}


function MapHasJS(key) {
  if (!IS_MAP(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Map.prototype.has', this]);
  }
  return %MapHas(this, key);
}


function MapDeleteJS(key) {
  if (!IS_MAP(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Map.prototype.delete', this]);
  }
  return %MapDelete(this, key);
}


function MapGetSizeJS() {
  if (!IS_MAP(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Map.prototype.size', this]);
  }
  return %MapGetSize(this);
}


function MapClearJS() {
  if (!IS_MAP(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Map.prototype.clear', this]);
  }
  %MapClear(this);
}


function MapForEach(f, receiver) {
  if (!IS_MAP(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Map.prototype.forEach', this]);
  }

  if (!IS_SPEC_FUNCTION(f)) {
    throw MakeTypeError('called_non_callable', [f]);
  }

  var iterator = %MapCreateIterator(this, ITERATOR_KIND_ENTRIES);
  var entry;
  var stepping = %_DebugCallbackSupportsStepping(f);
  while (!(entry = %MapIteratorNext(iterator)).done) {
    if (stepping) %DebugPrepareStepInIfStepping(f);
    %_CallFunction(receiver, entry.value[1], entry.value[0], this, f);
  }
}


// -------------------------------------------------------------------

function SetUpMap() {
  %CheckIsBootstrapping();

  %SetCode($Map, MapConstructor);
  %FunctionSetPrototype($Map, new $Object());
  %SetProperty($Map.prototype, "constructor", $Map, DONT_ENUM);

  %FunctionSetLength(MapForEach, 1);

  // Set up the non-enumerable functions on the Map prototype object.
  InstallGetter($Map.prototype, "size", MapGetSizeJS);
  InstallFunctions($Map.prototype, DONT_ENUM, $Array(
    "get", MapGetJS,
    "set", MapSetJS,
    "has", MapHasJS,
    "delete", MapDeleteJS,
    "clear", MapClearJS,
    "forEach", MapForEach
  ));
}

SetUpMap();
