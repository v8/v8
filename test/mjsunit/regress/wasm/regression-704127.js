// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

assertThrows( () => {
var v4 = {};
Object.prototype.__defineGetter__(0, function() {
this[0] = 1;
})
Object.prototype.__defineSetter__(0, function() {
    WebAssembly.compile();
this[0] = v4;
});
 v14 = new Intl.Collator();
var v34 = eval("");}, RangeError);
