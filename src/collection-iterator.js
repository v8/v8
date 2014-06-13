// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';


// This file relies on the fact that the following declaration has been made
// in runtime.js:
// var $Set = global.Set;
// var $Map = global.Map;


function SetIteratorConstructor(set, kind) {
  %SetIteratorInitialize(this, set, kind);
}


function SetIteratorNextJS() {
  if (!IS_SET_ITERATOR(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Set Iterator.prototype.next', this]);
  }
  return %SetIteratorNext(this);
}


function SetIteratorSymbolIterator() {
  return this;
}


function SetEntries() {
  if (!IS_SET(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Set.prototype.entries', this]);
  }
  return new SetIterator(this, ITERATOR_KIND_ENTRIES);
}


function SetValues() {
  if (!IS_SET(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Set.prototype.values', this]);
  }
  return new SetIterator(this, ITERATOR_KIND_VALUES);
}


function SetUpSetIterator() {
  %CheckIsBootstrapping();

  %SetCode(SetIterator, SetIteratorConstructor);
  %FunctionSetPrototype(SetIterator, new $Object());
  %FunctionSetInstanceClassName(SetIterator, 'Set Iterator');
  InstallFunctions(SetIterator.prototype, DONT_ENUM, $Array(
    'next', SetIteratorNextJS
  ));

  %FunctionSetName(SetIteratorSymbolIterator, '[Symbol.iterator]');
  %SetProperty(SetIterator.prototype, symbolIterator,
      SetIteratorSymbolIterator, DONT_ENUM);
}

SetUpSetIterator();


function ExtendSetPrototype() {
  %CheckIsBootstrapping();

  InstallFunctions($Set.prototype, DONT_ENUM, $Array(
    'entries', SetEntries,
    'values', SetValues
  ));

  %SetProperty($Set.prototype, symbolIterator, SetValues,
      DONT_ENUM);
}

ExtendSetPrototype();



function MapIteratorConstructor(map, kind) {
  %MapIteratorInitialize(this, map, kind);
}


function MapIteratorSymbolIterator() {
  return this;
}


function MapIteratorNextJS() {
  if (!IS_MAP_ITERATOR(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Map Iterator.prototype.next', this]);
  }
  return %MapIteratorNext(this);
}


function MapEntries() {
  if (!IS_MAP(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Map.prototype.entries', this]);
  }
  return new MapIterator(this, ITERATOR_KIND_ENTRIES);
}


function MapKeys() {
  if (!IS_MAP(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Map.prototype.keys', this]);
  }
  return new MapIterator(this, ITERATOR_KIND_KEYS);
}


function MapValues() {
  if (!IS_MAP(this)) {
    throw MakeTypeError('incompatible_method_receiver',
                        ['Map.prototype.values', this]);
  }
  return new MapIterator(this, ITERATOR_KIND_VALUES);
}


function SetUpMapIterator() {
  %CheckIsBootstrapping();

  %SetCode(MapIterator, MapIteratorConstructor);
  %FunctionSetPrototype(MapIterator, new $Object());
  %FunctionSetInstanceClassName(MapIterator, 'Map Iterator');
  InstallFunctions(MapIterator.prototype, DONT_ENUM, $Array(
    'next', MapIteratorNextJS
  ));

  %FunctionSetName(MapIteratorSymbolIterator, '[Symbol.iterator]');
  %SetProperty(MapIterator.prototype, symbolIterator,
      MapIteratorSymbolIterator, DONT_ENUM);
}

SetUpMapIterator();


function ExtendMapPrototype() {
  %CheckIsBootstrapping();

  InstallFunctions($Map.prototype, DONT_ENUM, $Array(
    'entries', MapEntries,
    'keys', MapKeys,
    'values', MapValues
  ));

  %SetProperty($Map.prototype, symbolIterator, MapEntries,
      DONT_ENUM);
}

ExtendMapPrototype();
