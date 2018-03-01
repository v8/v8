// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils) {

"use strict";

// -------------------------------------------------------------------
// Imports
var InternalArray = utils.InternalArray;

// -------------------------------------------------------------------

function SpreadIterable(collection) {
  if (IS_NULL_OR_UNDEFINED(collection)) {
    throw %make_type_error(kNotIterable, collection);
  }

  var args = new InternalArray();
  for (var value of collection) {
    args.push(value);
  }
  return args;
}

// ----------------------------------------------------------------------------
// Exports

%InstallToContext([
  "spread_iterable", SpreadIterable,
]);

})
