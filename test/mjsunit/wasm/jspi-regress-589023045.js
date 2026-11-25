// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-test-streaming

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// Enable debugger so suspending invokes Runtime_WasmSuspended ->
// GetWasmFrameCount.
d8.debugger.enable();

let resolveP1;
const p1 = new Promise(r => { resolveP1 = r; });
let callCount = 0;

const suspending = new WebAssembly.Suspending(() => {
  callCount++;
  if (callCount === 1) return p1;
  return Promise.resolve(42);
});

const builder1 = new WasmModuleBuilder();
const imp1 = builder1.addImport('env', 's', kSig_i_v);
builder1.addFunction('inner', kSig_i_v)
    .addBody([
      kExprCallFunction, imp1,
      kExprDrop,
      kExprCallFunction, imp1,
    ])
    .exportFunc();
const inst1 = builder1.instantiate({env: {s: suspending}});
const promising = WebAssembly.promising(inst1.exports.inner);

// Suspend on the first call to `suspending`, waiting on `p1`.
assertPromiseResult(promising(), result => assertEquals(42, result));

const builder2 = new WasmModuleBuilder();
const serializeImp = builder2.addImport('env', 'serialize', kSig_v_r);
const objGlobal = builder2.addImportedGlobal('env', 'obj', kWasmExternRef);
const startFunc = builder2.addFunction('outer', kSig_v_v)
    .addBody([
      kExprGlobalGet, objGlobal,
      kExprCallFunction, serializeImp,
    ]);
builder2.addStart(startFunc.index);
const bytes2 = builder2.toBuffer();

const triggerObj = {
  get x() {
    resolveP1(1);
    return 1;
  }
};

// Populate the NativeModuleCache so `instantiateStreaming` finishes in a
// foreground task with CallDepth == 0. When `d8.serializer.serialize` exits
// its EnterV8Scope inside the Wasm start function, the microtask checkpoint
// runs WasmResume (`WASM_JSPI`, not a JavaScriptFrame) above `$outer`
// (`WASM`). On the second suspend, GetWasmFrameCount must stop at the JSPI
// stack boundary instead of walking into `$outer`.
new WebAssembly.Module(bytes2);
assertPromiseResult(WebAssembly.instantiateStreaming(bytes2, {
  env: {
    serialize: d8.serializer.serialize,
    obj: triggerObj,
  }
}));
