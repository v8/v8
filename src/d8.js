// Copyright 2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

String.prototype.startsWith = function (str) {
  if (str.length > this.length) {
    return false;
  }
  return this.substr(0, str.length) == str;
};

function ToInspectableObject(obj) {
  if (!obj && typeof obj === 'object') {
    return UNDEFINED;
  } else {
    return Object(obj);
  }
}

function GetCompletions(global, last, full) {
  var full_tokens = full.split();
  full = full_tokens.pop();
  var parts = full.split('.');
  parts.pop();
  var current = global;
  for (var i = 0; i < parts.length; i++) {
    var part = parts[i];
    var next = current[part];
    if (!next) {
      return [];
    }
    current = next;
  }
  var result = [];
  current = ToInspectableObject(current);
  while (typeof current !== 'undefined') {
    var mirror = new $debug.ObjectMirror(current);
    var properties = mirror.properties();
    for (var i = 0; i < properties.length; i++) {
      var name = properties[i].name();
      if (typeof name === 'string' && name.startsWith(last)) {
        result.push(name);
      }
    }
    current = ToInspectableObject(Object.getPrototypeOf(current));
  }
  return result;
}

// A more universal stringify that supports more types than JSON.
// Used by the d8 shell to output results.
var stringifyDepthLimit = 4;  // To avoid crashing on cyclic objects

function Stringify(x, depth) {
  if (depth === undefined)
    depth = stringifyDepthLimit;
  else if (depth === 0)
    return "*";
  switch (typeof x) {
    case "undefined":
      return "undefined";
    case "boolean":
    case "number":
    case "function":
      return x.toString();
    case "string":
      return "\"" + x.toString() + "\"";
    case "symbol":
      return x.toString();
    case "object":
      if (IS_NULL(x)) return "null";
      if (x.constructor && x.constructor.name === "Array") {
        var elems = [];
        for (var i = 0; i < x.length; ++i) {
          elems.push(
            {}.hasOwnProperty.call(x, i) ? Stringify(x[i], depth - 1) : "");
        }
        return "[" + elems.join(", ") + "]";
      }
      try {
        var string = String(x);
        if (string && string !== "[object Object]") return string;
      } catch(e) {}
      var props = [];
      var names = Object.getOwnPropertyNames(x);
      names = names.concat(Object.getOwnPropertySymbols(x));
      for (var i in names) {
        var name = names[i];
        var desc = Object.getOwnPropertyDescriptor(x, name);
        if (IS_UNDEFINED(desc)) continue;
        if (IS_SYMBOL(name)) name = "[" + Stringify(name) + "]";
        if ("value" in desc) {
          props.push(name + ": " + Stringify(desc.value, depth - 1));
        }
        if (desc.get) {
          var getter = Stringify(desc.get);
          props.push("get " + name + getter.slice(getter.indexOf('(')));
        }
        if (desc.set) {
          var setter = Stringify(desc.set);
          props.push("set " + name + setter.slice(setter.indexOf('(')));
        }
      }
      return "{" + props.join(", ") + "}";
    default:
      return "[crazy non-standard value]";
  }
}
