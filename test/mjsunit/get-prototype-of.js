// Copyright 2010 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

assertThrows(function() {
  Object.getPrototypeOf(undefined);
}, TypeError);

assertThrows(function() {
  Object.getPrototypeOf(null);
}, TypeError);


function F(){};
var y = new F();

assertSame(Object.getPrototypeOf(y), F.prototype);
assertSame(Object.getPrototypeOf(F), Function.prototype);

assertSame(Object.getPrototypeOf({x: 5}), Object.prototype);
assertSame(Object.getPrototypeOf({x: 5, __proto__: null}), null);

assertSame(Object.getPrototypeOf([1, 2]), Array.prototype);


assertSame(Object.getPrototypeOf(1), Number.prototype);
assertSame(Object.getPrototypeOf(true), Boolean.prototype);
assertSame(Object.getPrototypeOf(false), Boolean.prototype);
assertSame(Object.getPrototypeOf('str'), String.prototype);
assertSame(Object.getPrototypeOf(Symbol()), Symbol.prototype);


// Builtin constructors.
var functions = [
  Array,
  ArrayBuffer,
  Boolean,
  // DataView,
  Date,
  Error,
  EvalError,
  Float32Array,
  Float64Array,
  Function,
  Int16Array,
  Int32Array,
  Int8Array,
  Map,
  Number,
  Object,
  // Promise,
  RangeError,
  ReferenceError,
  RegExp,
  Set,
  String,
  // Symbol, not constructible
  SyntaxError,
  TypeError,
  URIError,
  Uint16Array,
  Uint32Array,
  Uint8Array,
  Uint8ClampedArray,
  WeakMap,
  WeakSet,
];

for (var f of functions) {
  assertSame(Object.getPrototypeOf(f), Function.prototype);
  assertSame(Object.getPrototypeOf(new f), f.prototype);
}

var p = new Promise(function() {});
assertSame(Object.getPrototypeOf(p), Promise.prototype);

var dv = new DataView(new ArrayBuffer());
assertSame(Object.getPrototypeOf(dv), DataView.prototype);
