// Copyright 2012 the V8 project authors. All rights reserved.
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

// Flags: --harmony-object-observe

var allObservers = [];
function reset() {
  allObservers.forEach(function(observer) { observer.reset(); });
}

function createObserver() {
  "use strict";  // So that |this| in callback can be undefined.

  var observer = {
    records: undefined,
    callbackCount: 0,
    reset: function() {
      this.records = undefined;
      this.callbackCount = 0;
    },
    assertNotCalled: function() {
      assertEquals(undefined, this.records);
      assertEquals(0, this.callbackCount);
    },
    assertCalled: function() {
      assertEquals(1, this.callbackCount);
    },
    assertRecordCount: function(count) {
      this.assertCalled();
      assertEquals(count, this.records.length);
    },
    assertCallbackRecords: function(recs) {
      this.assertRecordCount(recs.length);
      for (var i = 0; i < recs.length; i++) {
        assertSame(this.records[i].object, recs[i].object);
        assertEquals('string', typeof recs[i].type);
        assertPropertiesEqual(this.records[i], recs[i]);
      }
    }
  };

  observer.callback = function(r) {
    assertEquals(undefined, this);
    assertEquals('object', typeof r);
    assertTrue(r instanceof Array)
    observer.records = r;
    observer.callbackCount++;
  };

  observer.reset();
  allObservers.push(observer);
  return observer;
}

var observer = createObserver();
assertEquals("function", typeof observer.callback);
var obj = {};

function frozenFunction() {}
Object.freeze(frozenFunction);
var nonFunction = {};
var changeRecordWithAccessor = { type: 'foo' };
var recordCreated = false;
Object.defineProperty(changeRecordWithAccessor, 'name', {
  get: function() {
    recordCreated = true;
  },
  enumerable: true
})

// Object.observe
assertThrows(function() { Object.observe("non-object", observer.callback); }, TypeError);
assertThrows(function() { Object.observe(obj, nonFunction); }, TypeError);
assertThrows(function() { Object.observe(obj, frozenFunction); }, TypeError);

// Object.unobserve
assertThrows(function() { Object.unobserve(4, observer.callback); }, TypeError);

// Object.notify
assertThrows(function() { Object.notify(obj, {}); }, TypeError);
assertThrows(function() { Object.notify(obj, { type: 4 }); }, TypeError);
Object.notify(obj, changeRecordWithAccessor);
assertFalse(recordCreated);

// Object.deliverChangeRecords
assertThrows(function() { Object.deliverChangeRecords(nonFunction); }, TypeError);

// Multiple records are delivered.
Object.observe(obj, observer.callback);
Object.notify(obj, {
  object: obj,
  type: 'updated',
  name: 'foo',
  expando: 1
});

Object.notify(obj, {
  object: obj,
  type: 'deleted',
  name: 'bar',
  expando2: 'str'
});
Object.deliverChangeRecords(observer.callback);
observer.assertCallbackRecords([
  { object: obj, name: 'foo', type: 'updated', expando: 1 },
  { object: obj, name: 'bar', type: 'deleted', expando2: 'str' }
]);

// No delivery takes place if no records are pending
reset();
Object.deliverChangeRecords(observer.callback);
observer.assertNotCalled();

// Multiple observation has no effect.
reset();
Object.observe(obj, observer.callback);
Object.observe(obj, observer.callback);
Object.notify(obj, {
  type: 'foo',
});
Object.deliverChangeRecords(observer.callback);
observer.assertCalled();

// Observation can be stopped.
reset();
Object.unobserve(obj, observer.callback);
Object.notify(obj, {
  type: 'foo',
});
Object.deliverChangeRecords(observer.callback);
observer.assertNotCalled();

// Multiple unobservation has no effect
reset();
Object.unobserve(obj, observer.callback);
Object.unobserve(obj, observer.callback);
Object.notify(obj, {
  type: 'foo',
});
Object.deliverChangeRecords(observer.callback);
observer.assertNotCalled();

// Re-observation works and only includes changeRecords after of call.
reset();
Object.notify(obj, {
  type: 'foo',
});
Object.observe(obj, observer.callback);
Object.notify(obj, {
  type: 'foo',
});
records = undefined;
Object.deliverChangeRecords(observer.callback);
observer.assertRecordCount(1);

// Observing a continuous stream of changes, while itermittantly unobserving.
reset();
Object.observe(obj, observer.callback);
Object.notify(obj, {
  type: 'foo',
  val: 1
});

Object.unobserve(obj, observer.callback);
Object.notify(obj, {
  type: 'foo',
  val: 2
});

Object.observe(obj, observer.callback);
Object.notify(obj, {
  type: 'foo',
  val: 3
});

Object.unobserve(obj, observer.callback);
Object.notify(obj, {
  type: 'foo',
  val: 4
});

Object.observe(obj, observer.callback);
Object.notify(obj, {
  type: 'foo',
  val: 5
});

Object.unobserve(obj, observer.callback);
Object.deliverChangeRecords(observer.callback);
observer.assertCallbackRecords([
  { object: obj, type: 'foo', val: 1 },
  { object: obj, type: 'foo', val: 3 },
  { object: obj, type: 'foo', val: 5 }
]);

// Observing multiple objects; records appear in order;.
reset();
var obj2 = {};
var obj3 = {}
Object.observe(obj, observer.callback);
Object.observe(obj3, observer.callback);
Object.observe(obj2, observer.callback);
Object.notify(obj, {
  type: 'foo',
});
Object.notify(obj2, {
  type: 'foo',
});
Object.notify(obj3, {
  type: 'foo',
});
Object.deliverChangeRecords(observer.callback);
observer.assertCallbackRecords([
  { object: obj, type: 'foo' },
  { object: obj2, type: 'foo' },
  { object: obj3, type: 'foo' }
]);