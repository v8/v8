// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --expose-wasm

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

var wire_bytes = readbuffer("test/mjsunit/regress/wasm/665402.wasm");
var module = new WebAssembly.Module(wire_bytes);
assertTrue(module != undefined);
