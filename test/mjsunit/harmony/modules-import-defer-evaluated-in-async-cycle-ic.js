// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-defer-import-eval --bundle --allow-natives-syntax --turbofan
// Flags: --no-lazy-feedback-allocation

// A synchronous module in a cycle with a top-level-await module is already
// kEvaluated while its cycle root is still kEvaluatingAsync. A load from its
// deferred namespace has to throw a TypeError until the root finishes, so
// neither may an inline cache cache a handler for it, nor may optimized code
// fold it into a load of the export, before that.
// https://tc39.es/proposal-defer-import-eval/#sec-GetModuleExportsList

// JS_BUNDLE_MODULE:setup.mjs
export const started = Promise.withResolvers();
export const unblock = Promise.withResolvers();

// JS_BUNDLE_MODULE:dep.mjs
import './root.mjs';
export let x = 42;

// JS_BUNDLE_MODULE:root.mjs
import {started, unblock} from 'setup.mjs';
import './dep.mjs';
import defer * as ns from './dep.mjs';
globalThis.ns = ns;
started.resolve();
await unblock.promise;

// JS_BUNDLE_MODULE_ENTRYPOINT
import {started, unblock} from 'setup.mjs';

function load(o) {
  return o.x;
}

// The load of "x" never runs on the namespace before {optimizedLoad} is
// optimized, as that would already throw, so it only has feedback for other
// objects. It gets inlined after a load of "then", which is not an evaluation
// trigger: that load gives the compiler the map of the namespace, which it
// then uses for the load of "x" instead of the feedback.
function inlinedLoad(o, doLoad) {
  if (!doLoad) return 0;
  return o.x;
}
function optimizedLoad(o, doLoad) {
  let _ = o.then;
  return inlinedLoad(o, doLoad);
}
%PrepareFunctionForOptimization(inlinedLoad);
%PrepareFunctionForOptimization(optimizedLoad);
inlinedLoad({a: 1}, true);
inlinedLoad({b: 2}, true);
inlinedLoad({c: 3}, true);
inlinedLoad({d: 4}, true);
inlinedLoad({e: 5}, true);
inlinedLoad({f: 6}, true);

const done = import('./root.mjs');
await started.promise;
const ns = globalThis.ns;

// The cycle root is still evaluating asynchronously.
assertThrows(() => load(ns), TypeError);
assertThrows(() => load(ns), TypeError);
assertThrows(() => load(ns), TypeError);

optimizedLoad(ns, false);
%OptimizeFunctionOnNextCall(optimizedLoad);
optimizedLoad(ns, false);
assertOptimized(optimizedLoad);
assertThrows(() => optimizedLoad(ns, true), TypeError);

unblock.resolve();
await done;

// The cycle root finished.
assertEquals(42, load(ns));
assertEquals(42, load(ns));
assertEquals(42, optimizedLoad(ns, true));
