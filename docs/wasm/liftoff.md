# Liftoff (WebAssembly Baseline Compiler)

Liftoff (`src/wasm/baseline/`) is V8's single-pass baseline JIT compiler for WebAssembly (`ExecutionTier::kLiftoff`). Positioned ahead of the Turboshaft optimizing tier (`ExecutionTier::kTurbofan`; see [architecture.md](architecture.md)), Liftoff prioritizes fast linear-time compilation and low memory overhead over peak code quality, emitting register-allocated machine code in one pass over validated WebAssembly wire bytes.

Beyond baseline execution and call-site type feedback collection (`--wasm-inlining`), Liftoff is the sole WebAssembly tier supporting interactive debugging (`DebugSideTable`) and serves as the unoptimized target when speculative Turboshaft code deoptimizes (`--wasm-deopt`).

## Design Decisions & Trade-offs

- **Single pass, no IR, no liveness**: Liftoff trades peak code quality for fast linear-time compilation and low memory overhead. Three core mechanics follow directly: all cache registers are spilled before calls (`PrepareCall` -> `SpillAllRegisters`), keeping normal call safepoints stack-only (`DefineSafepoint`); control-flow merge states are fixed by the first arriving branch (`MergeIntoNewState`); and when no volatile cache register can be reclaimed, spill victims are chosen round-robin (`GetNextSpillReg`).
- **Loop headers spill all locals and loop inputs**: Before binding a loop header, `LiftoffCompiler::Loop` calls `SpillLocals` and `SpillLoopArgs` to free cache registers for the loop body and avoid reloading stack values at branches. Pre-analyzing functions to keep loop-invariant locals in registers across iterations was tried and rejected because the added complexity did not yield worthwhile performance gains.
- **Tier-up cost model**: Under `--wasm-dynamic-tiering`, executed machine-code bytes approximate execution time: loop back-edges charge the back-edge jump distance (`BrImpl`), while returns (`ReturnImpl`) and tail calls (`TierupCheckOnTailCall`) charge `pc_offset()`. Every check adds `kTierUpCostForCheck` for loading, subtracting, and branching on the budget; returns and tail calls also add `kTierUpCostForFunctionEntry` for function-entry overhead (calling `WasmLiftoffFrameSetup` plus frame setup/teardown) so frequently called small functions (common in WasmGC) tier up without excessive call counts while leaving loop back-edge tiering unaffected. Each check is capped at a fraction of `--wasm-tiering-budget` so one check never drains the entire budget at once. Charging forward jumps with a blanket value (since their target distance is unknown in a single pass) was tried and rejected because it made no difference.
- **`kIntConst` `int32_t` payload and `SpillLocalsInitially`**: `LiftoffVarState::kIntConst` stores constants as `int32_t` (sign-extended for `kI64` and `kF64` bit patterns); larger 64-bit constants cannot be used as hardware immediates anyway, so materializing them directly into registers loses nothing. `SpillLocalsInitially` starts non-parameter locals on the stack when there are many locals relative to the GP cache registers or any non-scalar local (`kS128` or reference, which cannot be represented as constants), avoiding spilling rarely-used initial values at merge points.
- **Recompile on demand instead of storing metadata**: Rather than persisting per-PC frame metadata alongside every Liftoff code object, V8 regenerates `DebugSideTable` on demand for `kForDebugging` / `kForStepping` (`GenerateLiftoffDebugSideTable`) and computes `LiftoffFrameDescriptionForDeopt` on demand when Turboshaft deoptimizes (`CompileWithLiftoffAndGetDeoptInfo`). This drives the cross-compilation determinism invariants in Section 5.2.
- **Bailout completeness and non-serialization**: Liftoff is expected to be feature-complete for shipped WebAssembly features (`CheckBailoutAllowed` restricts bailouts to experimental features, missing CPU features, `--enable-testing-opcode-in-wasm`, and incomplete community ports, and makes unexpected bailouts `FATAL`). While ordinary debugging compile requests can still fall back to Turboshaft if Liftoff bails out (`WasmCompilationUnit::ExecuteCompilation`), breakpoint and stepping recompilation (`RecompileLiftoffWithBreakpoints`) relies on complete Liftoff support and treats failure as `FATAL` because Turboshaft cannot emit breakpoints. `NativeModuleSerializer::WriteCode` omits Liftoff code (writing only a `kEagerFunction` / `kLazyFunction` hint from `tiering_budget_array`) because it can contain breakpoints or non-relocatable constants; `NativeModule` drops Liftoff `reloc_info` after patching because Liftoff code is neither relocated nor serialized.

