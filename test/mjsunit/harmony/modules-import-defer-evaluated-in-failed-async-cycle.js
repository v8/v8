// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-defer-import-eval --bundle

// A synchronous module in a cycle with a top-level-await module is kEvaluated
// before its cycle root finishes, and it stays kEvaluated if the root fails
// afterwards. Its deferred namespace must not be treated as evaluated then: an
// access has to go through EvaluateModuleSync, which rethrows the error of the
// cycle root.
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
throw new Error('root failed');

// JS_BUNDLE_MODULE_ENTRYPOINT
import {started, unblock} from 'setup.mjs';

const done = import('./root.mjs');
await started.promise;
const ns = globalThis.ns;

unblock.resolve();
let error;
try {
  await done;
} catch (e) {
  error = e;
}
assertEquals('root failed', error.message);

// The dependency is still kEvaluated, but its namespace rethrows the error of
// its cycle root.
assertThrows(() => ns.x, Error, 'root failed');
assertThrows(() => Reflect.ownKeys(ns), Error, 'root failed');
