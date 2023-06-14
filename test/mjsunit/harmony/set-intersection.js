// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-set-methods

(function TestIntersectionSetFirstShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(46);
  otherSet.add(47);

  const resultSet = new Set();
  resultSet.add(42);

  const resultArray = Array.from(resultSet);
  const intersectionArray = Array.from(firstSet.intersection(otherSet));

  assertEquals(resultArray, intersectionArray);
})();

(function TestIntersectionSetSecondShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(45);

  const resultSet = new Set();
  resultSet.add(42);

  const resultArray = Array.from(resultSet);
  const intersectionArray = Array.from(firstSet.intersection(otherSet));

  assertEquals(resultArray, intersectionArray);
})();

(function TestIntersectionMapFirstShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const other = new Map();
  other.set(42);
  other.set(46);
  other.set(47);

  const resultSet = new Set();
  resultSet.add(42);

  const resultArray = Array.from(resultSet);
  const intersectionArray = Array.from(firstSet.intersection(other));

  assertEquals(resultArray, intersectionArray);
})();

(function TestIntersectionMapSecondShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const other = new Map();
  other.set(42);

  const resultSet = new Set();
  resultSet.add(42);

  const resultArray = Array.from(resultSet);
  const intersectionArray = Array.from(firstSet.intersection(other));

  assertEquals(resultArray, intersectionArray);
})();

(function TestIntersectionSetLikeObject() {
  const SetLike = {
    arr: [42, 43, 45],
    size: 3,
    keys() {
      return this.arr[Symbol.iterator]();
    },
    has(key) {
      return this.arr.indexOf(key) != -1;
    }
  };

  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const resultSet = new Set();
  resultSet.add(42);
  resultSet.add(43);

  const resultArray = Array.from(resultSet);
  const intersectionArray = Array.from(firstSet.intersection(SetLike));

  assertEquals(resultArray, intersectionArray);
})();
