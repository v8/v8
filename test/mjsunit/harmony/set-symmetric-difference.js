// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-set-methods

(function TestSymmetricDifferenceSetFirstShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(46);
  otherSet.add(47);

  const resultSet = new Set();
  resultSet.add(43);
  resultSet.add(46);
  resultSet.add(47);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(otherSet));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceSetSecondShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(44);

  const resultSet = new Set();
  resultSet.add(43);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(otherSet));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceMapFirstShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const other = new Map();
  other.set(42);
  other.set(46);
  other.set(47);

  const resultSet = new Set();
  resultSet.add(43);
  resultSet.add(46);
  resultSet.add(47);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(other));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceMapSecondShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const other = new Map();
  other.set(42);
  other.set(43);
  other.set(44);

  const resultSet = new Set();

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(other));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceSetLikeObjectFirstShorter() {
  const SetLike = {
    arr: [42, 44, 45],
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

  const resultSet = new Set();
  resultSet.add(43);
  resultSet.add(44);
  resultSet.add(45);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(SetLike));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceSetLikeObjectSecondShorter() {
  const SetLike = {
    arr: [42, 43],
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
  resultSet.add(44);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(SetLike));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceSetEqualLength() {
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
  resultSet.add(44);
  resultSet.add(45);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(SetLike));

  assertEquals(resultArray, symmetricDifferenceArray);
})();

(function TestSymmetricDifferenceSetShrinking() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);
  firstSet.add(45);
  firstSet.add(46);
  firstSet.add(47);
  firstSet.add(48);
  firstSet.add(49);

  const otherSet = new Set();
  otherSet.add(43);
  otherSet.add(44);
  otherSet.add(45);
  otherSet.add(46);
  otherSet.add(47);
  otherSet.add(48);
  otherSet.add(49);

  const resultSet = new Set();
  resultSet.add(42);

  const resultArray = Array.from(resultSet);
  const symmetricDifferenceArray =
      Array.from(firstSet.symmetricDifference(otherSet));

  assertEquals(resultArray, symmetricDifferenceArray);
})();
