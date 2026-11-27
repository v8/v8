// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-memory-corruption-api

const {session, contextGroup, Protocol} = InspectorTest.start(
    'Harden Debug::Break against SFI corruption in breakpoint condition');

(async function test() {
  await Protocol.Debugger.enable();

  await Protocol.Runtime.evaluate({
    expression: `
      const memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));
      const kHeapObjectTag = 1;
      const kJSFunctionType = Sandbox.getInstanceTypeIdFor('JS_FUNCTION_TYPE');
      const kSharedFunctionInfoType =
          Sandbox.getInstanceTypeIdFor('SHARED_FUNCTION_INFO_TYPE');
      const kJSFunctionSFIOffset =
          Sandbox.getFieldOffset(kJSFunctionType, 'shared_function_info');
      const kSharedFunctionInfoTrustedFunctionDataOffset =
          Sandbox.getFieldOffset(kSharedFunctionInfoType, 'trusted_function_data');
      const kSharedFunctionInfoUniqueIdOffset =
          Sandbox.getFieldOffset(kSharedFunctionInfoType, 'unique_id');

      function donor() { return 1; }
      donor();

      function victim() {
        let v0 = 0;
        let v1 = 1;
        let v2 = 2;
        let v3 = 3;
        let v4 = 4;
        let v5 = 5;
        let v6 = 6;
        let v7 = 7;
        let v8 = 8;
        let v9 = 9;
        let v10 = 10;
        let v11 = 11;
        let v12 = 12;
        let v13 = 13;
        let v14 = 14;
        let v15 = 15;
        let v16 = 16;
        let v17 = 17;
        let v18 = 18;
        let v19 = 19;
        let v20 = 20;
        let v21 = 21;
        let v22 = 22;
        let v23 = 23;
        let v24 = 24;
        let v25 = 25;
        let v26 = 26;
        let v27 = 27;
        let v28 = 28;
        let v29 = 29;
        return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 +
               v10 + v11 + v12 + v13 + v14 + v15 + v16 + v17 + v18 + v19 +
               v20 + v21 + v22 + v23 + v24 + v25 + v26 + v27 + v28 + v29;
      }

      const victimAddr = Sandbox.getAddressOf(victim);
      const donorAddr = Sandbox.getAddressOf(donor);
      const victimSfi = memory.getUint32(victimAddr + kJSFunctionSFIOffset, true) - kHeapObjectTag;
      const donorSfi = memory.getUint32(donorAddr + kJSFunctionSFIOffset, true) - kHeapObjectTag;

      let savedData = 0;
      let savedUniqueId = 0;

      function corrupt() {
        savedData = memory.getUint32(victimSfi + kSharedFunctionInfoTrustedFunctionDataOffset, true);
        savedUniqueId = memory.getInt32(victimSfi + kSharedFunctionInfoUniqueIdOffset, true);
        const donorData = memory.getUint32(donorSfi + kSharedFunctionInfoTrustedFunctionDataOffset, true);
        memory.setUint32(victimSfi + kSharedFunctionInfoTrustedFunctionDataOffset, donorData, true);
        memory.setInt32(victimSfi + kSharedFunctionInfoUniqueIdOffset, 0xdeadbeef, true);
        return true;
      }

      function restore() {
        if (savedData !== 0) {
          memory.setUint32(victimSfi + kSharedFunctionInfoTrustedFunctionDataOffset, savedData, true);
          memory.setInt32(victimSfi + kSharedFunctionInfoUniqueIdOffset, savedUniqueId, true);
          savedData = 0;
          savedUniqueId = 0;
        }
      }
      //# sourceURL=test.js
    `
  });

  Protocol.Debugger.onPaused(async message => {
    InspectorTest.log('Paused successfully');
    await Protocol.Runtime.evaluate({expression: 'restore()'});
    await Protocol.Debugger.resume();
  });

  await Protocol.Debugger.setBreakpointByUrl(
      {url: 'test.js', lineNumber: 45, condition: 'corrupt()'});

  await Protocol.Runtime.evaluate({expression: 'victim()'});
  InspectorTest.log('Finished');
  InspectorTest.completeTest();
})();
