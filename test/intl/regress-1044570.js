// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test no crash with array of very long string.
const num = 0xAFFFFFF;
const lfm = new Intl.ListFormat();
const s = 'a'.repeat(num);
// Ensure the following won't crash. The length will be 0
// because it will be too long to return correct result.
assertEquals(0, lfm.format(Array(16).fill(s)).length);
