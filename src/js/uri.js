// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains support for URI manipulations written in
// JavaScript.

(function(global, utils) {

"use strict";

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Define exported functions.

// ECMA-262 - B.2.1.
function URIEscapeJS(s) {
  return %URIEscape(s);
}

// ECMA-262 - B.2.2.
function URIUnescapeJS(s) {
  return %URIUnescape(s);
}

// -------------------------------------------------------------------
// Install exported functions.

// Set up non-enumerable URI functions on the global object and set
// their names.
utils.InstallFunctions(global, DONT_ENUM, [
  "escape", URIEscapeJS,
  "unescape", URIUnescapeJS
]);

})
