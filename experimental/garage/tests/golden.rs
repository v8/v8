// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Golden-file tests over the Phase 0 fixture corpus
//! 0.7).
//!
//! For every fixture whose `manifest.json` entry is `"reproducible": true` —
//! plus the `+deoptverbose` ones, which are non-reproducible *only* because of
//! the `(addr:0x…)` host pointer that this summary never prints — the indexer
//! and parser run over the file and the structural summary is compared against
//! a checked-in golden. Regenerate with:
//!
//! ```text
//! UPDATE_GOLDENS=1 cargo test --test golden
//! git diff tests/golden/   # review what the parser change did
//! ```
//!
//! Two invariants run over **every** fixture, reproducible or not (including
//! the concurrent-recompilation one):
//!
//! - sections partition the file: no line is dropped or double-counted
//!   (PLAN principle 1),
//! - indexing + parsing never panics.

use std::fmt::Write as _;
use std::path::{Path, PathBuf};

use garage::index::TraceIndex;
use garage::model::{EventKind, IcState, LineInfo, PhaseKind};
use garage::parse::maglev;
use garage::source::LogBuffer;

fn manifest_dir() -> PathBuf {
    let dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    if dir.join("fixtures").exists() {
        return dir;
    }
    if let Ok(exe) = std::env::current_exe()
        && let Some(bin_dir) = exe.parent()
    {
        let candidate = bin_dir.join(&dir);
        if candidate.join("fixtures").exists() {
            return candidate;
        }
    }
    dir
}

fn fixtures_root() -> PathBuf {
    manifest_dir().join("fixtures")
}

struct Fixture {
    build: String,
    file: String,
    reproducible: bool,
}

fn corpus() -> Vec<Fixture> {
    let mut out = Vec::new();
    let root = fixtures_root();
    let mut builds: Vec<_> = std::fs::read_dir(&root)
        .expect("fixtures/ exists")
        .filter_map(Result::ok)
        .filter(|e| e.path().join("manifest.json").exists())
        .collect();
    builds.sort_by_key(|e| e.file_name());

    for build in builds {
        let manifest =
            std::fs::read_to_string(build.path().join("manifest.json")).expect("manifest readable");
        let manifest: serde_json::Value =
            serde_json::from_str(&manifest).expect("manifest is valid JSON");
        let Some(fixtures) = manifest["fixtures"].as_array() else {
            continue;
        };
        for f in fixtures {
            let (Some(file), Some(reproducible)) =
                (f["file"].as_str(), f["reproducible"].as_bool())
            else {
                continue;
            };
            out.push(Fixture {
                build: build.file_name().to_string_lossy().into_owned(),
                file: file.to_string(),
                reproducible,
            });
        }
    }
    assert!(!out.is_empty(), "corpus should not be empty");
    out
}

fn load(fixture: &Fixture) -> LogBuffer {
    let path = fixtures_root().join(&fixture.build).join(&fixture.file);
    let bytes = std::fs::read(&path).expect("fixture readable");
    let mut buffer = LogBuffer::new();
    buffer.append(&bytes);
    buffer.finish();
    buffer
}

fn index(buffer: &LogBuffer) -> TraceIndex {
    let mut idx = TraceIndex::new(None);
    idx.ingest(buffer, true);
    idx
}

