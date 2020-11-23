// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Make sure it won't crash
var s = "b".repeat(0xAAAFFFF);
assertThrows(() => new Intl.ListFormat().format(Array(16).fill(s)).length,
    TypeError);
