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
    print(stringified_result.replace(/[}],[{]/g, "},\n {"));
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
 {"start":80,"end":83,"count":1},
 {"start":84,"end":97,"count":2},
 {"start":98,"end":107,"count":1},
 {"start":109,"end":121,"count":1},
 {"start":122,"end":135,"count":2},
 {"start":136,"end":141,"count":1},
 {"start":143,"end":151,"count":1},
 {"start":152,"end":163,"count":2},
 {"start":164,"end":169,"count":0},
 {"start":171,"end":179,"count":2},
 {"start":180,"end":191,"count":2},
 {"start":192,"end":197,"count":0},
 {"start":198,"end":208,"count":2},
 {"start":209,"end":214,"count":2},
 {"start":216,"end":224,"count":0},
 {"start":225,"end":235,"count":2},
 {"start":236,"end":241,"count":2},
 {"start":242,"end":244,"count":2}]
);

function nop() {}

TestCoverage(
"if statement (early return)",
`
!function() {                             // 0000
  if (true) {                             // 0050
    nop();                                // 0100
    return;                               // 0150
    nop();                                // 0200
  }                                       // 0250
  nop();                                  // 0300
}()                                       // 0350
`,
[{"start":0,"end":399,"count":1},
 {"start":1,"end":351,"count":1},
 {"start":60,"end":252,"count":1},
 {"start":253,"end":351,"count":0}]
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
 {"start":65,"end":94,"count":1},
 {"start":95,"end":110,"count":12},
 {"start":111,"end":139,"count":1},
 {"start":140,"end":145,"count":0},
 {"start":146,"end":173,"count":1},
 {"start":174,"end":181,"count":1},
 {"start":182,"end":211,"count":1},
 {"start":212,"end":253,"count":12},
 {"start":234,"end":239,"count":4},
 {"start":241,"end":249,"count":8},
 {"start":250,"end":253,"count":12},
 {"start":254,"end":256,"count":1}]
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
 {"start":78,"end":109,"count":1},
 {"start":110,"end":115,"count":12},
 {"start":116,"end":141,"count":1},
 {"start":142,"end":147,"count":12},
 {"start":148,"end":157,"count":1},
 {"start":158,"end":165,"count":1},
 {"start":166,"end":168,"count":1}]
);

TestCoverage(
"for statement (early return)",
`
!function() {                             // 0000
  for (var i = 0; i < 10; i++) {          // 0050
    nop();                                // 0100
    continue;                             // 0150
    nop();                                // 0200
  }                                       // 0250
  nop();                                  // 0300
  for (;;) {                              // 0350
    nop();                                // 0400
    break;                                // 0450
    nop();                                // 0500
  }                                       // 0550
  nop();                                  // 0600
  for (;;) {                              // 0650
    nop();                                // 0700
    return;                               // 0750
    nop();                                // 0800
  }                                       // 0850
  nop();                                  // 0900
}()                                       // 0950
`,
[{"start":0,"end":999,"count":1},
 {"start":1,"end":951,"count":1},
 {"start":79,"end":252,"count":10},
 {"start":253,"end":358,"count":1},
 {"start":359,"end":552,"count":1},
 {"start":553,"end":658,"count":1},
 {"start":659,"end":852,"count":1},
 {"start":853,"end":951,"count":0}]
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
 {"start":67,"end":89,"count":1},
 {"start":90,"end":104,"count":12},
 {"start":105,"end":126,"count":1},
 {"start":127,"end":132,"count":0},
 {"start":133,"end":153,"count":1},
 {"start":154,"end":161,"count":1},
 {"start":162,"end":172,"count":1},
 {"start":173,"end":179,"count":12},
 {"start":180,"end":205,"count":1},
 {"start":206,"end":221,"count":12},
 {"start":222,"end":247,"count":1},
 {"start":248,"end":258,"count":1},
 {"start":259,"end":283,"count":1},
 {"start":284,"end":296,"count":1},
 {"start":297,"end":313,"count":1}]
);

TestCoverage(
"while statement (early return)",
`
!function() {                             // 0000
  let i = 0;                              // 0050
  while (i < 10) {                        // 0100
    i++;                                  // 0150
    continue;                             // 0200
    nop();                                // 0250
  }                                       // 0300
  nop();                                  // 0350
  while (true) {                          // 0400
    nop();                                // 0450
    break;                                // 0500
    nop();                                // 0550
  }                                       // 0600
  nop();                                  // 0650
  while (true) {                          // 0700
    nop();                                // 0750
    return;                               // 0800
    nop();                                // 0850
  }                                       // 0900
  nop();                                  // 0950
}()                                       // 1000
`,
[{"start":0,"end":1049,"count":1},
 {"start":1,"end":1001,"count":1},
 {"start":115,"end":302,"count":10},
 {"start":303,"end":412,"count":1},
 {"start":413,"end":602,"count":1},
 {"start":603,"end":712,"count":1},
 {"start":713,"end":902,"count":1},
 {"start":903,"end":1001,"count":0}]
);

TestCoverage(
"do-while statement (early return)",
`
!function() {                             // 0000
  let i = 0;                              // 0050
  do {                                    // 0100
    i++;                                  // 0150
    continue;                             // 0200
    nop();                                // 0250
  } while (i < 10);                       // 0300
  nop();                                  // 0350
  do {                                    // 0400
    nop();                                // 0450
    break;                                // 0500
    nop();                                // 0550
  } while (true);                         // 0600
  nop();                                  // 0650
  do {                                    // 0700
    nop();                                // 0750
    return;                               // 0800
    nop();                                // 0850
  } while (true);                         // 0900
  nop();                                  // 0950
}()                                       // 1000
`,
[{"start":0,"end":1049,"count":1},
 {"start":1,"end":1001,"count":1},
 {"start":102,"end":302,"count":10},
 {"start":303,"end":401,"count":1},
 {"start":402,"end":602,"count":1},
 {"start":603,"end":701,"count":1},
 {"start":702,"end":902,"count":1},
 {"start":903,"end":1001,"count":0}]
);

%DebugToggleBlockCoverage(false);
