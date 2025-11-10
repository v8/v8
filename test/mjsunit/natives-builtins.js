// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Enable fuzzing flag in case undefined NaNs are not enabled.
// Flags: --allow-natives-syntax --fuzzing

(function testNaNs() {
  const holeNaN = %GetHoleNaN();
  assertTrue(Number.isNaN(holeNaN));

  let dataView = new DataView(new ArrayBuffer(8));
  dataView.setUint32(0, %GetHoleNaNUpper(), true);
  dataView.setUint32(4, %GetHoleNaNLower(), true);
  let holeNaNViaDataView = dataView.getFloat64(0, true);
  assertTrue(Number.isNaN(holeNaNViaDataView));

  const undefinedNaN = %GetUndefinedNaN();
  assertTrue(Number.isNaN(undefinedNaN) || undefinedNaN === undefined);
})();
