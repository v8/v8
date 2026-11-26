// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-defer-import-eval --bundle

// A synchronous module in a cycle with a top-level-await module is already
// kEvaluated while its cycle root is still kEvaluatingAsync. Its deferred
// namespace must not be treated as evaluated yet: until the root finishes, an
// access has to go through EvaluateModuleSync, which throws a TypeError
// because ReadyForSyncExecution is false. Once the root finishes, the same
// accesses succeed.
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

const done = import('./root.mjs');
await started.promise;
const ns = globalThis.ns;

// The cycle root is still evaluating asynchronously.
assertThrows(() => ns.x, TypeError);
assertThrows(() => Reflect.ownKeys(ns), TypeError);

unblock.resolve();
await done;

// The cycle root finished.
assertEquals(42, ns.x);
assertEquals(['x', Symbol.toStringTag], Reflect.ownKeys(ns));
