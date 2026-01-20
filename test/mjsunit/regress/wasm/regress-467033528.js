// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Regression test for crbug.com/467033528
// WebAssembly.Module should work with ArrayBuffer from Uint8Array.fromBase64

// A minimal valid WebAssembly module (magic + version only)
// Bytes: [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]
const base64 = "AGFzbQEAAAA=";

// Test 1: Creating WebAssembly.Module from Uint8Array.fromBase64().buffer
// This test ensures the ArrayBuffer from Uint8Array.fromBase64 has consistent
// byte_length between JSArrayBuffer and BackingStore.
const fromBase64 = Uint8Array.fromBase64(base64);
assertEquals(8, fromBase64.length);
assertEquals(8, fromBase64.buffer.byteLength);

// This should not throw
const module = new WebAssembly.Module(fromBase64.buffer);
assertTrue(module instanceof WebAssembly.Module);

// Test 2: Verify the module is valid by instantiating it
const instance = new WebAssembly.Instance(module);
assertTrue(instance instanceof WebAssembly.Instance);
