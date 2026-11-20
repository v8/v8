// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// When a generator function is called before its prototype is read, the
// prototype that the call creates starts out with fast properties.
function* gen() { yield 1; }
const it = gen();
assertTrue(%HasFastProperties(gen.prototype));
gen.prototype.foo = 42;
assertEquals(42, it.foo);
assertSame(gen.prototype, Object.getPrototypeOf(it));

// Expando properties on generator objects still work.
it.a = 1;
it.d = 1.5;
it.o = it;
assertEquals(1, it.a);
assertEquals(1.5, it.d);
assertSame(it, it.o);
assertEquals({value: 1, done: false}, it.next());

// The same holds for async generators.
async function* agen() { yield 1; }
const ait = agen();
assertTrue(%HasFastProperties(agen.prototype));
agen.prototype.foo = 42;
assertEquals(42, ait.foo);
assertSame(agen.prototype, Object.getPrototypeOf(ait));
ait.a = 1;
ait.o = ait;
assertEquals(1, ait.a);
assertSame(ait, ait.o);