---

## 1. Overview & Single-Pass Decoder Interface

- **`WasmFullDecoder` Single-Pass Pipeline**: `ExecuteLiftoffCompilation` instantiates `WasmFullDecoder<Decoder::NoValidationTag, LiftoffCompiler>` over pre-validated wire bytes. Opcodes stream sequentially into `LiftoffCompiler` callbacks (`StartFunctionBody`, per-opcode handlers, `FinishFunction`), emitting machine code directly via `LiftoffAssembler` with ad-hoc register allocation in `CacheState` (Section 2) and returning a `WasmCompilationResult`.
- **Structured Control Flow**: WebAssembly's structured control constructs (`block`, `loop`, `if`/`else`/`end`, `try`/`catch`, `try_table`, `br`, `br_if`, `br_table`) eliminate any bytecode pre-walk for jump targets. Forward branches target an enclosing `Control` entry (`c->label`, `c->label_state`); backward branches target an already-bound `loop` header.
- **Bailouts (`LiftoffBailoutReason`) and Tier Fallback**: On an unsupported architecture, missing CPU feature, or unimplemented instruction, `LiftoffAssembler::bailout` / `LiftoffCompiler::unsupported` aborts decoding and returns a failed `WasmCompilationResult`. Callers (`ExecuteCompilationUnits`, `CompileLazy`, `WasmCompilationUnit::CompileWasmFunction`) transparently retry with Turboshaft (`ExecutionTier::kTurbofan`). Under `--liftoff-only`, `CheckBailoutAllowed` makes any non-`kDecodeError` bailout `FATAL`.

---

## 2. Abstract Value State, Register Allocation & Control-Flow Merges

### 1. `LiftoffAssembler::CacheState` and `LiftoffVarState`
`CacheState::stack_state` unifies parameters and locals at `[0, num_locals_)` with active operand-stack values (and implicit caught exception references in `catch` handlers) from `num_locals_` upward. Each `LiftoffVarState` records a slot's `ValueKind`, `Location`, payload, and stack-frame spill offset:

| `LiftoffVarState::Location` | Active Payload | Meaning |
|---|---|---|
| `kStack` | `offset()` | Value resides in the stack frame at `fp - offset()`. |
| `kRegister` | `reg()` (`LiftoffRegister`) | Value resides in a cache register (or register pair); `offset()` reserves its home slot if later spilled. |
| `kIntConst` | `i32_const()` (`int32_t`) | Unmaterialized constant representable via `int32_t`: `kI32`, sign-extended 32-bit `kI64`, raw-bit `kF32`, or `kF64` bit patterns representable as sign-extended `int32_t` (notably `+0.0`). |

Spill offsets are assigned deterministically via `NextSpillOffset` (growing downward from `fp` past `StaticStackFrameSize()`, aligned when `NeedsAlignment(kind)` holds, and re-packed across merge regions in `InitMergeRegion`). Spilling writes to `fp - offset()` and transitions the slot to `kStack` without shifting neighboring slots.

### 2. Cache Register Pools, Reference Counting & Spill Victim Selection
- **Register Pools & Cross-Component Coupling**: Per-architecture pools `kLiftoffAssemblerGpCacheRegs` and `kLiftoffAssemblerFpCacheRegs` (`kGpCacheRegList` / `kFpCacheRegList`, with `kNeedI64RegPair` / `kNeedS128RegPair` on 32-bit or paired-FP targets) satisfy two external invariants:
  1. `WasmDebugBreakFrameConstants::kPushedGpRegs` and `kPushedFpRegs` must match the Liftoff cache register lists (`static_assert`ed in `test/unittests/wasm/liftoff-register-unittests.cc`) so `WasmDebugBreak` frames preserve and trace all live cache registers.
  2. Under `--wasm-deopt`, Liftoff's FP and SIMD128 cache registers must be allocatable in `RegisterConfiguration::Default()` so the deoptimizer can materialize register state (`DCHECK`ed in `EmitDeoptPoint`; skipped on 32-bit ARM, where the deoptimizer handles all double/SIMD registers).
