// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-inlining --allow-natives-syntax
// Flags: --trace-wasm-inlining --turboshaft-wasm --wasm-dynamic-tiering
// Explicitly set Turboshaft since the tracing output is slightly different in
// TurboFan (which is also on its way out anyway).
// Explicitly enable dynamic tiering, since we need Liftoff to collect feedback
// for speculative optimizations.

d8.file.execute('test/mjsunit/mjsunit.js');
d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function CallRefTest(callee_count) {
  print(
      'Test call_ref speculative inlining with ' + callee_count +
      ' different callees during feedback collection.');
  let builder = new WasmModuleBuilder();

  let sig_index = builder.addType(kSig_i_i);

  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,

      kExprLocalGet, 1,
      kExprTableGet, kTableZero,
      kGCPrefix, kExprRefCast, sig_index,

      kExprCallRef, sig_index,
    ])
    .exportAs("main");

  let callee = [];
  // Add one more function than what will be called before tier-up below,
  // for also testing mis-speculation.
  for (let i = 0; i < callee_count + 1; i++) {
    callee.push(builder.addFunction("callee" + i, sig_index)
      .addBody([kExprLocalGet, 0, kExprI32Const, i, kExprI32Add]));
  }
  builder.appendToTable(callee.map(f => f.index));

  let instance = builder.instantiate();

  for (let i = 0; i < 10; i++) {
    // Call each of the `callee`s except the last one to collect feedback.
    for (let j = 0; j < callee_count; j++) {
      assertEquals(10 + j, instance.exports.main(10, j));
    }
  };
  %WasmTierUpFunction(instance.exports.main);

  // Tier-up is done, and inlining should have happened (in the monomorphic and
  // polymorphic cases). The result should be correct in any case.
  for (let j = 0; j < callee_count; j++) {
    assertEquals(10 + j, instance.exports.main(10, j));
  }

  // Now, run with a `callee` that was never seen before tier-up.
  // While this is not inlined, it should still return the correct result.
  assertEquals(10 + callee_count, instance.exports.main(10, callee_count));
}

const kMaxPolymorphism = 4;

(function CallRefUninitializedFeedback() { CallRefTest(0); })();
(function CallRefMonomorphic() { CallRefTest(1); })();
(function CallRefPolymorphic() { CallRefTest(kMaxPolymorphism); })();
(function CallRefMegamorphic() { CallRefTest(kMaxPolymorphism + 1); })();
