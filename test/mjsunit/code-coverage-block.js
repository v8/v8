// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-always-opt --ignition --block-coverage

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

function nop() {}

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
function g() {}                           // 0000
function f(x) {                           // 0050
  if (x == 42) {                          // 0100
    if (x == 43) g(); else g();           // 0150
  }                                       // 0200
  if (x == 42) { g(); } else { g(); }     // 0250
  if (x == 42) g(); else g();             // 0300
  if (false) g(); else g();               // 0350
  if (false) g();                         // 0400
  if (true) g(); else g();                // 0450
  if (true) g();                          // 0500
}                                         // 0550
f(42);                                    // 0600
f(43);                                    // 0650
`,
[{"start":0,"end":699,"count":1},
 {"start":0,"end":15,"count":11},
 {"start":50,"end":551,"count":2},
 {"start":115,"end":203,"count":1},
 {"start":167,"end":171,"count":0},
 {"start":265,"end":273,"count":1},
 {"start":279,"end":287,"count":1},
 {"start":315,"end":319,"count":1},
 {"start":325,"end":329,"count":1},
 {"start":363,"end":367,"count":0},
 {"start":413,"end":417,"count":0},
 {"start":472,"end":476,"count":0}]

);

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
 {"start":161,"end":351,"count":0}]
);

TestCoverage(
"if statement (no semi-colon)",
`
!function() {                             // 0000
  if (true) nop()                         // 0050
  if (true) nop(); else nop()             // 0100
  nop();                                  // 0150
}()                                       // 0200
`,
[{"start":0,"end":249,"count":1},
 {"start":1,"end":201,"count":1},
 {"start":124,"end":129,"count":0}]
);

TestCoverage(
"for statements",
`
function g() {}                           // 0000
!function() {                             // 0050
  for (var i = 0; i < 12; i++) g();       // 0100
  for (var i = 0; i < 12; i++) {          // 0150
    g();                                  // 0200
  }                                       // 0250
  for (var i = 0; false; i++) g();        // 0300
  for (var i = 0; true; i++) break;       // 0350
  for (var i = 0; i < 12; i++) {          // 0400
    if (i % 3 == 0) g(); else g();        // 0450
  }                                       // 0500
}();                                      // 0550
`,
[{"start":0,"end":599,"count":1},
 {"start":0,"end":15,"count":36},
 {"start":51,"end":551,"count":1},
 {"start":131,"end":135,"count":12},
 {"start":181,"end":253,"count":12},
 {"start":330,"end":334,"count":0},
 {"start":431,"end":503,"count":12},
 {"start":470,"end":474,"count":4},
 {"start":480,"end":484,"count":8}]
);

TestCoverage(
"for statements pt. 2",
`
function g() {}                           // 0000
!function() {                             // 0050
  let j = 0;                              // 0100
  for (let i = 0; i < 12; i++) g();       // 0150
  for (const i = 0; j < 12; j++) g();     // 0200
  for (j = 0; j < 12; j++) g();           // 0250
  for (;;) break;                         // 0300
}();                                      // 0350
`,
[{"start":0,"end":399,"count":1},
 {"start":0,"end":15,"count":36},
 {"start":51,"end":351,"count":1},
 {"start":181,"end":185,"count":12},
 {"start":233,"end":237,"count":12},
 {"start":277,"end":281,"count":12}]
);

TestCoverage(
"for statements (no semicolon)",
`
function g() {}                           // 0000
!function() {                             // 0050
  for (let i = 0; i < 12; i++) g()        // 0100
  for (let i = 0; i < 12; i++) break      // 0150
  for (let i = 0; i < 12; i++) break; g() // 0200
}();                                      // 0250
`,
[{"start":0,"end":299,"count":1},
 {"start":0,"end":15,"count":13},
 {"start":51,"end":251,"count":1},
 {"start":131,"end":134,"count":12}]
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
 {"start":81,"end":253,"count":10},
 {"start":163,"end":253,"count":0},
 {"start":460,"end":553,"count":0},
 {"start":761,"end":951,"count":0}]
);

TestCoverage(
"while and do-while statements",
`
function g() {}                           // 0000
!function() {                             // 0050
  var i;                                  // 0100
  i = 0; while (i < 12) i++;              // 0150
  i = 0; while (i < 12) { g(); i++; }     // 0200
  i = 0; while (false) g();               // 0250
  i = 0; while (true) break;              // 0300
                                          // 0350
  i = 0; do i++; while (i < 12);          // 0400
  i = 0; do { g(); i++; }                 // 0450
         while (i < 12);                  // 0500
  i = 0; do { g(); } while (false);       // 0550
  i = 0; do { break; } while (true);      // 0600
}();                                      // 0650
`,
[{"start":0,"end":699,"count":1},
 {"start":0,"end":15,"count":25},
 {"start":51,"end":651,"count":1},
 {"start":174,"end":178,"count":12},
 {"start":224,"end":237,"count":12},
 {"start":273,"end":277,"count":0},
 {"start":412,"end":416,"count":12},
 {"start":462,"end":475,"count":12},
 {"start":620,"end":622,"count":0}]
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
 {"start":117,"end":303,"count":10},
 {"start":213,"end":303,"count":0},
 {"start":510,"end":603,"count":0},
 {"start":811,"end":1001,"count":0}]
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
 {"start":105,"end":303,"count":10},
 {"start":213,"end":303,"count":0},
 {"start":510,"end":603,"count":0},
 {"start":811,"end":1001,"count":0}]
);

TestCoverage(
"return statements",
`
!function() { nop(); return; nop(); }();  // 0000
!function() { nop(); return 42;           // 0050
              nop(); }();                 // 0100
