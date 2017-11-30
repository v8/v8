// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var string_len = 0x0ffffff0 - 19;

print("Allocating backing store");
var backing = new ArrayBuffer(string_len + 19);

print("Allocating typed array buffer");
var buffer = new Uint8Array(backing);

print("Filling...");
buffer.fill(0x41);

print("Setting up array buffer");
// Magic
buffer.set([0x00, 0x61, 0x73, 0x6D], 0);
// Version
buffer.set([0x01, 0x00, 0x00, 0x00], 4);
// kUnknownSection (0)
buffer.set([0], 8);
// Section length
buffer.set([0x80, 0x80, 0x80, 0x80, 0x00],  9);
// Name length
buffer.set([0xDE, 0xFF, 0xFF, 0x7F], 14);

print("Parsing module...");
var m = new WebAssembly.Module(buffer);

print("Triggering!");
var c = WebAssembly.Module.customSections(m, "A".repeat(string_len + 1));
assertEquals(0, c.length);
