// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// Exercise TransitionsAccessor::ExpectedTransition across direct linear (<= 8),
// hash-filtered linear (9..kMaxElementsForLinearSearch), and sorted
// binary-search (> kMaxElementsForLinearSearch) TransitionArrays, including
// symbol transitions, non-NONE attribute transitions, and 2-byte JSON input.
(function TestExpectedTransitionAllRegimes() {
  // Each iteration adds 3 non-NONE/non-string transitions (symbol, duplicate
  // READ_ONLY 'branch_0', and sole READ_ONLY 'ro_only') plus `branchCount`
  // writable transitions, yielding exact TransitionArray sizes [6, 8, 9, 20, 48].
  for (const isTwoByte of [false, true]) {
    for (const branchCount of [3, 5, 6, 17, 45]) {
      const rootKey = (isTwoByte ? 'shared2_' : 'shared1_') + branchCount;
      const sym = Symbol('special');

      const withSym = {[rootKey]: 1};
      withSym[sym] = 123;

      const withRoDup = {[rootKey]: 1};
      Object.defineProperty(withRoDup, 'branch_0', {
        value: 999,
        writable: false,
        enumerable: true,
        configurable: true,
      });

      const withRoOnly = {[rootKey]: 99};
      Object.defineProperty(withRoOnly, 'ro_only', {
        value: 777,
        writable: false,
        enumerable: true,
        configurable: true,
      });

      // Keep weak TransitionArray targets alive across GC stress until after
      // JSON.parse completes.
      const keepAlive = [withSym, withRoDup, withRoOnly];

      const objs = [];
      for (let i = 0; i < branchCount; i++) {
        objs.push({[rootKey]: i, ['branch_' + i]: i * 2});
      }

      const list = objs.concat([withRoOnly], objs, [withRoOnly]);
      const payload = JSON.stringify({list, tag: isTwoByte ? '\u2603' : 'ok'});
      assertEquals(!isTwoByte, %StringIsOneByteRepresentation(payload));

      const parsed = JSON.parse(payload).list;
      assertEquals(branchCount * 2 + 2, parsed.length);
      for (let i = 0; i < branchCount; i++) {
        const first = parsed[i];
        const second = parsed[branchCount + 1 + i];
        assertEquals(i * 2, first['branch_' + i]);
        assertEquals(i * 2, second['branch_' + i]);
        assertTrue(%HaveSameMap(first, second));
        assertTrue(
            Object.getOwnPropertyDescriptor(first, 'branch_' + i).writable);
      }

      const roFirst = parsed[branchCount];
      const roSecond = parsed[branchCount * 2 + 1];
      assertEquals(777, roFirst.ro_only);
      assertEquals(777, roSecond.ro_only);
      assertTrue(Object.getOwnPropertyDescriptor(roFirst, 'ro_only').writable);
      assertFalse(%HaveSameMap(roFirst, withRoOnly));
      assertTrue(%HaveSameMap(roFirst, roSecond));

      assertEquals(123, keepAlive[0][sym]);
      assertEquals(999, keepAlive[1].branch_0);
      assertEquals(777, keepAlive[2].ro_only);
    }
  }
})();
