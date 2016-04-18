// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --ignition-generators --harmony-do-expressions


{ // yield in try-catch

  let g = function*() {
    try {yield 1} catch (error) {assertEquals("caught", error)}
  };

  assertThrowsEquals(() => g().throw("not caught"), "not caught");

  {
    let x = g();
    assertEquals({value: 1, done: false}, x.next());
    assertEquals({value: undefined, done: true}, x.throw("caught"));
  }

  {
    let x = g();
    assertEquals({value: 1, done: false}, x.next());
    assertEquals({value: undefined, done: true}, x.next());
    assertThrowsEquals(() => x.throw("not caught"), "not caught");
  }
}


{ // return that doesn't close
  let g = function*() { try {return 42} finally {yield 43} };

  {
    let x = g();
    assertEquals({value: 43, done: false}, x.next());
    assertEquals({value: 42, done: true}, x.next());
  }
}


{ // return that doesn't close
  let x;
  let g = function*() { try {return 42} finally {x.throw(666)} };

  {
    x = g();
    assertThrows(() => x.next(), TypeError);  // still executing
  }
}


{ // yield in try-finally, finally clause performs return

  let g = function*() { try {yield 42} finally {return 13} };

  { // "return" closes at suspendedStart
    let x = g();
    assertEquals({value: 666, done: true}, x.return(666));
    assertEquals({value: undefined, done: true}, x.next(42));
    assertThrowsEquals(() => x.throw(43), 43);
    assertEquals({value: 42, done: true}, x.return(42));
  }

  { // "throw" closes at suspendedStart
    let x = g();
    assertThrowsEquals(() => x.throw(666), 666);
    assertEquals({value: undefined, done: true}, x.next(42));
    assertEquals({value: 43, done: true}, x.return(43));
    assertThrowsEquals(() => x.throw(44), 44);
  }

  { // "next" closes at suspendedYield
    let x = g();
    assertEquals({value: 42, done: false}, x.next());
    assertEquals({value: 13, done: true}, x.next(666));
    assertEquals({value: undefined, done: true}, x.next(666));
    assertThrowsEquals(() => x.throw(666), 666);
  }

  { // "return" closes at suspendedYield
    let x = g();
    assertEquals({value: 42, done: false}, x.next());
    assertEquals({value: 13, done: true}, x.return(666));
    assertEquals({value: undefined, done: true}, x.next(666));
    assertEquals({value: 666, done: true}, x.return(666));
  }

  { // "throw" closes at suspendedYield
    let x = g();
    assertEquals({value: 42, done: false}, x.next());
    assertEquals({value: 13, done: true}, x.throw(666));
    assertThrowsEquals(() => x.throw(666), 666);
    assertEquals({value: undefined, done: true}, x.next(666));
  }
}


{ // yield in try-finally, finally clause doesn't perform return

  let g = function*() { try {yield 42} finally {13} };

  { // "return" closes at suspendedStart
    let x = g();
    assertEquals({value: 666, done: true}, x.return(666));
    assertEquals({value: undefined, done: true}, x.next(42));
    assertThrowsEquals(() => x.throw(43), 43);
    assertEquals({value: 42, done: true}, x.return(42));
  }

  { // "throw" closes at suspendedStart
    let x = g();
    assertThrowsEquals(() => x.throw(666), 666);
    assertEquals({value: undefined, done: true}, x.next(42));
    assertEquals({value: 43, done: true}, x.return(43));
    assertThrowsEquals(() => x.throw(44), 44);
  }

  { // "next" closes at suspendedYield
    let x = g();
    assertEquals({value: 42, done: false}, x.next());
    assertEquals({value: undefined, done: true}, x.next(666));
    assertEquals({value: undefined, done: true}, x.next(666));
    assertThrowsEquals(() => x.throw(666), 666);
    assertEquals({value: 42, done: true}, x.return(42));
  }

  { // "return" closes at suspendedYield
    let x = g();
    assertEquals({value: 42, done: false}, x.next());
    assertEquals({value: 666, done: true}, x.return(666));
    assertEquals({value: undefined, done: true}, x.next(666));
    assertThrowsEquals(() => x.throw(44), 44);
    assertEquals({value: 42, done: true}, x.return(42));
  }

  { // "throw" closes at suspendedYield
    let x = g();
    assertEquals({value: 42, done: false}, x.next());
    assertThrowsEquals(() => x.throw(666), 666);
    assertEquals({value: undefined, done: true}, x.next(666));
    assertThrowsEquals(() => x.throw(666), 666);
    assertEquals({value: 42, done: true}, x.return(42));
  }
}


