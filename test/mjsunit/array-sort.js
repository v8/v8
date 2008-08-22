// Copyright 2008 Google Inc. All Rights Reserved.
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

// Test array sort.

// Test counter-intuitive default number sorting.
function TestNumberSort() {
  var a = [ 200, 45, 7 ];
  // Default sort calls toString on each element and orders
  // lexicographically.
  a.sort();
  assertArrayEquals([ 200, 45, 7 ], a);
  // Sort numbers by value using a compare functions.
  a.sort(function(x, y) { return x - y; });
  assertArrayEquals([ 7, 45, 200 ], a);
}

TestNumberSort();


// Test lexicographical string sorting.
function TestStringSort() {
  var a = [ "cc", "c", "aa", "a", "bb", "b", "ab", "ac" ];
  a.sort();
  assertArrayEquals([ "a", "aa", "ab", "ac", "b", "bb", "c", "cc" ], a);
}

TestStringSort();


// Test object sorting.  Calls toString on each element and sorts
// lexicographically.
function TestObjectSort() {
  var obj0 = { toString: function() { return "a"; } };
  var obj1 = { toString: function() { return "b"; } };
  var obj2 = { toString: function() { return "c"; } };
  var a = [ obj2, obj0, obj1 ];
  a.sort();
  assertArrayEquals([ obj0, obj1, obj2 ], a);
}

TestObjectSort();
