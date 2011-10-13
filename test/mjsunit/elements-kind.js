// Copyright 2011 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Flags: --allow-natives-syntax --smi-only-arrays --expose-gc

// Test element kind of objects.
// Since --smi-only-arrays affects builtins, its default setting at compile
// time sticks if built with snapshot.  If --smi-only-arrays is deactivated
// by default, only a no-snapshot build actually has smi-only arrays enabled
// in this test case.  Depending on whether smi-only arrays are actually
// enabled, this test takes the appropriate code path to check smi-only arrays.

support_smi_only_arrays = %HasFastSmiOnlyElements([]);

if (support_smi_only_arrays) {
  print("Tests include smi-only arrays.");
} else {
  print("Tests do NOT include smi-only arrays.");
}

var elements_kind = {
  fast_smi_only            :  'fast smi only elements',
  fast                     :  'fast elements',
  fast_double              :  'fast double elements',
  dictionary               :  'dictionary elements',
  external_byte            :  'external byte elements',
  external_unsigned_byte   :  'external unsigned byte elements',
  external_short           :  'external short elements',
  external_unsigned_short  :  'external unsigned short elements',
  external_int             :  'external int elements',
  external_unsigned_int    :  'external unsigned int elements',
  external_float           :  'external float elements',
  external_double          :  'external double elements',
  external_pixel           :  'external pixel elements'
}

function getKind(obj) {
  if (%HasFastSmiOnlyElements(obj)) return elements_kind.fast_smi_only;
  if (%HasFastElements(obj)) return elements_kind.fast;
  if (%HasFastDoubleElements(obj)) return elements_kind.fast_double;
  if (%HasDictionaryElements(obj)) return elements_kind.dictionary;
  // Every external kind is also an external array.
  assertTrue(%HasExternalArrayElements(obj));
  if (%HasExternalByteElements(obj)) {
    return elements_kind.external_byte;
  }
  if (%HasExternalUnsignedByteElements(obj)) {
    return elements_kind.external_unsigned_byte;
  }
  if (%HasExternalShortElements(obj)) {
    return elements_kind.external_short;
  }
  if (%HasExternalUnsignedShortElements(obj)) {
    return elements_kind.external_unsigned_short;
  }
  if (%HasExternalIntElements(obj)) {
    return elements_kind.external_int;
  }
  if (%HasExternalUnsignedIntElements(obj)) {
    return elements_kind.external_unsigned_int;
  }
  if (%HasExternalFloatElements(obj)) {
    return elements_kind.external_float;
  }
  if (%HasExternalDoubleElements(obj)) {
    return elements_kind.external_double;
  }
  if (%HasExternalPixelElements(obj)) {
    return elements_kind.external_pixel;
  }
}

function assertKind(expected, obj, name_opt) {
  if (!support_smi_only_arrays &&
      expected == elements_kind.fast_smi_only) {
    expected = elements_kind.fast;
  }
  assertEquals(expected, getKind(obj), name_opt);
}

var me = {};
assertKind(elements_kind.fast, me);
me.dance = 0xD15C0;
me.drink = 0xC0C0A;
assertKind(elements_kind.fast, me);

var too = [1,2,3];
assertKind(elements_kind.fast_smi_only, too);
too.dance = 0xD15C0;
too.drink = 0xC0C0A;
assertKind(elements_kind.fast_smi_only, too);

// Make sure the element kind transitions from smionly when a non-smi is stored.
var you = new Array();
assertKind(elements_kind.fast_smi_only, you);
for (var i = 0; i < 1337; i++) {
  var val = i;
  if (i == 1336) {
    assertKind(elements_kind.fast_smi_only, you);
    val = new Object();
  }
  you[i] = val;
}
assertKind(elements_kind.fast, you);

assertKind(elements_kind.dictionary, new Array(0xDECAF));

var fast_double_array = new Array(0xDECAF);
for (var i = 0; i < 0xDECAF; i++) fast_double_array[i] = i / 2;
assertKind(elements_kind.fast_double, fast_double_array);

assertKind(elements_kind.external_byte,           new Int8Array(9001));
assertKind(elements_kind.external_unsigned_byte,  new Uint8Array(007));
assertKind(elements_kind.external_short,          new Int16Array(666));
assertKind(elements_kind.external_unsigned_short, new Uint16Array(42));
assertKind(elements_kind.external_int,            new Int32Array(0xF));
assertKind(elements_kind.external_unsigned_int,   new Uint32Array(23));
assertKind(elements_kind.external_float,          new Float32Array(7));
assertKind(elements_kind.external_double,         new Float64Array(0));
assertKind(elements_kind.external_pixel,          new PixelArray(512));

