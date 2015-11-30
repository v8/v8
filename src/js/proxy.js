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
var ToNameArray;

utils.Import(function(from) {
  MakeTypeError = from.MakeTypeError;
  ToNameArray = from.ToNameArray;
});

//----------------------------------------------------------------------------

function ProxyCreate(target, handler) {
  if (IS_UNDEFINED(new.target)) {
    throw MakeTypeError(kConstructorNotFunction, "Proxy");
  }
  return %CreateJSProxy(target, handler);
}

function ProxyCreateFunction(handler, callTrap, constructTrap) {
  if (!IS_SPEC_OBJECT(handler))
    throw MakeTypeError(kProxyHandlerNonObject, "createFunction")
  if (!IS_CALLABLE(callTrap))
    throw MakeTypeError(kProxyTrapFunctionExpected, "call")
  if (IS_UNDEFINED(constructTrap)) {
    constructTrap = DerivedConstructTrap(callTrap)
  } else if (IS_CALLABLE(constructTrap)) {
    // Make sure the trap receives 'undefined' as this.
    var construct = constructTrap
    constructTrap = function() {
      return %Apply(construct, UNDEFINED, arguments, 0, %_ArgumentsLength());
    }
  } else {
    throw MakeTypeError(kProxyTrapFunctionExpected, "construct")
  }
  return %CreateJSFunctionProxy(
    {}, handler, callTrap, constructTrap, GlobalFunction.prototype)
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

function DerivedGetTrap(receiver, name) {
  var desc = this.getPropertyDescriptor(name)
  if (IS_UNDEFINED(desc)) { return desc }
  if ('value' in desc) {
    return desc.value
  } else {
    if (IS_UNDEFINED(desc.get)) { return desc.get }
    // The proposal says: desc.get.call(receiver)
    return %_Call(desc.get, receiver)
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

function DerivedKeysTrap() {
  var names = this.getOwnPropertyNames()
  var enumerableNames = []
  for (var i = 0, count = 0; i < names.length; ++i) {
    var name = names[i]
    if (IS_SYMBOL(name)) continue
    var desc = this.getOwnPropertyDescriptor(TO_STRING(name))
    if (!IS_UNDEFINED(desc) && desc.enumerable) {
      enumerableNames[count++] = names[i]
    }
  }
  return enumerableNames
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
%SetCode(GlobalProxy, ProxyCreate);

//Set up non-enumerable properties of the Proxy object.
utils.InstallFunctions(GlobalProxy, DONT_ENUM, [
  "createFunction", ProxyCreateFunction
]);

// -------------------------------------------------------------------
// Exports

utils.Export(function(to) {
  to.ProxyDelegateCallAndConstruct = DelegateCallAndConstruct;
  to.ProxyDerivedHasOwnTrap = DerivedHasOwnTrap;
  to.ProxyDerivedKeysTrap = DerivedKeysTrap;
});

%InstallToContext([
  "derived_get_trap", DerivedGetTrap,
  "derived_set_trap", DerivedSetTrap,
  "proxy_enumerate", ProxyEnumerate,
]);

})
