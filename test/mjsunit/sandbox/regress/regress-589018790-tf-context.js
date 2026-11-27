// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-memory-corruption-api
// Flags: --no-turbolev --no-optimize-on-next-call-optimizes-to-maglev
// Flags: --no-turbo-inlining --concurrent-recompilation

(function TestTurboFanContextRefConstantNoHole() {
  if (!%IsConcurrentRecompilationSupported() || !%IsTurbofanEnabled()) return;

  const memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));
  const heap_num = 1.5 + Math.random();
  const kHeapNumberMap = memory.getUint32(Sandbox.getAddressOf(heap_num), true);

  const realm = Realm.createAllowCrossRealmAccess();
  const {victim, callVictim} = Realm.eval(realm, `
    (function makeVictim() {
      let keep_fn_ctx = 1;
      let keep = () => keep_fn_ctx;
      const iters = [];
      const bases = [];
      for (let i = 0; i < 6; i++) {
        const custom_iter = Object.create(null);
        custom_iter['k' + i] = i;
        custom_iter[Symbol.iterator] = function() {
          const it = Object.create(null);
          it['m' + i] = i;
          it.next = function() {
            const res = Object.create(null);
            res['r' + i] = i;
            res.done = true;
            return res;
          };
          return it;
        };
        iters.push(custom_iter);
        const b = Object.create(null);
        b['p' + i] = i;
        b.f = function(a, b) { return a + b + 39; };
        bases.push(b);
      }
      const sub = {
        victim(iter) {
          return super.f(1, ...iter, 2);
        }
      };
      const v = sub.victim;
      return {
        victim: v,
        callVictim: function(i) {
          const idx = i % bases.length;
          Object.setPrototypeOf(sub, bases[idx]);
          return v.call(sub, iters[idx]);
        }
      };
    })();
  `);

  const kJSFunctionContextOffset = Sandbox.getFieldOffset(
      Sandbox.getInstanceTypeIdFor('JS_FUNCTION_TYPE'), 'context');
  const kContextPreviousOffset = Sandbox.getFieldOffset(
      Sandbox.getInstanceTypeIdFor('FUNCTION_CONTEXT_TYPE'), 'previous');
  const fn_ctx_addr =
      memory.getUint32(
          Sandbox.getAddressOf(callVictim) + kJSFunctionContextOffset, true) -
      1;
  const nc_addr =
      memory.getUint32(fn_ctx_addr + kContextPreviousOffset, true) - 1;

  %NeverOptimizeFunction(callVictim);
  %PrepareFunctionForOptimization(victim);
  for (let i = 0; i < 12; i++) callVictim(i);

  const orig_map = memory.getUint32(nc_addr, true);
  const kGraphBuilderPhase = 'TurbofanBytecodeGraphBuilder';
  const kInliningPhase = 'TurbofanInlining';
  const kTimeoutMs = 10000;

  // PrepareJobImpl constructs target_native_context_ on the main thread before
  // queuing the background TurboFan compilation job. Block right before
  // GraphBuilderPhase and InliningPhase so the map mutation is observed during
  // BytecodeGraphBuilder::CreateNativeContextNode and restored before
  // JSNativeContextSpecialization debug checks run.
  %WaitForBackgroundOptimization();
  %BlockAt(kGraphBuilderPhase, kTimeoutMs);
  %OptimizeFunctionOnNextCall(victim, 'concurrent');
  callVictim(0);

  assertNotEquals(
      0,
      %GetOptimizationStatus(victim) &
          V8OptimizationStatus.kOptimizingConcurrently);
  assertTrue(%WaitUntilBlocked(kGraphBuilderPhase, kTimeoutMs));

  %BlockAt(kInliningPhase, kTimeoutMs);
  memory.setUint32(nc_addr, kHeapNumberMap, true);
  const resumed_graph_builder = %Resume(kGraphBuilderPhase);
  const blocked_inlining = %WaitUntilBlocked(kInliningPhase, kTimeoutMs);
  memory.setUint32(nc_addr, orig_map, true);

  assertTrue(resumed_graph_builder);
  assertTrue(blocked_inlining);
  assertTrue(%Resume(kInliningPhase));
  %WaitForBackgroundOptimization();

  assertEquals(42, callVictim(0));
})();
