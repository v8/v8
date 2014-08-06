// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

// This file relies on the fact that the following declaration has been made.
// var $Array = global.Array;
// var $Symbol = global.Symbol;

function ExtendSymbol() {
  %CheckIsBootstrapping();
  InstallConstants($Symbol, $Array(
    "unscopables", symbolUnscopables
  ));
}

ExtendSymbol();


var arrayUnscopables = {
  __proto__: null,
  copyWithin: true,
  entries: true,
  fill: true,
  find: true,
  findIndex: true,
  keys: true,
  values: true,
};


function ExtendArrayPrototype() {
  %CheckIsBootstrapping();
  %AddNamedProperty($Array.prototype, symbolUnscopables, arrayUnscopables,
                    DONT_ENUM | READ_ONLY);
}

ExtendArrayPrototype();
