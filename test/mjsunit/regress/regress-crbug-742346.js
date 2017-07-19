// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --throws

TypeError.prototype.__defineGetter__("name", function() {
  this[1] = {};
  gc();
  new Uint16Array().reduceRight();
});
var v = WebAssembly.compile();
new TypeError().toString();
