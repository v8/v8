// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-simd --harmony-tostring  --harmony-reflect
// Flags: --allow-natives-syntax --expose-natives-as natives --noalways-opt

function lanesForType(typeName) {
  // The lane count follows the first 'x' in the type name, which begins with
  // 'float', 'int', or 'bool'.
  return Number.parseInt(typeName[typeName.indexOf('x') + 1]);
}


function isValidSimdString(string, value, type, lanes) {
  var simdFn = SIMD[type],
      parseFn =
          type.indexOf('float') === 0 ? Number.parseFloat : Number.parseInt,
      indexOfOpenParen = string.indexOf('(');
  // Check prefix for correct type name.
  if (string.substr(0, indexOfOpenParen).toUpperCase() !== type.toUpperCase())
    return false;
  // Remove type name and open parenthesis.
  string = string.substr(indexOfOpenParen + 1);
  var laneStrings = string.split(',');
  if (laneStrings.length !== lanes)
    return false;
  for (var i = 0; i < lanes; i++) {
    var fromString = parseFn(laneStrings[i]),
        fromValue = simdFn.extractLane(value, i);
    if (Math.abs(fromString - fromValue) > Number.EPSILON)
      return false;
  }
  return true;
}


// Test for structural equivalence.
function areEquivalent(type, lanes, a, b) {
  var simdFn = SIMD[type];
  for (var i = 0; i < lanes; i++) {
    if (simdFn.extractLane(a, i) !== simdFn.extractLane(b, i))
      return false;
  }
  return true;
}


var sameValue = natives.$sameValue;
var sameValueZero = natives.$sameValueZero;

// Calls SameValue and SameValueZero and checks that their results match. Also
// checks the internal SameValue checks using Object freeze and defineProperty.
function sameValueBoth(a, b) {
  var result = sameValue(a, b);
  assertTrue(result === sameValueZero(a, b));
  return result;
}


// Calls SameValue and SameValueZero and checks that their results don't match.
function sameValueZeroOnly(a, b) {
  var result = sameValueZero(a, b);
  assertTrue(result && !sameValue(a, b));
  return result;
}


// Tests for the global SIMD object.
function TestSIMDObject() {
  assertSame(typeof SIMD, 'object');
  assertSame(SIMD.constructor, Object);
  assertSame(Object.getPrototypeOf(SIMD), Object.prototype);
  assertSame(SIMD + "", "[object SIMD]");
}
TestSIMDObject()

// TestConstructor populates this with interesting values for the other tests.
var values;

// Test different forms of constructor calls. This test populates 'values' with
// a variety of SIMD values as a side effect, which are used by other tests.
function TestConstructor(type, lanes) {
  var simdFn = SIMD[type];
  assertFalse(Object === simdFn.prototype.constructor)
  assertFalse(simdFn === Object.prototype.constructor)
  assertSame(simdFn, simdFn.prototype.constructor)

  values = []

  // The constructor expects values for all lanes.
  switch (type) {
    case 'float32x4':
      // The constructor expects values for all lanes.
      assertThrows(function () { simdFn() }, TypeError)
      assertThrows(function () { simdFn(0) }, TypeError)
      assertThrows(function () { simdFn(0, 1) }, TypeError)
      assertThrows(function () { simdFn(0, 1, 2) }, TypeError)

      values.push(simdFn(1, 2, 3, 4))
      values.push(simdFn(1, 2, 3, 4))       // test structural equivalence
      values.push(simdFn(-0, NaN, 0, 0.5))
      values.push(simdFn(-0, NaN, 0, 0.5))  // test structural equivalence
      values.push(simdFn(3, 2, 1, 0))
      values.push(simdFn(0, 0, 0, 0))
      break
  }
  for (var i in values) {
    assertSame(simdFn, values[i].__proto__.constructor)
    assertSame(simdFn, Object(values[i]).__proto__.constructor)
    assertSame(simdFn.prototype, values[i].__proto__)
    assertSame(simdFn.prototype, Object(values[i]).__proto__)
  }
}


function TestType(type, lanes) {
  for (var i in values) {
    assertEquals(type, typeof values[i])
    assertTrue(typeof values[i] === type)
    assertTrue(typeof Object(values[i]) === 'object')
    assertEquals(null, %_ClassOf(values[i]))
    assertEquals("Float32x4", %_ClassOf(Object(values[i])))
  }
}


function TestPrototype(type, lanes) {
  var simdFn = SIMD[type];
  assertSame(Object.prototype, simdFn.prototype.__proto__)
  for (var i in values) {
    assertSame(simdFn.prototype, values[i].__proto__)
    assertSame(simdFn.prototype, Object(values[i]).__proto__)
  }
}


