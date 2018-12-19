// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils, extrasUtils) {

"use strict";

%CheckIsBootstrapping();

// -----------------------------------------------------------------------
// Utils

var imports = UNDEFINED;
var exports_container = %ExportFromRuntime({});

// Export to other scripts.
function Export(f) {
  f(exports_container);
}


// Import from other scripts. The actual importing happens in PostNatives so
// that we can import from scripts executed later. However, that means that
// the import is not available until the very end. If the import needs to be
// available immediately, use ImportNow.
function Import(f) {
  f.next = imports;
  imports = f;
}


// Import immediately from exports of previous scripts. We need this for
// functions called during bootstrapping. Hooking up imports in PostNatives
// would be too late.
function ImportNow(name) {
  return exports_container[name];
}


var GlobalArray = global.Array;

// -----------------------------------------------------------------------
// To be called by bootstrapper

function PostNatives(utils) {
  %CheckIsBootstrapping();

  // -------------------------------------------------------------------
  // Array

  var iteratorSymbol = ImportNow("iterator_symbol");
  var unscopablesSymbol = ImportNow("unscopables_symbol");

  // Set up unscopable properties on the Array.prototype object.
  var unscopables = {
    __proto__: null,
    copyWithin: true,
    entries: true,
    fill: true,
    find: true,
    findIndex: true,
    includes: true,
    keys: true,
    values: true,
  };

  %ToFastProperties(unscopables);

  %AddNamedProperty(GlobalArray.prototype, unscopablesSymbol, unscopables,
                    DONT_ENUM | READ_ONLY);

  // Array prototype functions that return iterators. They are exposed to the
  // public API via Template::SetIntrinsicDataProperty().
  var ArrayEntries = GlobalArray.prototype.entries;
  var ArrayForEach = GlobalArray.prototype.forEach;
  var ArrayKeys = GlobalArray.prototype.keys;
  var ArrayValues = GlobalArray.prototype[iteratorSymbol];

  %InstallToContext([
    "array_entries_iterator", ArrayEntries,
    "array_for_each_iterator", ArrayForEach,
    "array_keys_iterator", ArrayKeys,
    "array_values_iterator", ArrayValues,
  ]);

  // -------------------------------------------------------------------

  for ( ; !IS_UNDEFINED(imports); imports = imports.next) {
    imports(exports_container);
  }

  exports_container = UNDEFINED;
  utils.Export = UNDEFINED;
  utils.Import = UNDEFINED;
  utils.ImportNow = UNDEFINED;
  utils.PostNatives = UNDEFINED;
}

// -----------------------------------------------------------------------

%OptimizeObjectForAddingMultipleProperties(utils, 14);

utils.Import = Import;
utils.ImportNow = ImportNow;
utils.Export = Export;
utils.PostNatives = PostNatives;

%ToFastProperties(utils);

// -----------------------------------------------------------------------

%OptimizeObjectForAddingMultipleProperties(extrasUtils, 11);

extrasUtils.logStackTrace = function logStackTrace() {
  %DebugTrace();
};

extrasUtils.log = function log() {
  let message = '';
  for (const arg of arguments) {
    message += arg;
  }

  %GlobalPrint(message);
};

// Extras need the ability to store private state on their objects without
// exposing it to the outside world.

extrasUtils.createPrivateSymbol = function createPrivateSymbol(name) {
  return %CreatePrivateSymbol(name);
};

// These functions are key for safe meta-programming:
// http://wiki.ecmascript.org/doku.php?id=conventions:safe_meta_programming
//
// Technically they could all be derived from combinations of
// Function.prototype.{bind,call,apply} but that introduces lots of layers of
// indirection.

extrasUtils.uncurryThis = function uncurryThis(func) {
  return function(thisArg, ...args) {
    return %reflect_apply(func, thisArg, args);
  };
};

extrasUtils.markPromiseAsHandled = function markPromiseAsHandled(promise) {
  %PromiseMarkAsHandled(promise);
};

extrasUtils.promiseState = function promiseState(promise) {
  return %PromiseStatus(promise);
};

// [[PromiseState]] values (for extrasUtils.promiseState())
// These values should be kept in sync with PromiseStatus in globals.h
extrasUtils.kPROMISE_PENDING = 0;
extrasUtils.kPROMISE_FULFILLED = 1;
extrasUtils.kPROMISE_REJECTED = 2;

%ToFastProperties(extrasUtils);

})
