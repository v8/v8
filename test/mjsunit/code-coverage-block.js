// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-always-opt --block-coverage

// Test precise code coverage.

function GetCoverage(source) {
  for (var script of %DebugCollectCoverage()) {
    if (script.script.source == source) return script;
  }
  return undefined;
}

function TestCoverage(name, source, expectation) {
  source = source.trim();
  eval(source);
  %CollectGarbage("collect dead objects");
  var coverage = GetCoverage(source);
  var result = JSON.stringify(coverage);
  print(result);
  assertEquals(JSON.stringify(expectation), result, name + " failed");
}

%DebugToggleBlockCoverage(true);

TestCoverage(
"call an IIFE",
`
(function f() {})();
`,
[{"start":0,"end":20,"count":1},{"start":1,"end":16,"count":1}]
);

TestCoverage(
"call locally allocated function",
`
for (var i = 0; i < 10; i++) {
  let f = () => 1;
  i += f();
}
`,
[{"start":0,"end":63,"count":1},{"start":41,"end":48,"count":5}]
);

TestCoverage(
"if statements",
`
function g() {}
function f(x) {
  if (x == 42) {
    if (x == 43) g(); else g();
  }
  if (x == 42) { g(); } else { g(); }
  if (x == 42) g(); else g();
  if (false) g(); else g();
  if (false) g();
  if (true) g(); else g();
  if (true) g();
}
f(42);
f(43);
`,
[{"start":0,"end":258,"count":1},
 {"start":0,"end":15,"count":11},
 {"start":16,"end":244,"count":2},
 {"start":45,"end":83,"count":1},
 {"start":64,"end":69,"count":0},
 {"start":71,"end":79,"count":1},
 {"start":98,"end":107,"count":1},
 {"start":109,"end":121,"count":1},
 {"start":136,"end":141,"count":1},
 {"start":143,"end":151,"count":1},
 {"start":164,"end":169,"count":0},
 {"start":171,"end":179,"count":2},
 {"start":192,"end":197,"count":0},
 {"start":209,"end":214,"count":2},
 {"start":216,"end":224,"count":0},
 {"start":236,"end":241,"count":2}]
);

%DebugToggleBlockCoverage(false);
