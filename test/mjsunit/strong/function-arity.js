// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --strong-mode --harmony-arrow-functions --harmony-reflect
// Flags: --harmony-spreadcalls --allow-natives-syntax

'use strict';


function generateArguments(n, prefix) {
  let a = [];
  if (prefix) {
    a.push(prefix);
  }
  for (let i = 0; i < n; i++) {
    a.push(String(i));
  }

  return a.join(', ');
}


function generateParams(n) {
  let a = [];
  for (let i = 0; i < n; i++) {
    a[i] = `p${i}`;
  }
  return a.join(', ');
}


function generateSpread(n) {
  return `...[${generateArguments(n)}]`;
}


(function FunctionCall() {
  for (let parameter_count = 0; parameter_count < 3; parameter_count++) {
    let defs = [
      `'use strong'; function f(${generateParams(parameter_count)}) {}`,
      `'use strong'; function* f(${generateParams(parameter_count)}) {}`,
      `'use strong'; let f = (${generateParams(parameter_count)}) => {}`,
      `function f(${generateParams(parameter_count)}) { 'use strong'; }`,
      `function* f(${generateParams(parameter_count)}) { 'use strong'; }`,
      `let f = (${generateParams(parameter_count)}) => { 'use strong'; }`,
    ];
    for (let def of defs) {
      for (let argument_count = 0; argument_count < 3; argument_count++) {
        let calls = [
          `f(${generateArguments(argument_count)})`,
          `f(${generateSpread(argument_count)})`,
          `f.call(${generateArguments(argument_count, 'undefined')})`,
          `f.call(undefined, ${generateSpread(argument_count)})`,
          `f.apply(undefined, [${generateArguments(argument_count)}])`,
          `f.bind(undefined)(${generateArguments(argument_count)})`,
          `%_CallFunction(${generateArguments(argument_count, 'undefined')},
                          f)`,
          `%Call(${generateArguments(argument_count, 'undefined')}, f)`,
          `%Apply(f, undefined, [${generateArguments(argument_count)}], 0,
                  ${argument_count})`,
        ];

        for (let call of calls) {
          let code = `'use strict'; ${def}; ${call};`;
          if (argument_count < parameter_count) {
            assertThrows(code, TypeError);
          } else {
            assertDoesNotThrow(code);
          }
        }
      }

      let calls = [
        `f.call()`,
        `f.apply()`,
        `f.apply(undefined)`,
      ];
      for (let call of calls) {
        let code = `'use strict'; ${def}; ${call};`;
        if (parameter_count > 0) {
          assertThrows(code, TypeError);
        } else {
          assertDoesNotThrow(code);
        }
      }
    }
  }
})();


(function MethodCall() {
  for (let parameter_count = 0; parameter_count < 3; parameter_count++) {
    let defs = [
      `let o = new class {
        m(${generateParams(parameter_count)}) { 'use strong'; }
      }`,
      `let o = new class {
        *m(${generateParams(parameter_count)}) { 'use strong'; }
      }`,
      `let o = { m(${generateParams(parameter_count)}) { 'use strong'; } }`,
      `let o = { *m(${generateParams(parameter_count)}) { 'use strong'; } }`,
      `'use strong';
      let o = new class { m(${generateParams(parameter_count)}) {} }`,
      `'use strong';
      let o = new class { *m(${generateParams(parameter_count)}) {} }`,
      `'use strong'; let o = { m(${generateParams(parameter_count)}) {} }`,
      `'use strong'; let o = { *m(${generateParams(parameter_count)}) {} }`,
    ];
    for (let def of defs) {
      for (let argument_count = 0; argument_count < 3; argument_count++) {
        let calls = [
          `o.m(${generateArguments(argument_count)})`,
          `o.m(${generateSpread(argument_count)})`,
          `o.m.call(${generateArguments(argument_count, 'o')})`,
          `o.m.call(o, ${generateSpread(argument_count)})`,
          `o.m.apply(o, [${generateArguments(argument_count)}])`,
          `o.m.bind(o)(${generateArguments(argument_count)})`,
          `%_CallFunction(${generateArguments(argument_count, 'o')}, o.m)`,
          `%Call(${generateArguments(argument_count, 'o')}, o.m)`,
          `%Apply(o.m, o, [${generateArguments(argument_count)}], 0,
                  ${argument_count})`,
        ];

        for (let call of calls) {
          let code = `'use strict'; ${def}; ${call};`;
          if (argument_count < parameter_count) {
            assertThrows(code, TypeError);
          } else {
            assertDoesNotThrow(code);
          }
        }
      }

      let calls = [
        `o.m.call()`,
        `o.m.apply()`,
        `o.m.apply(o)`,
      ];
      for (let call of calls) {
        let code = `'use strict'; ${def}; ${call};`;
        if (parameter_count > 0) {
          assertThrows(code, TypeError);
        } else {
          assertDoesNotThrow(code);
        }
      }
    }
  }
})();


(function Constructor() {
  for (let argument_count = 0; argument_count < 3; argument_count++) {
    for (let parameter_count = 0; parameter_count < 3; parameter_count++) {
      let defs = [
        `'use strong';
        class C { constructor(${generateParams(parameter_count)}) {} }`,
        `'use strict';
        class C {
          constructor(${generateParams(parameter_count)}) { 'use strong'; }
        }`,
      ];
      for (let def of defs) {
        let calls = [
          `new C(${generateArguments(argument_count)})`,
          `new C(${generateSpread(argument_count)})`,
          `Reflect.construct(C, [${generateArguments(argument_count)}])`,
        ];
        for (let call of calls) {
          let code = `${def}; ${call};`;
          if (argument_count < parameter_count) {
            assertThrows(code, TypeError);
          } else {
            assertDoesNotThrow(code);
          }
        }
      }
    }
  }
})();


