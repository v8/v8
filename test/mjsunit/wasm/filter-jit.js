// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm
// Flags: --wasm-jit-prototype

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

var builder = (function () {
  var builder = new WasmModuleBuilder();

  var kSig_i_iiii = makeSig([kAstI32, kAstI32, kAstI32, kAstI32], [kAstI32]);
  var sig_index4 = builder.addType(kSig_i_iiii);

  builder.addMemory(1, 1, true);
  var filter1 = [
    01, 03, 01,
      // count = columns + 1;
          kExprGetLocal, 01,
          kExprI32Const, 01,
        kExprI32Add,
      kExprSetLocal, 04,
      // Offset: 10
      // Bytes:
      // for (i = 0; i < columns, ++i) {
      //   dst[i] = src[i];
      // }
        kExprI32Const, 00,
      kExprSetLocal, 06,
      kExprLoop,
            kExprGetLocal, 06,
            kExprGetLocal, 01,
          kExprI32GeS,
        kExprBrIf, 00, 01,
              kExprGetLocal, 03,
              kExprGetLocal, 06,
            kExprI32Add,
                kExprGetLocal, 02,
                kExprGetLocal, 06,
              kExprI32Add,
            kExprI32LoadMem8U, 00, 00,
          kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 06,
            kExprI32Const, 01,
          kExprI32Add,
        kExprSetLocal, 06,
        kExprBr, 00, 00,
      kExprEnd,
      // Offset: 50
      // Bytes:
      // for (i = (rows - 1) * columns;
      //      i < row * columns, ++i) {
      //   dst[i] = src[i];
      // }
            kExprGetLocal, 00,
            kExprI32Const, 01,
          kExprI32Sub,
          kExprGetLocal, 01,
        kExprI32Mul,
      kExprSetLocal, 06,
      kExprLoop,
            kExprGetLocal, 06,
              kExprGetLocal, 00,
              kExprGetLocal, 01,
            kExprI32Mul,
          kExprI32GeS,
        kExprBrIf, 00, 01,
              kExprGetLocal, 03,
              kExprGetLocal, 06,
            kExprI32Add,
                kExprGetLocal, 02,
                kExprGetLocal, 06,
              kExprI32Add,
            kExprI32LoadMem8U, 00, 00,
          kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 06,
            kExprI32Const, 01,
          kExprI32Add,
        kExprSetLocal, 06,
        kExprBr, 00, 00,
      kExprEnd,
      // Offset: 99
      // for(int i = 1; i < rows - 1; ++i) {
      //   dst[count - 1] = src[count - 1];
      //   dst[count + columns - 1]
      //     = src[count _ columns -1];
      // }
        kExprI32Const, 01,
      kExprSetLocal, 05,
      kExprLoop,
            kExprGetLocal, 05,
              kExprGetLocal, 00,
              kExprI32Const, 01,
            kExprI32Sub,
          kExprI32GeS,
        kExprBrIf, 00, 01,
      // Offset 115
            kExprGetLocal, 04,
            kExprI32Const, 01,
          kExprI32Sub,
        kExprSetLocal, 06,
            kExprGetLocal, 03,
            kExprGetLocal, 06,
          kExprI32Add,
              kExprGetLocal, 02,
              kExprGetLocal, 06,
            kExprI32Add,
          kExprI32LoadMem8U, 00, 00,
        kExprI32StoreMem8, 00, 00,
              kExprGetLocal, 04,
              kExprI32Const, 02,
            kExprI32Sub,
            kExprGetLocal, 01,
          kExprI32Add,
        kExprSetLocal, 06,
            kExprGetLocal, 03,
            kExprGetLocal, 06,
          kExprI32Add,
              kExprGetLocal, 02,
              kExprGetLocal, 06,
            kExprI32Add,
          kExprI32LoadMem8U, 00, 00,
        kExprI32StoreMem8, 00, 00,
        // Offset: 164
        // Bytes:
        // for (int j = 1; j < columns -1; ++j) {
        //  <Kernel to be jitted will be placed here>
        // }
          kExprI32Const, 01,
        kExprSetLocal, 06,
        kExprLoop,
              kExprGetLocal, 06,
                kExprGetLocal, 01,
                kExprI32Const, 01,
              kExprI32Sub,
            kExprI32GeS,
          kExprBrIf, 00, 01,
              kExprGetLocal, 03,
              kExprGetLocal, 04,
            kExprI32Add,
                 kExprI32Const, 00
      /* Offset 187 */
  ];
  builder.addDataSegment(0x1000, filter1, false);

  builder.addPadFunctionTable(10);
  builder.addFunction("main", sig_index4)
    .addLocals({i32_count: 1})
    .addBody([
        kExprI32Const, 0xbb, 0x21,
      kExprSetLocal, 04,
    // if (matrix[0] != 0)
    //   emit (sum += matrix[0] * src[count - columns - 1])
           kExprGetLocal, 03,
          kExprI32LoadMem8U, 00, 00,
          kExprI32Const, 00,
        kExprI32Ne,
      kExprIf,
           kExprGetLocal, 04,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x01, kExprI32Add,
        kExprI8Const, 02, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x02, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x03, kExprI32Add,
        kExprI8Const, 04, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x04, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x05, kExprI32Add,
        kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x06, kExprI32Add,
        kExprI8Const, kExprI32Sub, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x07, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x08, kExprI32Add,
        kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x09, kExprI32Add,
        kExprI8Const, kExprI32Sub, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0a, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0b, kExprI32Add,
        kExprI8Const, kExprI32LoadMem8U, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0c, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0d, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0e, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0f, kExprI32Add,
        kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x10, kExprI32Add,
        kExprI8Const, kExprI32Shl, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x11, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x12, kExprI32Add,
        kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x13, kExprI32Add,
        kExprI8Const, kExprI32ShrS, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x14, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x15, kExprI32Add,
          kExprGetLocal, 03,
        kExprI32LoadMem8U, 00, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x16, kExprI32Add,
        kExprI8Const, kExprI32Mul, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x17, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04,
            kExprI32Const, 0x18,
          kExprI32Add,
        kExprSetLocal, 04,
      kExprEnd,
    // if (matrix[1] != 0)
    //   emit (sum += matrix[1] * src[count - columns])
              kExprGetLocal, 03,
              kExprI32Const, 01,
            kExprI32Add,
          kExprI32LoadMem8U, 00, 00,
          kExprI32Const, 00,
        kExprI32Ne,
        kExprIf,
            kExprGetLocal, 04,
          kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x01, kExprI32Add,
          kExprI8Const, 02, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x02, kExprI32Add,
          kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x03, kExprI32Add,
          kExprI8Const, 04, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x04, kExprI32Add,
          kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x05, kExprI32Add,
          kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x06, kExprI32Add,
          kExprI8Const, kExprI32Sub, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x07, kExprI32Add,
          kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x08, kExprI32Add,
          kExprI8Const, kExprI32LoadMem8U, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x09, kExprI32Add,
          kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x0a, kExprI32Add,
          kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x0b, kExprI32Add,
          kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x0c, kExprI32Add,
          kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x0d, kExprI32Add,
          kExprI8Const, kExprI32Shl, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x0e, kExprI32Add,
          kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x0f, kExprI32Add,
          kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x10, kExprI32Add,
          kExprI8Const, kExprI32ShrS, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x11, kExprI32Add,
          kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x12, kExprI32Add,
            kExprGetLocal, 03, kExprI32Const, 01, kExprI32Add,
          kExprI32LoadMem8U, 00, 00, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x13, kExprI32Add,
          kExprI8Const, kExprI32Mul, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x14, kExprI32Add,
          kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
              kExprGetLocal, 04,
              kExprI32Const, 0x15,
            kExprI32Add,
          kExprSetLocal, 04,
        kExprEnd,
    // if (matrix[2] != 0)
    //   emit (sum += matrix[2] * src[count - columns + 1])
                kExprGetLocal, 03,
                kExprI32Const, 02,
              kExprI32Add,
            kExprI32LoadMem8U, 00, 00,
            kExprI32Const, 00,
          kExprI32Ne,
        kExprIf,
            kExprGetLocal, 04,
          kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x01, kExprI32Add,
          kExprI8Const, 02, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x02, kExprI32Add,
          kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x03, kExprI32Add,
          kExprI8Const, 04, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x04, kExprI32Add,
          kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x05, kExprI32Add,
          kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x06, kExprI32Add,
          kExprI8Const, kExprI32Sub, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x07, kExprI32Add,
          kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x08, kExprI32Add,
          kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x09, kExprI32Add,
          kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x0a, kExprI32Add,
          kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x0b, kExprI32Add,
          kExprI8Const, kExprI32LoadMem8U, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x0c, kExprI32Add,
          kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x0d, kExprI32Add,
          kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x0e, kExprI32Add,
          kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x0f, kExprI32Add,
          kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x10, kExprI32Add,
          kExprI8Const, kExprI32Shl, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x11, kExprI32Add,
          kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x12, kExprI32Add,
          kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x13, kExprI32Add,
          kExprI8Const, kExprI32ShrS, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x14, kExprI32Add,
          kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x15, kExprI32Add,
            kExprGetLocal, 03, kExprI32Const, 02, kExprI32Add,
          kExprI32LoadMem8U, 00, 00, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x16, kExprI32Add,
          kExprI8Const, kExprI32Mul, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x17, kExprI32Add,
          kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
              kExprGetLocal, 04,
              kExprI32Const, 0x18,
            kExprI32Add,
          kExprSetLocal, 04,
        kExprEnd,
    // if (matrix[3] != 0)
    //   emit (sum += matrix[3] * src[count - 1])
              kExprGetLocal, 03,
              kExprI32Const, 03,
            kExprI32Add,
          kExprI32LoadMem8U, 00, 00,
          kExprI32Const, 00,
        kExprI32Ne,
      kExprIf,
          kExprGetLocal, 04,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x01, kExprI32Add,
        kExprI8Const, 02, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x02, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x03, kExprI32Add,
        kExprI8Const, 04, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x04, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x05, kExprI32Add,
        kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x06, kExprI32Add,
        kExprI8Const, kExprI32Sub, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x07, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x08, kExprI32Add,
        kExprI8Const, kExprI32LoadMem8U, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x09, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0a, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0b, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0c, kExprI32Add,
        kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0d, kExprI32Add,
        kExprI8Const, kExprI32Shl, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0e, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0f, kExprI32Add,
        kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x10, kExprI32Add,
        kExprI8Const, kExprI32ShrS, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x11, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x12, kExprI32Add,
          kExprGetLocal, 03, kExprI32Const, 03, kExprI32Add,
        kExprI32LoadMem8U, 00, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x13, kExprI32Add,
        kExprI8Const, kExprI32Mul, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x14, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04,
            kExprI32Const, 0x15,
          kExprI32Add,
        kExprSetLocal, 04,
      kExprEnd,
    // if (matrix[4] != 0)
    //   emit (sum += matrix[4] * src[count])
              kExprGetLocal, 03,
              kExprI32Const, 04,
            kExprI32Add,
          kExprI32LoadMem8U, 00, 00,
          kExprI32Const, 00,
        kExprI32Ne,
      kExprIf,
          kExprGetLocal, 04,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x01, kExprI32Add,
        kExprI8Const, 02, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x02, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x03, kExprI32Add,
        kExprI8Const, 04, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x04, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x05, kExprI32Add,
        kExprI8Const, kExprI32LoadMem8U, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x06, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x07, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x08, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x09, kExprI32Add,
        kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0a, kExprI32Add,
        kExprI8Const, kExprI32Shl, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0b, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0c, kExprI32Add,
        kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0d, kExprI32Add,
        kExprI8Const, kExprI32ShrS, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0e, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0f, kExprI32Add,
          kExprGetLocal, 03, kExprI32Const, 04, kExprI32Add,
        kExprI32LoadMem8U, 00, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x10, kExprI32Add,
        kExprI8Const, kExprI32Mul, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x11, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04,
            kExprI32Const, 0x12,
          kExprI32Add,
       kExprSetLocal, 04,
     kExprEnd,
    // if (matrix[5] != 0)
    //   emit (sum += matrix[5] * src[count + 1])
              kExprGetLocal, 03,
              kExprI32Const, 05,
            kExprI32Add,
          kExprI32LoadMem8U, 00, 00,
          kExprI32Const, 00,
        kExprI32Ne,
      kExprIf,
         kExprGetLocal, 04,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x01, kExprI32Add,
        kExprI8Const, 02, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x02, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x03, kExprI32Add,
        kExprI8Const, 04, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x04, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x05, kExprI32Add,
        kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x06, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x07, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x08, kExprI32Add,
        kExprI8Const, kExprI32LoadMem8U, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x09, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0a, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0b, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0c, kExprI32Add,
        kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0d, kExprI32Add,
        kExprI8Const, kExprI32Shl, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0e, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0f, kExprI32Add,
        kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x10, kExprI32Add,
        kExprI8Const, kExprI32ShrS, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x11, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x12, kExprI32Add,
          kExprGetLocal, 03, kExprI32Const, 05, kExprI32Add,
        kExprI32LoadMem8U, 00, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x13, kExprI32Add,
        kExprI8Const, kExprI32Mul, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x14, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04,
            kExprI32Const, 0x15,
          kExprI32Add,
        kExprSetLocal, 04,
      kExprEnd,
    // if (matrix[6] != 0)
    //   emit (sum += matrix[6] * src[count + columns - 1])
              kExprGetLocal, 03,
              kExprI32Const, 06,
            kExprI32Add,
          kExprI32LoadMem8U, 00, 00,
          kExprI32Const, 00,
        kExprI32Ne,
      kExprIf,
          kExprGetLocal, 04,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x01, kExprI32Add,
        kExprI8Const, 02, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x02, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x03, kExprI32Add,
        kExprI8Const, 04, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x04, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x05, kExprI32Add,
        kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x06, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x07, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x08, kExprI32Add,
        kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x09, kExprI32Add,
        kExprI8Const, kExprI32Sub, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0a, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0b, kExprI32Add,
        kExprI8Const, kExprI32LoadMem8U, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0c, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0d, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0e, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0f, kExprI32Add,
        kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x10, kExprI32Add,
        kExprI8Const, kExprI32Shl, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x11, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x12, kExprI32Add,
        kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x13, kExprI32Add,
        kExprI8Const, kExprI32ShrS, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x14, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x15, kExprI32Add,
          kExprGetLocal, 03, kExprI32Const, 06, kExprI32Add,
        kExprI32LoadMem8U, 00, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x16, kExprI32Add,
        kExprI8Const, kExprI32Mul, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x17, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04,
            kExprI32Const, 0x18,
          kExprI32Add,
        kExprSetLocal, 04,
      kExprEnd,
    // if (matrix[7] != 0)
    //   emit (sum += matrix[7] * src[count + columns])
              kExprGetLocal, 03,
              kExprI32Const, 07,
            kExprI32Add,
          kExprI32LoadMem8U, 00, 00,
          kExprI32Const, 00,
        kExprI32Ne,
      kExprIf,
          kExprGetLocal, 04,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x01, kExprI32Add,
        kExprI8Const, 02, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x02, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x03, kExprI32Add,
        kExprI8Const, 04, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x04, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x05, kExprI32Add,
        kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x06, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x07, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x08, kExprI32Add,
        kExprI8Const, kExprI32LoadMem8U, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x09, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0a, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0b, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0c, kExprI32Add,
        kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0d, kExprI32Add,
        kExprI8Const, kExprI32Shl, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0e, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0f, kExprI32Add,
        kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x10, kExprI32Add,
        kExprI8Const, kExprI32ShrS, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x11, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04, kExprI32Const, 0x12, kExprI32Add,
            kExprGetLocal, 03, kExprI32Const, 07, kExprI32Add,
        kExprI32LoadMem8U, 00, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x13, kExprI32Add,
        kExprI8Const, kExprI32Mul, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x14, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04,
            kExprI32Const, 0x15,
          kExprI32Add,
        kExprSetLocal, 04,
      kExprEnd,
    // if (matrix[8] != 0)
    //   emit (sum += matrix[8] * src[count + columns])
              kExprGetLocal, 03,
              kExprI32Const, 08,
            kExprI32Add,
          kExprI32LoadMem8U, 00, 00,
          kExprI32Const, 00,
        kExprI32Ne,
      kExprIf,
          kExprGetLocal, 04,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x01, kExprI32Add,
        kExprI8Const, 02, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x02, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x03, kExprI32Add,
        kExprI8Const, 04, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x04, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x05, kExprI32Add,
        kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x06, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x07, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x08, kExprI32Add,
        kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x09, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0a, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0b, kExprI32Add,
        kExprI8Const, kExprI32LoadMem8U, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0c, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0d, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0e, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0f, kExprI32Add,
        kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x10, kExprI32Add,
        kExprI8Const, kExprI32Shl, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x11, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x12, kExprI32Add,
        kExprI8Const, 0x18, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x13, kExprI32Add,
        kExprI8Const, kExprI32ShrS, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x14, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x15, kExprI32Add,
          kExprGetLocal, 03, kExprI32Const, 08, kExprI32Add,
        kExprI32LoadMem8U, 00, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x16, kExprI32Add,
        kExprI8Const, kExprI32Mul, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x17, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04,
            kExprI32Const, 0x18,
          kExprI32Add,
        kExprSetLocal, 04,
      kExprEnd,
// Emit:
    //               kExprI32Const, 80, 02,
    //             kExprI32RemS,
    //             kExprI32Const, 7f,
    //           kExprI32And,
    //           kExprI32Const, ff, 01,
    //         kExprI32And,
    //       kExprI32StoreMem8, 00, 00,
    //           kExprGetLocal, kExprBr,
    //           kExprI32Const, 01,
    //         kExprI32Add,
    //       kExprSetLocal, 06,
    //           kExprGetLocal, 04,
    //           kExprI32Const, 01,
    //         kExprI32Add,
    //       kExprSetLocal, 04,
    //       kExprBr, 00, 00,
    //     kExprEnd,
    //         kExprGetLocal, 05,
    //         kExprI32Const, 01,
    //       kExprI32Add,
    //     kExprSetLocal, 05,
    //         kExprGetLocal, 04,
    //         kExprI32Const, 02,
    //       kExprI32Add,
    //     kExprSetLocal, 04,
    //     kExprBr, 00, 00,
    //   kExprEnd,
          kExprGetLocal, 04,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x01, kExprI32Add,
        kExprI32Const, 0x80, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x02, kExprI32Add,
        kExprI8Const, 02, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x03, kExprI32Add,
        kExprI8Const, kExprI32RemS, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x04, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x05, kExprI32Add,
        kExprI8Const, 0x7f, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x06, kExprI32Add,
        kExprI8Const, kExprI32And, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x07, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x08, kExprI32Add,
        kExprI32Const, 0xff, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x09, kExprI32Add,
        kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0a, kExprI32Add,
        kExprI8Const, kExprI32And, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0b, kExprI32Add,
        kExprI8Const, kExprI32StoreMem8, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0c, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0d, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0e, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x0f, kExprI32Add,
        kExprI8Const, 06, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x10, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x11, kExprI32Add,
        kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x12, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x13, kExprI32Add,
        kExprI8Const, kExprSetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x14, kExprI32Add,
        kExprI8Const, 0x06, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x15, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x16, kExprI32Add,
        kExprI8Const, 04, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x17, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x18, kExprI32Add,
        kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x19, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x1a, kExprI32Add,
        kExprI8Const, kExprSetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x1b, kExprI32Add,
        kExprI8Const, 04, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x1c, kExprI32Add,
        kExprI8Const, kExprBr, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x1d, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x1e, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x1f, kExprI32Add,
        kExprI8Const, kExprEnd, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x20, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x21, kExprI32Add,
        kExprI8Const, 05, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x22, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x23, kExprI32Add,
        kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x24, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x25, kExprI32Add,
        kExprI8Const, kExprSetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x26, kExprI32Add,
        kExprI8Const, 05, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x27, kExprI32Add,
        kExprI8Const, kExprGetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x28, kExprI32Add,
        kExprI8Const, 04, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x29, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x2a, kExprI32Add,
        kExprI8Const, 02, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x2b, kExprI32Add,
        kExprI8Const, kExprI32Add, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x2c, kExprI32Add,
        kExprI8Const, kExprSetLocal, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x2d, kExprI32Add,
        kExprI8Const, 04, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x2e, kExprI32Add,
        kExprI8Const, kExprBr, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x2f, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x30, kExprI32Add,
        kExprI8Const, 00, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x31, kExprI32Add,
        kExprI8Const, kExprEnd, kExprI32StoreMem8, 00, 00,
   // Change this
          kExprGetLocal, 04, kExprI32Const, 0x32, kExprI32Add,
        kExprI8Const, kExprI32Const, kExprI32StoreMem8, 00, 00,
          kExprGetLocal, 04, kExprI32Const, 0x33, kExprI32Add,
        kExprI8Const, 01, kExprI32StoreMem8, 00, 00,
            kExprGetLocal, 04,
            kExprI32Const, 0x34,
          kExprI32Add,
        kExprSetLocal, 04,
    // Call Filter function
        kExprI32Const, 0x80, 0x20,
          kExprGetLocal, 04,
          kExprI32Const, 0x80, 0x20,
        kExprI32Sub,
        kExprI32Const, 01,
      kExprJITSingleFunction, sig_index4,
        kExprI32Const, 01,
        kExprGetLocal, 00,
        kExprGetLocal, 01,
        kExprGetLocal, 02,
        kExprI32Const, 0x80, 0xc0, 0x00,
      kExprCallIndirect, kArity4, sig_index4
      ])
    .exportFunc()
  builder.appendToTable([0]);
  return builder;
})();

