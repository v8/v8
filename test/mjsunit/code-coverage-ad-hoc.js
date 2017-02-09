// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-always-opt

// Test code coverage without explicitly activating it upfront.

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
  expectation = expectation.trim();
  eval(source);
  var coverage = GetCoverage(source);
  var result = ApplyCoverageToSource(source, coverage);
  print(result);
  assertEquals(expectation, result, name + " failed");
}

TestCoverage(
"call simple function twice",
`
function f() {}
f();
f();
`,
`
[function f() {}[2]][
f();
f();[1]]
`
);

TestCoverage(
"call arrow function twice",
`
var f = () => 1;
f();
f();
`,
`
[var f = [1]][() => 1[2]][;
f();
f();[1]]
`
);

TestCoverage(
"call nested function",
`
function f() {
  function g() {}
  g();
  g();
}
f();
f();
`,
`
[function f() {
  [2]][function g() {}[4]][
  g();
  g();
}[2]][
f();
f();[1]]
`
);

TestCoverage(
"call recursive function",
`
function fib(x) {
  if (x < 2) return 1;
  return fib(x-1) + fib(x-2);
}
fib(5);
`,
`
[function fib(x) {
  if (x < 2) return 1;
  return fib(x-1) + fib(x-2);
}[15]][
fib(5);[1]]
`
);
