// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var a = [1, 2, 3];
Object.defineProperty(a, '1', {
  get: function() { delete this[1]; return undefined; },
  configurable: true
});
var s = a.slice(1);
assertTrue('0' in s);

// Sparse case should hit the same code as above due to presence of the getter.
a = [1, 2, 3];
a[0xffff] = 4;
Object.defineProperty(a, '1', {
  get: function() { delete this[1]; return undefined; },
  configurable: true
});
s = a.slice(1);
assertTrue('0' in s);
