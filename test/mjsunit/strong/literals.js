// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --strong-mode --allow-natives-syntax
// Flags: --harmony-arrow-functions --harmony-rest-parameters


(function WeakObjectLiterals() {
  assertTrue(!%IsStrong({}));
  assertTrue(!%IsStrong({a: 0, b: 0}));
  assertTrue(!%IsStrong({f: function(){}}));
  assertTrue(!%IsStrong(Realm.eval(Realm.current(),
                                   "({f: function(){}})")));
})();

(function StrongObjectLiterals() {
  'use strong';
  assertTrue(%IsStrong({}));
  assertTrue(%IsStrong({a: 0, b: 0}));
  assertTrue(%IsStrong({__proto__: {}, get a() {}, set b(x) {}}));
  assertTrue(%IsStrong({[Date() + ""]: 0, [Symbol()]: 0}));
  // TODO(rossberg): super does not work yet
  // assertTrue(%IsStrong({m() { super.m() }}));
  // Object literals with constant functions are treated specially,
  // but currently only on the toplevel.
  assertTrue(%IsStrong({f: function(){}}));
  // TODO(rossberg): implement strong object literals with functions
  // assertTrue(%IsStrong(Realm.eval(Realm.current(),
  //                      "'use strong'; ({f: function(){}})")));
})();

(function WeakArrayLiterals(...args) {
  assertTrue(!%IsStrong(args));
  assertTrue(!%IsStrong([]));
  assertTrue(!%IsStrong([1, 2, 3]));
  Array.prototype = {}
  assertTrue(!%IsStrong([]));
  assertTrue(!%IsStrong([1, 2, 3]));
})();

(function StrongArrayLiterals(...args) {
  'use strong';
  // TODO(rossberg): implement strong array literals
  // assertTrue(%IsStrong(args));
  // assertTrue(%IsStrong([]));
  // assertTrue(%IsStrong([1, 2, 3]));
  // Array.prototype = {}
  // assertTrue(%IsStrong([]));
  // assertTrue(%IsStrong([1, 2, 3]));
})(0);  // TODO(arv): drop dummy

(function WeakFunctionLiterals() {
  function f() {}
  assertTrue(!%IsStrong(f));
  assertTrue(!%IsStrong(function(){}));
  assertTrue(!%IsStrong(() => {}));
  assertTrue(!%IsStrong(x => x));
})();

(function StrongFunctionLiterals(g) {
  'use strong';
  function f() {}
  assertTrue(%IsStrong(f));
  assertTrue(%IsStrong(g));
  assertTrue(%IsStrong(function(){}));
  assertTrue(%IsStrong(() => {}));
  assertTrue(%IsStrong(x => x));
})(function() { 'use strong' });

(function WeakRegExpLiterals() {
  assertTrue(!%IsStrong(/abc/));
})();

(function StrongRegExpLiterals() {
  'use strong';
  // TODO(rossberg): implement strong regexp literals
  // assertTrue(%IsStrong(/abc/));
})();