/// The structural summary a golden records. Everything here must be stable
/// across regenerations of a reproducible fixture; host pointers (`(addr:…)`,
/// `pc`, timings) are deliberately never included.
fn summarize(buffer: &LogBuffer, idx: &TraceIndex) -> String {
    let mut s = String::new();
    let w = &mut s;

    let _ = writeln!(w, "lines: {}", buffer.line_count());
    let _ = writeln!(
        w,
        "version: {}",
        idx.detected_version.as_deref().unwrap_or("unknown")
    );

    for (i, c) in idx.compilations.iter().enumerate() {
        let osr = match &c.osr {
            Some(o) => format!(" osr(offset={:?}, {:?})", o.offset, o.confidence),
            None => String::new(),
        };
        let _ = writeln!(
            w,
            "compilation {i} L{}..{} sfi={} tier={} ord={} name={:?}{osr}",
            c.lines.start,
            c.lines.end,
            c.key.sfi,
            c.key.tier.label(),
            c.key.ordinal,
            c.name,
        );

        let parsed = maglev::parse_compilation(buffer, c);
        if let Some(script) = &parsed.script {
            let _ = writeln!(w, "  script {script:?}");
        }
        let preamble_annotations = parsed
            .preamble
            .iter()
            .filter(|l| matches!(l, LineInfo::Annotation { .. }))
            .count();
        if preamble_annotations > 0 {
            let _ = writeln!(
                w,
                "  preamble L{}..{}: {} annotations",
                c.preamble.start, c.preamble.end, preamble_annotations
            );
        }
        let preamble_source = parsed
            .preamble
            .iter()
            .filter(|l| matches!(l, LineInfo::Source))
            .count();
        if preamble_source > 0 {
            let _ = writeln!(
                w,
                "  preamble L{}..{}: {} JS source lines",
                c.preamble.start, c.preamble.end, preamble_source
            );
        }
        for d in &parsed.inline_decisions {
            let _ = writeln!(
                w,
                "  inline L{} {} callee={:?} reason={:?}",
                d.line,
                if d.inlined { "INLINE" } else { "SKIP" },
                d.callee,
                d.reason
            );
        }

        for (p, phase) in c.phases.iter().enumerate() {
            let kind = match &phase.kind {
                PhaseKind::Graph { known: true } => "graph".to_string(),
                PhaseKind::Graph { known: false } => "graph?".to_string(),
                PhaseKind::Bytecode => "bytecode".to_string(),
                PhaseKind::Inlining { callee, .. } => format!("inlining {callee:?}"),
                PhaseKind::Listing => "listing".to_string(),
            };
            let stats = parsed.phases.get(p);
            let detail = match stats {
                Some(st) if matches!(phase.kind, PhaseKind::Listing) => {
                    // Evidence for the Phase 8 coverage claim: disassembly
                    // rows recognised (with resolved branch destinations),
                    // and unrecognised rows counted.
                    let disasm = st
                        .infos
                        .iter()
                        .filter(|l| matches!(l, LineInfo::Disasm { .. }))
                        .count();
                    let labels = st
                        .infos
                        .iter()
                        .filter(|l| matches!(l, LineInfo::Disasm { labeled: true, .. }))
                        .count();
                    let source = st
                        .infos
                        .iter()
                        .filter(|l| matches!(l, LineInfo::Source))
                        .count();
                    let mut detail = format!(
                        ": {} disasm ({} labels), {} annotations",
                        disasm, labels, st.annotation_count
                    );
                    if source > 0 {
                        let _ = write!(detail, ", {source} source");
                    }
                    detail
                }
                Some(st)
                    if matches!(phase.kind, PhaseKind::Bytecode | PhaseKind::Inlining { .. }) =>
                {
                    // Evidence for the coverage claim: bytecode rows
                    // recognised with their operand refs, pool entries and
                    // feedback slots classified (by IC state).
                    let (mut rows, mut jumps, mut fbv, mut pools) = (0, 0, 0, 0);
                    let mut entries = 0;
                    let (mut mono, mut poly, mut mega, mut other) = (0, 0, 0, 0);
                    for info in &st.infos {
                        match info {
                            LineInfo::Bytecode(bc) => {
                                rows += 1;
                                jumps += bc.targets.len();
                                fbv += bc.fbv.len();
                                pools += bc.pool.len();
                            }
                            LineInfo::PoolEntry { .. } => entries += 1,
                            LineInfo::FeedbackSlot { ic, .. } => match ic {
                                IcState::Monomorphic => mono += 1,
                                IcState::Polymorphic => poly += 1,
                                IcState::Megamorphic => mega += 1,
                                _ => other += 1,
                            },
                            _ => {}
                        }
                    }
                    let mut detail = format!(
                        ": {rows} rows ({jumps} jump refs, {fbv} fbv refs, {pools} pool refs)"
                    );
                    if entries > 0 {
                        let _ = write!(detail, ", {entries} pool entries");
                    }
                    if mono + poly + mega + other > 0 {
                        let _ = write!(
                            detail,
                            ", slots {mono} mono / {poly} poly / {mega} mega / {other} other"
                        );
                    }
                    detail
                }
                Some(st) if matches!(phase.kind, PhaseKind::Graph { .. }) => {
                    let frame_lines = st
                        .infos
                        .iter()
                        .filter(|l| matches!(l, LineInfo::Frame { .. }))
                        .count();
                    format!(
                        ": {} nodes, {} blocks, {} frame-lines, {} annotations",
                        st.node_count, st.block_count, frame_lines, st.annotation_count
                    )
                }
                _ => String::new(),
            };
            let _ = writeln!(
                w,
                "  phase L{}..{} {:?} [{kind}]{detail}",
                phase.lines.start, phase.lines.end, phase.name
            );
        }
        let _ = writeln!(
            w,
            "  totals: {} opcodes, {} distinct frames",
            parsed.opcodes.len(),
            parsed.frames.len(),
        );
    }

    for r in &idx.raw {
        let _ = writeln!(
            w,
            "raw L{}..{} {:?}",
            r.lines.start,
            r.lines.end,
            mask(&r.label)
        );
    }

    for e in &idx.events {
        let detail = match &e.kind {
            EventKind::Marking {
                name,
                sfi,
                target,
                reason,
            } => format!(
                "name={name:?} sfi={} target={} reason={reason:?}",
                opt_addr(sfi),
                target.label()
            ),
            EventKind::CompileStart {
                name,
                sfi,
                target,
                osr,
            }
            | EventKind::CompileDone {
                name,
                sfi,
                target,
                osr,
            } => format!(
                "name={name:?} sfi={} target={} osr={osr}",
                opt_addr(sfi),
                target.label()
            ),
            EventKind::DeoptBegin {
                kind,
                reason,
                name,
                sfi,
                tier,
                opt_id,
                bytecode_offset,
            } => format!(
                "kind={kind} reason={reason:?} name={name:?} sfi={} tier={} opt_id={opt_id:?} offset={bytecode_offset:?}",
                opt_addr(sfi),
                tier.label()
            ),
            EventKind::DeoptEnd { invalidated } => format!("invalidated={invalidated}"),
            EventKind::Osr {
                what,
                name,
                osr_offset,
            } => format!("what={what:?} name={name:?} offset={osr_offset:?}"),
        };
        let _ = writeln!(w, "event L{} {} {detail}", e.line, e.kind.label());
    }

    s
}

