// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-d8-web-snapshot-api --allow-natives-syntax



const external_1 = {external: 1};
const external_2 = {external: 2};
const object = {
  a: [1,2],
  b: external_1,
  c: [external_1, external_2],
  d: { d_a: external_2 }
};


(function testNoExternals() {
  const snapshot = %WebSnapshotSerialize(object);
  const deserialized = %WebSnapshotDeserialize(snapshot);
  %HeapObjectVerify(deserialized);
  assertEquals(deserialized, object);
  assertEquals(deserialized.b, external_1);
  assertNotSame(deserialized.b, external_1);
  assertEquals(deserialized.d.d_a, external_2);
  assertNotSame(deserialized.d.d_a, external_2);
})();

(function testOneExternals() {
  const externals = [ external_1];
  const snapshot = %WebSnapshotSerialize(object, externals);
  const replaced_externals = [{replacement:1}]
  const deserialized = %WebSnapshotDeserialize(snapshot, replaced_externals);
  %HeapObjectVerify(deserialized);
  assertEquals(deserialized.a, object.a);
  assertSame(deserialized.b, replaced_externals[0]);
  assertArrayEquals(deserialized.c, [replaced_externals[0], external_2]);
  assertSame(deserialized.c[0], replaced_externals[0]);
  assertNotSame(deserialized.c[1], external_2);
  assertEquals(deserialized.d.d_a, external_2);
  assertNotSame(deserialized.d.d_a, external_2);
})();

(function testTwoExternals() {
  const externals = [external_1, external_2];
  const snapshot = %WebSnapshotSerialize(object, externals);
  const replaced_externals = [{replacement:1}, {replacement:2}]
  const deserialized = %WebSnapshotDeserialize(snapshot, replaced_externals);
  %HeapObjectVerify(deserialized);
  assertEquals(deserialized.a, object.a);
  assertSame(deserialized.b, replaced_externals[0]);
  assertArrayEquals(deserialized.c, replaced_externals);
  assertSame(deserialized.c[0], replaced_externals[0]);
  assertSame(deserialized.c[1], replaced_externals[1]);
  assertSame(deserialized.d.d_a, replaced_externals[1]);
})();
