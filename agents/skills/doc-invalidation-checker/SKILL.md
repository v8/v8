---
name: doc-invalidation-checker
description: Checks whether uncommitted changes, current branch diffs (git diff @{upstream}...HEAD), or recent commits contradict an existing docs/ file or change a design decision, trade-off, or cross-component invariant covered by one, and applies focused updates to affected markdown files. Use before uploading a CL, during pre-commit verification, or when auditing recent commits for documentation drift. Do not use for general C++ compilation or running test suites.
---

# Documentation Invalidation Checker

Use this skill to verify whether code changes on the current branch, in the
working tree, or in recent commits contradict an existing design document in
`docs/` or change a design decision, trade-off, or cross-component invariant
covered by one, and to apply focused updates to affected `.md` files.

## Workflow

### 1. Retrieve the Target Diff

Select the command matching your workflow stage:

- **Current Branch / Pre-Upload Check (Default for CL Authoring)**: Inspect all
  committed changes on the current branch relative to upstream
  (`@{upstream}...HEAD`) together with any uncommitted `src/` and `include/`
  changes in the working tree:

  ```bash
  agents/scripts/get_recent_commits.py --branch
  ```

- **Uncommitted Working-Tree Changes**: Inspect staged and unstaged edits
  against `HEAD`:

  ```bash
  agents/scripts/get_recent_commits.py --uncommitted
  ```

- **Recent Commits Audit**: Inspect the last `N` commits (e.g., `5`) or a
  specific revision range:

  ```bash
  agents/scripts/get_recent_commits.py 5
  agents/scripts/get_recent_commits.py --range origin/main..HEAD
  ```

### 2. Locate Relevant Documentation in `docs/`

1. Review the `CANDIDATE DOCS DIRECTORIES/FILES TO CHECK` output from
   `get_recent_commits.py`, which maps modified `src/` directories to their
   corresponding `docs/` folders and files.

2. Search across `docs/` for every C++ class, method, enum, bitmask, register,
   pipeline pass, bytecode, IR node, or flag modified, renamed, or removed in
   the diff:

   ```bash
   git --no-pager grep -n "<SymbolOrFlagName>" docs/
   ```

3. Check both subsystem subdirectories (`docs/compiler/`, `docs/heap/`,
   `docs/interpreter/`, `docs/objects/`, `docs/parsing/`, `docs/regexp/`,
   `docs/runtime/`, `docs/sandbox/`, `docs/snapshot/`, `docs/torque/`,
   `docs/wasm/`, `docs/builtins/`, `docs/codegen/`) and top-level files in
   `docs/`.

### 3. Verify Documentation Against `src/`

For each candidate `.md` file in `docs/`:

- **Direct Verification (Single CL / Pre-Upload)**: Read the relevant sections
  of the `.md` file and compare every claim against the diff and `src/`.

- **Parallel Subagent Verification (Multi-Commit / Broad Audit)**: When auditing
  multiple commits or many documentation files, call `invoke_subagent` with
  `TypeName: "self"`, passing the diff and target `docs/` file path(s) with the
  following prompt:

  > Compare this diff and the current implementation in `src/` with
  > `[doc_path]`. Identify any statement, symbol, bitmask, register, pipeline
  > pass, bytecode/IR node, flag, or invariant in `[doc_path]` that is now
  > inaccurate, or any design-level trade-off or cost-model change covered by
  > `[doc_path]` per "Writing Guidelines" in `docs/README.md`. Provide exact
  > file:line citations and concise present-tense replacement text verified
  > against `src/`.

Check specifically for:

1. **Renamed or Removed Symbols**: Class names, method names, enum constants,
   bitmasks, bytecodes, IR operations, or CLI flags
   (`src/flags/flag-definitions.h`).
2. **Pipeline & Ordering Changes**: Added, reordered, or removed compiler
   phases/reducers (`src/maglev/`, `src/compiler/turboshaft/`, `src/baseline/`),
   register allocation steps, or GC/write-barrier paths.
3. **Cross-Tier & Security Invariants**: Changes to object layouts (`V8_OBJECT`,
   `TaggedMember`, `TrustedPointerMember`), pointer table tags, hole sentinels
   (`TheHole` vs. `TdzHole`), deoptimization frame state, or background
   compilation serialization contracts.
4. **Design Decisions & Trade-offs**: New or changed heuristics, cost models,
   fast-path vs. slow-path trade-offs, tried-and-rejected alternatives, or
   cross-component constraints. When a CL changes a design-level trade-off
   covered by a doc, add a concise present-tense explanation grounded in code
   comments or commit rationale. When a CL only retunes a numeric value for a
   constant or flag whose value is not already quoted in `docs/`, keep `docs/`
   unchanged; if a doc already quotes the old value, update that value in place
   without rewriting surrounding prose.

### 4. Update Affected Documentation

Follow "Writing Guidelines" in `docs/README.md`:

1. **Stop When No Update Is Needed**: Most refactorings, bug fixes, and
   value-only constant retunes need no `docs/` change. When no existing `docs/`
   file is contradicted and no design-level trade-off or cross-component
   invariant covered by one changed, report that no `docs/` update is needed and
   stop without editing. Update existing docs during ordinary CLs rather than
   creating brand-new `docs/` files unprompted.
2. **Apply Small, Proportional Updates Directly**: Edit affected `.md` file(s)
   in `docs/` within the same CL using the smallest diff that describes the new
   design so reading the `docs/` diff alone summarizes the design change for a
   reviewer. Write in present tense describing the current design; leave
   benchmark numbers, percentages, CL/change history, and bug narratives in the
   commit message, and name constants/flags without quoting mutable numeric
   values in new text. State only rationale traceable to code comments, the
   commit message, or `src/`.
3. **Preserve Manual Security Triaging Rules**: Keep `docs/security/triaging.md`
   unchanged unless the user explicitly asks to modify it, as it is maintained
   manually.
4. **Formatting & Link Hygiene**:
   - Verify every C++ symbol, constant, and invariant written into `docs/`
     against `src/`.
   - Ensure all relative markdown links resolve to existing files. When moving
     or renaming a `docs/` file, run
     `git --no-pager grep -n "docs/<filename>" src/` and update any matching
     code-to-doc links.
   - Use standard ASCII punctuation (commas, colons, parentheses) rather than
     the Unicode em-dash (`U+2014`).

## Tools & References

- **[get_recent_commits.py](../../scripts/get_recent_commits.py)**: Retrieves
  branch diffs (`--branch`), uncommitted diffs (`--uncommitted`), revision
  ranges (`--range`), or recent commits (`[N]`), and maps modified `src/` paths
  to candidate `docs/` directories.
