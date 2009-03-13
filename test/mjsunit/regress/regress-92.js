// Copyright 2009 the V8 project authors. All rights reserved.
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

function introduceSetter(useProto, Constructor) {
  // Before introducing the setter this test expects 'y' to be set
  // normally.  Afterwards setting 'y' will throw an exception.
  var runTest = new Function("Constructor",
    "var p = new Constructor(3); p.y = 4; assertEquals(p.y, 4);");

  // Create the prototype object first because defining a setter should
  // clear inline caches.
  if (useProto) {
    var newProto = { };
    newProto.__defineSetter__('y', function () { throw signal; });
  }

  // Ensure that monomorphic ics have been set up.
  runTest(Constructor);
  runTest(Constructor);

  var signal = "was called";
  if (useProto) {
    // Either introduce the setter through __proto__...
    Constructor.prototype.__proto__ = newProto;
  } else {
    // ...or introduce it directly using __defineSetter__.
    Constructor.prototype.__defineSetter__('y', function () { throw signal; });
  }

  // Now setting 'y' should throw an exception.
  try {
    runTest(Constructor);
    fail("Accessor was not called.");
  } catch (e) {
    assertEquals(e, signal);
  }

}

introduceSetter(false, function FastCase(x) { this.x = x; });
introduceSetter(true, function FastCase(x) { this.x = x; });
introduceSetter(false, function SlowCase(x) { this.x = x; delete this.x; });
introduceSetter(true, function SlowCase(x) { this.x = x; delete this.x; });
