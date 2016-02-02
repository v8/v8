// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types

function hello_typed_decl() {
  "use types";
  var msg: string = "Hello world!";
  return msg;
};

assertEquals("Hello world!", hello_typed_decl(), "Typed declaration");

assertThrows('(function hello_wrongly_typed() { var msg: string = "Hello world!"; return msg; })()', SyntaxError);