fn opt_addr(a: &Option<garage::model::Addr>) -> String {
    match a {
        Some(a) => a.to_string(),
        None => "-".to_string(),
    }
}

/// Raw-section labels quote a trace line verbatim, which can contain host
/// pointers, PIDs and wall-clock timings — the exact things that vary between
/// two runs of the same binary (spike-findings.md §10). Masking them makes the
/// summary stable across corpus regeneration, which is what lets the
/// timing-volatile fixtures (`mvp.*`, `trace-opt+deopt.*`, `trace-osr.*`) be
/// goldens at all.
fn mask(label: &str) -> String {
    let mut out = String::with_capacity(label.len());
    let mut chars = label.char_indices().peekable();
    while let Some((i, c)) = chars.next() {
        // `0x…` hex runs → `0x*`.
        if c == '0' && label[i..].starts_with("0x") {
            chars.next(); // the 'x'
            while chars.peek().is_some_and(|(_, c)| c.is_ascii_hexdigit()) {
                chars.next();
            }
            out.push_str("0x*");
            continue;
        }
        // Decimal numbers with a fractional part (timings) → `*`, and bare
        // integers directly followed by `:0x` (the `[pid:isolate]` prefix of
        // --trace-gc lines) → `*`.
        if c.is_ascii_digit() {
            let start = i;
            let mut end = i + 1;
            let mut fractional = false;
            while let Some(&(j, d)) = chars.peek() {
                if d.is_ascii_digit() {
                    end = j + 1;
                    chars.next();
                } else if d == '.' && label[j + 1..].starts_with(|c: char| c.is_ascii_digit()) {
                    fractional = true;
                    end = j + 1;
                    chars.next();
                } else {
                    break;
                }
            }
            if fractional || label[end..].starts_with(":0x") {
                out.push('*');
            } else {
                out.push_str(&label[start..end]);
            }
            continue;
        }
        out.push(c);
    }
    out
}

