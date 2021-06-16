// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --experimental-wasm-gc --wasm-gc-js-interop
// Flags: --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const kIterationsCountForICProgression = 20;

// TODO(ishell): remove once leaked maps could keep NativeModule alive.
let instances = [];

function createSimpleStruct(field_type, value1, value2) {
  const builder = new WasmModuleBuilder();

  const is_small_int = (field_type == kWasmI8) || (field_type == kWasmI16);
  const parameter_type = is_small_int ? kWasmI32 : field_type;
  const struct_get_opcode = is_small_int ? kExprStructGetS : kExprStructGet;

  const type_index = builder.addStruct([
      {type: field_type, mutability: true},
  ]);

  let sig_a_t = makeSig_r_x(kWasmDataRef, parameter_type);
  let sig_t_a = makeSig_r_x(parameter_type, kWasmDataRef);
  let sig_v_at = makeSig([kWasmDataRef, parameter_type], []);

  builder.addFunction("new_struct", sig_a_t)
    .addBody([
      kExprLocalGet, 0,                              // --
      kGCPrefix, kExprRttCanon, type_index,          // --
      kGCPrefix, kExprStructNewWithRtt, type_index]) // --
    .exportAs("new_struct");

  builder.addFunction("get_field", sig_t_a)
    .addBody([
      kExprLocalGet, 0,                             // --
      kGCPrefix, kExprRttCanon, type_index,         // --
      kGCPrefix, kExprRefCast,                      // --
      kGCPrefix, struct_get_opcode, type_index, 0]) // --
    .exportAs("get_field");

  builder.addFunction("set_field", sig_v_at)
    .addBody([
      kExprLocalGet, 0,                          // --
      kGCPrefix, kExprRttCanon, type_index,      // --
      kGCPrefix, kExprRefCast,                   // --
      kExprLocalGet, 1,                          // --
      kGCPrefix, kExprStructSet, type_index, 0]) // --
    .exportAs("set_field");

  let instance = builder.instantiate();
  instances.push(instance);
  let new_struct = instance.exports.new_struct;
  let get_field = instance.exports.get_field;
  let set_field = instance.exports.set_field;

  // Check that ctor, getter and setter work.
  let o = new_struct(value1);

  let res;
  res = get_field(o);
  assertEquals(value1, res);

  set_field(o, value2);
  res = get_field(o);
  assertEquals(value2, res);

  return {new_struct, get_field, set_field};
}

function SimpleStructInterop(field_type, value_generator,
                             value1 = 42, value2 = 111) {
  const { new_struct, get_field, set_field } =
      createSimpleStruct(field_type, value1, value2);

  function f(o) {
    for (let i = 0; i < kIterationsCountForICProgression; i++) {
      let v = o.$field0;
      let expected = get_field(o);
      assertEquals(expected, v);

      v = o[i];
      assertEquals(undefined, v);
    }
  }
  // Start collecting feedback from scratch.
  %ClearFunctionFeedback(f);

  let o = new_struct(value1);

  for (const value of value_generator()) {
    print("value: " + value);
    set_field(o, value);
    f(o);
  }
  gc();
}

(function TestSimpleStructsInteropI8() {
  SimpleStructInterop(kWasmI8, function*() {
    const max = 0x7f;
    const min = -max - 1;
    yield min;
    yield max;
    yield 0;

    for (let i = 0; i < 10; i++) {
      yield Math.floor((Math.random() - 0.5) * max);
    }
  });
})();

(function TestSimpleStructsInteropI16() {
  SimpleStructInterop(kWasmI16, function*() {
    const max = 0x7fff;
    const min = -max - 1;
    yield min;
    yield max;
    yield 0;

    for (let i = 0; i < 10; i++) {
      yield Math.floor((Math.random() - 0.5) * max);
    }
  });
})();

(function TestSimpleStructsInteropI32() {
  SimpleStructInterop(kWasmI32, function*() {
    const max = 0x7fffffff;
    const min = -max - 1;
    yield min;
    yield max;
    yield 0;

    for (let i = 0; i < 10; i++) {
      yield Math.floor((Math.random() - 0.5) * max);
    }
  });
})();

(function TestSimpleStructsInteropI64() {
  SimpleStructInterop(kWasmI64, function*() {
    const max = 0x7fffffffffffffn;
    const min = -max - 1n;
    yield min;
    yield max;
    yield 0n;

    for (let i = 0; i < 10; i++) {
      yield BigInt(Math.floor((Math.random() - 0.5) * Number(max)));
    }
  }, 42n, 153n);
})();

(function TestSimpleStructsInteropF32() {
  SimpleStructInterop(kWasmF32, function*() {
    const max_safe_integer = 2**23 - 1;
    const max = 3.4028234664e+38;
    yield -max;
    yield max;
    yield max_safe_integer;
    yield -max_safe_integer;
    yield 0.0;
    yield -0.0;

    for (let i = 0; i < 10; i++) {
      yield Math.floor((Math.random() - 0.5) * max);
    }
  });
})();

(function TestSimpleStructsInteropF64() {
  SimpleStructInterop(kWasmF64, function*() {
    const max = 1.7976931348623157e+308
    yield -max;
    yield max;
    yield Number.MAX_SAFE_INTEGER;;
    yield Number.MIN_SAFE_INTEGER;
    yield 0.0;
    yield -0.0;

    for (let i = 0; i < 10; i++) {
      yield Math.floor((Math.random() - 0.5) * max);
    }
  });
})();

(function TestSimpleStructsInteropRef() {
  SimpleStructInterop(kWasmAnyRef, function*() {
    yield "foo";
    yield null;
    yield {x:1};
    yield {x:1.4, y:2.3};
    yield Number(13);
    yield this;
  }, null, undefined);
})();
