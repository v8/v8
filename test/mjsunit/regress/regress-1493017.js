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

// Test for collection of abandoned maps

// This test makes a wide, shallow tree of map transitions and maps
// by adding the properties "a" through "j" in a pseudorandom order
// to a new A() object.  This should create map transitions forming
// a partial denary tree.  These objects only stick around for about
// 1000 iterations, with each iteration creating a new object.  Therefore,
// some of the maps going to leaves will become abandoned.
// There are still map transitions going to them though, so
// only the new map-collection code will remove them.

// Every 101 object creations, the object is created again, and tested
// after each property addition to make sure that no map transitions
// are visible as properties.  This is a regression test for a bug.

// Flags: --expose-gc --collect-maps

function dotest() {
  function A() {
  }

  function B() {
    this.x = 3;
  }

  var a_B = new B();
  var r = 1;
  var i = 0;
  var holder = new Array();
  while (i++ < 2001) {
    if (i == 1400) {
      gc();
    }
    var s = r % 100000000;
    var obj = new A();
    holder[i % 1000] = obj;
    while (s > 0) {
      var property_name = String.fromCharCode(s % 10 + 97);
      obj[property_name] = a_B;
      s = s / 10;
    }
    if (i % 101 == 0) {
      // Check that all object maps have no undefined properties
      s = r % 100000000;
      obj = new A();
      while (s > 0) {
        for (var p in obj) {
          assertEquals(a_B, obj[p] );
        }
        property_name = String.fromCharCode(s % 10 + 97);
        obj[property_name] = a_B;
        s = s / 10;
      }
    }
    r = r * 7 % 100000000;
  }
}

dotest();
