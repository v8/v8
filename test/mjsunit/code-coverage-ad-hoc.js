// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-always-opt

// Test code coverage without explicitly activating it upfront.

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
[[function f() {}](f:2)
f();
f();](anonymous:1)
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
[var f = [() => 1](f:2);
f();
f();](anonymous:1)
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
[[function f() {
  [function g() {}](g:4)
  g();
  g();
}](f:2)
f();
f();](anonymous:1)

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
[[function fib(x) {
  if (x < 2) return 1;
  return fib(x-1) + fib(x-2);
}](fib:15)
fib(5);](anonymous:1)
`
);
