// Flags: --harmony-proxies

// Copyright 2008 the V8 project authors. All rights reserved.
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



// Getters.

function TestGet(handler) {
  var o = Proxy.create(handler)
  assertEquals(42, o.a)
  assertEquals(42, o["b"])
//  assertEquals(Object.getOwnPropertyDescriptor(o, "b").value, 42)
}

TestGet({get: function(r, k) { return 42 }})
TestGet({getPropertyDescriptor: function(k) { return {value: 42} }})
TestGet({getPropertyDescriptor: function(k) { return {get value() { return 42 }} }})
TestGet({get: undefined, getPropertyDescriptor: function(k) { return {value: 42} }})

TestGet(Proxy.create({get: function(pr, pk) { return function(r, k) { return 42 } }}))



// Setters.

var key
var val
function TestSet(handler) {
  var o = Proxy.create(handler)
  assertEquals(42, o.a = 42)
  assertEquals("a", key)
  assertEquals(42, val)
  assertEquals(43, o["b"] = 43)
  assertEquals("b", key)
  assertEquals(43, val)
//  assertTrue(Object.defineProperty(o, "c", {value: 44}))
//  assertEquals("c", key)
//  assertEquals(44, val)
}

TestSet({set: function(r, k, v) { key = k; val = v; return true }})
TestSet({getOwnPropertyDescriptor: function(k) { return {writable: true} },
         defineProperty: function(k, desc) { key = k, val = desc.value }})
TestSet({getOwnPropertyDescriptor: function(k) { return {get writable() { return true }} },
         defineProperty: function(k, desc) { key = k, val = desc.value }})
TestSet({getOwnPropertyDescriptor: function(k) { return {set: function(v) { key = k, val = v }} }})
TestSet({getOwnPropertyDescriptor: function(k) { return null },
         getPropertyDescriptor: function(k) { return {writable: true} },
         defineProperty: function(k, desc) { key = k, val = desc.value }})
TestSet({getOwnPropertyDescriptor: function(k) { return null },
         getPropertyDescriptor: function(k) { return {get writable() { return true }} },
         defineProperty: function(k, desc) { key = k, val = desc.value }})
TestSet({getOwnPropertyDescriptor: function(k) { return null },
         getPropertyDescriptor: function(k) { return {set: function(v) { key = k, val = v }} }})
TestSet({getOwnPropertyDescriptor: function(k) { return null },
         getPropertyDescriptor: function(k) { return null },
         defineProperty: function(k, desc) { key = k, val = desc.value }})

TestSet(Proxy.create({get: function(pr, pk) { return function(r, k, v) { key = k; val = v; return true } }}))



// Comparison.

var o1 = Proxy.create({})
var o2 = Proxy.create({})

assertTrue(o1 == o1)
assertTrue(o2 == o2)
assertTrue(!(o1 == o2))
assertTrue(!(o1 == {}))
assertTrue(!({} == o2))
assertTrue(!({} == {}))

assertTrue(o1 === o1)
assertTrue(o2 === o2)
assertTrue(!(o1 === o2))
assertTrue(!(o1 === {}))
assertTrue(!({} === o2))
assertTrue(!({} === {}))



// Type.

assertEquals("object", typeof Proxy.create({}))
assertTrue(typeof Proxy.create({}) == "object")
assertTrue("object" == typeof Proxy.create({}))

// No function proxies yet.