- **Zero-Copy Aliasing (`used_registers` & `register_use_count`)**: `local.get` and `local.tee` push a `kRegister` slot sharing the existing register and increment `register_use_count` without emitting moves. Modifying an operand in place uses `LoadToModifiableRegister` / `PopToModifiableRegister` to copy into an unused register whenever `!is_free(reg)` or the register is `pinned`.
- **Cached Special Registers & Spilling (`SpillOneRegister`)**: `CacheState` opportunistically caches `WasmTrustedInstanceData` in `cached_instance_data` (preferring `kWasmImplicitArgRegister` via `TrySetCachedInstanceRegister`) and a linear-memory base pointer in `cached_mem_start` (`cached_mem_index`). Under register pressure, `SpillOneRegister` first reclaims either volatile register via `take_volatile_register` without a store (both reload from the frame or instance), falling back to `GetNextSpillReg` and `SpillRegister`.

### 3. Control-Flow Merges, Loop Headers & `ParallelMove`

```
  Source stack_state:  |--- locals ---|--- stack prefix ---|--- (discarded) ---|--- merge (arity) ---|
  Target label_state:  |--- locals ---|--- stack prefix ---|--- merge (arity) ---|
```

- **First Forward Merge (`MergeIntoNewState`)**: The first path reaching an unbound merge calls `InitMergeRegion` to establish `Control::label_state` (`BrIf` calls `PrepareForBranch` first so taken and fallthrough paths share materialized transfers):
  - `locals` and single-value `merge` (`arity == 1`) materialize `kIntConst` slots and deduplicate aliased registers so incoming branches have distinct targets.
  - The unchanged `stack prefix` preserves `kIntConst` entries and shared registers (`kConstantsAllowed`, `kReuseRegisters`).
  - Multi-value `merge` regions (`arity > 1`) force all merge slots to `kStack` (`kRegistersNotAllowed`, re-packed after the prefix) because simultaneous stack-to-stack moves and register reloads could otherwise conflict in `ParallelMove`.
- **Subsequent Merges (`MergeFullStackWith` / `MergeStackWith`)**: On `kForwardJump`, later paths transfer `stack_state` into `target` and clear mismatched `cached_instance_data` / `cached_mem_start`. On `kBackwardJump`, the loop-header state is fixed, so `MergeStackWith` restores cached special registers (`RestoreCachedRegisters`) to match the header.
- **Loop Headers (`Loop`)**: Calls `SpillLocals()` and `SpillLoopArgs(loop->start_merge.arity)` before binding `loop->label` and snapshotting `loop->label_state`.
- **`ParallelMove`**: `Transfer` emits stack stores eagerly, records register-to-register moves, and queues constant/stack loads; `Execute` runs unblocked register moves first, breaks cycles through a temporary stack slot, and emits constant/stack loads last.

---

## 3. Frame Layout, Calls & Platform Backend Split

### 1. Frame Layout & Deferred Frame Setup
Liftoff frames use `StackFrame::WASM` (`WasmLiftoffFrameConstants` and `wasm::kOSRTargetOffset`):

```
  high addr  [fp + kSystemPointerSize]  Caller return PC
             [fp + 0]                   Saved caller fp
             [fp - 1 * ptr]             Frame marker: StackFrame::WASM (kFrameTypeOffset)
             [fp - 2 * ptr]             WasmTrustedInstanceData (kInstanceDataOffset)
             [fp - 3 * ptr]             Feedback vector FixedArray (kFeedbackVectorOffset; populated if --wasm-inlining)
             [fp - 4 * ptr]             OSR target PC (wasm::kOSRTargetOffset, x64 only)
             [fp - offset]              Locals & operand-stack spill slots (StaticStackFrameSize..max_used_spill_offset_)
  low addr   [sp ..]                    OOL register-push spill area & outgoing stack call arguments
```