/// Sections must partition the file: every line in exactly one section.
fn assert_partition(idx: &TraceIndex, line_count: usize, name: &str) {
    let mut ranges: Vec<(usize, usize)> = idx
        .compilations
        .iter()
        .map(|c| (c.lines.start, c.lines.end))
        .chain(idx.raw.iter().map(|r| (r.lines.start, r.lines.end)))
        .collect();
    ranges.sort();
    let mut at = 0;
    for (start, end) in ranges {
        assert_eq!(start, at, "{name}: gap or overlap at line {at}");
        assert!(end >= start, "{name}: negative section");
        at = end;
    }
    assert_eq!(at, line_count, "{name}: sections do not cover the file");
}

/// Which fixtures get goldens. The summary is a canonicalized projection —
/// no timings, no host pointers, masked raw labels — so byte-level
/// reproducibility is not required; what *is* required is stable structure
/// (line numbers, section boundaries). The one fixture that genuinely lacks
/// that is the concurrent-recompilation one, where thread interleaving
/// reorders lines run to run.
fn is_golden(f: &Fixture) -> bool {
    let _ = f.reproducible; // recorded in the manifest; see the doc comment
    !f.file.starts_with("concurrent")
}

#[test]
fn goldens_match_the_corpus() {
    let update = std::env::var_os("UPDATE_GOLDENS").is_some();
    let root = manifest_dir().join("tests/golden");
    let mut failures = Vec::new();
    let mut checked = 0;

    for fixture in corpus().iter().filter(|f| is_golden(f)) {
        let buffer = load(fixture);
        let idx = index(&buffer);
        let summary = summarize(&buffer, &idx);

        let golden_path = root
            .join(&fixture.build)
            .join(format!("{}.golden", fixture.file));
        if update {
            std::fs::create_dir_all(golden_path.parent().unwrap()).unwrap();
            std::fs::write(&golden_path, &summary).unwrap();
            continue;
        }

        checked += 1;
        match std::fs::read_to_string(&golden_path) {
            Ok(expected) if expected == summary => {}
            Ok(expected) => failures.push(format!(
                "{}/{}: summary drifted\n--- golden\n{}\n--- actual\n{}",
                fixture.build,
                fixture.file,
                diff_head(&expected, &summary),
                diff_head(&summary, &expected),
            )),
            Err(_) => failures.push(format!(
                "{}/{}: missing golden {} (run UPDATE_GOLDENS=1 cargo test --test golden)",
                fixture.build,
                fixture.file,
                golden_path.display()
            )),
        }
    }

    if !update {
        assert!(checked > 20, "expected a real corpus, checked {checked}");
    }
    assert!(
        failures.is_empty(),
        "{} golden mismatches:\n{}",
        failures.len(),
        failures.join("\n\n")
    );
}

/// First few lines that differ, to keep assertion output readable.
fn diff_head(a: &str, b: &str) -> String {
    let mut out = String::new();
    for (la, lb) in a.lines().zip(b.lines()) {
        if la != lb {
            let _ = writeln!(out, "{la}");
            if out.lines().count() >= 6 {
                break;
            }
        }
    }
    if out.is_empty() {
        out.push_str("(line-count difference)");
    }
    out
}

