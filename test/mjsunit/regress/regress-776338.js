// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const obj = {};
Object.defineProperty(obj, 'value', {
  enumerable: true,
  configurable: true,
  get: function() {
    assertUnreachable();
  }
});

const get = function(target, prop) {
  if (prop === 'value') {
    return 'yep';
  }
  return Reflect.get(target, prop)
};

const proxy = new Proxy(obj, { get });
assertEquals('yep', proxy.value);