- **`WasmLiftoffFrameSetup` Stub**: Under `--wasm-inlining`, `StartFunctionBody` calls `Builtin::kWasmLiftoffFrameSetup` (passing the defined function index in `kLiftoffFrameSetupFunctionReg`) to build the frame, lazily allocate the feedback vector under a temporary `StackFrame::WASM_LIFTOFF_SETUP` marker, increment the invocation count, and populate instance/feedback slots. Otherwise Liftoff emits `EnterFrame(StackFrame::WASM)` directly.
- **Parameter & Local Entry Setup**: `ParameterProcessor` initializes parameters (re-applying `DecompressTagged` on reference parameters under `V8_ENABLE_SANDBOX`); non-parameter locals start as `kIntConst(0)` or on the stack via `SpillLocalsInitially`.
- **`PrepareStackFrame` and `PatchPrepareStackFrame`**: Because `max_used_spill_offset_` and `max_pushed_argument_slots_` are unknown until decoding ends, `PrepareStackFrame()` reserves a fixed-width `sp`-subtraction placeholder (also doubling as the early unsupported-arch bailout probe). In `FinishFunction`, `PatchPrepareStackFrame` patches `sub sp` in place when `max_stack_space` (frame size plus `max_pushed_argument_slots_`) stays below the large-frame threshold matching `Isolate::StackOverflow`'s `DCHECK`; otherwise it patches a jump to an OOL extended stack check appended at the end of the code buffer so overflow is caught before moving `sp`, leaving headroom for `Builtin::kWasmStackOverflow` (or `Builtin::kWasmHandleStackOverflow` under `--wasm-growable-stacks`).

### 2. Call Lowering & Platform Backend Split
- **Calls (`PrepareCall` / `FinishCall`)**: `PrepareCall` drops argument slots, calls `SpillAllRegisters()`, constructs outgoing stack arguments, and moves register arguments plus `kWasmImplicitArgRegister` via `ParallelMove`. `FinishCall` defines the safepoint, records debug/deopt/OSR/landing-pad state, and pushes returns (re-applying `DecompressTagged` on Wasm reference returns under `V8_ENABLE_SANDBOX`).
- **Indirect & Reference Calls (`call_indirect`, `call_ref`)**: Dispatch-table bounds/signature checks and `WasmCodePointerTable` CFI checks are shared with Turboshaft (see [architecture.md](architecture.md)); under `--wasm-inlining` / `--wasm-inlining-call-indirect`, Liftoff collects feedback via `Builtin::kCallRefIC` / `Builtin::kCallIndirectIC`.
- **Tail Calls (`return_call*`)**: `PrepareTailCall` shifts outgoing stack arguments over incoming parameter slots, restores caller `fp` and return PC, and jumps via `TailCallNativeWasmCode` or `TailCallIndirect`.
- **Platform Backend Split**: Shared compiler and `CacheState` logic lives in `liftoff-compiler.cc` and `liftoff-assembler.{h,cc,-inl.h}`; per-architecture emission lives inline in `src/wasm/baseline/<arch>/liftoff-assembler-<arch>-inl.h` (`x64`, `arm64`, `ia32`, `arm`, plus community ports).

---

## 4. GC Safepoints, Out-of-Line Code & Traps

- **Precise Root Tracking (`SafepointTableBuilder`)**: `LiftoffCompiler` uses `SafepointTableBuilder::LazyDeoptSupport::kNo` (dropping duplicate entries at commit time), precomputes `tagged_locals_slots_` (`GrowableBitVector`) for reference locals in `[0, num_locals_)` via `RecordTaggedLocalSlots()`, and emits the XOR-delta-compressed safepoint table in `FinishFunction`. Normal calls use `DefineSafepoint` -- taking `DefineEmptySafepoint()` directly for functions that never hold a reference on `stack_state` (`!has_ref_values()`), otherwise marking reference stack slots and bulk-merging `tagged_locals_slots_`. `Builtin::kWasmDebugBreak` and debug traps use `DefineSafepointWithCalleeSavedRegisters` (also recording `kRegister` reference slots and `cached_instance_data`, since the callee pushes all cache registers per `WasmDebugBreakFrameConstants`).
- **Exception Landing Pads (`EmitLandingPad` & `HandlerTable`)**: Calls inside `try` / `try_table` emit an inline landing pad recorded via `HandlerTable::EmitReturnEntry`, which pushes the caught exception (`kReturnRegister0`), merges into `TryInfo::catch_state`, and jumps to `TryInfo::catch_label`.
- **`OutOfLineCode` (`GenerateOutOfLineCode`)**: Cold paths (`Trap`, `StackCheck`, `TierupCheck`) are queued during decoding -- guarded by `FreezeCacheState` / `FREEZE_STATE` in `DEBUG` builds against unintended `CacheState` mutations across internal branches -- and emitted after the epilogue. `StackCheck` and `TierupCheck` preserve live registers and record pushed reference slots; non-debug traps never return and need no roots, so `AddOutOfLineTrap` queues them separately in `out_of_line_code_without_safepoints_` after safepoint-bearing OOL stubs so their empty safepoint entries deduplicate.
- **`TrappingInstructionData`**: For hardware-protected linear-memory accesses and implicit `WasmNull` dereferences (see [architecture.md](architecture.md)), `RegisterTrappingInstruction` records `trap_handler::TrappingInstructionData` with a source position, plus a `DefineSafepointWithCalleeSavedRegisters` entry at `trapping_instruction_pc + WasmFrameConstants::kTrappingInstructionReturnAddressOffset` when `for_debugging_` is set.