// Crankshaft support for smi-only array elements.
function monomorphic(array) {
  for (var i = 0; i < 3; i++) {
    array[i] = i + 10;
  }
  assertKind(elements_kind.fast_smi_only, array);
  for (var i = 0; i < 3; i++) {
    var a = array[i];
    assertEquals(i + 10, a);
  }
}
var smi_only = [1, 2, 3];
for (var i = 0; i < 3; i++) monomorphic(smi_only);
%OptimizeFunctionOnNextCall(monomorphic);
monomorphic(smi_only);
function polymorphic(array, expected_kind) {
  array[1] = 42;
  assertKind(expected_kind, array);
  var a = array[1];
  assertEquals(42, a);
}
var smis = [1, 2, 3];
var strings = [0, 0, 0]; strings[0] = "one";
var doubles = [0, 0, 0]; doubles[0] = 1.5;
assertKind(support_smi_only_arrays
               ? elements_kind.fast_double
               : elements_kind.fast,
           doubles);
for (var i = 0; i < 3; i++) {
  polymorphic(smis, elements_kind.fast_smi_only);
}
for (var i = 0; i < 3; i++) {
  polymorphic(strings, elements_kind.fast);
}
/* In the first iteration, feeding polymorphic with a fast double elements
 * array leads to a miss and is then routed to runtime code.  No conversion
 * is done in there.  The second time the store is handled by the newly
 * created IC, which converts the fast double elements into fast elements
 * since arrays with fast elements have been handled earlier in polymorphic.
 * Since the x64 and arm port of the generated code conversion does not yet
 * exist, this test is skipped for now.
for (var i = 0; i < 3; i++) {
  polymorphic(doubles, i == 0 && support_smi_only_arrays
                           ? elements_kind.fast_double
                           : elements_kind.fast);
}
*/

/* Element transitions have not been implemented in crankshaft yet.
%OptimizeFunctionOnNextCall(polymorphic);
polymorphic(smis, elements_kind.fast_smi_only);
polymorphic(strings, elements_kind.fast);
polymorphic(doubles, elements_kind.fast);

// Crankshaft support for smi-only elements in dynamic array literals.
function get(foo) { return foo; }  // Used to generate dynamic values.

function crankshaft_test() {
  var a = [get(1), get(2), get(3)];
  assertKind(elements_kind.fast_smi_only, a);
  var b = [get(1), get(2), get("three")];
  assertKind(elements_kind.fast, b);
  var c = [get(1), get(2), get(3.5)];
  // The full code generator doesn't support conversion to fast_double
  // yet. Crankshaft does, but only with --smi-only-arrays support.
  if ((%GetOptimizationStatus(crankshaft_test) & 1) &&
      support_smi_only_arrays) {
    assertKind(elements_kind.fast_double, c);
  } else {
    assertKind(elements_kind.fast, c);
  }
}
for (var i = 0; i < 3; i++) {
  crankshaft_test();
}
%OptimizeFunctionOnNextCall(crankshaft_test);
crankshaft_test();
*/

// Elements_kind transitions for arrays.

// A map can have three different elements_kind transitions: SMI->DOUBLE,
// DOUBLE->OBJECT, and SMI->OBJECT. No matter in which order these three are
// created, they must always end up with the same FAST map.

// This test is meaningless without FAST_SMI_ONLY_ELEMENTS.
if (support_smi_only_arrays) {
  // Preparation: create one pair of identical objects for each case.
  var a = [1, 2, 3];
  var b = [1, 2, 3];
  assertTrue(%HaveSameMap(a, b));
  assertKind(elements_kind.fast_smi_only, a);
  var c = [1, 2, 3];
  c["case2"] = true;
  var d = [1, 2, 3];
  d["case2"] = true;
  assertTrue(%HaveSameMap(c, d));
  assertFalse(%HaveSameMap(a, c));
  assertKind(elements_kind.fast_smi_only, c);
  var e = [1, 2, 3];
  e["case3"] = true;
  var f = [1, 2, 3];
  f["case3"] = true;
  assertTrue(%HaveSameMap(e, f));
  assertFalse(%HaveSameMap(a, e));
  assertFalse(%HaveSameMap(c, e));
  assertKind(elements_kind.fast_smi_only, e);
  // Case 1: SMI->DOUBLE, DOUBLE->OBJECT, SMI->OBJECT.
  a[0] = 1.5;
  assertKind(elements_kind.fast_double, a);
  a[0] = "foo";
  assertKind(elements_kind.fast, a);
  b[0] = "bar";
  assertTrue(%HaveSameMap(a, b));
  // Case 2: SMI->DOUBLE, SMI->OBJECT, DOUBLE->OBJECT.
  c[0] = 1.5;
  assertKind(elements_kind.fast_double, c);
  assertFalse(%HaveSameMap(c, d));
  d[0] = "foo";
  assertKind(elements_kind.fast, d);
  assertFalse(%HaveSameMap(c, d));
  c[0] = "bar";
  assertTrue(%HaveSameMap(c, d));
  // Case 3: SMI->OBJECT, SMI->DOUBLE, DOUBLE->OBJECT.
  e[0] = "foo";
  assertKind(elements_kind.fast, e);
  assertFalse(%HaveSameMap(e, f));
  f[0] = 1.5;
  assertKind(elements_kind.fast_double, f);
  assertFalse(%HaveSameMap(e, f));
  f[0] = "bar";
  assertKind(elements_kind.fast, f);
  assertTrue(%HaveSameMap(e, f));
}

// Throw away type information in the ICs for next stress run.
gc();
