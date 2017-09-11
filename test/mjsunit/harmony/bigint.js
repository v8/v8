// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-bigint --no-opt

'use strict'

const zero = %BigInt(0);
const another_zero = %BigInt(0);
const one = %BigInt(1);
const another_one = %BigInt(1);

// typeof
{
  assertEquals(typeof zero, "bigint");
  assertEquals(typeof one, "bigint");
}
{
  // TODO(neis): Enable once --no-opt can be removed.
  //
  // function Typeof(x) { return typeof x }
  // assertEquals(Typeof(zero), "bigint");
  // assertEquals(Typeof(zero), "bigint");
  // %OptimizeFunctionOnNextCall(Typeof);
  // assertEquals(Typeof(zero), "bigint");
}

// ToString
{
  assertEquals(String(zero), "0");
  assertEquals(String(one), "1");
}

// ToBoolean
{
  assertTrue(!zero);
  assertFalse(!!zero);
  assertTrue(!!!zero);

  assertFalse(!one);
  assertTrue(!!one);
  assertFalse(!!!one);
}

// Strict equality
{
  assertTrue(zero === zero);
  assertFalse(zero !== zero);

  assertTrue(zero === another_zero);
  assertFalse(zero !== another_zero);

  assertFalse(zero === one);
  assertTrue(zero !== one);
  assertTrue(one !== zero);
  assertFalse(one === zero);

  assertFalse(zero === 0);
  assertTrue(zero !== 0);
  assertFalse(0 === zero);
  assertTrue(0 !== zero);
}

// SameValue
{
  const obj = Object.defineProperty({}, 'foo',
      {value: zero, writable: false, configurable: false});

  assertTrue(Reflect.defineProperty(obj, 'foo', {value: zero}));
  assertTrue(Reflect.defineProperty(obj, 'foo', {value: another_zero}));
  assertFalse(Reflect.defineProperty(obj, 'foo', {value: one}));
}

// SameValueZero
{
  assertTrue([zero].includes(zero));
  assertTrue([zero].includes(another_zero));

  assertFalse([zero].includes(+0));
  assertFalse([zero].includes(-0));

  assertFalse([+0].includes(zero));
  assertFalse([-0].includes(zero));

  assertTrue([one].includes(one));
  assertTrue([one].includes(another_one));

  assertFalse([one].includes(1));
  assertFalse([1].includes(one));
}{
  assertTrue(new Set([zero]).has(zero));
  assertTrue(new Set([zero]).has(another_zero));

  assertFalse(new Set([zero]).has(+0));
  assertFalse(new Set([zero]).has(-0));

  assertFalse(new Set([+0]).has(zero));
  assertFalse(new Set([-0]).has(zero));

  assertTrue(new Set([one]).has(one));
  assertTrue(new Set([one]).has(another_one));
}{
  assertTrue(new Map([[zero, 42]]).has(zero));
  assertTrue(new Map([[zero, 42]]).has(another_zero));

  assertFalse(new Map([[zero, 42]]).has(+0));
  assertFalse(new Map([[zero, 42]]).has(-0));

  assertFalse(new Map([[+0, 42]]).has(zero));
  assertFalse(new Map([[-0, 42]]).has(zero));

  assertTrue(new Map([[one, 42]]).has(one));
  assertTrue(new Map([[one, 42]]).has(another_one));
}
