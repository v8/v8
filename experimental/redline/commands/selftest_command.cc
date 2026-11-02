// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// "redline selftest": end-to-end checks of the execution layer against a
// real target. Modeled on Fuzzilli's startup tests, extended with the
// behaviours the fuzzer relies on (timeouts, respawning, output capture,
// coverage evaluation and edge determinism). Prints one PASS/FAIL line per
// check and exits non-zero if any check failed.

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <set>

#include "experimental/redline/base/file_util.h"
#include "experimental/redline/base/flags.h"
#include "experimental/redline/base/log.h"
#include "experimental/redline/base/time_util.h"
#include "experimental/redline/commands/command.h"
#include "experimental/redline/commands/selftest_checker.h"
#include "experimental/redline/commands/selftest_selection.h"
#include "experimental/redline/engine/crash.h"
#include "experimental/redline/exec/coverage.h"
#include "experimental/redline/exec/runner.h"

namespace redline {

namespace {

constexpr const char* kLog = "selftest";

std::string Describe(const Execution& e) {
  return StringPrintf("%s (exit code %d, signal %d, %.1f ms)",
                      OutcomeName(e.outcome), e.exit_code, e.signal,
                      static_cast<double>(e.exec_time_us) / 1000.0);
}

// Runs `script` and returns false on infrastructure failure (reported as
// a failed check).
bool RunOrFail(Checker* checker, ScriptRunner* runner, const char* name,
               const std::string& script, ScriptRunner::Result* result) {
  std::string error;
  if (!runner->Run(script, result, &error)) {
    checker->Check(name, false, "execution failed: " + error);
    return false;
  }
  return true;
}

// experimental/redline/testdata, found from the location of the binary, or
// empty. Look for testdata/selection so we do not pick up unrelated
// out/<dir>/testdata directories created by other build targets.
std::string DefaultTestdataDir() {
  std::string selection = RedlineDataDir("testdata/selection");
  if (!selection.empty()) {
    return selection.substr(0, selection.rfind('/'));
  }
  return RedlineDataDir("testdata");
}

// Offline: crash signatures. A recursion that overflows the stack must get
// the same signature wherever the stack ran out; other crashes keep their
// first frames.
void RunCrashSignatureChecks(Checker* checker) {
  auto frame = [](const char* mangled) {
    return StringPrintf("./d8(%s+0x10)[0x5555]\n", mangled);
  };
  auto trace = [&](const char* top, int depth) {
    std::string text = "Received signal 11 SEGV_MAPERR 7ffd00000ff8\n\n";
    text += "==== C stack trace ===============================\n\n";
    text += frame(top);
    const char* cycle[] = {"_ZN2v88internal3FooEv", "_ZN2v88internal3BarEv",
                           "_ZN2v88internal3BazEv"};
    for (int i = 0; i < depth; i++) {
      text += frame(cycle[i % 3]);
    }
    return text + "[end of stack trace]\n";
  };
  CrashSignature a =
      CrashSignature::FromOutput(11, 0, trace("_ZN2v88internal4Top1Ev", 57));
  CrashSignature b =
      CrashSignature::FromOutput(11, 0, trace("_ZN2v88internal4Top2Ev", 55));
  checker->Check(
      "crash signature: a stack overflow does not depend on the faulting frame",
      a == b, a.text + " vs " + b.text);
  checker->Check("crash signature: a stack overflow names its recursion",
                 a.text ==
                     "sig11 SEGV_MAPERR stack-overflow v8::internal::Bar | "
                     "v8::internal::Baz | v8::internal::Foo",
                 a.text);
  CrashSignature c =
      CrashSignature::FromOutput(11, 0, trace("_ZN2v88internal4Top1Ev", 2));
  checker->Check("crash signature: other crashes keep their first three frames",
                 c.text ==
                     "sig11 SEGV_MAPERR v8::internal::Top1 < v8::internal::Foo "
                     "< v8::internal::Bar",
                 c.text);
}

// Offline: the coverage header (experimental/redline/d8/cov.h) on synthetic
// shared memory.
void RunCoverageHeaderChecks(Checker* checker) {
  std::string error;
  std::unique_ptr<CoverageMap> map = CoverageMap::Create(9998, &error);
  if (!checker->Check("coverage header: create a coverage map", map != nullptr,
                      error)) {
    return;
  }
  int fd = shm_open(map->shm_name().c_str(), O_RDWR, 0);
  void* mapping = fd < 0 ? MAP_FAILED
                         : mmap(nullptr, CoverageMap::kShmSize,
                                PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (fd >= 0) {
    close(fd);
  }
  const bool mapped = mapping != MAP_FAILED;
  if (!checker->Check("coverage header: map the shared memory", mapped,
                      mapped ? "" : strerror(errno))) {
    return;
  }
  uint8_t* shm = static_cast<uint8_t*>(mapping);
  // A target with inline-bool-flag, 80 code edges and 10 builtins blocks
  // writes this header.
  cov::Header header = {};
  header.magic = cov::kMagic;
  header.version = cov::kVersion;
  header.header_size = cov::kHeaderSize;
  header.shm_size = static_cast<uint32_t>(CoverageMap::kShmSize);
  header.mode = static_cast<uint32_t>(cov::Mode::kInlineBoolFlag);
  header.bits_per_edge = 1;
  header.num_edges = 90;
  header.max_edges = cov::MaxEdges(header.shm_size, 1);
  header.num_regions = 2;
  header.regions[0] = {static_cast<uint32_t>(cov::RegionKind::kCode), 1, 80};
  header.regions[1] = {static_cast<uint32_t>(cov::RegionKind::kBuiltins), 81,
                       10};
  header.build_id_size = 2;
  header.build_id[0] = 0xab;
  header.build_id[1] = 0x01;
  memcpy(shm, &header, sizeof(header));
  bool ok = map->FinishInitialization(&error);
  checker->Check("coverage header: read a valid header", ok, error);
  if (ok) {
    checker->Check(
        "coverage header: geometry",
        map->num_edges() == 90 &&
            strcmp(map->mode_name(), "inline-bool-flag") == 0 &&
            map->build_id() == "ab01",
        StringPrintf("%u edges, mode %s, build id %s", map->num_edges(),
                     map->mode_name(), map->build_id().c_str()));
    // The bitmap starts after the header.
    uint8_t* bits = shm + cov::kHeaderSize;
    bits[5 / 8] |= static_cast<uint8_t>(1u << (5 % 8));
    bits[85 / 8] |= static_cast<uint8_t>(1u << (85 % 8));
    const std::vector<uint32_t> expected = {5, 85};
    checker->Check("coverage header: bits after the header",
                   map->CurrentEdges() == expected,
                   StringPrintf("%zu edges", map->CurrentEdges().size()));
    // A new target process with other edges is another binary.
    cov::Header other = header;
    other.num_edges = 91;
    other.regions[1].count = 11;
    memcpy(shm, &other, sizeof(other));
    std::string changed;
    checker->Check("coverage header: a changed header is an error",
                   !map->FinishInitialization(&changed) &&
                       changed.find("mixing binaries") != std::string::npos,
                   changed);
  }
  // Headers that this Redline rejects.
  struct BadHeader {
    const char* name;
    cov::Header header;
  };
  BadHeader bad[4] = {{"no magic", header},
                      {"a region beyond the edges", header},
                      {"no instrumentation", header},
                      {"a wrong edge size", header}};
  bad[0].header.magic = 0;
  bad[1].header.regions[1].count = 11;
  bad[2].header.mode = static_cast<uint32_t>(cov::Mode::kNone);
  bad[3].header.mode = static_cast<uint32_t>(cov::Mode::kInline8bitCounters);
  for (const BadHeader& b : bad) {
    std::unique_ptr<CoverageMap> fresh = CoverageMap::Create(9997, &error);
    if (fresh == nullptr) {
      checker->Check("coverage header: create a coverage map", false, error);
      break;
    }
    int bad_fd = shm_open(fresh->shm_name().c_str(), O_RDWR, 0);
    if (bad_fd >= 0) {
      if (pwrite(bad_fd, &b.header, sizeof(b.header), 0) !=
          static_cast<ssize_t>(sizeof(b.header))) {
        bad_fd = -1;
      }
      close(bad_fd);
    }
    std::string reason;
    checker->Check(StringPrintf("coverage header: reject %s", b.name).c_str(),
                   bad_fd >= 0 && !fresh->FinishInitialization(&reason),
                   reason);
  }
  munmap(mapping, CoverageMap::kShmSize);
}

// Offline: the counters layout of the coverage (exec/coverage.h) on a
// synthetic header.
void RunCoverageCounterChecks(Checker* checker) {
  const uint32_t buckets[][2] = {{1, 0},   {2, 1},   {3, 2},  {4, 3},  {7, 3},
                                 {8, 4},   {15, 4},  {16, 5}, {31, 5}, {32, 6},
                                 {127, 6}, {128, 7}, {255, 7}};
  bool buckets_ok = true;
  for (const auto& [count, bucket] : buckets) {
    buckets_ok = buckets_ok &&
                 CoverageMap::Bucket(static_cast<uint8_t>(count)) == bucket;
  }
  checker->Check("coverage counters: AFL bucket table", buckets_ok, "");

  std::string error;
  std::unique_ptr<CoverageMap> map = CoverageMap::Create(9999, &error);
  if (!checker->Check("coverage counters: create a coverage map",
                      map != nullptr, error)) {
    return;
  }
  // A target with 80 code edges, 10 builtins blocks and 10 optimized code
  // features writes this header.
  int fd = shm_open(map->shm_name().c_str(), O_RDWR, 0);
  void* mapping = fd < 0 ? MAP_FAILED
                         : mmap(nullptr, CoverageMap::kShmSize,
                                PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (fd >= 0) {
    close(fd);
  }
  const bool mapped = mapping != MAP_FAILED;
  if (!checker->Check("coverage counters: map the shared memory", mapped,
                      mapped ? "" : strerror(errno))) {
    return;
  }
  uint8_t* shm = static_cast<uint8_t*>(mapping);
  cov::Header header = {};
  header.magic = cov::kMagic;
  header.version = cov::kVersion;
  header.header_size = cov::kHeaderSize;
  header.shm_size = static_cast<uint32_t>(CoverageMap::kShmSize);
  header.mode = static_cast<uint32_t>(cov::Mode::kInline8bitCounters);
  header.bits_per_edge = 8;
  header.num_edges = 100;
  header.max_edges = cov::MaxEdges(header.shm_size, 8);
  header.num_regions = 3;
  header.regions[0] = {static_cast<uint32_t>(cov::RegionKind::kCode), 1, 80};
  header.regions[1] = {static_cast<uint32_t>(cov::RegionKind::kBuiltins), 81,
                       10};
  header.regions[2] = {static_cast<uint32_t>(cov::RegionKind::kOptimized), 91,
                       10};
  memcpy(shm, &header, sizeof(header));
  CoverageMap::Watch watch;
  watch.edges = {7, 5};
  map->SetWatch(watch);
  bool ok = map->FinishInitialization(&error);
  checker->Check("coverage counters: read the header", ok, error);
  if (ok) {
    checker->Check(
        "coverage counters: geometry",
        map->counters_mode() && map->real_edges() == 100 &&
            map->counted_edges() == 90 && map->watched().size() == 12 &&
            map->num_edges() == 100 + 12 * CoverageMap::kBuckets,
        StringPrintf("features %u, counted %u, watched %zu", map->num_edges(),
                     map->counted_edges(), map->watched().size()));
    uint8_t* counters = shm + cov::kHeaderSize;
    counters[5] = 3;
    counters[50] = 1;
    counters[91] = 200;
    map->Refresh();
    // Edge 5 is watched edge 0 (bucket 2), edge 91 is watched edge 2
    // (bucket 7). Bucket features start at 101.
    const std::vector<uint32_t> expected = {5, 50, 91, 103, 124};
    checker->Check(
        "coverage counters: hit bits and bucket features",
        map->CurrentEdges() == expected && map->CountCurrentEdges() == 2,
        StringPrintf("%zu features, %u counted edges",
                     map->CurrentEdges().size(), map->CountCurrentEdges()));
    // Several counters in one 8-byte word. Also a watched edge in the middle
    // of the optimized region: watched edge 10, bucket 4 for a count of 9.
    map->ClearBitmap();
    counters[8] = 1;
    counters[12] = 255;
    counters[15] = 128;
    counters[99] = 9;
    map->Refresh();
    const std::vector<uint32_t> packed = {8, 12, 15, 99, 185};
    checker->Check("coverage counters: packed hit bits",
                   map->CurrentEdges() == packed,
                   StringPrintf("%zu features", map->CurrentEdges().size()));
  }
  munmap(mapping, CoverageMap::kShmSize);
}

int RunSelftest(const Flags& flags) {
  Checker checker;
  std::string error;

  // Offline checks first: they need no target.
  RunCrashSignatureChecks(&checker);
  RunCoverageHeaderChecks(&checker);
  RunCoverageCounterChecks(&checker);
  RunNodeKindChecks(&checker);
  RunPresetChecks(&checker);
  std::string testdata = flags.GetString("testdata", DefaultTestdataDir());
  if (testdata.empty()) {
    checker.Check("selection: locate experimental/redline/testdata", false,
                  "not found next to the binary; pass --testdata=DIR");
  } else {
    RunSelectionChecks(&checker, testdata,
                       flags.GetBool("write-golden", false));
  }
  if (flags.GetBool("offline", false)) {
    printf("selftest: %d passed, %d failed\n", checker.passed(),
           checker.failed());
    return checker.failed() == 0 ? kExitOk : kExitFailure;
  }

  TargetOptions target;
  if (!TargetOptions::FromFlags(flags, &target, &error)) {
    REDLINE_ERROR(kLog, "%s", error.c_str());
    return kExitUsage;
  }
  // The respawn behaviour is part of what is tested; use a small value.
  target.max_execs_before_respawn =
      static_cast<int>(flags.GetInt("max-execs-before-respawn", 50));
  int speed_runs = static_cast<int>(flags.GetInt("speed-runs", 200));

  printf("target: %s\n", target.CommandLine().c_str());

  std::unique_ptr<ScriptRunner> runner =
      ScriptRunner::Create(target, 0, &error);
  if (!checker.Check("create runner", runner != nullptr, error)) {
    return kExitFailure;
  }
  if (!checker.Check("REPRL handshake and coverage initialization",
                     runner->Initialize(&error), error)) {
    return kExitFailure;
  }
  checker.Info("instrumented edges",
               StringPrintf("%u", runner->coverage().num_edges()));
  {
    const CoverageMap& coverage = runner->coverage();
    const cov::Header& header = coverage.header();
    std::string regions;
    for (uint32_t i = 0; i < header.num_regions; i++) {
      static const char* const kKinds[] = {"?", "code", "builtins",
                                           "optimized"};
      const cov::Region& region = header.regions[i];
      regions +=
          StringPrintf(" %s %u..%u", kKinds[region.kind <= 3 ? region.kind : 0],
                       region.first, region.first + region.count - 1);
    }
    checker.Info("coverage header",
                 StringPrintf("%s, %u bit(s) per edge,%s, build id %s",
                              coverage.mode_name(), header.bits_per_edge,
                              regions.c_str(), coverage.build_id().c_str()));
  }

  ScriptRunner::Result result;

  // Basic outcomes.
  if (RunOrFail(&checker, runner.get(), "empty script", "", &result)) {
    checker.Check("empty script succeeds", result.execution.succeeded(),
                  Describe(result.execution));
  }
  if (RunOrFail(&checker, runner.get(), "uncaught exception",
                "throw new Error('expected');", &result)) {
    checker.Check("uncaught exception is reported as failed",
                  result.execution.outcome == Outcome::kFailed,
                  Describe(result.execution));
  }
  if (RunOrFail(&checker, runner.get(), "syntax error", "let let = ;",
                &result)) {
    checker.Check("syntax error is reported as failed",
                  result.execution.outcome == Outcome::kFailed,
                  Describe(result.execution));
  }

  // Output channels.
  if (RunOrFail(&checker, runner.get(), "fuzzout",
                "redline('REDLINE_PRINT', 'hello fuzzout');", &result)) {
    checker.Check(
        "redline('REDLINE_PRINT') is captured",
        result.execution.succeeded() &&
            result.execution.fuzzout.find("hello fuzzout") != std::string::npos,
        Describe(result.execution) + ", fuzzout=\"" + result.execution.fuzzout +
            "\"");
  }
  if (RunOrFail(&checker, runner.get(), "stdout",
                "print('hello stdout'); console.log('hello console');",
                &result)) {
    bool ok = result.execution.succeeded() &&
              result.execution.stdout_output.find("hello stdout") !=
                  std::string::npos &&
              result.execution.stdout_output.find("hello console") !=
                  std::string::npos;
    checker.Check("stdout is captured", ok,
                  Describe(result.execution) + ", stdout=\"" +
                      result.execution.stdout_output + "\"");
  }
  if (RunOrFail(&checker, runner.get(), "stderr", "console.error('to stderr')",
                &result)) {
    checker.Check(
        "stderr is captured",
        result.execution.stderr_output.find("to stderr") != std::string::npos,
        "stderr=\"" + result.execution.stderr_output + "\"");
  }
  if (RunOrFail(&checker, runner.get(), "output isolation", "print('second')",
                &result)) {
    checker.Check(
        "output of earlier executions is not leaked",
        result.execution.stdout_output.find("hello") == std::string::npos &&
            result.execution.fuzzout.empty(),
        "stdout=\"" + result.execution.stdout_output + "\"");
  }

  // Timeouts.
  {
    uint64_t timeout_us = static_cast<uint64_t>(300) * 1000;
    Stopwatch stopwatch;
    if (runner->Run("while (true) {}", timeout_us, &result, &error)) {
      uint64_t elapsed = stopwatch.ElapsedUs();
      checker.Check("infinite loop times out",
                    result.execution.outcome == Outcome::kTimedOut &&
                        elapsed >= timeout_us && elapsed < timeout_us * 20,
                    Describe(result.execution) +
                        StringPrintf(", wall %.1f ms",
                                     static_cast<double>(elapsed) / 1000.0));
    } else {
      checker.Check("infinite loop times out", false, error);
    }
    if (RunOrFail(&checker, runner.get(), "after timeout", "1 + 1", &result)) {
      checker.Check("target is respawned after a timeout",
                    result.execution.succeeded(), Describe(result.execution));
    }
  }

  // Crashes.
  struct CrashCase {
    const char* description;
    int kind;
    bool required;
  };
  const CrashCase crash_cases[] = {
      {"IMMEDIATE_CRASH", 0, true},    {"CHECK failure", 1, true},
      {"DCHECK failure", 2, false},    {"wild write", 3, true},
      {"DEBUG build check", 8, false}, {"sandbox violation", 9, false},
  };
  for (const CrashCase& c : crash_cases) {
    std::string script = StringPrintf("redline('REDLINE_CRASH', %d)", c.kind);
    std::string name = StringPrintf("redline('REDLINE_CRASH', %d) [%s]", c.kind,
                                    c.description);
    if (!RunOrFail(&checker, runner.get(), name.c_str(), script, &result)) {
      continue;
    }
    bool crashed = result.execution.crashed();
    if (c.required) {
      checker.Check(name.c_str(), crashed, Describe(result.execution));
    } else {
      checker.Info(name.c_str(), (crashed ? "crashes" : "does not crash") +
                                     std::string(" -- ") +
                                     Describe(result.execution));
    }
    if (RunOrFail(&checker, runner.get(), "after crash", "1 + 1", &result)) {
      checker.Check("target is respawned after a crash",
                    result.execution.succeeded(), Describe(result.execution));
    }
  }

  // Coverage.
  const char* kProgramA =
      "function f(a) { let s = 0; for (let i = 0; i < a.length; i++) s += "
      "a[i]; return s; }\n"
      "let arr = [1, 2.5, 'x', {}, null];\n"
      "for (let j = 0; j < 100; j++) f(arr);\n"
      "class C { #p = 1; get p() { return this.#p; } }\n"
      "new C().p;\n";
  const char* kProgramB =
      "let m = new Map(); for (let i = 0; i < 50; i++) m.set(i, 'v' + i);\n"
      "let s = new Set([1, 2, 3]); [...m.keys()].filter(x => s.has(x));\n"
      "JSON.parse(JSON.stringify({a: [1, {b: 2}], c: 'd'}));\n"
      "/a(b+)c/.exec('xxabbbcxx'); new Date(0).toISOString();\n";
  std::vector<uint32_t> edges_a;
  if (RunOrFail(&checker, runner.get(), "coverage A", kProgramA, &result)) {
    edges_a = result.new_edges;
    checker.Check("first execution of a program finds new edges",
                  result.execution.succeeded() && !result.new_edges.empty(),
                  StringPrintf("%zu new edges, %u hit", result.new_edges.size(),
                               result.edges_hit));
  }
  if (RunOrFail(&checker, runner.get(), "coverage A again", kProgramA,
                &result)) {
    checker.Check(
        "re-running the same program finds (almost) no new edges",
        result.new_edges.size() * 100 < std::max<size_t>(edges_a.size(), 1),
        StringPrintf("%zu new edges on second run vs %zu on first",
                     result.new_edges.size(), edges_a.size()));
  }
  if (RunOrFail(&checker, runner.get(), "coverage B", kProgramB, &result)) {
    checker.Check("a different program finds new edges",
                  result.execution.succeeded() && !result.new_edges.empty(),
                  StringPrintf("%zu new edges", result.new_edges.size()));
  }
  if (RunOrFail(&checker, runner.get(), "coverage of failing program",
                "let q = []; q.push(1); throw 1;", &result)) {
    checker.Check("failed executions do not advance coverage",
                  result.execution.outcome == Outcome::kFailed &&
                      result.new_edges.empty() && result.edges_hit > 0,
                  StringPrintf("%zu new edges, %u hit", result.new_edges.size(),
                               result.edges_hit));
  }

  // Determinism of edges across runs. V8 is not deterministic at the edge
  // level: GC timing, tiering decisions and concurrent compiler threads make
  // 5-15% of the edges of any script flaky (even `let x = 1 + 2;` shows ~6%).
  // This is the reason for the multi-run determinism check in the fuzzing
  // loop; here we only sanity-check that the stable core is large (a broken
  // coverage map would show up as near-random bitmaps) and report the rate.
  {
    struct Probe {
      const char* name;
      const char* script;
    };
    const Probe kProbes[] = {{"trivial script", "let x = 1 + 2;"},
                             {"program A", kProgramA}};
    for (const Probe& probe : kProbes) {
      std::vector<uint32_t> all_runs;
      std::set<uint32_t> any_run;
      bool ok = true;
      for (int i = 0; i < 5; i++) {
        Execution execution;
        if (!runner->RunWithoutEvaluation(probe.script, target.timeout_us,
                                          &execution, &error)) {
          ok = false;
          break;
        }
        std::vector<uint32_t> current = runner->coverage().CurrentEdges();
        if (i == 0) {
          all_runs = current;
        } else {
          std::vector<uint32_t> intersection;
          std::set_intersection(all_runs.begin(), all_runs.end(),
                                current.begin(), current.end(),
                                std::back_inserter(intersection));
          all_runs.swap(intersection);
        }
        any_run.insert(current.begin(), current.end());
      }
      double stable_pct = any_run.empty()
                              ? 0.0
                              : 100.0 * static_cast<double>(all_runs.size()) /
                                    static_cast<double>(any_run.size());
      checker.Info(
          StringPrintf("edge nondeterminism of %s", probe.name).c_str(),
          StringPrintf("%zu edges in every one of 5 runs, %zu in at "
                       "least one run (%.1f%% stable)",
                       all_runs.size(), any_run.size(), stable_pct));
      checker.Check(
          StringPrintf("stable edge core of %s is >= 75%% of the union",
                       probe.name)
              .c_str(),
          ok && !all_runs.empty() && stable_pct >= 75.0,
          StringPrintf("%.1f%%", stable_pct));
    }
  }

  // Respawning.
  {
    uint64_t spawns_before = runner->child().total_spawns();
    int runs = target.max_execs_before_respawn * 2 + 1;
    bool ok = true;
    for (int i = 0; i < runs; i++) {
      if (!runner->Run("", &result, &error) || !result.execution.succeeded()) {
        ok = false;
        break;
      }
    }
    uint64_t spawns = runner->child().total_spawns() - spawns_before;
    checker.Check("target is respawned every max_execs_before_respawn runs",
                  ok && spawns >= 2,
                  StringPrintf("%llu spawns in %d runs",
                               static_cast<unsigned long long>(spawns), runs));
  }

  // Speed (informational).
  if (speed_runs > 0) {
    Stopwatch stopwatch;
    bool ok = true;
    for (int i = 0; i < speed_runs; i++) {
      if (!runner->Run("let x = 1 + 1;", &result, &error)) {
        ok = false;
        break;
      }
    }
    double seconds = stopwatch.ElapsedSeconds();
    checker.Info(
        "execution speed",
        ok ? StringPrintf("%.0f execs/s for a trivial script (%d runs)",
                          static_cast<double>(speed_runs) / seconds, speed_runs)
           : "failed: " + error);
  }

  printf("selftest: %d passed, %d failed\n", checker.passed(),
         checker.failed());
  return checker.failed() == 0 ? kExitOk : kExitFailure;
}

}  // namespace

const Command kSelftestCommand = {
    "selftest",
    "Check that the execution layer works against a target",
    "usage: redline selftest --d8=PATH [options] | --offline [options]\n"
    "\n"
    "Runs a series of checks against the target: REPRL handshake, outcome\n"
    "classification (success, exception, timeout, crash), output capture,\n"
    "coverage evaluation, edge determinism and respawning. Prints PASS/FAIL\n"
    "per check and exits with 1 if any check failed.\n"
    "\n"
    "Before the target checks it runs the offline selection checks: the\n"
    "default seed, partner, mutator and generator strategies must reproduce\n"
    "the picks recorded in experimental/redline/testdata/selection/*.trace.\n"
    "\n"
    "options:\n"
    "  --speed-runs=N     executions for the speed measurement (200)\n"
    "  --offline          only the checks that need no target (--d8 not "
    "needed)\n"
    "  --testdata=DIR     experimental/redline/testdata (default: found next "
    "to the binary)\n"
    "  --write-golden     rewrite the selection traces instead of comparing "
    "them\n"
    "                     (only for an intended change of the default picks)\n",
    RunSelftest,
};

}  // namespace redline
