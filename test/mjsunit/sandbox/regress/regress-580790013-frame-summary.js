// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-memory-corruption-api --allow-natives-syntax

const memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));
const kHeapObjectTag = 1;
const kJSFunctionType = Sandbox.getInstanceTypeIdFor('JS_FUNCTION_TYPE');
const kSharedFunctionInfoType =
    Sandbox.getInstanceTypeIdFor('SHARED_FUNCTION_INFO_TYPE');
const kJSFunctionSFIOffset =
    Sandbox.getFieldOffset(kJSFunctionType, 'shared_function_info');
const kSharedFunctionInfoTrustedFunctionDataOffset =
    Sandbox.getFieldOffset(kSharedFunctionInfoType, 'trusted_function_data');

function getSfiDataHandle(fn) {
  const fnAddr = Sandbox.getAddressOf(fn);
  const sfi =
      memory.getUint32(fnAddr + kJSFunctionSFIOffset, true) - kHeapObjectTag;
  return memory.getUint32(
      sfi + kSharedFunctionInfoTrustedFunctionDataOffset, true);
}

function setSfiDataHandle(fn, handle) {
  const fnAddr = Sandbox.getAddressOf(fn);
  const sfi =
      memory.getUint32(fnAddr + kJSFunctionSFIOffset, true) - kHeapObjectTag;
  memory.setUint32(
      sfi + kSharedFunctionInfoTrustedFunctionDataOffset, handle, true);
}

// 1. Lightweight walk: OptimizedJSFrame::Summarize
function inlinedLightweight() {
  return new Error().stack;
}

function outerLightweight() {
  return inlinedLightweight();
}

%PrepareFunctionForOptimization(inlinedLightweight);
%PrepareFunctionForOptimization(outerLightweight);
outerLightweight();
outerLightweight();
%OptimizeFunctionOnNextCall(outerLightweight);
outerLightweight();

if (isOptimized(outerLightweight)) {
  const originalDataLight = getSfiDataHandle(inlinedLightweight);
  setSfiDataHandle(inlinedLightweight, 0);
  try {
    const stack = outerLightweight();
    assertTrue(stack.includes('inlinedLightweight'));
    assertTrue(stack.includes('outerLightweight'));
  } finally {
    setSfiDataHandle(inlinedLightweight, originalDataLight);
  }
}

// 2. Full walk: OptimizedJSFrame::SummarizeFull (forced via unboxed double
// receiver)
function inlinedFull(val) {
  return new Error().stack;
}

function outerFull(x) {
  return inlinedFull.call(x + 0.5, x);
}

%PrepareFunctionForOptimization(inlinedFull);
%PrepareFunctionForOptimization(outerFull);
outerFull(1);
outerFull(2);
%OptimizeFunctionOnNextCall(outerFull);
outerFull(3);

if (isOptimized(outerFull)) {
  const originalDataFull = getSfiDataHandle(inlinedFull);
  setSfiDataHandle(inlinedFull, 0);
  try {
    const stack = outerFull(4);
    assertTrue(stack.includes('inlinedFull'));
    assertTrue(stack.includes('outerFull'));
  } finally {
    setSfiDataHandle(inlinedFull, originalDataFull);
  }
}
