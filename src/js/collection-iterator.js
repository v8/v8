// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils) {

"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var GlobalMap = global.Map;
var GlobalSet = global.Set;
var iteratorSymbol = utils.ImportNow("iterator_symbol");
var MapIterator = utils.ImportNow("MapIterator");
var toStringTagSymbol = utils.ImportNow("to_string_tag_symbol");
var SetIterator = utils.ImportNow("SetIterator");

// -------------------------------------------------------------------

function SetIteratorConstructor(set, kind) {
  %SetIteratorInitialize(this, set, kind);
}


DEFINE_METHOD(
  SetIterator.prototype,
  next() {
    if (!IS_SET_ITERATOR(this)) {
      throw %make_type_error(kIncompatibleMethodReceiver,
                          'Set Iterator.prototype.next', this);
    }

    var value_array = [UNDEFINED, UNDEFINED];
    var result = %_CreateIterResultObject(value_array, false);
    switch (%SetIteratorNext(this, value_array)) {
      case 0:
        result.value = UNDEFINED;
        result.done = true;
        break;
      case ITERATOR_KIND_VALUES:
        result.value = value_array[0];
        break;
      case ITERATOR_KIND_ENTRIES:
        value_array[1] = value_array[0];
        break;
    }

    return result;
  }
);

DEFINE_METHODS(
  GlobalSet.prototype,
  {
    entries() {
      if (!IS_SET(this)) {
        throw %make_type_error(kIncompatibleMethodReceiver,
                            'Set.prototype.entries', this);
      }
      return new SetIterator(this, ITERATOR_KIND_ENTRIES);
    }

    values() {
      if (!IS_SET(this)) {
        throw %make_type_error(kIncompatibleMethodReceiver,
                            'Set.prototype.values', this);
      }
      return new SetIterator(this, ITERATOR_KIND_VALUES);
    }
  }
);

// -------------------------------------------------------------------

%SetCode(SetIterator, SetIteratorConstructor);
%FunctionSetInstanceClassName(SetIterator, 'Set Iterator');

var SetIteratorNext = SetIterator.prototype.next;

%AddNamedProperty(SetIterator.prototype, toStringTagSymbol,
    "Set Iterator", READ_ONLY | DONT_ENUM);

var SetValues = GlobalSet.prototype.values;
%AddNamedProperty(GlobalSet.prototype, "keys", SetValues, DONT_ENUM);
%AddNamedProperty(GlobalSet.prototype, iteratorSymbol, SetValues, DONT_ENUM);

// -------------------------------------------------------------------

function MapIteratorConstructor(map, kind) {
  %MapIteratorInitialize(this, map, kind);
}


DEFINE_METHOD(
  MapIterator.prototype,
  next() {
    if (!IS_MAP_ITERATOR(this)) {
      throw %make_type_error(kIncompatibleMethodReceiver,
                          'Map Iterator.prototype.next', this);
    }

    var value_array = [UNDEFINED, UNDEFINED];
    var result = %_CreateIterResultObject(value_array, false);
    switch (%MapIteratorNext(this, value_array)) {
      case 0:
        result.value = UNDEFINED;
        result.done = true;
        break;
      case ITERATOR_KIND_KEYS:
        result.value = value_array[0];
        break;
      case ITERATOR_KIND_VALUES:
        result.value = value_array[1];
        break;
      // ITERATOR_KIND_ENTRIES does not need any processing.
    }

    return result;
  }
);


DEFINE_METHODS(
  GlobalMap.prototype,
  {
    entries() {
      if (!IS_MAP(this)) {
        throw %make_type_error(kIncompatibleMethodReceiver,
                            'Map.prototype.entries', this);
      }
      return new MapIterator(this, ITERATOR_KIND_ENTRIES);
    }

    keys() {
      if (!IS_MAP(this)) {
        throw %make_type_error(kIncompatibleMethodReceiver,
                            'Map.prototype.keys', this);
      }
      return new MapIterator(this, ITERATOR_KIND_KEYS);
    }

    values() {
      if (!IS_MAP(this)) {
        throw %make_type_error(kIncompatibleMethodReceiver,
                            'Map.prototype.values', this);
      }
      return new MapIterator(this, ITERATOR_KIND_VALUES);
    }
  }
);


// -------------------------------------------------------------------

%SetCode(MapIterator, MapIteratorConstructor);
%FunctionSetInstanceClassName(MapIterator, 'Map Iterator');

var MapIteratorNext = MapIterator.prototype.next;

%AddNamedProperty(MapIterator.prototype, toStringTagSymbol,
    "Map Iterator", READ_ONLY | DONT_ENUM);


var MapEntries = GlobalMap.prototype.entries;
%AddNamedProperty(GlobalMap.prototype, iteratorSymbol, MapEntries, DONT_ENUM);

// -------------------------------------------------------------------
// Exports

utils.Export(function(to) {
  to.MapEntries = MapEntries;
  to.MapIteratorNext = MapIteratorNext;
  to.SetIteratorNext = SetIteratorNext;
  to.SetValues = SetValues;
});

})