{ // yield in try-finally, finally clause yields and performs return

  let g = function*() { try {yield 42} finally {yield 43; return 13} };

  {
    let x = g();
    assertEquals({value: 42, done: false}, x.next());
    assertEquals({value: 43, done: false}, x.return(666));
    assertEquals({value: 13, done: true}, x.next());
    assertEquals({value: 666, done: true}, x.return(666));
  }

  {
    let x = g();
    assertEquals({value: 666, done: true}, x.return(666));
    assertEquals({value: undefined, done: true}, x.next());
    assertEquals({value: 666, done: true}, x.return(666));
  }
}


{ // yield in try-finally, finally clause yields and doesn't perform return

  let g = function*() { try {yield 42} finally {yield 43; 13} };

  {
    let x = g();
    assertEquals({value: 42, done: false}, x.next());
    assertEquals({value: 43, done: false}, x.return(666));
    assertEquals({value: 666, done: true}, x.next());
    assertEquals({value: 5, done: true}, x.return(5));
  }

  {
    let x = g();
    assertEquals({value: 666, done: true}, x.return(666));
    assertEquals({value: undefined, done: true}, x.next());
    assertEquals({value: 666, done: true}, x.return(666));
  }
}


{ // yield*, finally clause performs return

  let h = function*() { try {yield 42} finally {yield 43; return 13} };
  let g = function*() { yield 1; yield yield* h(); };

  {
    let x = g();
    assertEquals({value: 1, done: false}, x.next());
    assertEquals({value: 42, done: false}, x.next());
    assertEquals({value: 43, done: false}, x.next(666));
    assertEquals({value: 13, done: false}, x.next());
    assertEquals({value: undefined, done: true}, x.next());
  }

  {
    let x = g();
    assertEquals({value: 1, done: false}, x.next());
    assertEquals({value: 42, done: false}, x.next());
    assertEquals({value: 43, done: false}, x.return(666));
    assertEquals({value: 13, done: false}, x.next());
    assertEquals({value: undefined, done: true}, x.next());
  }

  {
    let x = g();
    assertEquals({value: 1, done: false}, x.next());
    assertEquals({value: 42, done: false}, x.next());
    assertEquals({value: 43, done: false}, x.throw(666));
    assertEquals({value: 13, done: false}, x.next());
    assertEquals({value: undefined, done: true}, x.next());
  }
}


{ // yield*, finally clause does not perform return

  let h = function*() { try {yield 42} finally {yield 43; 13} };
  let g = function*() { yield 1; yield yield* h(); };

  {
    let x = g();
    assertEquals({value: 1, done: false}, x.next());
    assertEquals({value: 42, done: false}, x.next());
    assertEquals({value: 43, done: false}, x.next(666));
    assertEquals({value: undefined, done: false}, x.next());
    assertEquals({value: undefined, done: true}, x.next());
  }

  {
    let x = g();
    assertEquals({value: 1, done: false}, x.next());
    assertEquals({value: 42, done: false}, x.next());
    assertEquals({value: 43, done: false}, x.return(44));
    assertEquals({value: 44, done: false}, x.next());
    assertEquals({value: undefined, done: true}, x.next());
  }

  {
    let x = g();
    assertEquals({value: 1, done: false}, x.next());
    assertEquals({value: 42, done: false}, x.next());
    assertEquals({value: 43, done: false}, x.throw(666));
    assertThrowsEquals(() => x.next(), 666);
  }
}


{ // yield*, .return argument is final result

  function* inner() {
    yield 2;
  }

  function* g() {
    yield 1;
    return yield* inner();
  }

  {
    let x = g();
    assertEquals({value: 1, done: false}, x.next());
    assertEquals({value: 2, done: false}, x.next());
    assertEquals({value: 42, done: true}, x.return(42));
  }
}


// More or less random tests from here on.


