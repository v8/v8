// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils) {

"use strict";

%CheckIsBootstrapping();

// -----------------------------------------------------------------------
// Utils

var imports = UNDEFINED;
var exports = UNDEFINED;


utils.Export = function Export(f) {
  f.next = exports;
  exports = f;
};


utils.Import = function Import(f) {
  f.next = imports;
  imports = f;
};

// -----------------------------------------------------------------------
// To be called by bootstrapper

utils.PostNatives = function() {
  %CheckIsBootstrapping();

  var container = {};
  for ( ; !IS_UNDEFINED(exports); exports = exports.next) exports(container);
  for ( ; !IS_UNDEFINED(imports); imports = imports.next) imports(container);

  var expose_to_experimental = [
    "MathMax",
    "MathMin",
  ];
  var experimental = {};
  %OptimizeObjectForAddingMultipleProperties(
      experimental, expose_to_experimental.length);
  for (var key of expose_to_experimental) experimental[key] = container[key];
  %ToFastProperties(experimental);
  container = UNDEFINED;

  utils.Export = UNDEFINED;
  utils.PostNatives = UNDEFINED;
  utils.Import = function ImportFromExperimental(f) {
    f(experimental);
  };
};

})