(function DerivedConstructor() {
  for (let genArgs of [generateArguments, generateSpread]) {
    for (let argument_count = 0; argument_count < 3; argument_count++) {
      for (let parameter_count = 0; parameter_count < 3; parameter_count++) {
        let defs = [
          `'use strong';
          class B {
            constructor(${generateParams(parameter_count)}) {}
          }
          class C extends B {
            constructor() {
              super(${genArgs(argument_count)});
            }
          }`,
          `'use strict';
          class B {
            constructor(${generateParams(parameter_count)}) { 'use strong'; }
          }
          class C extends B {
            constructor() {
              super(${genArgs(argument_count)});
            }
          }`,
        ];
        for (let def of defs) {
          let code = `${def}; new C();`;
          if (argument_count < parameter_count) {
            assertThrows(code, TypeError);
          } else {
            assertDoesNotThrow(code);
          }
        }
      }
    }
  }
})();


(function DerivedConstructorDefaultConstructorInDerivedClass() {
  for (let genArgs of [generateArguments, generateSpread]) {
    for (let argument_count = 0; argument_count < 3; argument_count++) {
      for (let parameter_count = 0; parameter_count < 3; parameter_count++) {
        let defs = [
          `'use strong';
          class B {
            constructor(${generateParams(parameter_count)}) {}
          }
          class C extends B {}`,
          `'use strict';
          class B {
            constructor(${generateParams(parameter_count)}) { 'use strong'; }
          }
          class C extends B {}`,
        ];
        for (let def of defs) {
          let code = `${def}; new C(${genArgs(argument_count)})`;
          if (argument_count < parameter_count) {
            assertThrows(code, TypeError);
          } else {
            assertDoesNotThrow(code);
          }
        }
      }
    }
  }
})();


(function TestOptimized() {
  function f(x, y) { 'use strong'; }

  assertThrows(f, TypeError);
  %OptimizeFunctionOnNextCall(f);
  assertThrows(f, TypeError);

  function g() {
    f(1);
  }
  assertThrows(g, TypeError);
  %OptimizeFunctionOnNextCall(g);
  assertThrows(g, TypeError);

  f(1, 2);
  %OptimizeFunctionOnNextCall(f);
  f(1, 2);
})();


(function TestOptimized2() {
  'use strong';
  function f(x, y) {}

  assertThrows(f, TypeError);
  %OptimizeFunctionOnNextCall(f);
  assertThrows(f, TypeError);

  function g() {
    f(1);
  }
  assertThrows(g, TypeError);
  %OptimizeFunctionOnNextCall(g);
  assertThrows(g, TypeError);

  f(1, 2);
  %OptimizeFunctionOnNextCall(f);
  f(1, 2);
})();


(function TestOptimized3() {
  function f(x, y) {}
  function g() {
    'use strong';
    f(1);
  }

  g();
  %OptimizeFunctionOnNextCall(f);
  g();
})();


// https://code.google.com/p/v8/issues/detail?id=4077
// (function NoParametersSuper() {
//   'use strong';
//
//   class B {
//     m() {}
//   }
//   class D extends B {
//     m0() { super.m(); }
//     m1() { super.m(1); }
//     s0() { super.m(); }
//     s1() { super.m(1); }
//   }
//
//   new D().m0();
//   new D().m1();
//
//   new D().s0();
//   new D().s1();
// })();


// https://code.google.com/p/v8/issues/detail?id=4077
// (function OneParamentSuper() {
//   'use strong';
//
//   class B {
//     m(x) {}
//   }
//   class D extends B {
//     m0() { super.m(); }
//     m1() { super.m(1); }
//     m2() { super.m(1, 2); }
//     s0() { super.m(...[]); }
//     s1() { super.m(...[1]); }
//     s2() { super.m(...[1, 2]); }
//   }
//
//   assertThrows(function() { new D().m0(); }, TypeError);
//   new D().m1();
//   new D().m2();
//
//   assertThrows(function() { new D().s0(); }, TypeError);
//   new D().s1();
//   new D().s2();
// })();


// https://code.google.com/p/v8/issues/detail?id=4077
// (function TwoParametersSuper() {
//   'use strong';
//
//   class B {
//     m(x, y) {}
//   }
//   class D extends B {
//     m0() { super.m(); }
//     m1() { super.m(1); }
//     m2() { super.m(1, 2); }
//     m3() { super.m(1, 2, 3); }
//     s0() { super.m(...[]); }
//     s1() { super.m(...[1]); }
//     s2() { super.m(...[1, 2]); }
//     s3() { super.m(...[1, 2, 3]); }
//   }
//
//   assertThrows(function() { new D().m0(); }, TypeError);
//   assertThrows(function() { new D().m1(); }, TypeError);
//   new D().m2();
//   new D().m3();
//
//   assertThrows(function() { new D().s0(); }, TypeError);
//   assertThrows(function() { new D().s1(); }, TypeError);
//   new D().s2();
//   new D().s3();
// })();
