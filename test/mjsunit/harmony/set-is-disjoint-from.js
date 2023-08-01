// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-set-methods

(function TestIsDisjointFromSetFirstShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const otherSet = new Set();
  otherSet.add(42);
  otherSet.add(46);
  otherSet.add(47);

  assertEquals(firstSet.isDisjointFrom(otherSet), false);
})();

(function TestIsDisjointFromSetSecondShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const otherSet = new Set();
  otherSet.add(45);
  otherSet.add(46);

  assertEquals(firstSet.isDisjointFrom(otherSet), true);
})();

(function TestIsDisjointFromMapFirstShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);

  const other = new Map();
  other.set(45);
  other.set(46);
  other.set(47);

  assertEquals(firstSet.isDisjointFrom(other), true);
})();

(function TestIsDisjointFromMapSecondShorter() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const other = new Map();
  other.set(42);

  assertEquals(firstSet.isDisjointFrom(other), false);
})();

(function TestIsDisjointFromSetLikeObjectFirstShorter() {
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

  assertEquals(firstSet.isDisjointFrom(SetLike), false);
})();

(function TestIsDisjointFromSetLikeObjectSecondShorter() {
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

  assertEquals(firstSet.isDisjointFrom(SetLike), false);
})();

(function TestIsDisjointFromSetEqualLength() {
  let arrIndex = [];
  const SetLike = {
    arr: [42, 43, 45],
    size: 3,
    keys() {
      return this.arr[Symbol.iterator]();
    },
    has(key) {
      arrIndex.push(this.arr.indexOf(key));
      return this.arr.indexOf(key) != -1;
    }
  };

  const firstSet = new Set();
  firstSet.add(46);
  firstSet.add(47);
  firstSet.add(48);

  assertEquals(firstSet.isDisjointFrom(SetLike), true);
  assertEquals(arrIndex, [-1, -1, -1]);
})();