---

## 5. Tiering, Debugging, OSR & Deoptimization Invariants

### 1. Tier-Up Trigger, `ForDebugging` Modes & Debug OSR
- **Tier-Up Trigger (`CheckTierUp` / `TierupCheck`)**: Under `--wasm-dynamic-tiering` (`for_debugging_ == kNotForDebugging`), Liftoff decrements `WasmTrustedInstanceData::tiering_budget_array()` at loop back-edges, returns, and tail calls using the cost model above. When negative, `OutOfLineCode::TierupCheck` calls `Builtin::kWasmTriggerTierUp` (`TriggerTierUp`) to enqueue background Turboshaft compilation and later patch the `NativeModule` jump table (Wasm does not perform Liftoff-to-Turboshaft loop OSR; when `!dynamic_tiering()`, `Loop` emits a header `StackCheck` instead).
- **`ForDebugging` & `DebugSideTable`**: `ForDebugging` selects `kNotForDebugging`, `kForDebugging` (inspectable code spilling register-held slots to home frame offsets via `GetSpilledRegistersForInspection` and routing OOL traps through `Builtin::kWasmTrapHandlerThrowTrap`), `kWithBreakpoints` (`RecompileLiftoffWithBreakpoints` emitting `Builtin::kWasmDebugBreak`), or `kForStepping` (breakpoint-flooded via sentinel offset `0`). `DebugSideTable` delta-encodes per-PC value locations (`kConstant`, `kRegister`, `kStack`), built eagerly for `kWithBreakpoints` and lazily via `GenerateLiftoffDebugSideTable` for `kForDebugging` / `kForStepping`.
- **Debug OSR (`UpdateReturnAddress` / `MaybeOSR`)**: Setting or clearing breakpoints redirects active `for_debugging()` frames across Liftoff debug variants (`PointerAuthentication::ReplacePC`, or `fp - wasm::kOSRTargetOffset` on `x64` for CET shadow-stack compatibility, consumed by `MaybeOSR` -> `Builtin::kWasmOnStackReplace`). Non-debugging frames are never OSR-patched into debugging code because `GetSpilledRegistersForInspection` spills register-held slots to home frame offsets in OOL stubs, giving debugging code a strictly larger regular frame and different safepoint tagged-slot map than non-debugging code.

### 2. Deoptimization (`--wasm-deopt`) & Cross-Compilation Contracts
When speculative Turboshaft code deoptimizes (see [architecture.md](architecture.md)), `CompileWithLiftoffAndGetDeoptInfo` re-runs `ExecuteLiftoffCompilation` for each inlined frame at `deopt_info_bytecode_offset` (`kEagerDeopt` in `EmitDeoptPoint` for the innermost call site, `kInlinedCall` in `StoreFrameDescriptionForDeopt` for outer callers), producing `LiftoffFrameDescriptionForDeopt` and publishing the Liftoff `WasmCode` (`for_deopt = true`).

