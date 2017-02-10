// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-always-opt

// Test precise code coverage.

function GetCoverage(source) {
  var scripts = %DebugGetLoadedScripts();
  for (var script of scripts) {
    if (script.source == source) {
      var coverage = %DebugCollectCoverage();
      for (var data of coverage) {
        if (data.script_id == script.id) return data.entries;
      }
    }
  }
  return undefined;
}

function ApplyCoverageToSource(source, coverage) {
  var result = "";
  var cursor = 0;
  for (var entry of coverage) {
    var chunk = source.substring(cursor, entry.end_position);
    cursor = entry.end_position;
    result += `[${chunk}[${entry.count}]]`;
  }
  return result;
}

function TestCoverage(name, source, expectation) {
  source = source.trim();
  eval(source);
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
[(function f() {})();[1]]
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
  let f = [1]][() => 1[5]][;
  i += f();
}[1]]
`
);

%DebugTogglePreciseCoverage(false);
