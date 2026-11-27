// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

utils.load('test/inspector/wasm-inspector-test.js');

const {contextGroup, Protocol} = InspectorTest.start(
    'Tests mutating locals and parameters on inactive JSPI stacks via the ' +
    'debugger followed by minor GC (crbug.com/590988412).');

const builder = new WasmModuleBuilder();
const sig_e_v = makeSig([], [kWasmExternRef]);
const imp = builder.addImport('imp', 'f', sig_e_v);
builder.addFunction('call_imp', sig_e_v)
    .addBody([kExprCallFunction, imp])
    .exportFunc();
const wasmBytes = builder.toArray();

contextGroup.addScript(
    `
const wasmBytes = new Uint8Array(${JSON.stringify(wasmBytes)});
const wasmModule = new WebAssembly.Module(wasmBytes);

function inner() {
  // Major GC marks inactive ancestor StackMemory instances as
  // contains_only_old_pointers_ = true.
  gc();
  debugger;
  // Minor GC must still scan stacks mutated by the debugger so young-gen
  // pointers written to inactive frames are updated/kept alive.
  gc({type: 'minor'});
  // Allocate some young-gen objects to overwrite freed space if the stack was
  // skipped.
  for (let i = 0; i < 1000; ++i) {
    ({a: i, b: i + 1});
  }
}

function makePromising(fn) {
  const inst = new WebAssembly.Instance(wasmModule, {imp: {f: fn}});
  return WebAssembly.promising(inst.exports.call_imp);
}

const pInner = makePromising(inner);

function outer(paramObj) {
  let localObj = {tag: 'old_local'};
  pInner();
  return localObj.tag + ':' + paramObj.tag;
}

function mid(paramObj) {
  let localObj = {tag: 'old_mid_local'};
  pInner();
  return localObj.tag + ':' + paramObj.tag;
}

const pMid = makePromising(() => mid({tag: 'old_mid_param'}));
`,
    0, 0, 'test.js');

async function allocateYoungObject(tag) {
  const res = await Protocol.Runtime.evaluate({
    expression: `({tag: '${tag}'})`,
  });
  return res.result.result.objectId;
}

(async function test() {
  await Protocol.Debugger.enable();

  // 1. Debugger.setVariableValue on local and parameter of inactive central
  // stack.
  {
    const pausedPromise = Protocol.Debugger.oncePaused();
    const evalPromise = Protocol.Runtime.evaluate({
      expression: 'outer({tag: \'old_param\'})',
      returnByValue: true,
    });
    const {params: {callFrames}} = await pausedPromise;
    const outerFrame = callFrames.find(f => f.functionName === 'outer');
    const localScopeIdx =
        outerFrame.scopeChain.findIndex(s => s.type === 'local');

    const localObjId = await allocateYoungObject('young_local_1');
    const paramObjId = await allocateYoungObject('young_param_1');

    await Protocol.Debugger.setVariableValue({
      scopeNumber: localScopeIdx,
      variableName: 'localObj',
      newValue: {objectId: localObjId},
      callFrameId: outerFrame.callFrameId,
    });
    await Protocol.Debugger.setVariableValue({
      scopeNumber: localScopeIdx,
      variableName: 'paramObj',
      newValue: {objectId: paramObjId},
      callFrameId: outerFrame.callFrameId,
    });

    // Release remote object handles so the inactive stack slots are the sole
    // references to the young objects.
    await Protocol.Runtime.releaseObject({objectId: localObjId});
    await Protocol.Runtime.releaseObject({objectId: paramObjId});

    await Protocol.Debugger.resume();
    const res = await evalPromise;
    InspectorTest.log('Case 1 (setVariableValue): ' + res.result.result.value);
  }

  // 2. Debugger.evaluateOnCallFrame mutating local and parameter of inactive
  // central stack.
  {
    const pausedPromise = Protocol.Debugger.oncePaused();
    const evalPromise = Protocol.Runtime.evaluate({
      expression: 'outer({tag: \'old_param\'})',
      returnByValue: true,
    });
    const {params: {callFrames}} = await pausedPromise;
    const outerFrame = callFrames.find(f => f.functionName === 'outer');

    await Protocol.Debugger.evaluateOnCallFrame({
      callFrameId: outerFrame.callFrameId,
      expression:
          'localObj = {tag: \'young_local_2\'}; paramObj = {tag: \'young_param_2\'}',
    });

    await Protocol.Debugger.resume();
    const res = await evalPromise;
    InspectorTest.log(
        'Case 2 (evaluateOnCallFrame): ' + res.result.result.value);
  }

  // 3. Nested JSPI: mutate local and parameter on intermediate inactive stack
  // (whose JS frames reside on the central stack but are owned by the
  // intermediate StackMemory).
  {
    const pausedPromise = Protocol.Debugger.oncePaused();
    const evalPromise = Protocol.Runtime.evaluate({
      expression: 'pMid()',
      awaitPromise: true,
      returnByValue: true,
    });
    const {params: {callFrames}} = await pausedPromise;
    const midFrame = callFrames.find(f => f.functionName === 'mid');
    const localScopeIdx =
        midFrame.scopeChain.findIndex(s => s.type === 'local');

    const localObjId = await allocateYoungObject('young_mid_local');
    await Protocol.Debugger.setVariableValue({
      scopeNumber: localScopeIdx,
      variableName: 'localObj',
      newValue: {objectId: localObjId},
      callFrameId: midFrame.callFrameId,
    });
    await Protocol.Runtime.releaseObject({objectId: localObjId});

    await Protocol.Debugger.evaluateOnCallFrame({
      callFrameId: midFrame.callFrameId,
      expression: 'paramObj = {tag: \'young_mid_param\'}',
    });

    await Protocol.Debugger.resume();
    const res = await evalPromise;
    InspectorTest.log('Case 3 (nested JSPI): ' + res.result.result.value);
  }

  await Protocol.Debugger.disable();
  InspectorTest.completeTest();
})();
