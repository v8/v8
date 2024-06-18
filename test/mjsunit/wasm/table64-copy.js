// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function exportTable64Getter(builder, table, type) {
  const table64_get_sig = makeSig([kWasmI64], [type]);
  builder.addFunction('table64_get', table64_get_sig)
      .addBody([
        kExprLocalGet, 0,
        kExprTableGet, table.index])
      .exportFunc();
}

function exportTable64Copy(builder, table_dst, table_src) {
  const kSig_v_lll = makeSig([kWasmI64, kWasmI64, kWasmI64], []);
  builder.addFunction('table64_copy', kSig_v_lll)
      .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        kExprLocalGet, 2,
        kNumericPrefix, kExprTableCopy, table_dst.index, table_src.index
      ])
      .exportFunc();
}

function exportTable64FillExternRef(builder, table) {
    let kSig_v_lrl = makeSig([kWasmI64, kWasmExternRef, kWasmI64], []);
    builder.addFunction('table64_fill', kSig_v_lrl)
        .addBody([
          kExprLocalGet, 0,
          kExprLocalGet, 1,
          kExprLocalGet, 2,
          kNumericPrefix, kExprTableFill, table.index
        ])
        .exportFunc();
}

function exportTable64Size(builder, table) {
  builder.addFunction('table64_size', kSig_l_v)
  .addBody([kNumericPrefix, kExprTableSize, table.index])
  .exportFunc();
}

function checkExternRefTable(getter, size, start, count, value) {
  for (let i = 0; i < size; ++i) {
    if (i < start || i >= start + count) {
      assertEquals(null, getter(BigInt(i)))
    } else {
      assertEquals(value, getter(BigInt(i)));
    }
  }
}

(function TestTable64Copy() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table_src =
      builder.addTable64(kWasmExternRef, 15, 20).exportAs('table_src');
  const table_dst =
      builder.addTable64(kWasmExternRef, 10).exportAs('table_dst');

  exportTable64Getter(builder, table_dst, kWasmExternRef);
  exportTable64Size(builder, table_dst);
  exportTable64Copy(builder, table_dst, table_src);
  exportTable64FillExternRef(builder, table_src)

  let exports = builder.instantiate().exports;

  let dummy_externref = {foo: 12, bar: 34};

  // Just in bounds.
  let start_dst = 3n;
  let start_src = 6n;
  let count = 7n;
  exports.table64_fill(start_src, dummy_externref, count);
  exports.table64_copy(start_dst, start_src, count);
  let size = exports.table64_size(builder, table_dst);
  checkExternRefTable(
      exports.table64_get, size, start_dst, count, dummy_externref);

  // start_dst is OOB.
  start_dst = 4n;
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_copy(start_dst, start_src, count));
  start_dst = 1n << 32n;
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_copy(start_dst, start_src, count));

  // start_src is OOB.
  start_dst = 1n;
  start_src = 9n;
  count = 7n;
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_copy(start_dst, start_src, count));
  start_src = 1n << 32n;
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_copy(start_dst, start_src, count));
  // count is OOB.
  start_dst = 3n;
  start_src = 6n;
  count = 1n << 32n;
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_copy(start_dst, start_src, count));
})();
