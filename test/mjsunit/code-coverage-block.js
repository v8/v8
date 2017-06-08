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
  var covfefe = GetCoverage(source);
  var stringified_result = JSON.stringify(covfefe);
  var stringified_expectation = JSON.stringify(expectation);
  if (stringified_result != stringified_expectation) {
    print(JSON.stringify(covfefe, undefined, 1));
  }
  assertEquals(stringified_expectation, stringified_result, name + " failed");
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
`let f = () => 1; f();`,
[{"start":0,"end":21,"count":1},{"start":8,"end":15,"count":1}]
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

TestCoverage(
"for statements",
`
function g() {}
!function() {
  for (var i = 0; i < 12; i++) g();
  for (var i = 0; i < 12; i++) {
    g();
  }
  for (var i = 0; false; i++) g();
  for (var i = 0; true; i++) break;
  for (var i = 0; i < 12; i++) {
    if (i % 3 == 0) g(); else g();
  }
}();
`,
[{"start":0,"end":259,"count":1},
 {"start":0,"end":15,"count":36},
 {"start":17,"end":256,"count":1},
 {"start":59,"end":64,"count":12},
 {"start":95,"end":110,"count":12},
 {"start":140,"end":145,"count":0},
 {"start":174,"end":181,"count":1},
 {"start":212,"end":253,"count":12},
 {"start":234,"end":239,"count":4},
 {"start":241,"end":249,"count":8}]
);

TestCoverage(
"for statements pt. 2",
`
function g() {}
!function() {
  let j = 0;
  for (let i = 0; i < 12; i++) g();
  for (const i = 0; j < 12; j++) g();
  for (j = 0; j < 12; j++) g();
  for (;;) break;
}();
`,
[{"start":0,"end":171,"count":1},
 {"start":0,"end":15,"count":36},
 {"start":17,"end":168,"count":1},
 {"start":72,"end":77,"count":12},
 {"start":110,"end":115,"count":12},
 {"start":142,"end":147,"count":12},
 {"start":158,"end":165,"count":1}]
);

TestCoverage(
"while and do-while statements",
`
function g() {}
!function() {
  var i;
  i = 0; while (i < 12) i++;
  i = 0; while (i < 12) { g(); i++; }
  i = 0; while (false) g();
  i = 0; while (true) break;

  i = 0; do i++; while (i < 12);
  i = 0; do { g(); i++; } while (i < 12);
  i = 0; do { g(); } while (false);
  i = 0; do { break; } while (true);
}();
`,
[{"start":0,"end":316,"count":1},
 {"start":0,"end":15,"count":25},
 {"start":17,"end":313,"count":1},
 {"start":61,"end":66,"count":12},
 {"start":90,"end":104,"count":12},
 {"start":127,"end":132,"count":0},
 {"start":154,"end":161,"count":1},
 {"start":173,"end":179,"count":12},
 {"start":206,"end":221,"count":12},
 {"start":248,"end":258,"count":1},
 {"start":284,"end":296,"count":1}]
);

%DebugToggleBlockCoverage(false);
