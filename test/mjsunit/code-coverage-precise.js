// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-always-opt

// Test precise code coverage.

function GetCoverage(source) {
  for (var script of %DebugCollectCoverage()) {
    if (script.script.source == source) return script.toplevel;
  }
  return undefined;
}

function ApplyCoverageToSource(source, range) {
  var content = "";
  var cursor = range.start;
  if (range.inner) for (var inner of range.inner) {
    content += source.substring(cursor, inner.start);
    content += ApplyCoverageToSource(source, inner);
    cursor = inner.end;
  }
  content += source.substring(cursor, range.end);
  return `[${content}](${range.name}:${range.count})`;
}

function TestCoverage(name, source, expectation) {
  source = source.trim();
  eval(source);
  %CollectGarbage("remove dead objects");
  var coverage = GetCoverage(source);
  if (expectation === undefined) {
    assertEquals(undefined, coverage);
  } else {
    expectation = expectation.trim();
    var result = ApplyCoverageToSource(source, coverage);
    print(result);
    assertEquals(expectation, result, name + " failed");
  }
}


// Without precise coverage enabled, we lose coverage data to the GC.
TestCoverage(
"call an IIFE",
`
(function f() {})();
`,
undefined  // The IIFE has been garbage-collected.
);

TestCoverage(
"call locally allocated function",
`
for (var i = 0; i < 10; i++) {
  let f = () => 1;
  i += f();
}
`,
undefined
);

// This does not happen with precise coverage enabled.
%DebugTogglePreciseCoverage(true);

TestCoverage(
"call an IIFE",
`
(function f() {})();
`,
`
[([function f() {}](f:1))();](anonymous:1)
`
);

TestCoverage(
"call locally allocated function",
`
for (var i = 0; i < 10; i++) {
  let f = () => 1;
  i += f();
}
`,
`
[for (var i = 0; i < 10; i++) {
  let f = [() => 1](f:5);
  i += f();
}](anonymous:1)
`
);

%DebugTogglePreciseCoverage(false);
