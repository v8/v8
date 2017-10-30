// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils) {

"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var GlobalWeakMap = global.WeakMap;
var GlobalWeakSet = global.WeakSet;
var MathRandom = global.Math.random;

// -------------------------------------------------------------------
// Harmony WeakMap

function WeakMapConstructor(iterable) {
  if (IS_UNDEFINED(new.target)) {
    throw %make_type_error(kConstructorNotFunction, "WeakMap");
  }

  %WeakCollectionInitialize(this);

  if (!IS_NULL_OR_UNDEFINED(iterable)) {
    var adder = this.set;
    if (!IS_CALLABLE(adder)) {
      throw %make_type_error(kPropertyNotFunction, adder, 'set', this);
    }
    for (var nextItem of iterable) {
      if (!IS_RECEIVER(nextItem)) {
        throw %make_type_error(kIteratorValueNotAnObject, nextItem);
      }
      %_Call(adder, this, nextItem[0], nextItem[1]);
    }
  }
}


// -------------------------------------------------------------------

%SetCode(GlobalWeakMap, WeakMapConstructor);
%FunctionSetLength(GlobalWeakMap, 0);

// -------------------------------------------------------------------
// Harmony WeakSet

function WeakSetConstructor(iterable) {
  if (IS_UNDEFINED(new.target)) {
    throw %make_type_error(kConstructorNotFunction, "WeakSet");
  }

  %WeakCollectionInitialize(this);

  if (!IS_NULL_OR_UNDEFINED(iterable)) {
    var adder = this.add;
    if (!IS_CALLABLE(adder)) {
      throw %make_type_error(kPropertyNotFunction, adder, 'add', this);
    }
    for (var value of iterable) {
      %_Call(adder, this, value);
    }
  }
}


// -------------------------------------------------------------------

%SetCode(GlobalWeakSet, WeakSetConstructor);
%FunctionSetLength(GlobalWeakSet, 0);

})
