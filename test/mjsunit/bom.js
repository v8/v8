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

// According to section 7.1 of EcmaScript-262 format control characters
// should be removed before parsing. We're following the discussion at
// https://bugs.webkit.org/show_bug.cgi?id=4931 in only removing the BOM.
// See also https://bugzilla.mozilla.org/show_bug.cgi?id=274152.

// Ignores BOM (and only BOM) in string literals.
var format_controls =
  eval('"\uFEFF\u200F\u200E\u00AD\u2062\u200D\u200C\u200B"');
assertEquals('\u200F\u200E\u00AD\u2062\u200D\u200C\u200B', 
             format_controls);

// Ignores BOM in identifiers.
eval('var x\uFEFFy = 7');
assertEquals(7, xy);

// Doesn't ignore non-BOM format control characters.
assertThrows('var y\u200Fx = 7');
assertThrows('var y\u200Ex = 7');
assertThrows('var y\u20ADx = 7');
assertThrows('var y\u2062x = 7');
assertThrows('var y\u200Dx = 7');
assertThrows('var y\u200Cx = 7');
assertThrows('var y\u200Bx = 7');