Separate Liftoff compilations of the same function must satisfy these determinism invariants:
1. **`FunctionTypeFeedback::liftoff_frame_size`**: Recorded in `FinishFunction` and read by Turboshaft's `GetLiftoffFrameSize` to size deopt frame states; a deopt-time Liftoff recompile must produce the exact same frame size.
2. **Feedback Slot Numbering (`call_targets`)**: `encountered_call_instructions_` must match identically across recompiles (`DCHECK_EQ` in `FinishFunction`).
3. **Lazy `DebugSideTable` Regeneration**: `GenerateLiftoffDebugSideTable` recompiles the function and must reproduce the exact PC offsets and slot placements of the running `kForDebugging` / `kForStepping` code.
4. **Breakpoint-Independent Debug OSR Layout**: Frame size and safepoint tagged-slot maps across `for_debugging()` recompiles must not depend on the active breakpoint set.

---

## 6. Compilation Memory Lifecycle & Publishing

To avoid per-function `Zone` and `AssemblerBuffer` allocation overhead, batched compilation (`ExecuteCompilationUnits`, one context per worker task) and lazy compilation (`CompileLazy`, via `Isolate::wasm_liftoff_context()`, reset on Mark-Compact GC so idle isolates do not retain compilation buffers) reuse a single-threaded `LiftoffCompilationContext` across Liftoff units: `ResetZone()` clears the compilation `Zone` on exit of `ExecuteLiftoffCompilation` (retaining it only up to `kMaxRetainedZoneSize`), while `NewAssemblerBuffer` / `CommitCode` bump-allocate instructions and relocation info into arena chunks (`WasmCompilationResult::instr_buffer` stays `nullptr`). A context must outlive all produced `WasmCompilationResult`s until `NativeModule::AddCompiledCode` copies code out and `ReleaseCode()` recycles chunks (dropping chunks above `kMaxRetainedChunkSize`). Standalone callers without a context (such as debugging or deopt recompiles) fall back to a per-function `Zone` and `NewLiftoffAssemblerBuffer`. Publishing into off-heap executable space (`NativeModule::AddCompiledCode` / `PublishCode`) follows the shared Wasm pipeline (see [architecture.md](architecture.md)), dropping `reloc_info` after patching.

---

## 7. Key Files Reference

| File | Purpose |
|---|---|
| `src/wasm/baseline/liftoff-compiler.{h,cc}` | `LiftoffCompiler` callbacks, `LiftoffCompilationContext`, tier-up cost model, `OutOfLineCode`, `ExecuteLiftoffCompilation`, `GenerateLiftoffDebugSideTable`. |
| `src/wasm/baseline/liftoff-assembler.{h,cc}`, `-inl.h` | `LiftoffAssembler`, `CacheState`, spill/merge algorithms (`MergeIntoNewState`, `MergeStackWith`), `PrepareCall`. |
| `src/wasm/baseline/<arch>/liftoff-assembler-<arch>-inl.h` | Per-architecture `LiftoffAssembler` implementations (`x64`, `arm64`, `ia32`, `arm`, plus community ports). |
| `src/wasm/baseline/liftoff-varstate.h` | `LiftoffVarState` (`kStack`, `kRegister`, `kIntConst`, spill offset). |
| `src/wasm/baseline/liftoff-register.h`, `liftoff-assembler-defs.h` | `LiftoffRegister`, `LiftoffRegList`, `kLiftoffAssemblerGpCacheRegs`, `kLiftoffAssemblerFpCacheRegs`. |
| `src/wasm/baseline/parallel-move.{h,cc}`, `-inl.h` | `ParallelMove` dependency ordering and cycle-breaking spill/reload resolution. |
| `src/wasm/baseline/liftoff-bailout-reasons.h` | `LiftoffBailoutReason` enum and histogram mapping. |
| `src/execution/<arch>/frame-constants-<arch>.h`, `src/wasm/wasm-constants.h` | `WasmLiftoffFrameConstants`, `WasmLiftoffSetupFrameConstants`, `wasm::kOSRTargetOffset`. |
| `src/wasm/wasm-debug.{h,cc}`, `src/wasm/wasm-deopt-data.h`, `src/deoptimizer/deoptimizer.cc` | `DebugSideTable`, `RecompileLiftoffWithBreakpoints`, `UpdateReturnAddress`, `LiftoffFrameDescriptionForDeopt`, `CompileWithLiftoffAndGetDeoptInfo`. |
