// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


(function testNonObjectTargetTypes() {
  assertThrows(function(){ new Proxy(undefined, {}); }, TypeError);

  assertThrows(function(){ new Proxy(null, {}); }, TypeError);

  assertThrows(function(){ new Proxy('', {}); }, TypeError);

  assertThrows(function(){ new Proxy(0, {}); }, TypeError);

  assertThrows(function(){ new Proxy(0.5, {}); }, TypeError);

  assertThrows(function(){ new Proxy(false, {}); }, TypeError);
})();


(function testRevokedTarget() {
  var revocable = Proxy.revocable({}, {});
  revocable.revoke();

  assertThrows(function(){ new Proxy(revocable.proxy, {}); }, TypeError);
})();


(function testNonObjectHandlerTypes() {
  assertThrows(function(){ new Proxy({}, undefined); }, TypeError);

  assertThrows(function(){ new Proxy({}, null); }, TypeError);

  assertThrows(function(){ new Proxy({}, ''); }, TypeError);

  assertThrows(function(){ new Proxy({}, 0); }, TypeError);

  assertThrows(function(){ new Proxy({}, 0.5); }, TypeError);

  assertThrows(function(){ new Proxy({}, false); }, TypeError);
})();


(function testRevokedHandler() {
  var revocable = Proxy.revocable({}, {});
  revocable.revoke();

  assertThrows(function(){ new Proxy({}, revocable.proxy); }, TypeError);
})();
