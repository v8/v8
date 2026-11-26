// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/test-async.js');

assertTrue(%ArrayIteratorProtector());
assertTrue(%NoElementsProtector());

// Test Promise.all and Promise.allSettled fast JSArray path.
testAsync(assert => {
  assert.plan(23);

  // Empty array.
  Promise.all([]).then(res => assert.equals([], res));
  Promise.allSettled([]).then(res => assert.equals([], res));

  // PACKED_ELEMENTS of JSPromises.
  Promise.all([
    Promise.resolve(1),
    Promise.resolve(2),
    Promise.resolve(3),
  ]).then(res => assert.equals([1, 2, 3], res));

  Promise.allSettled([
    Promise.resolve(1),
    Promise.reject('err'),
  ]).then(res =>
    assert.equals(
        [{status: 'fulfilled', value: 1}, {status: 'rejected', reason: 'err'}],
        res));

  // PACKED_DOUBLE_ELEMENTS and HOLEY_DOUBLE_ELEMENTS.
  Promise.all([1.5, 2.5]).then(res => assert.equals([1.5, 2.5], res));
  const holeyDoubles = [1.5, , 3.5];
  Promise.all(holeyDoubles).then(
      res => assert.equals([1.5, undefined, 3.5], res));

  // HOLEY_SMI_ELEMENTS.
  Promise.all([1, , 3]).then(res => assert.equals([1, undefined, 3], res));

  // Synchronous mutation to dictionary elements & shrink mid-iteration.
  const shrinkArr = [1, 2, 3];
  class ShrinkPromise extends Promise {
    static resolve(x) {
      if (x === 1) {
        shrinkArr[100000] = 42;
        shrinkArr.length = 2;
      }
      return super.resolve(x);
    }
  }
  ShrinkPromise.all(shrinkArr).then(res => assert.equals([1, 2], res));

  // Synchronous mutation that grows the array mid-iteration.
  const growArr = [1, 2];
  class GrowPromise extends Promise {
    static resolve(x) {
      if (x === 1) {
        growArr.push(3);
      }
      return super.resolve(x);
    }
  }
  GrowPromise.all(growArr).then(res => assert.equals([1, 2, 3], res));

  // Non-extensible elements kinds.
  Promise.all(Object.freeze([1, 2])).then(res => assert.equals([1, 2], res));

  // Promise.allSettled shares the fast path; shrink mid-iteration.
  const settledArr = [1, 2, 3];
  class SettledShrinkPromise extends Promise {
    static resolve(x) {
      if (x === 1) settledArr.length = 2;
      return super.resolve(x);
    }
  }
  SettledShrinkPromise.allSettled(settledArr).then(
      res => assert.equals(
          [{status: 'fulfilled', value: 1}, {status: 'fulfilled', value: 2}],
          res));

  // IteratorClose (.return) must be called when an exception is thrown
  // after reading an element. The iterator it receives must be positioned
  // after that element.
  const ArrayIteratorProto = Object.getPrototypeOf([][Symbol.iterator]());
  let returnCalled = 0;
  let nextAfterClose;
  ArrayIteratorProto.return = function() {
    returnCalled++;
    nextAfterClose = this.next();
    return {done: true};
  };
  try {
    class ThrowingPromise extends Promise {
      static resolve(x) {
        if (x === 2) throw new Error('boom');
        return super.resolve(x);
      }
    }
    const p1 = ThrowingPromise.all([1, 2, 3]);
    assert.equals(1, returnCalled);
    assert.equals({value: 3, done: false}, nextAfterClose);
    p1.then(assert.unreachable, e => assert.equals('boom', e.message));

    returnCalled = 0;
    const p2 = ThrowingPromise.allSettled([1, 2, 3]);
    assert.equals(1, returnCalled);
    assert.equals({value: 3, done: false}, nextAfterClose);
    p2.then(assert.unreachable, e => assert.equals('boom', e.message));

    // Throwing on the last element: the closed iterator is positioned at the
    // end, so next() inside return() reports done.
    returnCalled = 0;
    const p4 = ThrowingPromise.all([1, 2]);
    assert.equals(1, returnCalled);
    assert.equals({value: undefined, done: true}, nextAfterClose);
    p4.then(assert.unreachable, e => assert.equals('boom', e.message));

    // A throwing element getter corresponds to a throwing next(), after which
    // the iterator is done and must not be closed.
    returnCalled = 0;
    const getterArr = [1, 2];
    class GetterMutatingPromise extends Promise {
      static resolve(x) {
        if (x === 1) {
          Object.defineProperty(getterArr, 1, {
            get() {
              throw new Error('getboom');
            },
          });
        }
        return super.resolve(x);
      }
    }
    const p3 = GetterMutatingPromise.all(getterArr);
    assert.equals(0, returnCalled);
    p3.then(assert.unreachable, e => assert.equals('getboom', e.message));
  } finally {
    delete ArrayIteratorProto.return;
  }

  // The next method is cached when iteration starts, so replacing it
  // mid-iteration has no effect. Holes read through to the prototype chain.
  // Both invalidate protectors, so this must stay last.
  const holeyArr = [1, , 3];
  const originalNext = ArrayIteratorProto.next;
  class ProtoMutatingPromise extends Promise {
    static resolve(x) {
      if (x === 1) {
        ArrayIteratorProto.next = () => {
          throw new Error('unreachable');
        };
        Array.prototype[1] = 'proto';
      }
      return super.resolve(x);
    }
  }
  try {
    ProtoMutatingPromise.all(holeyArr).then(
        res => assert.equals([1, 'proto', 3], res));
  } finally {
    ArrayIteratorProto.next = originalNext;
    delete Array.prototype[1];
  }
});
