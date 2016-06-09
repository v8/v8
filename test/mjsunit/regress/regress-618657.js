// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --ignition --ignition-generators --ignition-filter=-foo

function* foo() { yield 42 }
var g = foo();
assertEquals(42, g.next().value);
assertEquals(true, g.next().done);
