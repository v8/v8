// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils) {

"use strict";

%CheckIsBootstrapping();

// ----------------------------------------------------------------------------
// Imports
//
var GlobalProxy = global.Proxy;
var GlobalFunction = global.Function;
var GlobalObject = global.Object;
var MakeTypeError;

utils.Import(function(from) {
  MakeTypeError = from.MakeTypeError;
});

//----------------------------------------------------------------------------

function ProxyCreateRevocable(target, handler) {
  var p = new GlobalProxy(target, handler);
  return {proxy: p, revoke: () => %RevokeProxy(p)};
}

// -------------------------------------------------------------------
// Proxy Builtins

function DerivedConstructTrap(callTrap) {
  return function() {
    var proto = this.prototype
    if (!IS_SPEC_OBJECT(proto)) proto = GlobalObject.prototype
    var obj = { __proto__: proto };
    var result = %Apply(callTrap, obj, arguments, 0, %_ArgumentsLength());
    return IS_SPEC_OBJECT(result) ? result : obj
  }
}

function DelegateCallAndConstruct(callTrap, constructTrap) {
  return function() {
    return %Apply(IS_UNDEFINED(new.target) ? callTrap : constructTrap,
                  this, arguments, 0, %_ArgumentsLength())
  }
}

function DerivedSetTrap(receiver, name, val) {
  var desc = this.getOwnPropertyDescriptor(name)
  if (desc) {
    if ('writable' in desc) {
      if (desc.writable) {
        desc.value = val
        this.defineProperty(name, desc)
        return true
      } else {
        return false
      }
    } else { // accessor
      if (desc.set) {
        // The proposal says: desc.set.call(receiver, val)
        %_Call(desc.set, receiver, val)
        return true
      } else {
        return false
      }
    }
  }
  desc = this.getPropertyDescriptor(name)
  if (desc) {
    if ('writable' in desc) {
      if (desc.writable) {
        // fall through
      } else {
        return false
      }
    } else { // accessor
      if (desc.set) {
        // The proposal says: desc.set.call(receiver, val)
        %_Call(desc.set, receiver, val)
        return true
      } else {
        return false
      }
    }
  }
  this.defineProperty(name, {
    value: val,
    writable: true,
    enumerable: true,
    configurable: true});
  return true;
}

function DerivedHasOwnTrap(name) {
  return !!this.getOwnPropertyDescriptor(name)
}


// Implements part of ES6 9.5.11 Proxy.[[Enumerate]]:
// Call the trap, which should return an iterator, exhaust the iterator,
// and return an array containing the values.
function ProxyEnumerate(trap, handler, target) {
  // 7. Let trapResult be ? Call(trap, handler, «target»).
  var trap_result = %_Call(trap, handler, target);
  // 8. If Type(trapResult) is not Object, throw a TypeError exception.
  if (!IS_SPEC_OBJECT(trap_result)) {
    throw MakeTypeError(kProxyHandlerReturned, handler, "non-Object",
                        "enumerate");
  }
  // 9. Return trapResult.
  var result = [];
  for (var it = trap_result.next(); !it.done; it = trap_result.next()) {
    var key = it.value;
    // Not yet spec'ed as of 2015-11-25, but will be spec'ed soon:
    // If the iterator returns a non-string value, throw a TypeError.
    if (!IS_STRING(key)) {
      throw MakeTypeError(kProxyHandlerReturned, handler, "non-String",
                          "enumerate-iterator");
    }
    result.push(key);
  }
  return result;
}

//-------------------------------------------------------------------

//Set up non-enumerable properties of the Proxy object.
utils.InstallFunctions(GlobalProxy, DONT_ENUM, [
  "revocable", ProxyCreateRevocable
]);

// -------------------------------------------------------------------
// Exports

utils.Export(function(to) {
  to.ProxyDelegateCallAndConstruct = DelegateCallAndConstruct;
  to.ProxyDerivedHasOwnTrap = DerivedHasOwnTrap;
});

%InstallToContext([
  "proxy_enumerate", ProxyEnumerate,
]);

})