// Check that the module is properly defined
function testBasics() {
  var module = builder.instantiate({});

  assertFalse(module === undefined);
  assertFalse(module === null);
  assertFalse(module === 0);
  assertEquals("object", typeof module.exports);
  assertEquals("function", typeof module.exports.main);
}

testBasics();


function testMatrix(rows, columns, input_buffer, matrix_buffer,
                    expected_buffer) {
    var matrix_start = 0;
  builder.addDataSegment(matrix_start, matrix_buffer, false);

    var input_start = 9;
    var input_size = input_buffer.length;
  builder.addDataSegment(input_start, input_buffer, false);

  var test_module = builder.instantiate();

  // Check that the filter function returns 1
  assertEquals(1, test_module.exports.main(rows, columns,
                                           input_start, matrix_start));

  // Check filtered data
  var output_buffer = new Uint8Array(test_module.exports.memory);
  for (var i = 0; i < input_size; ++i) {
    assertEquals(expected_buffer[i], output_buffer[i + 8192]);
  }
}

function testZeroMatrix(){
  var matrix, input, expected;
  matrix = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00];

  input = [0x05];
  expected = [0x05];
  testMatrix(1, 1, input, matrix, expected);

  input = [0x0d, 0x2e, 0x2f, 0x01, 0x36, 0x20, 0x19, 0x12, 0x11];
  expected = [0x0d, 0x2e, 0x2f, 0x01, 0x00, 0x20, 0x19, 0x12, 0x11];
  testMatrix(3, 3, input, matrix, expected);

  input = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
           0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
           0x16, 0x17, 0x18];
  expected = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x00, 0x00, 0x00, 0x09, 0x0a,
              0x00, 0x00, 0x00, 0x0e, 0x0f, 0x00, 0x00, 0x00, 0x13, 0x14, 0x15,
              0x16, 0x17, 0x18];
  testMatrix(5, 5, input, matrix, expected);

  input = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
           0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b];
  expected = [0x00, 0x01, 0x02, 0x03, 0x04, 0x00,
              0x00, 0x07, 0x08, 0x09, 0x0a, 0x0b];
  testMatrix(3, 4, input, matrix, expected);
}