#[test]
fn every_fixture_partitions_and_never_panics() {
    for fixture in corpus() {
        let buffer = load(&fixture);
        let idx = index(&buffer);
        let name = format!("{}/{}", fixture.build, fixture.file);
        assert_partition(&idx, buffer.line_count(), &name);
        for c in &idx.compilations {
            let parsed = maglev::parse_compilation(&buffer, c);
            // Parsed phases mirror the indexed ones one-to-one.
            assert_eq!(parsed.phases.len(), c.phases.len(), "{name}");
            for (section, parsed_phase) in c.phases.iter().zip(&parsed.phases) {
                assert_eq!(
                    parsed_phase.infos.len(),
                    section.lines.len(),
                    "{name}: every line of {:?} classified",
                    section.name
                );
            }
        }
    }
}

/// golden requirement in test form: the volatile `[ML:<id>]` prefix
/// must not be load-bearing — the same workload with and without ids yields
/// identical inlining decisions.
#[test]
fn inline_decisions_do_not_depend_on_the_id_prefix() {
    let root = fixtures_root().join("x64-v16.0.0");
    let mut decisions = Vec::new();
    for file in [
        "maglev-graphs+inlining.inlining.log",
        "maglev-graphs+inlining-ids.inlining.log",
    ] {
        let bytes = std::fs::read(root.join(file)).expect("fixture readable");
        let mut buffer = LogBuffer::new();
        buffer.append(&bytes);
        buffer.finish();
        let idx = index(&buffer);
        let all: Vec<(bool, String, String)> = idx
            .compilations
            .iter()
            .flat_map(|c| {
                maglev::parse_compilation(&buffer, c)
                    .inline_decisions
                    .into_iter()
                    .map(|d| (d.inlined, d.callee, d.reason))
            })
            .collect();
        assert!(
            !all.is_empty(),
            "{file}: expected inlining decisions in the fixture"
        );
        decisions.push(all);
    }
    assert_eq!(
        decisions[0], decisions[1],
        "decisions must match with and without [ML:] prefixes"
    );
}

/// A bounded random fuzz pass: many seeds, mutations of real
/// fixture data — byte flips, splices, truncations — plus pure noise. Every
/// input must index into a valid partition and parse without panicking.
/// (A coverage-guided fuzzer would be better; this is the always-on floor.)
#[test]
fn fuzz_mutated_fixtures_never_panic() {
    let base = std::fs::read(fixtures_root().join("x64-v16.0.0/maglev-graphs.throw.log"))
        .expect("fixture readable");
    let base = &base[..base.len().min(64 * 1024)];

    let mut state = 0x9e37_79b9_7f4a_7c15_u64;
    let mut rand = move || {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        state
    };

    for round in 0..200 {
        let mut data = base.to_vec();
        // A handful of random mutations per round.
        for _ in 0..(rand() % 8 + 1) {
            match rand() % 4 {
                0 => {
                    // Flip a byte.
                    let at = (rand() as usize) % data.len();
                    data[at] = (rand() & 0xff) as u8;
                }
                1 => {
                    // Truncate.
                    let at = (rand() as usize) % data.len();
                    data.truncate(at.max(1));
                }
                2 => {
                    // Splice a boundary-looking fragment somewhere random.
                    let fragments: [&[u8]; 4] = [
                        b"\nCompiling 0x1 <JSFunction f (sfi = 0x10)> with Maglev\n",
                        b"\n----- Register allocation -----\n",
                        b"\n      \xe2\x86\xb1 eager @2 (5 live vars)\n",
                        b"\n[bailout (kind: deopt-eager, reason: x): begin. deoptimizing ",
                    ];
                    let frag = fragments[(rand() as usize) % fragments.len()];
                    let at = (rand() as usize) % data.len();
                    data.splice(at..at, frag.iter().copied());
                }
                _ => {
                    // Duplicate a random slice (creates weird nesting).
                    let a = (rand() as usize) % data.len();
                    let b = ((rand() as usize) % data.len()).max(a);
                    let slice = data[a..b.min(a + 4096)].to_vec();
                    data.extend_from_slice(&slice);
                }
            }
        }

        let mut buffer = LogBuffer::new();
        buffer.append(&data);
        buffer.finish();
        let idx = index(&buffer);
        assert_partition(&idx, buffer.line_count(), &format!("fuzz round {round}"));
        for c in &idx.compilations {
            let _ = maglev::parse_compilation(&buffer, c);
        }
    }
}

