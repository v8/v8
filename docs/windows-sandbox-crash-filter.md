# Windows x64 sandbox crash filter

This change adds an initial Windows VEH implementation for V8 sandbox testing.

## Scope

- Handles well-formed `EXCEPTION_ACCESS_VIOLATION` records and Windows debug-trap exceptions (`EXCEPTION_BREAKPOINT` and `EXCEPTION_SINGLE_STEP`).
- Recognizes the faulting address in the sandbox reservation and in the
  reserved zero segment (4GB base plus V8's calculated padding).
- Uses Windows access kinds 0 (read), 1 (write), and 8 (execute), rejecting
  malformed or unknown records.
- Registers with `AddVectoredExceptionHandler(FALSE, ...)`, so V8's existing
  first-priority WebAssembly trap handler gets the first opportunity to handle
  Wasm bounds-check traps.
- Exits with code 0 in `kForTesting` mode and a non-zero code in
  `kForFuzzing` mode for a classified harmless fault.

## Deliberate limitations

This is an initial Windows implementation, not yet feature-equivalent to the
Linux crash filter. It does not yet classify non-canonical addresses, kernel
addresses, stack overflows, safe memory regions, sanitizer reports, illegal-
instruction classification, or arbitrary unmapped-memory conditions. Unknown and
malformed exceptions continue normal Windows exception search.

The VEH callback is intentionally small: it does not call `VirtualQuery`, allocate memory, take locks, access the V8
heap, or dereference the faulting address. It uses `TerminateProcess` to implement the harmless-crash exit path;
this behavior must be exercised on supported Windows configurations and
reviewed for exception-handler safety before production use.

## Validation still required

- Build V8 x64 with `v8_enable_sandbox=true`.
- Run the Windows sandbox crash-filter unit tests.
- Exercise coexistence with the Wasm trap handler and supported ASan builds.
- Validate the included integration/death tests for harmless sandbox faults
  and fuzzing-mode exit codes.
- Add an end-to-end test that verifies outside-sandbox faults are reported by
  the normal Windows exception/crash-reporting path.
- Review with V8 crash-handler/security owners before treating this as
  production-ready.
