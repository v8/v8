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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <set>

#include "experimental/redline/base/file_util.h"
#include "experimental/redline/base/flags.h"
#include "experimental/redline/base/log.h"
#include "experimental/redline/base/time_util.h"
#include "experimental/redline/commands/command.h"
#include "experimental/redline/commands/selftest_checker.h"
#include "experimental/redline/commands/selftest_selection.h"
#include "experimental/redline/core/program.h"
#include "experimental/redline/core/random.h"
#include "experimental/redline/core/tunable.h"
#include "experimental/redline/corpus/corpus.h"
#include "experimental/redline/corpus/feature_weights.h"
#include "experimental/redline/engine/crash.h"
#include "experimental/redline/engine/d8_flags.h"
#include "experimental/redline/engine/fuzzer.h"
#include "experimental/redline/engine/shared_coverage.h"
#include "experimental/redline/exec/coverage.h"
#include "experimental/redline/exec/runner.h"
#include "experimental/redline/minimizer/minimizer.h"

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

// Offline: the profiles of the random d8 flags. The Turbolev profile must not
// draw the flags that only Maglev reads, and the Maglev profile must not draw
// the Turbolev and Turboshaft flags.
void RunD8FlagProfileChecks(Checker* checker) {
  // Returns the first flag of 500 draws that starts with one of `prefixes`
  // and is not in `allowed`.
  auto draws_any = [](D8FlagProfile profile,
                      std::initializer_list<const char*> prefixes,
                      std::initializer_list<const char*> allowed = {}) {
    Random rng(7);
    std::string found;
    for (int i = 0; i < 500 && found.empty(); i++) {
      for (const std::string& flag : RandomD8Flags(rng, profile)) {
        if (std::any_of(allowed.begin(), allowed.end(),
                        [&](const char* name) { return flag == name; })) {
          continue;
        }
        for (const char* prefix : prefixes) {
          if (flag.rfind(prefix, 0) == 0) {
            found = flag;
          }
        }
      }
    }
    return found;
  };
  D8FlagProfile parsed = D8FlagProfile::kMaglev;
  checker->Check("random flags: the profile names parse",
                 ParseD8FlagProfile("turbolev", &parsed) &&
                     parsed == D8FlagProfile::kTurbolev &&
                     ParseD8FlagProfile("maglev", &parsed) &&
                     parsed == D8FlagProfile::kMaglev &&
                     !ParseD8FlagProfile("turbofan", &parsed),
                 "");
  // Turbolev builds its graph with the Maglev graph builder, so it also reads
  // the Maglev assertions and the allowed --no-maglev- flags. Only Turbolev
  // reads --maglev-range-analysis.
  std::string maglev_only = draws_any(
      D8FlagProfile::kTurbolev,
      {"--maglev-future", "--maglev-non-eager", "--max-maglev",
       "--optimize-on-next-call-optimizes-to-maglev", "--no-maglev-",
       "--maglev-as-top-tier", "--maglev-inline-api-calls"},
      {"--no-maglev-inlining", "--no-maglev-poly-calls",
       "--no-maglev-escape-analysis", "--no-maglev-optimistic-peeled-loops",
       "--no-maglev-truncation", "--no-maglev-cse",
       "--no-maglev-range-analysis"});
  checker->Check("random flags: the turbolev profile draws no Maglev-only flag",
                 maglev_only.empty(), maglev_only);
  const bool turbolev_has_maglev_assert =
      !draws_any(D8FlagProfile::kTurbolev, {"--maglev-assert"},
                 {"--maglev-assert-types"})
           .empty() &&
      !draws_any(D8FlagProfile::kTurbolev, {"--maglev-assert-types"}).empty();
  checker->Check("random flags: the turbolev profile draws Maglev assertions",
                 turbolev_has_maglev_assert, "");
  std::string turbolev_only = draws_any(
      D8FlagProfile::kMaglev,
      {"--no-turbolev", "--no-turboshaft", "--turboshaft-",
       "--verify-turboshaft", "--maglev-range-verification",
       "--always-osr-from-maglev", "--no-additive-safe-int-feedback",
       "--max-inlined-bytecode-size", "--no-maglev-range-analysis",
       "--no-turbo-", "--turbo-", "--optimize-maglev-optimizes-to-turbofan",
       "--stress-concurrent-inlining", "--concurrent-recompilation-"});
  checker->Check("random flags: the maglev profile draws no Turbolev flag",
                 turbolev_only.empty(), turbolev_only);

  // The rules of the draw. A draw names each flag at most once. A flag that
  // needs another flag comes only with that flag. The cumulative inlining
  // limit is not smaller than the limit for one function.
  auto name_of = [](const std::string& flag) {
    return flag.substr(0, flag.find('='));
  };
  auto value_of = [](const std::string& flag) -> int64_t {
    size_t eq = flag.find('=');
    return eq == std::string::npos
               ? -1
               : std::strtoll(flag.c_str() + eq + 1, nullptr, 10);
  };
  std::string bad_draw;
  for (D8FlagProfile profile :
       {D8FlagProfile::kMaglev, D8FlagProfile::kTurbolev}) {
    Random rng(11);
    for (int i = 0; i < 500 && bad_draw.empty(); i++) {
      std::vector<std::string> flags = RandomD8Flags(rng, profile);
      std::set<std::string> names;
      int64_t size = -1;
      bool chaos = false;
      for (const std::string& flag : flags) {
        const std::string name = name_of(flag);
        if (!names.insert(name).second) {
          bad_draw = "two " + name;
        } else if (name == "--max-inlined-bytecode-size") {
          size = value_of(flag);
        } else if (name == "--max-inlined-bytecode-size-cumulative" &&
                   (size < 0 || value_of(flag) < size)) {
          bad_draw = flag + " without a smaller --max-inlined-bytecode-size";
        } else if (name == "--scavenger-chaos-mode") {
          chaos = true;
        } else if (name == "--scavenger-chaos-mode-threshold" && !chaos) {
          bad_draw = flag + " without --scavenger-chaos-mode";
        }
      }
      if (bad_draw.empty()) {
        if (names.count("--verify-bytecode-full") > 0 &&
            names.count("--verify-bytecode-light") > 0) {
          bad_draw = "both --verify-bytecode-full and --verify-bytecode-light";
        } else if (names.count("--no-sparkplug") > 0 &&
                   names.count("--always-sparkplug") > 0) {
          bad_draw = "both --no-sparkplug and --always-sparkplug";
        } else if (names.count("--assert-types") > 0 &&
                   (names.count("--stress-concurrent-inlining") > 0 ||
                    names.count("--stress-concurrent-inlining-attach-code") >
                        0)) {
          bad_draw = "--assert-types with --stress-concurrent-inlining";
        }
      }
    }
  }
  checker->Check(
      "random flags: a draw names each flag once, with the flags it needs",
      bad_draw.empty(), bad_draw);
  // --single-threaded and --assert-types stop the extra jobs of
  // --stress-concurrent-inlining.
  TunableRegistry& registry = TunableRegistry::Instance();
  std::string error;
  bool set =
      registry.Set("flags.single_threaded", 1, &error) &&
      registry.Set("flags.stress_concurrent_inlining", 1, &error) &&
      registry.Set("flags.stress_concurrent_inlining_attach_code", 1, &error);
  std::string concurrent = set ? draws_any(D8FlagProfile::kTurbolev,
                                           {"--stress-concurrent-inlining"})
                               : error;
  registry.Reset();
  checker->Check(
      "random flags: --single-threaded comes without "
      "--stress-concurrent-inlining",
      set && concurrent.empty(), concurrent);
  bool set_assert =
      registry.Set("flags.assert_types", 1, &error) &&
      registry.Set("flags.stress_concurrent_inlining", 1, &error) &&
      registry.Set("flags.stress_concurrent_inlining_attach_code", 1, &error);
  std::string assert_concurrent =
      set_assert ? draws_any(D8FlagProfile::kTurbolev,
                             {"--stress-concurrent-inlining"})
                 : error;
  registry.Reset();
  checker->Check(
      "random flags: --assert-types comes without "
      "--stress-concurrent-inlining",
      set_assert && assert_concurrent.empty(), assert_concurrent);

  // A fixed --single-threaded or --assert-types also removes the
  // --stress-concurrent-inlining flags. Without a fixed flag, the same
  // tunables give these flags in each draw.
  auto fixed_draws = [](const std::vector<std::string>& fixed) {
    Random rng(19);
    int with_concurrent = 0;
    for (int i = 0; i < 100; i++) {
      for (const std::string& flag :
           RandomD8Flags(rng, D8FlagProfile::kTurbolev, fixed)) {
        if (flag.rfind("--stress-concurrent-inlining", 0) == 0) {
          with_concurrent++;
          break;
        }
      }
    }
    return with_concurrent;
  };
  const bool set_concurrent =
      registry.Set("flags.stress_concurrent_inlining", 1, &error) &&
      registry.Set("flags.stress_concurrent_inlining_attach_code", 1, &error);
  const int no_fixed = fixed_draws({});
  const int fixed_st = fixed_draws({"--single-threaded"});
  const int fixed_assert = fixed_draws({"--assert-types"});
  registry.Reset();
  checker->Check(
      "random flags: a fixed --single-threaded or --assert-types removes "
      "--stress-concurrent-inlining",
      set_concurrent && no_fixed == 100 && fixed_st == 0 && fixed_assert == 0,
      StringPrintf("no fixed flag %d, --single-threaded %d, --assert-types %d",
                   no_fixed, fixed_st, fixed_assert));

  // --maglev-as-top-tier turns off Turbofan and Turbolev, and turns on Maglev.
  // Like Fuzzilli, the Maglev profile draws it only with a fixed
  // --no-turbofan. A fixed or drawn --turbofan, --turbolev or --no-maglev
  // removes it.
  auto top_tier_draws = [](const std::vector<std::string>& fixed) {
    Random rng(23);
    int with_top_tier = 0;
    for (int i = 0; i < 100; i++) {
      const std::vector<std::string> flags =
          RandomD8Flags(rng, D8FlagProfile::kMaglev, fixed);
      if (std::find(flags.begin(), flags.end(), "--maglev-as-top-tier") !=
          flags.end()) {
        with_top_tier++;
      }
    }
    return with_top_tier;
  };
  auto sanitized_has_top_tier = [](const char* other) {
    std::vector<std::string> flags = {"--maglev-as-top-tier", other};
    SanitizeD8Flags(&flags);
    return std::find(flags.begin(), flags.end(), "--maglev-as-top-tier") !=
           flags.end();
  };
  const bool set_top_tier = registry.Set("flags.maglev_as_top_tier", 1, &error);
  const int top_no_fixed = top_tier_draws({});
  const int top_no_turbofan = top_tier_draws({"--no-turbofan"});
  const int top_turbolev = top_tier_draws({"--no-turbofan", "--turbolev"});
  const int top_no_maglev = top_tier_draws({"--no-turbofan", "--no-maglev"});
  const int top_disabled =
      top_tier_draws({"--no-turbofan", "--disable-optimizing-compilers"});
  registry.Reset();
  const bool drawn_removed = !sanitized_has_top_tier("--turbofan") &&
                             !sanitized_has_top_tier("--turbolev") &&
                             !sanitized_has_top_tier("--no-maglev") &&
                             sanitized_has_top_tier("--no-turbofan");
  checker->Check(
      "random flags: --maglev-as-top-tier needs --no-turbofan and no clash",
      set_top_tier && top_no_fixed == 0 && top_no_turbofan == 100 &&
          top_turbolev == 0 && top_no_maglev == 0 && top_disabled == 0 &&
          drawn_removed,
      StringPrintf("no fixed flag %d, --no-turbofan %d, --turbolev %d, "
                   "--no-maglev %d, --disable-optimizing-compilers %d, "
                   "drawn %d",
                   top_no_fixed, top_no_turbofan, top_turbolev, top_no_maglev,
                   top_disabled, drawn_removed ? 1 : 0));
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
  CrashSignature d = CrashSignature::FromOutput(
      5, 0,
      "abort: CSA_DCHECK failed: Torque assert 'Is<A>(o)' failed "
      "[src/builtins/cast.tq:1025] [gen/torque-generated/src/builtins/"
      "regexp-test-tq-csa.cc:174]\n\nStacktrace:\n");
  checker->Check(
      "crash signature: a failed CSA_DCHECK names its builtin and message",
      d.text == "csa regexp-test-tq-csa.cc:174 Torque assert 'Is<A>(o)' failed",
      d.text);
}