/// Truncation and garbage must never panic the indexer or parser.
#[test]
fn truncated_and_garbage_input_is_survivable() {
    let path = fixtures_root().join("x64-v16.0.0/maglev-graphs.simple.log");
    let bytes = std::fs::read(path).expect("fixture readable");

    // Truncate mid-file at several points, including mid-line and mid-escape.
    for cut in [1, 100, 1000, bytes.len() / 2, bytes.len() - 1] {
        let mut buffer = LogBuffer::new();
        buffer.append(&bytes[..cut]);
        buffer.finish();
        let idx = index(&buffer);
        assert_partition(&idx, buffer.line_count(), &format!("truncated@{cut}"));
        for c in &idx.compilations {
            let _ = maglev::parse_compilation(&buffer, c);
        }
    }

    // Deterministic pseudo-random bytes; a poor man's fuzzer (real one: 5.4).
    let mut state = 0x243f_6a88_85a3_08d3_u64;
    let mut garbage = Vec::with_capacity(1 << 16);
    for _ in 0..(1 << 16) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        garbage.push((state & 0xff) as u8);
    }
    // Splice recognizable boundaries into the noise so the classifier arms run.
    let spliced = [
        &garbage[..1024],
        b"\nCompiling 0x1 <JSFunction f (sfi = 0x10)> with Maglev\n".as_slice(),
        &garbage[1024..2048],
        b"\n----- Maglev graph building -----\n",
        &garbage[2048..4096],
        b"\n   9: CheckedSmiUntag [n2], 1 uses\n      \xe2\x86\xb1 eager @2 (5 live vars)\n",
        &garbage[4096..],
    ]
    .concat();
    let mut buffer = LogBuffer::new();
    buffer.append(&spliced);
    buffer.finish();
    let idx = index(&buffer);
    assert_partition(&idx, buffer.line_count(), "garbage");
    for c in &idx.compilations {
        let _ = maglev::parse_compilation(&buffer, c);
    }
}

// ---------------------------------------------------------------------------
// TODO 10.2: format-drift report.
//
// Driven by tools/check-format-drift.sh, which generates a corpus from a fresh
// d8 into a scratch directory and points this at it. Ignored by default because
// it needs that directory — a plain `cargo test` has nothing to report on.
//
// This is a *report*, not a golden: a new V8 is allowed to print new things.
// What it fails on is the parser losing ground — a banner no version knows, a
// file that stops partitioning, a fixture that yields nothing at all.
// ---------------------------------------------------------------------------

/// A drift finding, ordered by how much it matters.
#[derive(PartialEq, Eq, PartialOrd, Ord)]
enum Drift {
    /// The indexer's core invariant broke: sections no longer tile the file.
    Partition(String),
    /// A fixture that should describe compilations described none.
    NoCompilations(String),
    /// `----- <name> -----` matched no version in assets/markers.toml. The
    /// designed degradation (markers.toml header, journey J5) — and the signal
    /// that the marker table needs a new version column.
    UnknownBanner { file: String, banner: String },
}