testZeroMatrix();

function testSingleEntryMatrix(){
  var input, expected;
  var matrix1 = [0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00];
  var matrix2 = [0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00];
  var matrix3 = [0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00];

  // 3 x 3 input
  input = [0x0d, 0x2e, 0x2f, 0x01, 0x36, 0x20, 0x19, 0x12, 0x11];
  expected = [0x0d, 0x2e, 0x2f, 0x01, 0x0d, 0x20, 0x19, 0x12, 0x11];
  testMatrix(3, 3, input, matrix1, expected);
  expected = [0x0d, 0x2e, 0x2f, 0x01, 0x36, 0x20, 0x19, 0x12, 0x11];
  testMatrix(3, 3, input, matrix2, expected);
  expected = [0x0d, 0x2e, 0x2f, 0x01, 0x20, 0x20, 0x19, 0x12, 0x11];
  testMatrix(3, 3, input, matrix3, expected);

  // 5 x 5 input
  input = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
           0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
           0x16, 0x17, 0x18];
  expected = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x00, 0x01, 0x02, 0x09, 0x0a,
              0x05, 0x06, 0x07, 0x0e, 0x0f, 0x0a, 0x0b, 0x0c, 0x13, 0x14, 0x15,
              0x16, 0x17, 0x18];
  testMatrix(5, 5, input, matrix1, expected);
  expected = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
              0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
              0x16, 0x17, 0x18];
  testMatrix(5, 5, input, matrix2, expected);
  expected = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08, 0x09, 0x09, 0x0a,
              0x0c, 0x0d, 0x0e, 0x0e, 0x0f, 0x11, 0x12, 0x13, 0x13, 0x14, 0x15,
              0x16, 0x17, 0x18];
  testMatrix(5, 5, input, matrix3, expected);

  // 3 x 4 input
  input = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
           0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b];
  expected = [0x00, 0x01, 0x02, 0x03, 0x04, 0x00,
              0x01, 0x07, 0x08, 0x09, 0x0a, 0x0b];
  testMatrix(3, 4, input, matrix1, expected);
  expected = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
              0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b];
  testMatrix(3, 4, input, matrix2, expected);
  expected = [0x00, 0x01, 0x02, 0x03, 0x04, 0x06,
              0x07, 0x07, 0x08, 0x09, 0x0a, 0x0b];
  testMatrix(3, 4, input, matrix3, expected);
}