// Offline: the d8 bundle of a state-dependent crash (MakeReprlBundle).
void RunReprlBundleChecks(Checker* checker) {
  std::string bundle;
  bool ok = MakeReprlBundle("// header\n//\n", {"let x = 1;", "let x = 2;\n"},
                            &bundle);
  checker->Check("bundle: one script part for each script, in order",
                 ok && bundle ==
                           "// header\n//\n"
                           "// JS_BUNDLE_SCRIPT\nlet x = 1;\n"
                           "// JS_BUNDLE_SCRIPT\nlet x = 2;\n",
                 bundle);
  checker->Check(
      "bundle: a script with the marker prefix is refused",
      !MakeReprlBundle("", {"s = '// JS_BUNDLE_MODULE:a.mjs';"}, &bundle), "");
  checker->Check("bundle: a header line that is not a comment is refused",
                 !MakeReprlBundle("// a\nvar b;\n", {"1;"}, &bundle), "");
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
// synthetic header, and the feature weights (corpus/feature_weights.h).
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
    // Crash deduplication counts only the edges. Edge 100 and the bucket
    // features of edge 5 (101 to 108) are in the same 8-byte word.
    SharedCoverage shared;
    const bool shared_ok =
        shared.Initialize(map->num_edges(), map->bitmap_size(), &error,
                          map->counted_edges(), map->real_edges());
    map->ResetState();
    std::string crash_results;
    auto crash = [&](std::initializer_list<std::pair<uint32_t, uint8_t>> hits) {
      map->ClearBitmap();
      for (const auto& [edge, count] : hits) {
        counters[edge] = count;
      }
      map->Refresh();
      const bool local = map->EvaluateCrash();
      const bool session = shared.ClaimNewCrashEdges(map->bitmap());
      crash_results += StringPrintf("%s%d%d", crash_results.empty() ? "" : " ",
                                    local, session);
    };
    // A new edge, the same edge with a different hit count, a new edge.
    crash({{5, 1}});
    crash({{5, 2}});
    crash({{5, 2}, {100, 1}});
    checker->Check("coverage counters: crash edges ignore bucket features",
                   shared_ok && crash_results == "11 00 11", crash_results);
    // A flaky feature that is not a counted edge (an optimized code feature
    // or a bucket feature) does not change coverage_fraction(). The packed
    // bitmap has 3 counted edges: 8, 12 and 15.
    SharedCoverage flaky;
    const bool flaky_ok =
        flaky.Initialize(map->num_edges(), map->bitmap_size(), &error,
                         map->counted_edges(), map->real_edges());
    map->ClearBitmap();
    counters[8] = 1;
    counters[12] = 255;
    counters[15] = 128;
    counters[99] = 9;
    map->Refresh();
    const bool claimed_packed = flaky.ClaimNewEdges(map->bitmap()) == packed;
    const double denominator = static_cast<double>(map->counted_edges() + 1);
    flaky.RecordFlaky(99, 1);
    flaky.RecordFlaky(185, 1);
    const double three_edges = flaky.coverage_fraction();
    flaky.RecordFlaky(12, 1);
    const double two_edges = flaky.coverage_fraction();
    checker->Check(
        "coverage counters: only flaky counted edges reduce the coverage",
        flaky_ok && claimed_packed && flaky.found_edges() == 3 &&
            flaky.suppressed_flaky_edges() == 1 &&
            three_edges == 3 / denominator && two_edges == 2 / denominator,
        StringPrintf(
            "found %u, suppressed %llu, coverage %g then %g edges",
            flaky.found_edges(),
            static_cast<unsigned long long>(flaky.suppressed_flaky_edges()),
            three_edges * denominator, two_edges * denominator));
    map->ClearBitmap();
    counters[5] = 3;
    counters[50] = 1;
    counters[91] = 200;
    map->Refresh();
    FeatureWeights::Params params;
    std::unique_ptr<FeatureWeights> weights =
        FeatureWeights::Build(*map, params, {{5, 1.0}, {50, 0.3}});
    auto near = [](double a, double b) { return std::fabs(a - b) < 1e-6; };
    checker->Check(
        "coverage counters: feature weights",
        near(weights->Of(5), 21.0) && near(weights->Of(50), 7.0) &&
            near(weights->Of(85), 0.5) && near(weights->Of(91), 5.0) &&
            near(weights->Of(101), 0.0) && near(weights->Of(103), 10.5) &&
            near(weights->Of(124), 2.5) && near(weights->Of(10), 1.0) &&
            weights->target_edges() == std::vector<uint32_t>{5, 50},
        StringPrintf("%g %g %g %g %g %g %g", weights->Of(5), weights->Of(50),
                     weights->Of(85), weights->Of(91), weights->Of(101),
                     weights->Of(103), weights->Of(124)));
    // A focus on code edges 40 to 60. The focus edges, the target edges and
    // the buckets of the targets are in the focus. Optimized code features are
    // excluded by default and included when optimized_in_focus is true.
    std::unique_ptr<FeatureWeights> focused = FeatureWeights::Build(
        *map, params, {{5, 1.0}}, FeatureWeights::EdgeRanges{{40, 60}});
    FeatureWeights::Params opt_params = params;
    opt_params.optimized_in_focus = true;
    std::unique_ptr<FeatureWeights> focused_opt = FeatureWeights::Build(
        *map, opt_params, {{5, 1.0}}, FeatureWeights::EdgeRanges{{40, 60}});
    std::unique_ptr<FeatureWeights> weights_opt =
        FeatureWeights::Build(*map, opt_params, {{5, 1.0}, {50, 0.3}});
    checker->Check(
        "coverage counters: focus",
        focused->has_focus() && focused->filters_admission() &&
            focused->focus_edges() == 21 && focused->InFocus(40) &&
            focused->InFocus(60) && !focused->InFocus(61) &&
            focused->InFocus(5) && !focused->InFocus(95) &&
            focused_opt->InFocus(95) && focused->InFocus(103) &&
            !focused->InFocus(10) && !focused->InFocus(85) &&
            near(focused->Of(10), 0.1) && near(focused->Of(50), 1.0) &&
            near(focused->Of(85), 0.05) && near(focused->Of(5), 21.0) &&
            !weights->has_focus() && weights->filters_admission() &&
            weights->InFocus(10) && !weights->InFocus(95) &&
            !weights_opt->filters_admission() && weights_opt->InFocus(95),
        StringPrintf("focus edges %u, w(10) %g, w(85) %g",
                     focused->focus_edges(), focused->Of(10), focused->Of(85)));
    // Target features stay active in the rarity scheduler past rare_edge_limit.
    // Slow programs get less energy than fast ones, and partner picks favour
    // target programs in change-based fuzzing.
    FeatureWeights::SetActive(weights.get());
    auto& sel_reg = SelectorRegistry<const Program>::Instance();
    std::string sel_err;
    auto seeds =
        sel_reg.Create(SelectionConfig{"rarity", JsonValue()}, &sel_err);
    auto partners = sel_reg.Create(
        SelectionConfig{"rarity-partner", JsonValue()}, &sel_err);
    Corpus rarity_corpus(std::move(seeds), std::move(partners));
    ProgramPtr last_target, last_non_target;
    for (int i = 0; i < 10; i++) {
      Lineage l;
      auto pt = Program::Parse("let a = 1;", l, &sel_err);
      pt->edges = {5};
      if (i == 9) {
        for (uint32_t e = 15; e < 155; e++) {
          if (e != 50) {
            pt->edges.push_back(e);
          }
        }
      }
      pt->exec_time_us = i == 9 ? 1000 : 100000;
      last_target = rarity_corpus.Add(pt);
      auto pn = Program::Parse("let b = 2;", l, &sel_err);
      pn->edges = {10};
      pn->exec_time_us = 1000;
      last_non_target = rarity_corpus.Add(pn);
    }
    Random rng(42);
    bool partner_picked_target = false;
    for (int i = 0; i < 8; i++) {
      ProgramPtr p = rarity_corpus.PickPartner(*last_non_target, rng);
      if (p != nullptr && p->edges == std::vector<uint32_t>{5}) {
        partner_picked_target = true;
      }
    }
    FeatureWeights::SetActive(nullptr);
    // Without active targets, Pick() pops the max-energy seed first: the fast
    // program (1 ms) beats the slow one (100 ms) with equal edge rarity.
    auto time_seeds =
        sel_reg.Create(SelectionConfig{"rarity", JsonValue()}, &sel_err);
    auto time_partners = sel_reg.Create(
        SelectionConfig{"uniform-partner", JsonValue()}, &sel_err);
    Corpus time_corpus(std::move(time_seeds), std::move(time_partners));
    Lineage l;
    auto slow_p = Program::Parse("let x = 1;", l, &sel_err);
    slow_p->edges = {20};
    slow_p->exec_time_us = 100000;
    time_corpus.Add(slow_p);
    auto fast_p = Program::Parse("let y = 1;", l, &sel_err);
    fast_p->edges = {21};
    fast_p->exec_time_us = 1000;
    ProgramPtr fast_added = time_corpus.Add(fast_p);
    ProgramPtr first_pick = time_corpus.Pick(rng);
    const bool kept_target_5 = std::find(last_target->rare_edges.begin(),
                                         last_target->rare_edges.end(),
                                         5u) != last_target->rare_edges.end();
    checker->Check(
        "coverage counters: rarity target retention and time weighting",
        kept_target_5 && last_target->rare_edges.size() == 128 &&
            last_non_target->rare_edges.empty() && partner_picked_target &&
            first_pick == fast_added,
        StringPrintf("target rare_edges %zu, non-target %zu",
                     last_target->rare_edges.size(),
                     last_non_target->rare_edges.size()));

    std::vector<std::string> base_flags = {
        "--single-threaded",
        "--max-maglev-eager-inlined-bytecode-size=0",
        "--max-maglev-inlined-bytecode-size-small=0",
        "--always-sparkplug",
        "--verify-bytecode-light",
        "--stress-concurrent-inlining"};
    std::vector<std::string> extra_flags = {
        "--single-threaded", "--maglev-assert", "--no-sparkplug",
        "--verify-bytecode-full", "--assert-types"};
    // Keep all base flags, so that only SanitizeD8Flags removes flags.
    std::string keep_error;
    const bool set_keep = TunableRegistry::Instance().Set(
        "flags.target_keep_chance", 1.0, &keep_error);
    std::vector<std::string> combined =
        CombineD8Flags(base_flags, extra_flags, rng);
    TunableRegistry::Instance().Reset();
    const bool stripped_orphan =
        std::find(combined.begin(), combined.end(),
                  "--max-maglev-eager-inlined-bytecode-size=0") ==
            combined.end() &&
        std::find(combined.begin(), combined.end(),
                  "--max-maglev-inlined-bytecode-size-small=0") ==
            combined.end() &&
        std::find(combined.begin(), combined.end(), "--always-sparkplug") ==
            combined.end() &&
        std::find(combined.begin(), combined.end(),
                  "--verify-bytecode-light") == combined.end() &&
        std::find(combined.begin(), combined.end(),
                  "--stress-concurrent-inlining") == combined.end();
    const bool has_assert =
        std::find(combined.begin(), combined.end(), "--maglev-assert") !=
            combined.end() &&
        std::find(combined.begin(), combined.end(), "--no-sparkplug") !=
            combined.end() &&
        std::find(combined.begin(), combined.end(), "--verify-bytecode-full") !=
            combined.end();
    const ptrdiff_t st_count =
        std::count(combined.begin(), combined.end(), "--single-threaded");
    checker->Check(
        "d8 flags: CombineD8Flags deduplication and orphan stripping",
        set_keep && stripped_orphan && has_assert && st_count == 1,
        StringPrintf("size %zu, st_count %td", combined.size(), st_count));

    const bool formula_ok =
        std::abs(EffectiveTargetGuidedChance(0, 0) - 0.75) < 1e-9 &&
        std::abs(EffectiveTargetGuidedChance(3, 3) - 0.75) < 1e-9 &&
        std::abs(EffectiveTargetGuidedChance(4, 0) - 0.75) < 1e-9 &&
        std::abs(EffectiveTargetGuidedChance(10, 2) - 0.60) < 1e-9 &&
        std::abs(EffectiveTargetGuidedChance(8, 4) - 0.375) < 1e-9 &&
        std::abs(EffectiveTargetGuidedChance(10, 8) - 0.15) < 1e-9 &&
        std::abs(EffectiveTargetGuidedChance(4, 4) - 0.0) < 1e-9 &&
        std::abs(EffectiveTargetGuidedChance(20, 20) - 0.0) < 1e-9;
    checker->Check("d8 flags: EffectiveTargetGuidedChance warmup and scaling",
                   formula_ok, "");

    auto count_sentinel_draws = [](uint64_t completed, uint64_t hit,
                                   bool populate_pool) {
      FuzzConfig cfg;
      cfg.randomize_d8_flags = true;
      Fuzzer fuzzer(std::move(cfg));
      fuzzer.SetTargetsForTest({{5, 100.0}});
      if (populate_pool) {
        fuzzer.RecordTargetFlags({"--sentinel-pool-flag"}, 100.0);
      }
      for (uint64_t i = 0; i < completed; i++) {
        fuzzer.RecordTargetChild(i < hit);
      }
      Random draw_rng(12345);
      int sentinel_count = 0;
      for (int i = 0; i < 400; i++) {
        std::vector<std::string> drawn = fuzzer.DrawD8Flags(draw_rng);
        if (std::find(drawn.begin(), drawn.end(), "--sentinel-pool-flag") !=
            drawn.end()) {
          sentinel_count++;
        }
      }
      return sentinel_count;
    };
    TunableRegistry& tunable_reg = TunableRegistry::Instance();
    std::string tunable_err;
    const bool set_no_mutate =
        tunable_reg.Set("flags.target_mutate_chance", 0.0, &tunable_err);
    const int empty_pool_draws = count_sentinel_draws(10, 0, false);
    const int warmup_draws = count_sentinel_draws(3, 3, true);
    const int zero_hit_draws = count_sentinel_draws(20, 0, true);
    const int low_hit_draws = count_sentinel_draws(20, 2, true);
    const int high_hit_draws = count_sentinel_draws(20, 18, true);
    const int all_hit_draws = count_sentinel_draws(20, 20, true);
    tunable_reg.Reset();
    checker->Check(
        "d8 flags: Fuzzer scales target-guided draws by child hit rate",
        set_no_mutate && empty_pool_draws == 0 && warmup_draws > 240 &&
            zero_hit_draws > 240 && low_hit_draws > 200 && high_hit_draws > 0 &&
            high_hit_draws < 80 && low_hit_draws > high_hit_draws + 120 &&
            all_hit_draws == 0,
        StringPrintf("empty %d, warmup %d, zero %d, low %d, high %d, all %d",
                     empty_pool_draws, warmup_draws, zero_hit_draws,
                     low_hit_draws, high_hit_draws, all_hit_draws));

    // DrawD8Flags gives the d8 flags of the configuration to the new draws
    // and to the combined draws. A fixed --single-threaded thus removes
    // --stress-concurrent-inlining from all draws. A fixed
    // --maglev-non-eager-inlining keeps the inlining size 0 of the pool
    // flags in the combined draws. The Turbolev profile does not draw
    // --maglev-non-eager-inlining.
    const bool set_combined_st =
        tunable_reg.Set("flags.stress_concurrent_inlining", 1.0,
                        &tunable_err) &&
        tunable_reg.Set("flags.stress_concurrent_inlining_attach_code", 1.0,
                        &tunable_err) &&
        tunable_reg.Set("flags.target_mutate_chance", 1.0, &tunable_err) &&
        tunable_reg.Set("flags.target_keep_chance", 1.0, &tunable_err);
    FuzzConfig st_cfg;
    st_cfg.randomize_d8_flags = true;
    st_cfg.d8_flag_profile = "turbolev";
    st_cfg.target.args = {"--single-threaded", "--maglev-non-eager-inlining"};
    Fuzzer st_fuzzer(std::move(st_cfg));
    st_fuzzer.SetTargetsForTest({{5, 100.0}});
    st_fuzzer.RecordTargetFlags(
        {"--sentinel-pool-flag", "--max-maglev-eager-inlined-bytecode-size=0"},
        100.0);
    st_fuzzer.RecordTargetChild(false);
    Random st_rng(4242);
    int st_sentinel = 0;
    int st_size_zero = 0;
    bool st_had_concurrent = false;
    for (int i = 0; i < 100; i++) {
      std::vector<std::string> drawn = st_fuzzer.DrawD8Flags(st_rng);
      if (std::find(drawn.begin(), drawn.end(), "--sentinel-pool-flag") !=
          drawn.end()) {
        st_sentinel++;
        if (std::find(drawn.begin(), drawn.end(),
                      "--max-maglev-eager-inlined-bytecode-size=0") !=
            drawn.end()) {
          st_size_zero++;
        }
      }
      for (const std::string& flag : drawn) {
        if (flag.rfind("--stress-concurrent-inlining", 0) == 0) {
          st_had_concurrent = true;
        }
      }
    }
    tunable_reg.Reset();
    checker->Check(
        "d8 flags: Fuzzer draws keep the fixed d8 flags",
        set_combined_st && st_sentinel > 40 && st_sentinel < 100 &&
            st_size_zero == st_sentinel && !st_had_concurrent,
        StringPrintf("sentinel %d, size 0 %d, concurrent %d", st_sentinel,
                     st_size_zero, st_had_concurrent ? 1 : 0));

    Minimizer min_check;
    auto min_prog = Program::Parse(
        "let u0 = 1;\nlet u1 = 2;\nlet keep = 1;\n"
        "let u2 = 3;\nlet u3 = 4;\nkeep.m(1);\n",
        l, &sel_err);
    auto min_tree = min_prog->CopyTree();
    MinimizationResult min_res = min_check.Minimize(
        min_tree.get(),
        [](const std::string& src) {
          return src.find("let keep = 1") != std::string::npos &&
                 src.find("keep.m(1)") != std::string::npos;
        },
        nullptr);
    const uint64_t stmt_invocations =
        min_check.Find("statements")->stats().invocations;
    checker->Check(
        "minimizer: unreferenced declarations and fixed-point stopping",
        min_res.accepted == 1 && min_res.tests <= 8 && stmt_invocations == 1,
        StringPrintf("tests %d, accepted %d, stmt_invocations %llu",
                     min_res.tests, min_res.accepted,
                     static_cast<unsigned long long>(stmt_invocations)));
  }
  munmap(mapping, CoverageMap::kShmSize);
}

