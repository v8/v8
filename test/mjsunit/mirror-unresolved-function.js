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

// Flags: --expose-debug-as debug
// Test the mirror object for unresolved functions.

var mirror = new debug.UnresolvedFunctionMirror("f");
var json = mirror.toJSONProtocol(true);

// Check the mirror hierachy for unresolved functions.
assertTrue(mirror instanceof debug.Mirror);
assertTrue(mirror instanceof debug.ValueMirror);
assertTrue(mirror instanceof debug.ObjectMirror);
assertTrue(mirror instanceof debug.FunctionMirror);

// Check the mirror properties for unresolved functions.
assertTrue(mirror.isUnresolvedFunction());
assertEquals('function', mirror.type());
assertFalse(mirror.isPrimitive());
assertEquals("Function", mirror.className());
assertEquals("f", mirror.name());
assertFalse(mirror.resolved());
assertEquals(void 0, mirror.source());
assertEquals('undefined', mirror.constructorFunction().type());
assertEquals('undefined', mirror.protoObject().type());
assertEquals('undefined', mirror.prototypeObject().type());
  
// Parse JSON representation of unresolved functions and check.
/*var fromJSON = eval('(' + json + ')');
assertEquals('function', fromJSON.type);
assertEquals('Function', fromJSON.className);
assertEquals('undefined', fromJSON.constructorFunction.type);
assertEquals('undefined', fromJSON.protoObject.type);
assertEquals('undefined', fromJSON.prototypeObject.type);
assertFalse(fromJSON.resolved);
assertEquals("f", fromJSON.name);
assertEquals(void 0, fromJSON.source);*/