function TestValueOf(type, lanes) {
  var simdFn = SIMD[type];
  for (var i in values) {
    assertTrue(values[i] === Object(values[i]).valueOf())
    assertTrue(values[i] === values[i].valueOf())
    assertTrue(simdFn.prototype.valueOf.call(Object(values[i])) === values[i])
    assertTrue(simdFn.prototype.valueOf.call(values[i]) === values[i])
  }
}


function TestGet(type, lanes) {
  var simdFn = SIMD[type];
  for (var i in values) {
    assertEquals(undefined, values[i].a)
    assertEquals(undefined, values[i]["a" + "b"])
    assertEquals(undefined, values[i]["" + "1"])
    assertEquals(undefined, values[i][42])
  }
}


function TestToBoolean(type, lanes) {
  for (var i in values) {
    assertTrue(Boolean(Object(values[i])))
    assertFalse(!Object(values[i]))
    assertTrue(Boolean(values[i]).valueOf())
    assertFalse(!values[i])
    assertTrue(!!values[i])
    assertTrue(values[i] && true)
    assertFalse(!values[i] && false)
    assertTrue(!values[i] || true)
    assertEquals(1, values[i] ? 1 : 2)
    assertEquals(2, !values[i] ? 1 : 2)
    if (!values[i]) assertUnreachable();
    if (values[i]) {} else assertUnreachable();
  }
}


function TestToString(type, lanes) {
  var simdFn = SIMD[type];
  for (var i in values) {
    assertEquals(values[i].toString(), String(values[i]))
    assertTrue(isValidSimdString(values[i].toString(), values[i], type, lanes))
    assertTrue(
        isValidSimdString(Object(values[i]).toString(), values[i], type, lanes))
    assertTrue(isValidSimdString(
        simdFn.prototype.toString.call(values[i]), values[i], type, lanes))
  }
}


function TestToNumber(type, lanes) {
  for (var i in values) {
    assertThrows(function() { Number(Object(values[i])) }, TypeError)
    assertThrows(function() { +Object(values[i]) }, TypeError)
    assertThrows(function() { Number(values[i]) }, TypeError)
    assertThrows(function() { values[i] + 0 }, TypeError)
  }
}


function TestEquality(type, lanes) {
  // Every SIMD value should equal itself, and non-strictly equal its wrapper.
  for (var i in values) {
    assertSame(values[i], values[i])
    assertEquals(values[i], values[i])
    assertTrue(Object.is(values[i], values[i]))
    assertTrue(values[i] === values[i])
    assertTrue(values[i] == values[i])
    assertFalse(values[i] === Object(values[i]))
    assertFalse(Object(values[i]) === values[i])
    assertFalse(values[i] == Object(values[i]))
    assertFalse(Object(values[i]) == values[i])
    assertTrue(values[i] === values[i].valueOf())
    assertTrue(values[i].valueOf() === values[i])
    assertTrue(values[i] == values[i].valueOf())
    assertTrue(values[i].valueOf() == values[i])
    assertFalse(Object(values[i]) === Object(values[i]))
    assertEquals(Object(values[i]).valueOf(), Object(values[i]).valueOf())
  }

  // Test structural equivalence.
  for (var i = 0; i < values.length; i++) {
    for (var j = i + 1; j < values.length; j++) {
      var a = values[i], b = values[j],
          equivalent = areEquivalent(type, lanes, a, b);
      assertSame(equivalent, a == b);
      assertSame(equivalent, a === b);
    }
  }

  // SIMD values should not be equal to any other kind of object.
  var others = [347, 1.275, NaN, "string", null, undefined, {}, function() {}]
  for (var i in values) {
    for (var j in others) {
      assertFalse(values[i] === others[j])
      assertFalse(others[j] === values[i])
      assertFalse(values[i] == others[j])
      assertFalse(others[j] == values[i])
    }
  }
}


function TestSameValue(type, lanes) {
  // SIMD value types.
  // All lanes checked.
  // TODO(bbudge): use loops to test lanes when replaceLane is defined.
  assertTrue(sameValueBoth(SIMD.float32x4(1, 2, 3, 4),
                           SIMD.float32x4(1, 2, 3, 4)));
  assertFalse(sameValueBoth(SIMD.float32x4(1, 2, 3, 4),
                            SIMD.float32x4(NaN, 2, 3, 4)));
  assertFalse(sameValueBoth(SIMD.float32x4(1, 2, 3, 4),
                            SIMD.float32x4(1, NaN, 3, 4)));
  assertFalse(sameValueBoth(SIMD.float32x4(1, 2, 3, 4),
                            SIMD.float32x4(1, 2, NaN, 4)));
  assertFalse(sameValueBoth(SIMD.float32x4(1, 2, 3, 4),
                            SIMD.float32x4(1, 2, 3, NaN)));
  // Special values.
  // TODO(bbudge): use loops to test lanes when replaceLane is defined.
  assertTrue(sameValueBoth(SIMD.float32x4(NaN, 2, 3, 4),
                           SIMD.float32x4(NaN, 2, 3, 4)));
  assertTrue(sameValueBoth(SIMD.float32x4(+0, 2, 3, 4),
                           SIMD.float32x4(+0, 2, 3, 4)));
  assertTrue(sameValueBoth(SIMD.float32x4(-0, 2, 3, 4),
                           SIMD.float32x4(-0, 2, 3, 4)));
  assertTrue(sameValueZeroOnly(SIMD.float32x4(+0, 2, 3, 4),
                               SIMD.float32x4(-0, 2, 3, 4)));
  assertTrue(sameValueZeroOnly(SIMD.float32x4(-0, 2, 3, 4),
                               SIMD.float32x4(+0, 2, 3, 4)));
}