// Offline: optimized code features with one bit per edge. As with counters,
// they do not count for the coverage, they weigh in the rarity strategy and
// they do not admit a program.
void RunCoverageBitsChecks(Checker* checker) {
  std::string error;
  std::unique_ptr<CoverageMap> map = CoverageMap::Create(9996, &error);
  if (!checker->Check("coverage bits: create a coverage map", map != nullptr,
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
  if (!checker->Check("coverage bits: map the shared memory", mapped,
                      mapped ? "" : strerror(errno))) {
    return;
  }
  // A target with 80 code edges, 10 builtins blocks and 10 optimized code
  // features (inline-bool-flag) writes this header.
  cov::Header header = {};
  header.magic = cov::kMagic;
  header.version = cov::kVersion;
  header.header_size = cov::kHeaderSize;
  header.shm_size = static_cast<uint32_t>(CoverageMap::kShmSize);
  header.mode = static_cast<uint32_t>(cov::Mode::kInlineBoolFlag);
  header.bits_per_edge = 1;
  header.num_edges = 100;
  header.max_edges = cov::MaxEdges(header.shm_size, 1);
  header.num_regions = 3;
  header.regions[0] = {static_cast<uint32_t>(cov::RegionKind::kCode), 1, 80};
  header.regions[1] = {static_cast<uint32_t>(cov::RegionKind::kBuiltins), 81,
                       10};
  header.regions[2] = {static_cast<uint32_t>(cov::RegionKind::kOptimized), 91,
                       10};
  memcpy(mapping, &header, sizeof(header));
  const bool ok = map->FinishInitialization(&error);
  checker->Check("coverage bits: read the header", ok, error);
  if (ok) {
    FeatureWeights::Params params;
    std::unique_ptr<FeatureWeights> weights =
        FeatureWeights::Build(*map, params, {});
    checker->Check(
        "coverage bits: optimized code features",
        !map->counters_mode() && map->num_edges() == 100 &&
            map->counted_edges() == 90 &&
            map->KindOf(95) == CoverageMap::FeatureKind::kOptimized &&
            weights->Of(95) == params.optimized &&
            weights->Of(85) == params.builtins && weights->Of(10) == 1.0 &&
            weights->InFocus(10) && !weights->InFocus(95),
        StringPrintf("counted %u, w(95) %g, w(85) %g, admits 95: %d",
                     map->counted_edges(), weights->Of(95), weights->Of(85),
                     weights->InFocus(95)));
  }
  munmap(mapping, CoverageMap::kShmSize);
}

int RunSelftest(const Flags& flags) {
  Checker checker;
  std::string error;

  // Offline checks first: they need no target.
  RunCrashSignatureChecks(&checker);
  RunReprlBundleChecks(&checker);
  RunCoverageHeaderChecks(&checker);
  RunD8FlagProfileChecks(&checker);
  RunCoverageCounterChecks(&checker);
  RunCoverageBitsChecks(&checker);
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

  // Random flags (randomize_d8_flags): a new target process gets new
  // flags, except while the flags are pinned.
  {
    TargetOptions random_target = target;
    int draws = 0;
    random_target.random_args = [&draws] {
      draws++;
      // Two harmless flags, so that the check can see which draw a target
      // process got.
      return std::vector<std::string>{draws % 2 == 1 ? "--stress-ic"
                                                     : "--no-stress-ic"};
    };
    std::unique_ptr<ScriptRunner> random_runner =
        ScriptRunner::Create(random_target, 1, &error);
    bool ok = random_runner != nullptr && random_runner->Initialize(&error);
    std::vector<std::string> first, second, pinned, released;
    auto respawn = [&](std::vector<std::string>* args) {
      random_runner->child().Kill();
      ok = ok && random_runner->Run("", &result, &error) &&
           result.execution.succeeded();
      *args = random_runner->child().CurrentArgs();
    };
    if (ok) {
      first = random_runner->child().CurrentArgs();
      respawn(&second);
      random_runner->child().Pin(true);
      respawn(&pinned);
      random_runner->child().Pin(false);
      respawn(&released);
    }
    auto starts_with = [](const std::vector<std::string>& args,
                          const char* flag) {
      return !args.empty() && args.front() == flag;
    };
    checker.Check(
        "random flags: a new target gets new flags, a pinned one keeps them",
        ok && starts_with(first, "--stress-ic") &&
            starts_with(second, "--no-stress-ic") && pinned == second &&
            starts_with(released, "--stress-ic") && draws == 3,
        ok ? StringPrintf("%d draws", draws) : error);
    // A respawn while pinned keeps the flags. Then Pin(false) kills the
    // target, because its flags ran max_execs_before_respawn runs, and the
    // next target draws new flags.
    bool respawned = false;
    bool killed = false;
    std::vector<std::string> kept, renewed;
    if (ok) {
      random_runner->child().Pin(true);
      uint64_t spawns_before = random_runner->child().total_spawns();
      for (int i = 0; ok && i < random_target.max_execs_before_respawn; i++) {
        ok = random_runner->Run("", &result, &error) &&
             result.execution.succeeded();
      }
      respawned = random_runner->child().total_spawns() > spawns_before;
      kept = random_runner->child().CurrentArgs();
      random_runner->child().Pin(false);
      killed = !random_runner->child().is_running();
      ok = ok && random_runner->Run("", &result, &error) &&
           result.execution.succeeded();
      renewed = random_runner->child().CurrentArgs();
    }
    checker.Check("random flags: unpinning replaces a target with old flags",
                  ok && respawned && kept == released && killed &&
                      starts_with(renewed, "--no-stress-ic") && draws == 4,
                  ok ? StringPrintf("%d draws", draws) : error);
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
