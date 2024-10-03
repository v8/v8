// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
/*---
description: >
  GetModuleSource of SourceTextModule throws a ReferenceError.
esid: sec-source-text-module-record-getmodulesource
features: [source-phase-imports]
flags: [async]
includes: [asyncHelpers.js]
---*/

asyncTest(async function () {
  await assert.throwsAsync(
    ReferenceError,
    () => import.source('./modules-simple_FIXTURE.js'),
    "Promise should be rejected with ReferenceError");

  // Import a module that has a source phase import.
  await assert.throwsAsync(
    ReferenceError,
    () => import('./modules-import-source_FIXTURE.js'),
    "Promise should be rejected with ReferenceError in indirect import source");
});