testSingleEntryMatrix();

function testComplexMatrix() {
  var input, expected;
  var matrix1 = [0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01];
  var matrix2 = [0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04];
  var matrix3 = [0x00, 0x01, 0x00, 0x01, 0x04, 0x01, 0x00, 0x01, 0x00];

  // 3 x 3 input
  input = [0x0d, 0x2e, 0x2f, 0x01, 0x36, 0x20, 0x19, 0x12, 0x11];
  expected = [0x0d, 0x2e, 0x2f, 0x01, 0x87, 0x20, 0x19, 0x12, 0x11];
  testMatrix(3, 3, input, matrix1, expected);
  expected = [0x0d, 0x2e, 0x2f, 0x01, 0xf4, 0x20, 0x19, 0x12, 0x11];
  testMatrix(3, 3, input, matrix2, expected);
  expected = [0x0d, 0x2e, 0x2f, 0x01, 0x39, 0x20, 0x19, 0x12, 0x11];
  testMatrix(3, 3, input, matrix3, expected);

  // 5 x 5 input
  input = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
           0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
           0x16, 0x17, 0x18];
  expected = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x24, 0x2a, 0x30, 0x09, 0x0a,
              0x42, 0x48, 0x4e, 0x0e, 0x0f, 0x60, 0x66, 0x6c, 0x13, 0x14, 0x15,
              0x16, 0x17, 0x18];
  testMatrix(5, 5, input, matrix1, expected);
  expected = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0xd8, 0xfc, 0x20, 0x09, 0x0a,
              0x8c, 0xb0, 0xd4, 0x0e, 0x0f, 0x40, 0x64, 0x88, 0x13, 0x14, 0x15,
              0x16, 0x17, 0x18];
  testMatrix(5, 5, input, matrix2, expected);
  expected = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x30, 0x38, 0x40, 0x09, 0x0a,
              0x58, 0x60, 0x68, 0x0e, 0x0f, 0x80, 0x88, 0x90, 0x13, 0x14, 0x15,
              0x16, 0x17, 0x18];
  testMatrix(5, 5, input, matrix3, expected);

  // 3 x 4 input
  input = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
           0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b];
  expected = [0x00, 0x01, 0x02, 0x03, 0x04, 0x1e,
              0x24, 0x07, 0x08, 0x09, 0x0a, 0x0b];
  testMatrix(3, 4, input, matrix1, expected);
  expected = [0x00, 0x01, 0x02, 0x03, 0x04, 0xb4,
              0xd8, 0x07, 0x08, 0x09, 0x0a, 0x0b];
  testMatrix(3, 4, input, matrix2, expected);
  expected = [0x00, 0x01, 0x02, 0x03, 0x04, 0x28,
              0x30, 0x07, 0x08, 0x09, 0x0a, 0x0b];
  testMatrix(3, 4, input, matrix3, expected);
}

testComplexMatrix();
