---
name: v8-best-practices
trigger: always_on
---

# V8 Best Practices

These are best practices for proposing fixes and general development in V8.

## Fix Proposals

When proposing a fix, ensure the code adheres to V8 standards:

- **Simplicity**: Code should be as simple and direct as possible. Favor
  straightforward and maintainable constructs.
- **Readability**: Code must be understandable to future maintainers.
- **Structural Alignment**: Fits naturally into the existing architecture (e.g.,
  AST Visitor pattern, Bytecode generation flow).
- **Controlled Duplication**: Prefer duplication over complex abstractions ONLY
  when it significantly improves clarity.
- **Optimization Preservation (Methodology)**: To maximize performance, follow
  this systematic approach when fixing optimization violations:
  - **Trace Lifecycle**: Understand how optimized state is collected,
    propagated, and consumed (e.g., AST -> Bytecode -> JIT).
  - **Isolate Assumptions**: Identify the specific invariant that was violated.
  - **Surgical Fixes**: Address the violation without restricting valid
    optimized paths.
  - **Flaw over Disabling**: Prioritize finding the specific logic flaw in the
    optimization's implementation to preserve the feature.
- **Cross-Feature Awareness**: Understand how optimizations interact with core
  V8 features like Lazy Compilation, Ignition bytecode generation, and
  TurboFan/Maglev optimizations.
- **Fail Fast Over Defensive Workarounds**: Never introduce defensive
  programming patterns to paper over unexpected compiler states or invariant
  violations (e.g., silent early returns, fallback dummy values, defensive
  clamping, or non-exhaustive switch `default: break;`). Assert expected
  invariants: default to `DCHECK` (also for trusted inputs, accesses confined to
  the sandbox, and invariants release-checked downstream); reserve `SBXCHECK`
  (`src/sandbox/check.h`) for out-of-sandbox memory-safety invariants on
  untrusted in-sandbox data (rarely as defense in depth); use `CHECK` for other
  invariants that must hold in release builds. Apply the same criteria when
  choosing casts (`src/objects/casting.h`).
- **Encapsulation**: Encapsulate data-structure mutations as methods on the
  owning class rather than duplicating low-level steps across callers.
- **Mandatory Reproducer Rule**: Unless waived by the user, every bug fix
  (crash, correctness, or security) MUST include a regression test where every
  test case exercises the modified path and fails without the fix (for Maglev
  `%OptimizeFunctionOnNextCall` tests, omit
  `--optimize-on-next-call-optimizes-to-maglev` from `// Flags:` and verify
  failure under `--variants=stress_maglev`). Also add concise tests (preferring
  `mjsunit`, including `%` runtime functions) for new features, cleanly testable
  behavior/optimization effects, or touched/added paths that lack existing
  coverage and can be tested. Do NOT add new tests (or bloated C++ `unittests` /
  brittle change-detector checks) for refactorings or optimizations without such
  testable effects when existing tests already cover the affected paths.
- **Documentation and Comments Explain Why**: In code you add or change, add a
  brief rationale comment for local decisions (tuned constants, fast-path vs.
  slow-path splits, non-obvious caps and invariants), linking
  `See docs/<path>.md` (file path only) when deeper rationale lives there. When
  a CL changes a design decision, trade-off, or cross-component invariant
  covered by an existing `docs/` file, or contradicts one (search with
  `git --no-pager grep` or run `doc-invalidation-checker`), update that file in
  the same CL with the smallest diff describing the new design per "Writing
  Guidelines" in `docs/README.md`. Most refactorings and bug fixes need no
  `docs/` change. Keep `docs/security/triaging.md` unchanged unless explicitly
  requested.

## Technical Guardrails & Guidelines

- **Inline Functions (`-inl.h`)**: Many functions are declared as `inline` in
  the `.h` file, and defined in a `-inl.h` file. If you get compile errors about
  a missing definition, you are likely missing an `#include` for a `-inl.h`
  file. You can only include `-inl.h` files from other `-inl.h` files and `.cc`
  files.
- **`.cc` vs. `.h` Placement & Anonymous Namespaces**: Place non-trivial,
  non-hot method definitions in the corresponding `.cc` file rather than inline
  in `.h`, and place file-local helpers that do not need `this` in an anonymous
  `namespace {}` in the `.cc` file rather than as private class methods.
