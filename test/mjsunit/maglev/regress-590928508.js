// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --trace-maglev --fuzzing

// Build a deep ConsString (depth > ConsStringIterator stack size of 32,
// length <= String::kMaxShortPrintLength = 1024) so StringShortPrint()
// invokes ConsStringIterator::Search().
function makeDeepCons() {
  let s = "aaaaaaaaaaaaa";
  for (let i = 0; i < 900; i++) {
    s += "b";
  }
  return s;
}

const strings = [];
const props = [];
for (let i = 0; i < 50; i++) {
  const s = makeDeepCons();
  strings.push(s);
  const name = "s" + i;
  Object.defineProperty(globalThis, name, { value: s, writable: false });
  props.push(name);
}

const foo = new Function("return [" + props.join(", ") + "];");

%PrepareFunctionForOptimization(foo);
assertEquals(strings, foo());

if (%IsMaglevEnabled() && %IsConcurrentRecompilationSupported() &&
    (%GetOptimizationStatus(foo) &
     V8OptimizationStatus.kOptimizeMaglevOptimizesToTurbofan) === 0) {
  %BlockAt("MaglevDeadCodeMarking", 1000);
  %OptimizeMaglevOnNextCall(foo, "concurrent");
  foo();
  if (!%ActiveTierIsMaglev(foo) &&
      %WaitUntilBlocked("MaglevDeadCodeMarking", 1000)) {
    %Resume("MaglevDeadCodeMarking");
    for (const s of strings) {
      %FlattenString(s);
    }
  } else {
    %Resume("MaglevDeadCodeMarking");
  }
  %WaitForBackgroundOptimization();
  assertMaglevved(foo);
}

assertEquals(strings, foo());