function TestComparison(type, lanes) {
  var a = values[0], b = values[1];

  function lt() { a < b; }
  function gt() { a > b; }
  function le() { a <= b; }
  function ge() { a >= b; }
  function lt_same() { a < a; }
  function gt_same() { a > a; }
  function le_same() { a <= a; }
  function ge_same() { a >= a; }

  var throwFuncs = [lt, gt, le, ge, lt_same, gt_same, le_same, ge_same];

  for (var f of throwFuncs) {
    assertThrows(f, TypeError);
    %OptimizeFunctionOnNextCall(f);
    assertThrows(f, TypeError);
    assertThrows(f, TypeError);
  }
}


// Test SIMD value wrapping/boxing over non-builtins.
function TestCall(type, lanes) {
  var simdFn = SIMD[type];
  simdFn.prototype.getThisProto = function () {
    return Object.getPrototypeOf(this);
  }
  for (var i in values) {
    assertTrue(values[i].getThisProto() === simdFn.prototype)
  }
}


function TestAsSetKey(type, lanes, set) {
  function test(set, key) {
    assertFalse(set.has(key));
    assertFalse(set.delete(key));
    if (!(set instanceof WeakSet)) {
      assertSame(set, set.add(key));
      assertTrue(set.has(key));
      assertTrue(set.delete(key));
    } else {
      // SIMD values can't be used as keys in WeakSets.
      assertThrows(function() { set.add(key) });
    }
    assertFalse(set.has(key));
    assertFalse(set.delete(key));
    assertFalse(set.has(key));
  }

  for (var i in values) {
    test(set, values[i]);
  }
}


function TestAsMapKey(type, lanes, map) {
  function test(map, key, value) {
    assertFalse(map.has(key));
    assertSame(undefined, map.get(key));
    assertFalse(map.delete(key));
    if (!(map instanceof WeakMap)) {
      assertSame(map, map.set(key, value));
      assertSame(value, map.get(key));
      assertTrue(map.has(key));
      assertTrue(map.delete(key));
    } else {
      // SIMD values can't be used as keys in WeakMaps.
      assertThrows(function() { map.set(key, value) });
    }
    assertFalse(map.has(key));
    assertSame(undefined, map.get(key));
    assertFalse(map.delete(key));
    assertFalse(map.has(key));
    assertSame(undefined, map.get(key));
  }

  for (var i in values) {
    test(map, values[i], {});
  }
}


// Test SIMD type with Harmony reflect-apply.
function TestReflectApply(type) {
  function returnThis() { return this; }
  function returnThisStrict() { 'use strict'; return this; }
  function noop() {}
  function noopStrict() { 'use strict'; }
  var R = void 0;

  for (var i in values) {
    assertSame(SIMD[type].prototype,
               Object.getPrototypeOf(
                  Reflect.apply(returnThis, values[i], [])));
    assertSame(values[i], Reflect.apply(returnThisStrict, values[i], []));

    assertThrows(
        function() { 'use strict'; Reflect.apply(values[i]); }, TypeError);
    assertThrows(
        function() { Reflect.apply(values[i]); }, TypeError);
    assertThrows(
        function() { Reflect.apply(noopStrict, R, values[i]); }, TypeError);
    assertThrows(
        function() { Reflect.apply(noop, R, values[i]); }, TypeError);
  }
}


function TestSIMDTypes() {
  var types = [ 'float32x4' ];
  for (var i = 0; i < types.length; ++i) {
    var type = types[i],
        lanes = lanesForType(type);
    TestConstructor(type, lanes);
    TestType(type, lanes);
    TestPrototype(type, lanes);
    TestValueOf(type, lanes);
    TestGet(type, lanes);
    TestToBoolean(type, lanes);
    TestToString(type, lanes);
    TestToNumber(type, lanes);
    TestEquality(type, lanes);
    TestSameValue(type, lanes);
    TestComparison(type, lanes);
    TestCall(type, lanes);
    TestAsSetKey(type, lanes, new Set);
    TestAsSetKey(type, lanes, new WeakSet);
    TestAsMapKey(type, lanes, new Map);
    TestAsMapKey(type, lanes, new WeakMap);
    TestReflectApply(type);
  }
}
TestSIMDTypes();
