// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// MODULE

import * as foo from "modules-namespace4.js";

assertSame(undefined, a);
assertThrows(() => b, ReferenceError);
assertThrows(() => c, ReferenceError);
assertEquals(45, d());

assertSame(undefined, foo.a);
assertThrows(() => foo.b, ReferenceError);
assertThrows(() => foo.c, ReferenceError);
assertEquals(45, foo.d());
assertThrows(() => foo.default, ReferenceError);
assertSame(undefined, foo.doesnotexist);

export var a = 42;
export let b = 43;
export const c = 44;
export function d() { return 45 };
export default 46;

assertEquals(42, a);
assertEquals(43, b);
assertEquals(44, c);
assertEquals(45, d());

assertEquals(42, foo.a);
assertEquals(43, foo.b);
assertEquals(44, foo.c);
assertEquals(45, foo.d());
assertEquals(46, foo.default);
assertSame(undefined, foo.doesnotexist);
