// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --sandbox-testing --no-concurrent-osr

// Verify that mutating JSFunction::shared_function_info while a frame is active
// on the stack traps in GetOsrOffsetAndFunctionForOSR when triggering runtime
// OSR compilation, even when donor and victim have a JumpLoop at the exact same
// bytecode offset (so CurrentBytecodeIsValidOSREntry would pass).

const kJSFunctionType = Sandbox.getInstanceTypeIdFor('JS_FUNCTION_TYPE');
const kSharedFunctionInfoOffset =
    Sandbox.getFieldOffset(kJSFunctionType, 'shared_function_info');
const memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

function donor(n, do_swap) {
  let sum = 0;
  for (let i = 0; i < n; i++) {
    if (i === 20) do_swap();
    if (i === 21) %OptimizeOsr();
    sum += i;
  }
  return sum + ({a: 1}).a + ({b: 2}).b + ({c: 3}).c;
}

function victim(n, do_swap) {
  let sum = 0;
  for (let i = 0; i < n; i++) {
    if (i === 20) do_swap();
    if (i === 21) %OptimizeOsr();
    sum += i;
  }
  return sum;
}

function swap_sfi() {
  const donor_sfi = memory.getUint32(
      Sandbox.getAddressOf(donor) + kSharedFunctionInfoOffset, true);
  memory.setUint32(
      Sandbox.getAddressOf(victim) + kSharedFunctionInfoOffset, donor_sfi,
      true);
}

%PrepareFunctionForOptimization(donor);
%PrepareFunctionForOptimization(victim);
donor(5, () => {});
victim(5, () => {});

victim(30, swap_sfi);
