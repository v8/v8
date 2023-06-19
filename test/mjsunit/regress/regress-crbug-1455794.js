// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

function prettyPrinted(value) {
  switch (typeof value) {
    case "symbol":
      return String(value);
  }
};

%PrepareFunctionForOptimization(prettyPrinted);

function f(o) {
  return o.length;
}

%PrepareFunctionForOptimization(f);
f({});
f('1');
%OptimizeMaglevOnNextCall(f);

try {
  prettyPrinted(f());
} catch {
}