fn drift_for(path: &Path, name: &str, out: &mut Vec<Drift>) {
    let Ok(bytes) = std::fs::read(path) else {
        return;
    };
    let mut buffer = LogBuffer::new();
    buffer.append(&bytes);
    buffer.finish();
    let idx = index(&buffer);

    // The same invariant assert_partition checks, collected rather than
    // panicked so one bad file does not hide the rest of the report.
    let mut ranges: Vec<(usize, usize)> = idx
        .compilations
        .iter()
        .map(|c| (c.lines.start, c.lines.end))
        .chain(idx.raw.iter().map(|r| (r.lines.start, r.lines.end)))
        .collect();
    ranges.sort();
    let mut at = 0;
    let mut ok = true;
    for (start, end) in ranges {
        if start != at || end < start {
            ok = false;
            break;
        }
        at = end;
    }
    if !ok || at != buffer.line_count() {
        out.push(Drift::Partition(name.to_string()));
    }

    // A graph-printing flag that yields no compilation at all is the loudest
    // possible drift: the compilation anchor itself stopped matching.
    let anchored = name.contains("maglev-graph")
        || name.contains("turbolev")
        || name.contains("turbo-graph")
        || name.starts_with("mvp");
    if anchored && idx.compilations.is_empty() {
        out.push(Drift::NoCompilations(name.to_string()));
    }

    let mut seen = std::collections::BTreeSet::new();
    for c in &idx.compilations {
        for p in &c.phases {
            if matches!(p.kind, PhaseKind::Graph { known: false }) && seen.insert(p.name.clone()) {
                out.push(Drift::UnknownBanner {
                    file: name.to_string(),
                    banner: p.name.clone(),
                });
            }
        }
        // Parsing must still not panic on anything the new build prints.
        let _ = maglev::parse_compilation(&buffer, c);
    }
}

#[test]
#[ignore = "needs a corpus from a fresh d8; run tools/check-format-drift.sh"]
fn format_drift_report() {
    let Some(dir) = std::env::var_os("GARAGE_DRIFT_CORPUS") else {
        panic!("set GARAGE_DRIFT_CORPUS to a directory of .log files");
    };
    let dir = PathBuf::from(dir);

    let mut logs: Vec<PathBuf> = std::fs::read_dir(&dir)
        .unwrap_or_else(|e| panic!("cannot read {}: {e}", dir.display()))
        .filter_map(Result::ok)
        .map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|x| x == "log"))
        .collect();
    logs.sort();
    assert!(!logs.is_empty(), "no .log files in {}", dir.display());

    let mut findings = Vec::new();
    let mut versions = std::collections::BTreeMap::new();
    for path in &logs {
        let name = path
            .file_name()
            .unwrap_or_default()
            .to_string_lossy()
            .into_owned();
        // Record what the marker table thinks this build is, per file: a fresh
        // d8 whose banners all resolve to an *older* column is drift too, just
        // the quiet kind.
        let bytes = std::fs::read(path).unwrap_or_default();
        let mut buffer = LogBuffer::new();
        buffer.append(&bytes);
        buffer.finish();
        let detected = index(&buffer)
            .detected_version
            .unwrap_or_else(|| "unknown".to_string());
        *versions.entry(detected).or_insert(0usize) += 1;

        drift_for(path, &name, &mut findings);
    }
    findings.sort();

    println!("\nformat-drift report over {} files", logs.len());
    println!("  detected versions: {versions:?}");
    if findings.is_empty() {
        println!("  no drift: every banner is known, every file partitions\n");
        return;
    }

    let mut lines = Vec::new();
    for f in &findings {
        lines.push(match f {
            Drift::Partition(file) => {
                format!("  PARTITION  {file}: sections no longer tile the file")
            }
            Drift::NoCompilations(file) => {
                format!("  EMPTY      {file}: a graph flag produced no compilation")
            }
            Drift::UnknownBanner { file, banner } => {
                format!("  BANNER     {file}: {banner:?} is in no version of assets/markers.toml")
            }
        });
    }
    let report = lines.join("\n");
    println!("{report}\n");
    panic!(
        "{} drift finding(s):\n{report}\n\nIf this is a new V8, add its phase \
         names to assets/markers.toml (see the header there for where the \
         table comes from).",
        findings.len()
    );
}