- **Pre-Upload Diff Self-Review**: Inspect `git diff` before uploading to ensure
  minimal diff noise, tight local variable scopes (declaring variables at first
  use), clean regression tests, brief "why" comments on local trade-offs, and
  focused `docs/` updates when a design decision, invariant, or documented claim
  changed.
- **Cleanup Constraint**: Keep diffs small by changing only the code and `docs/`
  documentation intended for the task. Propose cleanups to nearby unrelated code
  as a separate patch to keep the current change focused.
- **Supported Architectures**: For most changes and bug fixes, it is fine to
  omit not officially supported architectures and only implement or fix the main
  four: `ia32`, `x64`, `arm`, and `arm64`. Architectures without official Google
  support (e.g., `mips64`, `riscv32`, `riscv64`, `s390x`, `ppc64`, `loong64`)
  are community-maintained and do not need to be updated unless specifically
  requested or targeted.

## General Best Practices

- **Always format before committing:** Run `git cl format` before creating a
  commit to ensure your code adheres to the style guide.
- **Do not edit generated files:** Files in `out/` are generated by the build
  process. Edits should be made to the source files (e.g., `.tq` files for
  Torque, `.pdl` for protocol definitions).
- **Match test configuration to build:** Ensure you are running tests against
  the correct build type (e.g., run mjsunit from out/x64.debug if you built
  x64.debug).
- **Relocating or copying `d8` binaries (`icudtl.dat` and
  `snapshot_blob.bin`):** `d8` resolves `icudtl.dat` and `snapshot_blob.bin`
  relative to the executable directory. When comparing baseline vs. modified
  `d8` across commits, either use a separate build directory (e.g.,
  `out/x64.release-baseline`) or, if copying `out/<config>/d8` to a scratch
  directory, also copy (do not symlink) `icudtl.dat` and `snapshot_blob.bin`
  alongside the copied binary.
- **Check surrounding code for conventions:** Before adding new code, always
  study the existing patterns, naming conventions, and architectural choices in
  the file and directory you are working in.
- **Keep related functions together:** When adding a new function, try to insert
  it near related functions, to keep similar behaviour close.
- **Don't guess header names:** If you don't know where the definition of a
  class or function is, don't try to guess the header name, but search for it
  instead.
- **Be careful with forward declarations:** Many types are forward declared; if
  you want to use them, you'll need to find the definition.

## Inspector & Debugger Invariants

- **Side-effect check mode is non-reentrant and per-call:**
  `Debug::StartSideEffectCheckMode()` asserts
  `DCHECK_EQ(isolate_->debug_execution_mode(), DebugInfo::kBreakpoints)` and is
  not re-entrant. `DebugEvaluate::Global`, `DebugEvaluate::Evaluate`, and
  `v8::debug::CallFunctionOn` enable it only around the single user-code
  `Execution::Call`. Inspector post-processing that touches JS objects (such as
  `InjectedScript::wrapEvaluateResult`, `createExceptionDetails`, or
  `.repl_result` lookup) must run inside an explicit
  `v8::debug::SideEffectCheckScope` when `throwOnSideEffect` is enabled; never
  nest `StartSideEffectCheckMode()` or `SideEffectCheckScope` inside an already
  active side-effect check mode.
- **Execution termination vs. `v8::TryCatch` and `PrepareForExecutionScope`:**
  `v8::TryCatch::HasCaught()` returns `true` even on execution termination
  (while `TryCatch::Exception()` returns `v8::Null()`), and
  `PrepareForExecutionScope` calls `i_isolate->clear_internal_exception()` on
  entry. Always check `tryCatch.HasTerminated() || !tryCatch.CanContinue()`
  before `tryCatch.HasCaught()`, and stop making V8 API calls once
  `isolate->IsExecutionTerminating()` is true unless explicitly propagating or
  recovering termination.
- **Avoid `toStringView("literal")` dangling pointers:**
  `v8_inspector::toStringView` (`src/inspector/string-util.h`) only provides
  overloads for `const String16&` and `const char* str[N]` (array of pointers,
  not a string literal reference). Passing a string literal decays to
  `const char*`, constructs a temporary `String16`, and returns a `StringView`
  pointing to freed memory. Use
  `StringView(reinterpret_cast<const uint8_t*>(lit), sizeof(lit) - 1)` or bind a
  local `String16` variable that outlives the `StringView` consumer.