{
  function* foo() { }
  let g = foo();
  assertEquals({value: undefined, done: true}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
}

{
  function* foo() { return new.target }
  let g = foo();
  assertEquals({value: undefined, done: true}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
}

{
  function* foo() { throw 666; return 42}
  let g = foo();
  assertThrowsEquals(() => g.next(), 666);
  assertEquals({value: undefined, done: true}, g.next());
}

{
  function* foo(a) { return a; }
  let g = foo(42);
  assertEquals({value: 42, done: true}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
}

{
  function* foo(a) { a.iwashere = true; return a; }
  let x = {};
  let g = foo(x);
  assertEquals({value: {iwashere: true}, done: true}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
}

{
  let a = 42;
  function* foo() { return a; }
  let g = foo();
  assertEquals({value: 42, done: true}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
}

{
  let a = 40;
  function* foo(b) { return a + b; }
  let g = foo(2);
  assertEquals({value: 42, done: true}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
}

{
  let a = 40;
  function* foo(b) { a--; b++; return a + b; }
  let g = foo(2);
  assertEquals({value: 42, done: true}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
}

{
  let g;
  function* foo() { g.next() }
  g = foo();
  assertThrows(() => g.next(), TypeError);
  assertEquals({value: undefined, done: true}, g.next());
}

{
  function* foo() { yield 2; yield 3; yield 4 }
  g = foo();
  assertEquals({value: 2, done: false}, g.next());
  assertEquals({value: 3, done: false}, g.next());
  assertEquals({value: 4, done: false}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
}

{
  function* foo() { yield 2; if (true) { yield 3 }; yield 4 }
  g = foo();
  assertEquals({value: 2, done: false}, g.next());
  assertEquals({value: 3, done: false}, g.next());
  assertEquals({value: 4, done: false}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
}

{
  function* foo() { yield 2; if (true) { yield 3; yield 4 } }
  g = foo();
  assertEquals({value: 2, done: false}, g.next());
  assertEquals({value: 3, done: false}, g.next());
  assertEquals({value: 4, done: false}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
}

{
  function* foo() { yield 2; if (false) { yield 3 }; yield 4 }
  g = foo();
  assertEquals({value: 2, done: false}, g.next());
  assertEquals({value: 4, done: false}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
}

{
  function* foo() { yield 2; while (true) { yield 3 }; yield 4 }
  g = foo();
  assertEquals({value: 2, done: false}, g.next());
  assertEquals({value: 3, done: false}, g.next());
  assertEquals({value: 3, done: false}, g.next());
  assertEquals({value: 3, done: false}, g.next());
  assertEquals({value: 3, done: false}, g.next());
}

{
  function* foo() { yield 2; (yield 3) + 42; yield 4 }
  g = foo();
  assertEquals({value: 2, done: false}, g.next());
  assertEquals({value: 3, done: false}, g.next());
  assertEquals({value: 4, done: false}, g.next());
}

{
  function* foo() { yield 2; (do {yield 3}) + 42; yield 4 }
  g = foo();
  assertEquals({value: 2, done: false}, g.next());
  assertEquals({value: 3, done: false}, g.next());
  assertEquals({value: 4, done: false}, g.next());
}

{
  function* foo() { yield 2; return (yield 3) + 42; yield 4 }
  g = foo();
  assertEquals({value: 2, done: false}, g.next());
  assertEquals({value: 3, done: false}, g.next());
  assertEquals({value: 42, done: true}, g.next(0));
  assertEquals({value: undefined, done: true}, g.next());
}

{
  let x = 42;
  function* foo() {
    yield x;
    for (let x in {a: 1, b: 2}) {
      let i = 2;
      yield x;
      yield i;
      do {
        yield i;
      } while (i-- > 0);
    }
    yield x;
    return 5;
  }
  g = foo();
  assertEquals({value: 42, done: false}, g.next());
  assertEquals({value: 'a', done: false}, g.next());
  assertEquals({value: 2, done: false}, g.next());
  assertEquals({value: 2, done: false}, g.next());
  assertEquals({value: 1, done: false}, g.next());
  assertEquals({value: 0, done: false}, g.next());
  assertEquals({value: 'b', done: false}, g.next());
  assertEquals({value: 2, done: false}, g.next());
  assertEquals({value: 2, done: false}, g.next());
  assertEquals({value: 1, done: false}, g.next());
  assertEquals({value: 0, done: false}, g.next());
  assertEquals({value: 42, done: false}, g.next());
  assertEquals({value: 5, done: true}, g.next());
}

{
  let a = 3;
  function* foo() {
    let b = 4;
    yield 1;
    { let c = 5; yield 2; yield a; yield b; yield c; }
  }
  g = foo();
  assertEquals({value: 1, done: false}, g.next());
  assertEquals({value: 2, done: false}, g.next());
  assertEquals({value: 3, done: false}, g.next());
  assertEquals({value: 4, done: false}, g.next());
  assertEquals({value: 5, done: false}, g.next());
  assertEquals({value: undefined, done: true}, g.next());
}

{
  function* foo() {
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
    yield 42;
  }
  g = foo();
  for (let i = 0; i < 100; ++i) {
    assertEquals({value: 42, done: false}, g.next());
  }
  assertEquals({value: undefined, done: true}, g.next());
}
