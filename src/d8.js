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

// How crappy is it that I have to implement completely basic stuff
// like this myself?  Answer: very.
String.prototype.startsWith = function (str) {
  if (str.length > this.length)
    return false;
  return this.substr(0, str.length) == str;
};

function ToInspectableObject(obj) {
  if (!obj && typeof obj === 'object') {
    return void 0;
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
    if (!next)
      return [];
    current = next;
  }
  var result = [];
  current = ToInspectableObject(current);
  while (typeof current !== 'undefined') {
    var mirror = new $debug.ObjectMirror(current);
    var properties = mirror.properties();
    for (var i = 0; i < properties.length; i++) {
      var name = properties[i].name();
      if (typeof name === 'string' && name.startsWith(last))
        result.push(name);
    }
    current = ToInspectableObject(current.__proto__);
  }
  return result;
}
