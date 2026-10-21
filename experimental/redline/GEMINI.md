# Redline Agent Instructions

Before modifying, reviewing, or testing code in `experimental/redline/`, read the local documentation in this directory:

- [README.md](README.md): Overview, build instructions (`out/redline` and `out/fuzzbuild/d8`), and command reference.
- [DESIGN.md](DESIGN.md): Pipeline architecture, stages, and design invariants.
- [TUTORIAL.md](TUTORIAL.md): End-to-end command usage and event log inspection.
- [CONTRIBUTE.md](CONTRIBUTE.md): **Mandatory reading before making or reviewing changes.** Covers adding/modifying generators, mutators, reducers, selection strategies, and presets, as well as the required verification and A/B measurement workflow.

## Required Verification Checklist (from [CONTRIBUTE.md](CONTRIBUTE.md))

Before uploading or finalizing any CL touching `experimental/redline/`:

1. **Build & Lint**:
   - Build `redline` cleanly (`-Werror` is enabled in `BUILD.gn`).
   - Run `git cl format` and `git cl lint` (`0` warnings/errors).
2. **Selftests** (Section 11):
   - Run `redline selftest --offline`.
   - Run `redline selftest --d8=out/fuzzbuild/d8`.
   - If a change intentionally alters deterministic generator/mutator/codegen output, regenerate golden traces with `--update` and inspect `git diff experimental/redline/testdata/`.
3. **Parser / Printer / Scope Round-Trip** (when modifying `fast/`):
   - Run `redline roundtrip --dir=test/mjsunit --no-run` and verify `0` false rejections (accepted by V8, rejected by `fast::Parse`) and `100%` semantic round-trip.
4. **Generator & Mutator Validity** (Sections 3–4, 11):
   - For any added or modified generator/mutator, run `redline mutate --generate --generator=<Name> --n=500` (or `--mutator=<Name>`) and verify at least `99.8%` V8 syntax acceptance.
5. **Before vs. After A/B Fuzzing Comparison** (Section 10):
   - For any change affecting fuzzing behavior, coverage, execution, generators, mutators, or selection, build `redline` before (`HEAD~1`) and after (`HEAD`) and run two 1-hour (`--duration=3600`) `redline fuzz` sessions side-by-side against the same instrumented `out/fuzzbuild/d8` (`--d8-extra-args="--single-threaded --random-seed=42" --preset=default --jobs=8 --seed=42 --duration=3600 --stats-interval=60 --no-component-stats`).
   - Note: `redline` embeds V8 for syntax checking (`SyntaxChecker`); if copying `redline` binaries outside `out/<dir>/`, build `redline_base` at the exact same V8 base commit (`HEAD~1`) and symlink `snapshot_blob.bin` and `icudtl.dat` into the binary directory, or use separate GN build directories.
   - Compare minute-by-minute coverage and execution counts from `events.jsonl` using the script in [CONTRIBUTE.md Section 10.1](CONTRIBUTE.md#101-a-local-ab-run).
