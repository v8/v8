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
// the import is not available until the very end.
function Import(f) {
  f.next = imports;
  imports = f;
}


// -----------------------------------------------------------------------
// To be called by bootstrapper

function PostNatives(utils) {
  %CheckIsBootstrapping();

  for ( ; !IS_UNDEFINED(imports); imports = imports.next) {
    imports(exports_container);
  }

  exports_container = UNDEFINED;
  utils.Export = UNDEFINED;
  utils.Import = UNDEFINED;
  utils.PostNatives = UNDEFINED;
}

// -----------------------------------------------------------------------

%OptimizeObjectForAddingMultipleProperties(utils, 14);

utils.Import = Import;
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