`,
[{"start":0,"end":149,"count":1},
 {"start":1,"end":37,"count":1},
 {"start":28,"end":37,"count":0},
 {"start":51,"end":122,"count":1},
 {"start":81,"end":122,"count":0}]
);

TestCoverage(
"try/catch/finally statements",
`
!function() {                             // 0000
  try { nop(); } catch (e) { nop(); }     // 0050
  try { nop(); } finally { nop(); }       // 0100
  try {                                   // 0150
    try { throw 42; } catch (e) { nop(); }// 0200
  } catch (e) { nop(); }                  // 0250
  try {                                   // 0300
    try { throw 42; } finally { nop(); }  // 0350
  } catch (e) { nop(); }                  // 0400
  try {                                   // 0450
    throw 42;                             // 0500
  } catch (e) {                           // 0550
    nop();                                // 0600
  } finally {                             // 0650
    nop();                                // 0700
  }                                       // 0750
}();                                      // 0800
`,
[{"start":0,"end":849,"count":1},
 {"start":1,"end":801,"count":1},
 {"start":77,"end":87,"count":0},
 {"start":219,"end":232,"count":0},
 {"start":264,"end":274,"count":0},
 {"start":369,"end":380,"count":0},
 {"start":513,"end":564,"count":0}]
);

TestCoverage(
"early return in blocks",
`
!function() {                             // 0000
  try { throw 42; } catch (e) { return; } // 0050
  nop();                                  // 0100
}();                                      // 0150
!function() {                             // 0200
  try { nop(); } finally { return; }      // 0250
  nop();                                  // 0300
}();                                      // 0350
!function() {                             // 0400
  {                                       // 0450
    let x = 42;                           // 0500
    return () => x;                       // 0550
  }                                       // 0600
  nop();                                  // 0650
}();                                      // 0700
`,
[{"start":0,"end":749,"count":1},
 {"start":1,"end":151,"count":1},
 {"start":67,"end":80,"count":0},
 {"start":89,"end":91,"count":0},  // TODO(jgruber): Missing continuation.
 {"start":201,"end":351,"count":1},
 {"start":284,"end":286,"count":0},  // TODO(jgruber): Missing continuation.
 {"start":401,"end":701,"count":1},
 {"start":569,"end":701,"count":0},
 {"start":561,"end":568,"count":0}]  // TODO(jgruber): Sorting.
);

TestCoverage(
"switch statements",
`
!function() {                             // 0000
  var x = 42;                             // 0050
  switch (x) {                            // 0100
    case 41: nop(); break;                // 0150
    case 42: nop(); break;                // 0200
    default: nop(); break;                // 0250
  }                                       // 0300
}();                                      // 0350
`,
[{"start":0,"end":399,"count":1},
 {"start":1,"end":351,"count":1},
 {"start":154,"end":204,"count":0},
 {"start":226,"end":303,"count":0}]
);

%DebugToggleBlockCoverage(false);
