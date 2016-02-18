// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-tailcalls --no-turbo-inlining

"use strict";

Error.prepareStackTrace = (error,stack) => {
  error.strace = stack;
  return error.message + "\n    at " + stack.join("\n    at ");
}


function CheckStackTrace(expected) {
  var e = new Error();
  e.stack;  // prepare stack trace
  var stack = e.strace;
  assertEquals("CheckStackTrace", stack[0].getFunctionName());
  for (var i = 0; i < expected.length; i++) {
    assertEquals(expected[i].name, stack[i + 1].getFunctionName());
  }
}
%NeverOptimizeFunction(CheckStackTrace);


function CheckArguments(expected, args) {
  args = Array.prototype.slice.call(args);
  assertEquals(expected, args);
}
%NeverOptimizeFunction(CheckArguments);


var CAN_INLINE_COMMENT  = "// Let it be inlined.";
var DONT_INLINE_COMMENT = (function() {
  var line = "// Don't inline. Don't inline. Don't inline. Don't inline.";
  for (var i = 0; i < 4; i++) {
    line += "\n  " + line;
  }
  return line;
})();


function ident_source(source, ident) {
  ident = " ".repeat(ident);
  return ident + source.replace(/\n/gi, "\n" + ident);
}


function run_tests() {

  function f_template_normal(f_inlinable, f_args) {
    var f_comment = f_inlinable ? CAN_INLINE_COMMENT : DONT_INLINE_COMMENT;
    var lines = [
      `function f(a) {`,
      `  ${f_comment}`,
      `  assertEquals(undefined, this);`,
      `  CheckArguments([${f_args}], arguments);`,
      `  CheckStackTrace([f, test]);`,
      `  %DeoptimizeNow();`,
      `  CheckArguments([${f_args}], arguments);`,
      `  CheckStackTrace([f, test]);`,
      `  return 42;`,
      `}`,
    ];
    return lines.join("\n");
  }

  function f_template_bound(f_inlinable, f_args) {
    var f_comment = f_inlinable ? CAN_INLINE_COMMENT : DONT_INLINE_COMMENT;
    var lines = [
      `function ff(a) {`,
      `  ${f_comment}`,
      `  assertEquals(153, this.a);`,
      `  CheckArguments([${f_args}], arguments);`,
      `  CheckStackTrace([ff, test]);`,
      `  %DeoptimizeNow();`,
      `  CheckArguments([${f_args}], arguments);`,
      `  CheckStackTrace([ff, test]);`,
      `  return 42;`,
      `}`,
      `var f = ff.bind({a: 153});`,
    ];
    return lines.join("\n");
  }

  function f_template_proxy(f_inlinable, f_args) {
    var f_comment = f_inlinable ? CAN_INLINE_COMMENT : DONT_INLINE_COMMENT;
    var lines = [
      `function ff(a) {`,
      `  ${f_comment}`,
      `  assertEquals(undefined, this);`,
      `  CheckArguments([${f_args}], arguments);`,
      `  CheckStackTrace([f, test]);`,
      `  %DeoptimizeNow();`,
      `  CheckArguments([${f_args}], arguments);`,
      `  CheckStackTrace([f, test]);`,
      `  return 42;`,
      `}`,
      `var f = new Proxy(ff, {});`,
    ];
    return lines.join("\n");
  }

  function g_template(g_inlinable, f_args, g_args) {
    var g_comment = g_inlinable ? CAN_INLINE_COMMENT : DONT_INLINE_COMMENT;
    var lines = [
      `function g(a) {`,
      `  ${g_comment}`,
      `  CheckArguments([${g_args}], arguments);`,
      `  return f(${f_args});`,
      `}`,
    ];
    return lines.join("\n");
  }

  function test_template(f_source, g_source, g_args,
                         f_inlinable, g_inlinable) {
    f_source = ident_source(f_source, 2);
    g_source = ident_source(g_source, 2);

    var lines = [
      `(function() {`,
      f_source,
      g_source,
      `  function test() {`,
      `    assertEquals(42, g(${g_args}));`,
      `  }`,
      `  ${f_inlinable ? "%SetForceInlineFlag(f)" : ""};`,
      `  ${g_inlinable ? "%SetForceInlineFlag(g)" : ""};`,
      ``,
      `  test();`,
      `  %OptimizeFunctionOnNextCall(test);`,
      `  try { %OptimizeFunctionOnNextCall(f); } catch(e) {}`,
      `  try { %OptimizeFunctionOnNextCall(ff); } catch(e) {}`,
      `  %OptimizeFunctionOnNextCall(g);`,
      `  test();`,
      `})();`,
      ``,
    ];
    var source = lines.join("\n");
    return source;
  }

  // TODO(v8:4698), TODO(ishell): support all commented cases.
  var f_args_variants = ["", "1", "1, 2"];
  var g_args_variants = [/*"",*/ "10", /*"10, 20"*/];
  var f_inlinable_variants = [/*true,*/ false];
  var g_inlinable_variants = [true, false];
  var f_variants = [
      f_template_normal,
      f_template_bound,
      f_template_proxy
  ];

  f_variants.forEach((f_template) => {
    f_args_variants.forEach((f_args) => {
      g_args_variants.forEach((g_args) => {
        f_inlinable_variants.forEach((f_inlinable) => {
          g_inlinable_variants.forEach((g_inlinable) => {
            var f_source = f_template(f_inlinable, f_args);
            var g_source = g_template(g_inlinable, f_args, g_args);
            var source = test_template(f_source, g_source, g_args,
                                       f_inlinable, g_inlinable);
            print("====================");
            print(source);
            eval(source);
          });
        });
      });
    });
  });
}

run_tests();
