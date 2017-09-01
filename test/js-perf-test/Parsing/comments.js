// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const iterations = 100;

// Each benchmark.run is surrounded by a loop of 100. To generate the right
// amount of test cases, we just need to know this constant.
const fixed_iterations_by_runner = 100;

new BenchmarkSuite('OneLineComment', [1000], [
  new Benchmark('OneLineComment', false, true, iterations, Run, OneLineCommentSetup)
]);

new BenchmarkSuite('OneLineComments', [1000], [
  new Benchmark('OneLineComments', false, true, iterations, Run, OneLineCommentsSetup)
]);

new BenchmarkSuite('MultiLineComment', [1000], [
  new Benchmark('MultiLineComment', false, true, iterations, Run, MultiLineCommentSetup)
]);

let codes;
let ix;

// Functions generating the code snippets which the actual test will eval. The
// snippets need to be different for each iteration, so that we won't hit the
// compilation cache.

function OneLineCommentSetup() {
  codes = [];
  ix = 0;
  for (let i = 0; i < iterations * fixed_iterations_by_runner; ++i) {
    let code = "//" + " This is a comment... ".repeat(600) + i;
    codes.push(code);
  }
}

function OneLineCommentsSetup() {
  codes = [];
  ix = 0;
  for (let i = 0; i < iterations * fixed_iterations_by_runner; ++i) {
    let code = "// This is a comment.\n".repeat(600) + "\n//" + i;
    codes.push(code);
  }
}

function MultiLineCommentSetup() {
  codes = [];
  ix = 0;
  for (let i = 0; i < iterations * fixed_iterations_by_runner; ++i) {
    let code = "/*" + " This is a comment... ".repeat(600) + "*/" + i;
    codes.push(code);
  }
}

function Run() {
   if (ix >= codes.length) {
    throw new Error("Not enough test data");
  }
  eval(codes[ix]);
  ++ix;
}
