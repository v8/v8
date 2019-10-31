// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var v_this = this;
function f() {
  var obj = {y: 0};
  var proxy = new Proxy(obj, {
    has() { y; },
  });
  Object.setPrototypeOf(v_this, proxy);
  x;
}
assertThrows(f, RangeError);

function f_store() {
  var obj = {z: 0};
  var proxy = new Proxy(obj, {
    has() { z = 10; },
  });
  Object.setPrototypeOf(v_this, proxy);
  z = 10;
}
assertThrows(f_store, RangeError);

function f_set() {
  var obj = {z: 0};
  var proxy = new Proxy(obj, {
    has() {return true; },
    set() { z = x; }
  });
  Object.setPrototypeOf(v_this, proxy);
  z = 10;
}
assertThrows(f_set, RangeError);
