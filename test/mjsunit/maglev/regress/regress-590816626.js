// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const realm = Realm.createAllowCrossRealmAccess();
const OtherCtor = Realm.eval(realm, '(function OtherCtor() {})');
const OtherArrow = Realm.eval(realm, '(() => 1)');
const OtherBoundArrow = Realm.eval(realm, '(() => 1).bind()');
const OtherFunctionPrototype = Realm.eval(realm, 'Function.prototype');
const OtherTypeError = Realm.eval(realm, 'TypeError');

const { proxy: revokedProxy, revoke } = Proxy.revocable({}, {});
revoke();

// 1. Constant cross-realm constructor with a revoked proxy on the LHS.
function testRevokedProxyConstant(o) {
  return o instanceof OtherCtor;
}
%PrepareFunctionForOptimization(testRevokedProxyConstant);
assertFalse(testRevokedProxyConstant({}));
assertThrows(() => testRevokedProxyConstant(revokedProxy), OtherTypeError);
%OptimizeFunctionOnNextCall(testRevokedProxyConstant);
assertFalse(testRevokedProxyConstant({}));
assertThrows(() => testRevokedProxyConstant(revokedProxy), OtherTypeError);

// 2. Constant cross-realm arrow function (no .prototype).
function testArrowConstant(o) {
  return o instanceof OtherArrow;
}
%PrepareFunctionForOptimization(testArrowConstant);
assertFalse(testArrowConstant(5));
assertThrows(() => testArrowConstant({}), OtherTypeError);
%OptimizeFunctionOnNextCall(testArrowConstant);
assertFalse(testArrowConstant(5));
assertThrows(() => testArrowConstant({}), OtherTypeError);

// 3. Feedback-based (non-constant RHS) local constructor whose @@hasInstance
//    comes from another realm, with a revoked proxy on the LHS.
function LocalCtorWithOtherProto() {}
Object.setPrototypeOf(LocalCtorWithOtherProto, OtherFunctionPrototype);
function testRevokedProxyFeedback(o, ctor) {
  return o instanceof ctor;
}
%PrepareFunctionForOptimization(testRevokedProxyFeedback);
assertFalse(testRevokedProxyFeedback({}, LocalCtorWithOtherProto));
assertThrows(
    () => testRevokedProxyFeedback(revokedProxy, LocalCtorWithOtherProto),
    OtherTypeError);
%OptimizeFunctionOnNextCall(testRevokedProxyFeedback);
assertFalse(testRevokedProxyFeedback({}, LocalCtorWithOtherProto));
assertThrows(
    () => testRevokedProxyFeedback(revokedProxy, LocalCtorWithOtherProto),
    OtherTypeError);

// 4. Feedback-based (non-constant RHS) local arrow function whose @@hasInstance
//    comes from another realm.
const LocalArrowWithOtherProto = () => 1;
Object.setPrototypeOf(LocalArrowWithOtherProto, OtherFunctionPrototype);
function testArrowFeedback(o, ctor) {
  return o instanceof ctor;
}
%PrepareFunctionForOptimization(testArrowFeedback);
assertFalse(testArrowFeedback(5, LocalArrowWithOtherProto));
assertThrows(
    () => testArrowFeedback({}, LocalArrowWithOtherProto), OtherTypeError);
%OptimizeFunctionOnNextCall(testArrowFeedback);
assertFalse(testArrowFeedback(5, LocalArrowWithOtherProto));
assertThrows(
    () => testArrowFeedback({}, LocalArrowWithOtherProto), OtherTypeError);

// 5. Local bound function wrapping a cross-realm arrow/constructor.
const BoundOtherArrow = OtherArrow.bind();
function testBoundOtherArrow(o) {
  return o instanceof BoundOtherArrow;
}
%PrepareFunctionForOptimization(testBoundOtherArrow);
assertFalse(testBoundOtherArrow(5));
assertThrows(() => testBoundOtherArrow({}), OtherTypeError);
%OptimizeFunctionOnNextCall(testBoundOtherArrow);
assertFalse(testBoundOtherArrow(5));
assertThrows(() => testBoundOtherArrow({}), OtherTypeError);

const BoundOtherCtor = OtherCtor.bind();
function testBoundOtherCtorRevoked(o) {
  return o instanceof BoundOtherCtor;
}
%PrepareFunctionForOptimization(testBoundOtherCtorRevoked);
assertFalse(testBoundOtherCtorRevoked({}));
assertThrows(() => testBoundOtherCtorRevoked(revokedProxy), OtherTypeError);
%OptimizeFunctionOnNextCall(testBoundOtherCtorRevoked);
assertFalse(testBoundOtherCtorRevoked({}));
assertThrows(() => testBoundOtherCtorRevoked(revokedProxy), OtherTypeError);

// 6. Cross-realm bound function.
function testOtherBoundArrow(o) {
  return o instanceof OtherBoundArrow;
}
%PrepareFunctionForOptimization(testOtherBoundArrow);
assertFalse(testOtherBoundArrow(5));
assertThrows(() => testOtherBoundArrow({}), OtherTypeError);
%OptimizeFunctionOnNextCall(testOtherBoundArrow);
assertFalse(testOtherBoundArrow(5));
assertThrows(() => testOtherBoundArrow({}), OtherTypeError);

// 7. Cross-realm function with null prototype (no @@hasInstance): falls back
//    to OrdinaryHasInstance in the caller's realm.
const OtherArrowNoHasInstance = Realm.eval(realm, '(() => 1)');
Object.setPrototypeOf(OtherArrowNoHasInstance, null);
function testOtherArrowNoHasInstance(o) {
  return o instanceof OtherArrowNoHasInstance;
}
%PrepareFunctionForOptimization(testOtherArrowNoHasInstance);
assertFalse(testOtherArrowNoHasInstance(5));
assertThrows(() => testOtherArrowNoHasInstance({}), TypeError);
%OptimizeFunctionOnNextCall(testOtherArrowNoHasInstance);
assertFalse(testOtherArrowNoHasInstance(5));
assertThrows(() => testOtherArrowNoHasInstance({}), TypeError);
