// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "experimental/redline/commands/selftest_selection.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "experimental/redline/base/file_util.h"
#include "experimental/redline/base/flags.h"
#include "experimental/redline/base/json.h"
#include "experimental/redline/base/log.h"
#include "experimental/redline/core/adaptive_selector.h"
#include "experimental/redline/core/builder.h"
#include "experimental/redline/core/environment.h"
#include "experimental/redline/core/program.h"
#include "experimental/redline/core/random.h"
#include "experimental/redline/core/swarm.h"
#include "experimental/redline/core/tunable.h"
#include "experimental/redline/corpus/corpus.h"
#include "experimental/redline/engine/amplifier.h"
#include "experimental/redline/engine/config.h"
#include "experimental/redline/engine/fuzzer.h"
#include "experimental/redline/engine/prelude.h"
#include "experimental/redline/engine/stages.h"
#include "experimental/redline/fast/parser.h"
#include "experimental/redline/fast/printer.h"
#include "experimental/redline/generators/codegen.h"
#include "experimental/redline/generators/generator.h"
#include "experimental/redline/mutators/mutator.h"

namespace redline {

namespace {

// The fixed seed and sizes of the traces. Changing any of them invalidates
// the golden files (regenerate with --write-golden and explain why).
constexpr uint64_t kSeed = 20261011;
constexpr int kMutatorPicks = 2000;
constexpr int kGeneratorPicks = 2000;
constexpr int kCodeGenMutations = 1000;
constexpr int kCorpusPrograms = 50;
constexpr int kCorpusPicks = 1000;

// A program with a few variables of different types, so that every
// generator finds something to work with.
constexpr const char* kProgram = R"(let n = 42;
let s = "text";
let a = [1, 2, 3];
let o = {x: 1, y: "two"};
function f(p, q) {
    let t = p + q;
    for (let i = 0; i < 3; i++) {
        t += a[i];
    }
    return t;
}
let r = f(n, 7);
class C {
    constructor(v) { this.v = v; }
    get value() { return this.v; }
}
let c = new C(r);
)";

std::shared_ptr<Program> ParseOrDie(const std::string& source,
                                    const std::string& origin) {
  Lineage lineage;
  lineage.origin = origin;
  std::string error;
  std::shared_ptr<Program> program = Program::Parse(source, lineage, &error);
  if (program == nullptr) {
    REDLINE_ERROR("selftest", "selection check program does not parse: %s",
                  error.c_str());
    abort();
  }
  return program;
}

// --- the four traces ------------------------------------------------------

// Which mutator is applied: MutatorSet::Pick with and without a partner.
std::string TraceMutatorPicks(const MutatorSet& mutators) {
  Random rng(kSeed);
  std::string trace;
  for (int i = 0; i < kMutatorPicks; i++) {
    bool has_partner = (i % 3) != 0;
    Mutator* m = mutators.Pick(rng, has_partner);
    trace += StringPrintf("%d\t%d\t%s\n", i, has_partner ? 1 : 0,
                          m != nullptr ? m->name().c_str() : "-");
  }
  return trace;
}

// Which generator is chosen for a random insertion slot of the fixed
// program: GeneratorSet::Pick.
std::string TraceGeneratorPicks(const GeneratorSet& generators,
                                const Environment& env) {
  Random rng(kSeed);
  ProgramPtr program = ParseOrDie(kProgram, "selftest");
  std::string trace;
  for (int i = 0; i < kGeneratorPicks; i++) {
    std::unique_ptr<fast::Tree> tree = program->CopyTree();
    Builder builder(tree.get(), &rng);
    Builder::Slot slot = builder.RandomInsertionSlot();
    if (!slot.valid()) {
      trace += StringPrintf("%d\t(no slot)\n", i);
      continue;
    }
    GenContext ctx = MakeGenContext(builder, slot, generators, env);
    Generator* g = generators.Pick(ctx);
    trace +=
        StringPrintf("%d\t%s\n", i, g != nullptr ? g->name().c_str() : "-");
  }
  return trace;
}

// Which generators CodeGen inserts, through the whole mutator (its own
// generator filter and slot choice included): the `detail` of each mutation.
std::string TraceCodeGen(const MutatorSet& mutators,
                         const GeneratorSet& generators,
                         const Environment& env) {
  Mutator* codegen = mutators.Find("CodeGen");
  if (codegen == nullptr) {
    return "(no CodeGen mutator)\n";
  }
  Random rng(kSeed);
  ProgramPtr program = ParseOrDie(kProgram, "selftest");
  std::string trace;
  for (int i = 0; i < kCodeGenMutations; i++) {
    std::string detail;
    std::unique_ptr<fast::Tree> tree =
        ApplyMutation(*codegen, *program, nullptr, rng, env, generators,
                      /*rounds=*/1, &detail);
    trace +=
        StringPrintf("%d\t%s\n", i, tree != nullptr ? detail.c_str() : "noop");
  }
  return trace;
}

// Which corpus program a scheduler picks next and which partner it offers,
// with the feedback a fuzzing session would give (every 7th mutation
// "interesting").
std::string TraceScheduler(const std::string& name) {
  std::string error;
  SelectorRegistry<const Program>& registry =
      SelectorRegistry<const Program>::Instance();
  std::unique_ptr<ProgramSelector> seeds =
      registry.Create(SelectionConfig{name, JsonValue()}, &error);
  if (seeds == nullptr) {
    return "(" + error + ")\n";
  }
  std::unique_ptr<ProgramSelector> partners =
      registry.Create(SelectionConfig{"uniform-partner", JsonValue()}, &error);
  if (partners == nullptr) {
    return "(" + error + ")\n";
  }
  Corpus corpus(std::move(seeds), std::move(partners));
  for (int i = 0; i < kCorpusPrograms; i++) {
    corpus.Add(ParseOrDie(StringPrintf("let v%d = %d;\n", i, i), "selftest"));
  }
  Random rng(kSeed);
  std::string trace;
  for (int i = 0; i < kCorpusPicks; i++) {
    ProgramPtr parent = corpus.Pick(rng);
    if (parent == nullptr) {
      trace += StringPrintf("%d\t-\t-\n", i);
      continue;
    }
    ProgramPtr partner = corpus.PickPartner(*parent, rng);
    corpus.OnResult(*parent, (i % 7) == 0);
    trace += StringPrintf("%d\t%u\t%u\n", i, parent->id,
                          partner != nullptr ? partner->id : 0u);
  }
  return trace;
}

// --- component outputs
// --------------------------------------------------------
//
// The output of every generator and every mutator for a fixed seed: pins
// the numbers inside them (the tunables), not only which one is picked. One
// line per output: name, index, FNV-1a hash of the program, a glimpse.

constexpr int kOutputsPerComponent = 5;

std::string Glimpse(const std::string& source) {
  std::string out;
  for (char c : source) {
    if (out.size() >= 70) {
      break;
    }
    out += (c == '\n' || c == '\t') ? ' ' : c;
  }
  return out;
}

// Sample `i` of generator `g`: inserts its code at a random place of a copy
// of `program`. Returns the tree and the generated statements in `*out`, or
// nullptr if the generator produced nothing.
std::unique_ptr<fast::Tree> GenerateSample(Generator* g, int i,
                                           const Program& program,
                                           const GeneratorSet& generators,
                                           const Environment& env,
                                           std::vector<fast::Statement*>* out) {
  Random rng(kSeed + static_cast<uint64_t>(i) * 7919 +
             Program::HashSource(g->name()));
  std::unique_ptr<fast::Tree> tree = program.CopyTree();
  Builder builder(tree.get(), &rng);
  for (int attempt = 0; attempt < 10; attempt++) {
    Builder::Slot slot = builder.RandomInsertionSlot();
    if (!slot.valid()) {
      break;
    }
    GenContext ctx = MakeGenContext(builder, slot, generators, env);
    if (!g->Applicable(ctx)) {
      continue;
    }
    ctx.out = out;
    if (g->Generate(ctx, out) && !out->empty()) {
      builder.InsertAll(slot, *out);
      return tree;
    }
    break;
  }
  return nullptr;
}

std::string TraceGeneratorOutputs(const GeneratorSet& generators,
                                  const Environment& env) {
  std::shared_ptr<Program> program = ParseOrDie(kProgram, "selftest");
  std::string trace;
  for (Generator* g : generators.candidates()) {
    for (int i = 0; i < kOutputsPerComponent; i++) {
      std::vector<fast::Statement*> out;
      std::unique_ptr<fast::Tree> tree =
          GenerateSample(g, i, *program, generators, env, &out);
      std::string result = tree != nullptr ? fast::Print(tree->root()) : "noop";
      trace += StringPrintf(
          "%s\t%d\t%016llx\t%s\n", g->name().c_str(), i,
          static_cast<unsigned long long>(Program::HashSource(result)),
          Glimpse(result).c_str());
    }
  }
  return trace;
}

std::string TraceMutatorOutputs(const MutatorSet& mutators,
                                const GeneratorSet& generators,
                                const Environment& env) {
  std::shared_ptr<Program> program = ParseOrDie(kProgram, "selftest");
  std::shared_ptr<Program> partner = ParseOrDie(
      "function g(x) { return x * 2; }\nlet q = [g(1), g(2)];\nlet m = new "
      "Map([[1, 'a']]);\n",
      "selftest");
  std::string trace;
  for (Mutator* m : mutators.candidates()) {
    for (int i = 0; i < kOutputsPerComponent; i++) {
      Random rng(kSeed + static_cast<uint64_t>(i) * 7919 +
                 Program::HashSource(m->name()));
      std::string detail;
      std::unique_ptr<fast::Tree> tree = ApplyMutation(
          *m, *program, partner, rng, env, generators, /*rounds=*/1, &detail);
      std::string result = tree != nullptr ? fast::Print(tree->root()) : "noop";
      trace += StringPrintf(
          "%s\t%d\t%016llx\t%s\t%s\n", m->name().c_str(), i,
          static_cast<unsigned long long>(Program::HashSource(result)),
          detail.c_str(), Glimpse(result).c_str());
    }
  }
  return trace;
}

// --- golden comparison ------------------------------------------------------

// Returns the first line that differs, formatted for a FAIL message.
std::string FirstDifference(const std::string& expected,
                            const std::string& actual) {
  size_t line = 1;
  size_t i = 0;
  while (i < expected.size() && i < actual.size() && expected[i] == actual[i]) {
    if (expected[i] == '\n') {
      line++;
    }
    i++;
  }
  auto line_at = [](const std::string& s, size_t pos) {
    size_t start = s.rfind('\n', pos == 0 ? 0 : pos - 1);
    start = start == std::string::npos ? 0 : start + 1;
    size_t end = s.find('\n', pos);
    return s.substr(start, (end == std::string::npos ? s.size() : end) - start);
  };
  return StringPrintf("first difference at line %zu: golden '%s', now '%s'",
                      line, line_at(expected, i).c_str(),
                      line_at(actual, i).c_str());
}

// Removes the spaces and tabs at the end of each line. Gerrit marks trailing
// whitespace, so the golden files must not have it.
std::string StripTrailingWhitespace(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  size_t start = 0;
  while (start < text.size()) {
    size_t newline = text.find('\n', start);
    size_t end = newline == std::string::npos ? text.size() : newline;
    size_t last = end;
    while (last > start && (text[last - 1] == ' ' || text[last - 1] == '\t')) {
      last--;
    }
    out.append(text, start, last - start);
    if (newline == std::string::npos) {
      break;
    }
    out += '\n';
    start = newline + 1;
  }
  return out;
}

void CompareOrWrite(Checker* checker, const std::string& check_name,
                    const std::string& path, const std::string& raw_trace,
                    bool write_golden) {
  const std::string trace = StripTrailingWhitespace(raw_trace);
  std::string error;
  if (write_golden) {
    bool ok = WriteStringToFile(path, trace, &error);
    checker->Check(check_name.c_str(), ok, ok ? "written: " + path : error);
    return;
  }
  std::string golden;
  if (!ReadFileToString(path, &golden, &error)) {
    checker->Check(
        check_name.c_str(), false,
        "cannot read " + path + " (" + error + "); --write-golden creates it");
    return;
  }
  if (golden == trace) {
    size_t lines =
        static_cast<size_t>(std::count(trace.begin(), trace.end(), '\n'));
    checker->Check(check_name.c_str(), true,
                   StringPrintf("%zu picks identical to %s", lines,
                                BaseName(path).c_str()));
    return;
  }
  checker->Check(check_name.c_str(), false, FirstDifference(golden, trace));
}

// Parses a JSON `selection` block the way FuzzConfig::Load does and runs the
// startup validation. Returns the error message ("" when accepted).
std::string SelectionError(const std::string& selection_json) {
  std::string text = "{\"selection\": " + selection_json + "}";
  JsonValue json;
  std::string error;
  if (!JsonValue::Parse(text, &json, &error)) {
    return "test bug, invalid JSON: " + error;
  }
  FuzzConfig config;
  // The JSON part of the real loader, then the startup validation that
  // Load() runs last.
  if (!FuzzConfig::LoadJsonForTest(json, &config, &error)) {
    return error;
  }
  return FuzzConfig::ValidateSelection(config.selection, &error) ? "" : error;
}

void CheckConfigErrors(Checker* checker) {
  struct Case {
    const char* name;
    const char* json;
    bool accepted;
    const char* expect;  // substring of the error message
  };
  const Case cases[] = {
      {"selection: default block is accepted", "{}", true, ""},
      {"selection: all built-in strategies with parameters are accepted",
       R"({"seeds": {"strategy": "recency", "params": {"recent_share": 0.3}},
             "partners": {"strategy": "uniform-partner", "params": {"tries": 2}},
             "mutators": {"strategy": "weighted", "params": {"weights": {"Splice": 4, "Reorder": 0}}},
             "generators": {"strategy": "weighted", "params": {"weights": {"JitOptimize": 10}}}})",
       true, ""},
      {"selection: unknown strategy is an error",
       R"({"seeds": {"strategy": "ml-magic"}})", false,
       "unknown selection strategy 'ml-magic'"},
      {"selection: unknown parameter is an error",
       R"({"seeds": {"strategy": "tournament", "params": {"sizes": 3}}})",
       false, "unknown parameter 'sizes'"},
      {"selection: wrong parameter type is an error",
       R"({"partners": {"strategy": "uniform-partner", "params": {"tries": "four"}}})",
       false, "must be a number"},
      {"selection: unknown mutator in weights is an error",
       R"({"mutators": {"strategy": "weighted", "params": {"weights": {"Splicer": 4}}}})",
       false, "unknown mutator in weights: Splicer"},
      {"selection: unknown generator in weights is an error",
       R"({"generators": {"params": {"weights": {"JitOptimise": 1}}}})", false,
       "unknown generator in weights: JitOptimise"},
      {"selection: a parameter the strategy does not take is an error",
       R"({"mutators": {"strategy": "weighted", "params": {"tries": 1}}})",
       false, "unknown parameter 'tries'"},
      {"selection: a weight that is not a number is an error",
       R"({"mutators": {"strategy": "weighted", "params": {"weights": {"Splice": "many"}}}})",
       false, "weight of 'Splice' must be a non-negative number"},
  };
  for (const Case& c : cases) {
    std::string error = SelectionError(c.json);
    bool ok = c.accepted ? error.empty()
                         : (!error.empty() &&
                            error.find(c.expect) != std::string::npos);
    checker->Check(c.name, ok, error.empty() ? "accepted" : error);
  }
}

// The "stages" block: parameters land in the right fields, the block wins
// over the flat keys, and mistakes are reported.
void CheckStageConfig(Checker* checker) {
  auto load = [](const std::string& text, FuzzConfig* config) -> std::string {
    JsonValue json;
    std::string error;
    if (!JsonValue::Parse(text, &json, &error)) {
      return "test bug, invalid JSON: " + error;
    }
    return FuzzConfig::LoadJsonForTest(json, config, &error) ? "" : error;
  };
  {
    FuzzConfig config;
    std::string error = load(
        R"({"consecutive_mutations": 9,
                "stages": {"prefix": {"min_values": 3, "extra_values": 1, "from_scratch": false},
                           "mutate": {"consecutive": 2, "rounds": 1, "scratch_codegen": 2, "log": "events",
                                      "capture": {"on": "timeout,invalid", "mutators": ["Input"], "max": 3}},
                           "check": {"min_reruns": 2, "max_reruns": 8, "capture": {"on": ["flaky"]}},
                           "minimize": {"enabled": false, "max_tests": 12},
                           "publish": {"save_corpus": false, "stats": false}}})",
        &config);
    bool ok = error.empty() && config.prefix_min_values == 3 &&
              config.prefix_extra_values == 1 && !config.prefix_from_scratch &&
              config.consecutive_mutations == 2 &&
              config.mutation_rounds == 1 &&
              config.scratch_codegen_links == 2 &&
              config.stage(Stage::kMutate).events &&
              config.stage(Stage::kMutate).capture.on.size() == 2 &&
              config.stage(Stage::kMutate).capture.mutators ==
                  std::vector<std::string>{"Input"} &&
              config.stage(Stage::kMutate).capture.max == 3 &&
              config.min_reruns == 2 && config.max_reruns == 8 &&
              config.stage(Stage::kCheck)
                  .capture.Matches(CaptureTrigger::kFlaky, "") &&
              !config.minimize && config.minimizer.max_tests == 12 &&
              !config.save_corpus && !config.stage(Stage::kPublish).stats;
    checker->Check(
        "stages: block sets the stage parameters and wins over flat keys", ok,
        error.empty() ? "" : error);
    // The block round-trips through ToJson (what the start event records).
    JsonValue again;
    std::string parse_error;
    bool round_trip =
        JsonValue::Parse(config.ToJson(), &again, &parse_error) &&
        again.Get("stages").Get("mutate").GetInt("consecutive", 0) == 2 &&
        again.Get("stages").Get("mutate").GetInt("scratch_codegen", 0) == 2 &&
        !again.Get("stages").Get("prefix").GetBool("from_scratch", true) &&
        again.Get("stages").Get("mutate").Get("capture").Get("on").is_array() &&
        !again.Get("stages").Get("minimize").GetBool("enabled", true);
    checker->Check(
        "stages: block round-trips through the recorded configuration",
        round_trip, parse_error);
  }
  struct Case {
    const char* name;
    const char* json;
    const char* expect;
  };
  const Case cases[] = {
      {"stages: unknown stage is an error", R"({"stages": {"mutat": {}}})",
       "unknown stage \"mutat\""},
      {"stages: unknown key is an error",
       R"({"stages": {"mutate": {"consecutiv": 3}}})",
       "unknown key \"consecutiv\" in stages.mutate"},
      {"stages: trigger that does not apply is an error",
       R"({"stages": {"check": {"capture": {"on": ["interesting"]}}}})",
       "does not apply to the check stage"},
      {"stages: bad log value is an error",
       R"({"stages": {"minimize": {"log": "verbose"}}})",
       "log must be \"events\" or \"off\""},
      {"stages: capture.mutators outside mutate is an error",
       R"({"stages": {"pick": {"capture": {"mutators": ["Splice"]}}}})",
       "only applies to the mutate stage"},
      {"stages: unknown trigger is an error",
       R"({"stages": {"mutate": {"capture": {"on": ["slow"]}}}})",
       "unknown capture trigger 'slow'"},
  };
  for (const Case& c : cases) {
    FuzzConfig config;
    std::string error = load(c.json, &config);
    checker->Check(c.name,
                   !error.empty() && error.find(c.expect) != std::string::npos,
                   error);
  }
  // The flag form of a capture rule.
  {
    Stage stage = Stage::kPick;
    CaptureRule rule;
    std::string error;
    bool ok = CaptureRule::FromFlag("mutate:timeout,crash:Splice,Combine:7",
                                    &stage, &rule, &error) &&
              stage == Stage::kMutate && rule.on.size() == 2 &&
              rule.mutators.size() == 2 && rule.max == 7 &&
              rule.Matches(CaptureTrigger::kCrash, "Combine") &&
              !rule.Matches(CaptureTrigger::kCrash, "Input");
    checker->Check("stages: --capture rule parses", ok, error);
    ok = !CaptureRule::FromFlag("publish:timeout", &stage, &rule, &error) &&
         error.find("does not apply") != std::string::npos;
    checker->Check("stages: --capture with an inapplicable trigger is an error",
                   ok, error);
  }
}

// Tunables (core/tunable.h): set from the config, validated, reported.
// A component for the adaptive selector tests.
struct FakeComponent {
  std::string label;
  double base = 1;
  ComponentStats counters;

  const std::string& name() const { return label; }
  double weight() const { return base; }
  ComponentStats& stats() { return counters; }
  const ComponentStats& stats() const { return counters; }
};

void CheckAdaptiveWindow(Checker* checker) {
  using Selector = AdaptiveSelector<FakeComponent>;
  Selector::Settings settings;
  settings.prior_executions = 10;
  Selector selector("component", {}, settings);
  uint64_t now_us = 0;
  selector.SetClockForTesting([&now_us] { return now_us; });
  FakeComponent cheap;
  cheap.label = "Cheap";
  FakeComponent costly;
  costly.label = "Costly";
  std::string error;
  const bool initialized = selector.Initialize({&cheap, &costly}, &error);
  // Same new edges; Costly spent 10 times more executions for them.
  cheap.counters.new_edges = 100;
  cheap.counters.spent = 1000;
  costly.counters.new_edges = 100;
  costly.counters.spent = 10000;
  Random rng(1);
  now_us = 1000000;
  selector.Pick({&cheap, &costly}, rng,
                [](const FakeComponent&) { return true; });
  checker->Check(
      "adaptive: no weight update before window_sec",
      initialized && selector.WeightOf(cheap) == selector.WeightOf(costly),
      error);
  now_us = 601000000;
  selector.Pick({&cheap, &costly}, rng,
                [](const FakeComponent&) { return true; });
  checker->Check(
      "adaptive: the time window updates the weights, and new edges per spent "
      "execution favour the cheap one",
      selector.WeightOf(cheap) > selector.WeightOf(costly),
      StringPrintf("Cheap %.2f, Costly %.2f", selector.WeightOf(cheap),
                   selector.WeightOf(costly)));
}

// The picks of `pick` with a fixed seed, one index for each pick.
std::vector<size_t> PickSequence(const std::function<size_t(Random&)>& pick) {
  Random rng(kSeed);
  std::vector<size_t> picks;
  for (int i = 0; i < kMutatorPicks; i++) {
    picks.push_back(pick(rng));
  }
  return picks;
}

// Swarm testing (core/swarm.h): the mask draw, its composition with the
// strategies, and the configuration.
void CheckSwarm(Checker* checker) {
  TunableRegistry::Instance().Reset();
  std::unique_ptr<MutatorSet> mutators = MutatorSet::CreateDefault();
  std::unique_ptr<GeneratorSet> generators = GeneratorSet::CreateDefault();
  // (a) Without a mask, and with a mask of factor 1, the picks do not change.
  {
    Selector<Mutator>& selector = mutators->selector();
    const auto& candidates = mutators->candidates();
    auto any = [](const Mutator&) { return true; };
    WeightMask ones;
    ones.factor.assign(candidates.size(), 1.0);
    const std::vector<size_t> plain = PickSequence(
        [&](Random& rng) { return selector.Pick(candidates, rng, any); });
    const std::vector<size_t> none = PickSequence([&](Random& rng) {
      return PickWithMask<Mutator>(selector, candidates, rng, any, nullptr);
    });
    const std::vector<size_t> masked = PickSequence([&](Random& rng) {
      return selector.PickMasked(candidates, rng, any, ones);
    });
    checker->Check(
        "swarm: without a mask or with factor 1, weighted picks do not change",
        plain == none && plain == masked);
    AdaptiveSelector<FakeComponent> adaptive("component", {}, {});
    adaptive.SetClockForTesting([] { return uint64_t{0}; });
    FakeComponent a{"A", 1, {}}, b{"B", 2, {}}, c{"C", 0.5, {}};
    const AdaptiveSelector<FakeComponent>::Candidates fakes = {&a, &b, &c};
    std::string error;
    adaptive.Initialize(fakes, &error);
    auto all = [](const FakeComponent&) { return true; };
    WeightMask fake_ones;
    fake_ones.factor.assign(fakes.size(), 1.0);
    checker->Check(
        "swarm: without a mask or with factor 1, adaptive picks do not change",
        PickSequence([&](Random& rng) {
          return adaptive.Pick(fakes, rng, all);
        }) == PickSequence([&](Random& rng) {
          return adaptive.PickMasked(fakes, rng, all, fake_ones);
        }));
  }
  // (b) The masks of worker 0 and of the other workers.
  SwarmCandidates gens, muts;
  std::string candidates_error;
  checker->Check("swarm: the core generators and the structural mutators exist",
                 BuildSwarmCandidates(*generators, *mutators, &gens, &muts,
                                      &candidates_error),
                 candidates_error);
  const SwarmSettings settings;
  const uint64_t seed = kSeed;
  {
    const SwarmMask zero = DrawSwarmMask(settings, gens, muts, seed, 0, 7);
    bool ones = zero.mutators.factor.empty() && zero.mutators.enable.empty();
    for (double f : zero.generators.factor) {
      ones = ones && f == 1.0;
    }
    checker->Check("swarm: worker 0 gets factor 1 for all generators", ones);
  }
  {
    size_t enabled = 0, removed = 0;
    bool core_kept = true, boost_in_range = true, min_kept = true;
    for (int worker = 1; worker <= 32; worker++) {
      const SwarmMask mask =
          DrawSwarmMask(settings, gens, muts, seed, worker, 0);
      min_kept =
          min_kept && mask.kept_generators >= settings.min_active_generators;
      for (size_t i = 0; i < gens.weights.size(); i++) {
        const double f = mask.generators.factor[i];
        if (gens.core[i]) {
          core_kept = core_kept && f == 1.0;
        } else if (gens.weights[i] > 0) {
          enabled++;
          removed += f == 0.0 ? 1 : 0;
          boost_in_range = boost_in_range &&
                           (f == 0.0 || (f >= 1.0 && f <= settings.max_boost));
        }
      }
    }
    const double removed_share =
        static_cast<double>(removed) / static_cast<double>(enabled);
    checker->Check("swarm: the masks keep the core generators", core_kept);
    checker->Check("swarm: kept generators get a factor in [1, max_boost]",
                   boost_in_range);
    checker->Check("swarm: the masks keep min_active_generators", min_kept);
    checker->Check(
        "swarm: the masks remove about 1 - keep_prob of the other generators",
        std::fabs(removed_share - (1 - settings.keep_prob)) < 0.05,
        StringPrintf("%.3f removed", removed_share));
    SwarmSettings few = settings;
    few.keep_prob = 0.1;
    few.min_active_generators = 40;
    const SwarmMask small = DrawSwarmMask(few, gens, muts, seed, 1, 0);
    checker->Check("swarm: a low keep_prob still keeps min_active_generators",
                   small.kept_generators >= 40,
                   StringPrintf("%d kept", small.kept_generators));
  }
  // (c) The mask is a function of (seed, worker, epoch).
  {
    auto draw = [&](uint64_t s, int worker, uint64_t epoch) {
      return DrawSwarmMask(settings, gens, muts, s, worker, epoch)
          .generators.factor;
    };
    checker->Check("swarm: the same seed, worker and epoch give the same mask",
                   draw(seed, 3, 5) == draw(seed, 3, 5));
    checker->Check("swarm: another epoch, worker or seed gives another mask",
                   draw(seed, 3, 5) != draw(seed, 3, 6) &&
                       draw(seed, 3, 5) != draw(seed, 4, 5) &&
                       draw(seed, 3, 5) != draw(seed + 1, 3, 5));
    int enabled_mutators = 0;
    bool structural_only = true;
    for (int worker = 1; worker <= 64; worker++) {
      const SwarmMask mask =
          DrawSwarmMask(settings, gens, muts, seed, worker, 0);
      if (mask.enabled_mutator >= 0) {
        const size_t i = static_cast<size_t>(mask.enabled_mutator);
        enabled_mutators++;
        structural_only =
            structural_only && muts.structural[i] && muts.weights[i] <= 0;
      }
    }
    checker->Check(
        "swarm: some masks turn on one structural mutator with weight 0",
        enabled_mutators > 0 && enabled_mutators < 64 && structural_only,
        StringPrintf("%d of 64 masks", enabled_mutators));
  }
  // (d) The mask multiplies the weights of the adaptive strategy.
  {
    AdaptiveSelector<FakeComponent> adaptive("component", {}, {});
    adaptive.SetClockForTesting([] { return uint64_t{0}; });
    FakeComponent a{"A", 1, {}}, b{"B", 1, {}}, c{"C", 1, {}},
        off{"Off", 0, {}};
    const AdaptiveSelector<FakeComponent>::Candidates fakes = {&a, &b, &c,
                                                               &off};
    std::string error;
    adaptive.Initialize(fakes, &error);
    auto all = [](const FakeComponent&) { return true; };
    WeightMask mask;
    mask.factor = {0.0, 1.0, 3.0, 1.0};
    int counts[4] = {0, 0, 0, 0};
    for (size_t pick : PickSequence([&](Random& rng) {
           return adaptive.PickMasked(fakes, rng, all, mask);
         })) {
      counts[pick]++;
    }
    const double ratio = static_cast<double>(counts[2]) / counts[1];
    checker->Check(
        "swarm: the mask removes factor 0 and multiplies the adaptive weights",
        counts[0] == 0 && counts[3] == 0 && ratio > 2.4 && ratio < 3.7,
        StringPrintf("A %d, B %d, C %d, Off %d", counts[0], counts[1],
                     counts[2], counts[3]));
    mask.enable = {0.0, 0.0, 0.0, 1.0};
    int enabled = 0;
    for (size_t pick : PickSequence([&](Random& rng) {
           return adaptive.PickMasked(fakes, rng, all, mask);
         })) {
      enabled += pick == 3 ? 1 : 0;
    }
    checker->Check("swarm: the mask turns on a candidate with weight 0",
                   enabled > 0, StringPrintf("%d picks", enabled));
  }
  // (f) The pick functions of the mutator and generator sets use the active
  // mask of the thread.
  {
    const Environment& env = Environment::Default();
    const std::string plain_generators = TraceGeneratorPicks(*generators, env);
    const std::string plain_mutators = TraceMutatorPicks(*mutators);
    SwarmMask mask;
    mask.generators.factor.assign(gens.weights.size(), 0.0);
    std::set<std::string, std::less<>> removed;
    for (size_t i = 0; i < gens.weights.size(); i++) {
      if (gens.core[i]) {
        mask.generators.factor[i] = 1.0;
      } else {
        removed.insert(generators->candidates()[i]->name());
      }
    }
    mask.mutators.factor.assign(muts.weights.size(), 1.0);
    mask.mutators.enable.assign(muts.weights.size(), 0.0);
    for (size_t i = 0; i < muts.weights.size(); i++) {
      if (mutators->candidates()[i]->name() == "Wrap") {
        mask.mutators.enable[i] = 1.0;
      }
    }
    SetActiveSwarmMask(&mask);
    const std::string masked_generators =
        TraceGeneratorPicks(*generators, env) +
        TraceCodeGen(*mutators, *generators, env);
    const std::string masked_mutators = TraceMutatorPicks(*mutators);
    SetActiveSwarmMask(nullptr);
    // The names in the traces: the words between tabs, spaces, newlines and
    // '+'.
    std::string found;
    size_t start = 0;
    for (size_t i = 0; i <= masked_generators.size(); i++) {
      if (i == masked_generators.size() ||
          std::string_view("\t\n +").find(masked_generators[i]) !=
              std::string_view::npos) {
        const std::string word = masked_generators.substr(start, i - start);
        if (removed.count(word) != 0 && found.empty()) {
          found = word;
        }
        start = i + 1;
      }
    }
    checker->Check(
        "swarm: GeneratorSet::Pick and CodeGen use only the generators of the "
        "active mask",
        found.empty(), found);
    checker->Check(
        "swarm: MutatorSet::Pick uses the mutator that the active mask turns "
        "on",
        masked_mutators.find("\tWrap\n") != std::string::npos);
    checker->Check("swarm: without an active mask, the picks do not change",
                   TraceGeneratorPicks(*generators, env) == plain_generators &&
                       TraceMutatorPicks(*mutators) == plain_mutators &&
                       plain_generators != masked_generators.substr(
                                               0, plain_generators.size()));
  }
  // (e) The configuration: JSON, flags and the full dump.
  {
    auto load = [](std::initializer_list<const char*> args, FuzzConfig* config,
                   std::string* error) {
      std::vector<const char*> argv = {"redline", "fuzz"};
      argv.insert(argv.end(), args.begin(), args.end());
      Flags flags = Flags::Parse(static_cast<int>(argv.size()), argv.data(), 2);
      return FuzzConfig::Load(flags, config, error, /*for_run=*/false);
    };
    std::string error;
    FuzzConfig plain, on, json_on, json_off;
    bool ok = load({}, &plain, &error) && !plain.selection.swarm &&
              load({"--swarm"}, &on, &error) && on.selection.swarm &&
              load({R"(--config={"selection": {"swarm": true}})"}, &json_on,
                   &error) &&
              json_on.selection.swarm &&
              load({R"(--config={"selection": {"swarm": true}})", "--no-swarm"},
                   &json_off, &error) &&
              !json_off.selection.swarm;
    checker->Check(
        "swarm: selection.swarm is off by default, and the JSON, --swarm and "
        "--no-swarm set it",
        ok, error);
    FuzzConfig bad;
    ok = !load({R"(--config={"selection": {"swarm": 1}})"}, &bad, &error) &&
         error.find("selection.swarm") != std::string::npos;
    checker->Check("swarm: a selection.swarm that is not a boolean is an error",
                   ok, error);
    JsonValue dump;
    FuzzConfig again;
    error.clear();
    ok =
        JsonValue::Parse(on.ToJson(FuzzConfig::Detail::kFull), &dump, &error) &&
        dump.Get("selection").Get("swarm").is_bool() &&
        dump.Get("selection").Get("swarm").as_bool() &&
        dump.Get("tunables").Has("selection.swarm.epoch_execs") &&
        FuzzConfig::LoadJsonForTest(dump, &again, &error) &&
        again.selection.swarm;
    checker->Check(
        "swarm: the full dump gives selection.swarm and its tunables, and "
        "loads back",
        ok, error);
    const SwarmSettings tuned = SwarmSettings::FromTunables();
    checker->Check(
        "swarm: the selection.swarm.* tunables give the default settings",
        tuned.epoch_execs == settings.epoch_execs &&
            tuned.keep_prob == settings.keep_prob &&
            tuned.min_active_generators == settings.min_active_generators &&
            tuned.max_boost == settings.max_boost &&
            tuned.mutator_variation == settings.mutator_variation);
  }
  TunableRegistry::Instance().Reset();
}

void CheckAmplifyConfig(Checker* checker) {
  struct Case {
    const char* name;
    const char* strategy;
    const char* params;
    bool accepted;
    const char* expect;  // substring of the error message
  };
  const Case cases[] = {
      {"amplify: the default strategy is accepted", "flags", "{}", true, ""},
      {"amplify: none is accepted", "none", "{}", true, ""},
      {"amplify: rules are accepted", "flags",
       R"({"rules": [{"frames": ["Heap::"], "args": "--stress-compaction"}], "processes": 3, "min_hits": 2})",
       true, ""},
      {"amplify: unknown strategy is an error", "magic", "{}", false,
       "unknown amplify strategy"},
      {"amplify: unknown param is an error", "flags", R"({"retries": 3})",
       false, "unknown amplify param"},
      {"amplify: min_hits above processes is an error", "flags",
       R"({"processes": 3, "min_hits": 4})", false, "min_hits <= processes"},
      {"amplify: a rule without args is an error", "flags",
       R"({"rules": [{"frames": ["Heap::"]}]})", false, "needs \"frames\""},
  };
  for (const Case& c : cases) {
    JsonValue params;
    std::string error;
    JsonValue::Parse(c.params, &params, &error);
    const bool built =
        CreateAmplifyStrategy(c.strategy, params, &error) != nullptr;
    const bool ok = c.accepted
                        ? built
                        : (!built && error.find(c.expect) != std::string::npos);
    checker->Check(c.name, ok, built ? "accepted" : error);
  }
  checker->Check(
      "amplify: --stress-runs follows the run time within [2, max_loops]",
      StressRunsFor(215000, 10, 100) == 46 &&
          StressRunsFor(100, 10, 100) == 100 &&
          StressRunsFor(60000000, 10, 100) == 2,
      StringPrintf("%d, %d, %d", StressRunsFor(215000, 10, 100),
                   StressRunsFor(100, 10, 100),
                   StressRunsFor(60000000, 10, 100)));
}

// WrapInTry on a real script (Redline's own prelude): the result parses, and
// everything except a leading directive prologue is in one try statement.
void CheckWrapInTry(Checker* checker) {
  const std::string wrapped = WrapInTry(std::string(DefaultPrelude()));
  fast::ParseError error;
  std::unique_ptr<fast::Tree> tree =
      wrapped.empty()
          ? nullptr
          : fast::Parser::ParseScript(wrapped, fast::ParseOptions(), &error);
  size_t statements = 0;
  bool last_is_try = false;
  if (tree != nullptr && tree->root() != nullptr) {
    for (const fast::Statement* s : tree->root()->body) {
      const auto* e = s->As<fast::ExpressionStatement>();
      if (e == nullptr || !e->is_directive) {
        statements++;
      }
    }
    last_is_try =
        !tree->root()->body.empty() &&
        tree->root()->body.back()->As<fast::TryStatement>() != nullptr;
  }
  checker->Check(
      "amplify: WrapInTry puts the program into one try statement that parses",
      statements == 1 && last_is_try,
      tree == nullptr ? "does not parse: " + error.message
                      : StringPrintf("%zu top-level statements", statements));
  checker->Check("amplify: WrapInTry of an empty program is empty",
                 WrapInTry("").empty());
}

void CheckTunables(Checker* checker) {
  TunableRegistry& registry = TunableRegistry::Instance();
  registry.Reset();
  std::vector<const Tunable*> all = registry.All();
  checker->Check("tunables: the registry is populated", !all.empty(),
                 StringPrintf("%zu tunables declared", all.size()));
  if (all.empty()) {
    return;
  }
  const Tunable* t = all.front();
  std::string error;
  // A value inside the range is accepted and visible; the recorded
  // configuration lists it as non-default.
  double inside = t->min() == t->max() ? t->min() : (t->min() + t->max()) / 2;
  bool ok = registry.Set(t->name(), inside, &error) && (*t)() == inside;
  checker->Check("tunables: a value in range is set", ok,
                 error.empty() ? std::string(t->name()) : error);
  std::string json = registry.NonDefaultsJson();
  checker->Check(
      "tunables: changed values appear in the recorded configuration",
      inside == t->default_value() ||
          json.find(std::string(t->name())) != std::string::npos,
      json);
  registry.Reset();
  checker->Check("tunables: Reset restores the default",
                 (*t)() == t->default_value());
  ok = !registry.Set(t->name(), t->max() + 1, &error) &&
       error.find("outside its range") != std::string::npos;
  checker->Check("tunables: a value outside the range is an error", ok, error);
  ok = !registry.Set("generators.NoSuchThing.chance", 0.5, &error) &&
       error.find("unknown tunable") != std::string::npos;
  checker->Check("tunables: an unknown name is an error", ok, error);
  ok = registry.SetFromList(
           std::string(t->name()) + "=" + std::to_string(inside), &error) &&
       (*t)() == inside;
  checker->Check("tunables: --tunable=name=value parses", ok, error);
  registry.Reset();
  ok = !registry.SetFromList("nonsense", &error) &&
       error.find("NAME=VALUE") != std::string::npos;
  checker->Check("tunables: a malformed --tunable is an error", ok, error);
  // Through the config loader.
  JsonValue cfg;
  FuzzConfig config;
  std::string parse;
  JsonValue::Parse("{\"tunables\": {\"generators.NoSuchThing.chance\": 1}}",
                   &cfg, &parse);
  ok = !FuzzConfig::LoadJsonForTest(cfg, &config, &error) &&
       error.find("unknown tunable") != std::string::npos;
  checker->Check("tunables: unknown name in the config file is an error", ok,
                 error);
  registry.Reset();
}

// The engine options that used to be constants, inline --config JSON, and
// the full configuration dump: complete, loadable, identical after a round
// trip.
void CheckConfigDump(Checker* checker) {
  TunableRegistry::Instance().Reset();
  std::string error;
  // The engine knobs land in their fields.
  {
    JsonValue json;
    FuzzConfig config;
    JsonValue::Parse(
        R"({"introspect_timeout_ms": 5000, "crash_reruns_stop_after": 1,
                "stages": {"minimize": {"probe_timeout_factor": 2, "probe_timeout_extra_ms": 10,
                                        "probe_timeout_min_ms": 30, "verify_attempts": 5}}})",
        &json, &error);
    bool ok = FuzzConfig::LoadJsonForTest(json, &config, &error) &&
              config.introspect_timeout_ms == 5000 &&
              config.crash_reruns_stop_after == 1 &&
              config.probe_timeout_factor == 2 &&
              config.probe_timeout_extra_ms == 10 &&
              config.probe_timeout_min_ms == 30 && config.verify_attempts == 5;
    checker->Check("config: engine options are read from the JSON", ok, error);
  }
  // Inline JSON on the command line goes through the same reader.
  {
    const char* argv[] = {
        "redline", "fuzz",
        R"(--config={"stages": {"minimize": {"verify_attempts": 7}}})"};
    Flags flags = Flags::Parse(3, argv, 2);
    FuzzConfig config;
    bool ok = FuzzConfig::Load(flags, &config, &error, /*for_run=*/false) &&
              config.verify_attempts == 7;
    checker->Check("config: --config accepts inline JSON", ok, error);
    const char* bad[] = {"redline", "fuzz", "--config={\"stages\": nonsense}"};
    flags = Flags::Parse(3, bad, 2);
    ok = !FuzzConfig::Load(flags, &config, &error, /*for_run=*/false) &&
         error.find("--config:") != std::string::npos;
    checker->Check("config: malformed inline JSON is an error naming --config",
                   ok, error);
  }
  // The full dump: every tunable and every weight, and it round-trips.
  {
    error.clear();
    FuzzConfig config;
    std::string dump = config.ToJson(FuzzConfig::Detail::kFull);
    JsonValue json;
    bool ok = JsonValue::Parse(dump, &json, &error);
    size_t tunables = ok ? json.Get("tunables").as_object().size() : 0;
    size_t weights = ok ? json.Get("selection")
                              .Get("mutators")
                              .Get("params")
                              .Get("weights")
                              .as_object()
                              .size()
                        : 0;
    ok = ok && tunables == TunableRegistry::Instance().size() &&
         weights == MutatorSet::CreateDefault()->candidates().size() &&
         json.Has("introspect_timeout_ms") &&
         json.Get("stages").Get("minimize").Has("verify_attempts");
    checker->Check(
        "config: the full dump lists every tunable and every mutator weight",
        ok, StringPrintf("%zu tunables, %zu weights", tunables, weights));
    FuzzConfig again;
    ok = FuzzConfig::LoadJsonForTest(json, &again, &error) &&
         again.ToJson(FuzzConfig::Detail::kFull) == dump;
    checker->Check(
        "config: the full dump loads back to an identical configuration", ok,
        error);
    // The compact form only lists what differs from the defaults.
    JsonValue compact;
    ok = JsonValue::Parse(config.ToJson(), &compact, &error) &&
         compact.Get("tunables").as_object().empty();
    checker->Check(
        "config: the compact form lists no tunable when all are at their "
        "default",
        ok, error);
    // The pretty form is the same JSON.
    JsonValue pretty;
    ok = JsonValue::Parse(JsonPretty(dump), &pretty, &error) &&
         pretty.ToString() == json.ToString();
    checker->Check("json: the pretty form parses to the same value", ok, error);
  }
  {
    // Event logs must stay valid UTF-8: UTF-8 is kept, other bytes
    // (a lone surrogate from a string literal) become U+FFFD.
    const std::string quoted = JsonQuote(
        "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80|\xed\xa0\x80|\xff|\xc3");
    const std::string expect =
        "\"a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80|\\ufffd\\ufffd\\ufffd|\\ufffd|"
        "\\ufffd\"";
    checker->Check("json: strings are quoted as valid UTF-8", quoted == expect,
                   quoted);
  }
  TunableRegistry::Instance().Reset();
}

// A --config flag with the two d8 keys, for the checks of the d8 flags.
constexpr const char* kD8ArgsConfig =
    R"(--config={"d8_args": ["--file-a"], "d8_extra_args": ["--extra-a"]})";

// Loads the configuration of `redline fuzz ARGS` and returns its d8 flags,
// with a space between two flags. If the configuration is not correct, it
// returns the error.
std::string LoadedD8Args(std::initializer_list<std::string> args) {
  std::vector<const char*> argv = {"redline", "fuzz"};
  for (const std::string& arg : args) {
    argv.push_back(arg.c_str());
  }
  Flags flags = Flags::Parse(static_cast<int>(argv.size()), argv.data(), 2);
  FuzzConfig config;
  std::string error;
  if (!FuzzConfig::Load(flags, &config, &error, /*for_run=*/false)) {
    return "error: " + error;
  }
  std::string joined;
  for (const std::string& arg : config.target.args) {
    joined += (joined.empty() ? "" : " ") + arg;
  }
  return joined;
}

// Loads the configuration of `redline fuzz --config=JSON` with all steps of
// FuzzConfig::Load, also the validation. Returns the error, or empty.
std::string LoadFullConfig(const std::string& json, FuzzConfig* config) {
  const std::string arg = "--config=" + json;
  std::vector<const char*> argv = {"redline", "fuzz", arg.c_str()};
  Flags flags = Flags::Parse(static_cast<int>(argv.size()), argv.data(), 2);
  std::string error;
  FuzzConfig::Load(flags, config, &error, /*for_run=*/false);
  TunableRegistry::Instance().Reset();
  return error;
}

// The flag --d8-args replaces the d8_args key of the configuration. The
// d8_extra_args key of the configuration stays after the flags of --d8-args.
void CheckD8ArgLayers(Checker* checker) {
  std::string got = LoadedD8Args({kD8ArgsConfig, "--d8-args=--base-a --base-b",
                                  "--d8-extra-args=--extra-b"});
  checker->Check("config: --d8-args keeps the d8_extra_args of --config",
                 got == "--base-a --base-b --extra-a --extra-b", got);
  TunableRegistry::Instance().Reset();
}

// The keys of the check and the minimizer that lower the cost of a commit:
// import_reruns, expressions_share and probe_timeout_tighten_*. The JSON and
// the flags set them, the dump gives them, and the loader clamps them.
void CheckCommitCostConfig(Checker* checker) {
  {
    const FuzzConfig defaults;
    checker->Check(
        "config: the defaults give 1 import rerun, a quarter of the budget "
        "to `expressions` and a probe timeout that tightens",
        defaults.import_reruns == 1 &&
            defaults.minimizer.expressions_share == 0.25 &&
            defaults.crash_minimizer.expressions_share == 1.0 &&
            defaults.probe_timeout_tighten_factor == 2.5 &&
            defaults.probe_timeout_tighten_extra_ms == 50);
  }
  FuzzConfig config;
  const std::string error = LoadFullConfig(
      R"({"stages": {"check": {"import_reruns": 3},
                     "minimize": {"expressions_share": 0.5,
                                  "probe_timeout_tighten_factor": 3,
                                  "probe_timeout_tighten_extra_ms": 10}},
          "crash_minimizer": {"expressions_share": 0.75}})",
      &config);
  checker->Check("config: the JSON sets the keys of the commit cost",
                 error.empty() && config.import_reruns == 3 &&
                     config.minimizer.expressions_share == 0.5 &&
                     config.crash_minimizer.expressions_share == 0.75 &&
                     config.probe_timeout_tighten_factor == 3 &&
                     config.probe_timeout_tighten_extra_ms == 10,
                 error);
  // The dump gives the values, and it loads back to the same configuration.
  const std::string dump = config.ToJson(FuzzConfig::Detail::kFull);
  JsonValue json;
  std::string parse_error;
  bool ok = JsonValue::Parse(dump, &json, &parse_error);
  const JsonValue& stages = json.Get("stages");
  ok = ok && stages.Get("check").GetInt("import_reruns", 0) == 3 &&
       stages.Get("minimize").GetNumber("expressions_share", 0) == 0.5 &&
       stages.Get("minimize").GetNumber("probe_timeout_tighten_factor", 0) ==
           3 &&
       stages.Get("minimize").GetNumber("probe_timeout_tighten_extra_ms", 0) ==
           10 &&
       json.Get("crash_minimizer").GetNumber("expressions_share", 0) == 0.75;
  FuzzConfig again;
  ok = ok && FuzzConfig::LoadJsonForTest(json, &again, &parse_error) &&
       again.ToJson(FuzzConfig::Detail::kFull) == dump;
  TunableRegistry::Instance().Reset();
  checker->Check(
      "config: the dump gives the keys of the commit cost and loads back", ok,
      parse_error);
  // The flag, and the clamps of the loader.
  {
    const char* argv[] = {"redline", "fuzz", "--import-reruns=2"};
    Flags flags = Flags::Parse(3, argv, 2);
    FuzzConfig flagged;
    std::string flag_error;
    const bool loaded =
        FuzzConfig::Load(flags, &flagged, &flag_error, /*for_run=*/false);
    TunableRegistry::Instance().Reset();
    checker->Check("config: --import-reruns sets import_reruns",
                   loaded && flagged.import_reruns == 2, flag_error);
  }
  FuzzConfig clamped;
  const std::string clamp_error = LoadFullConfig(
      R"({"stages": {"check": {"import_reruns": 500, "max_reruns": 20},
                     "minimize": {"expressions_share": 7,
                                  "probe_timeout_tighten_factor": -1}},
          "crash_minimizer": {"expressions_share": -2}})",
      &clamped);
  FuzzConfig low;
  const std::string low_error =
      LoadFullConfig(R"({"stages": {"check": {"import_reruns": 0}}})", &low);
  checker->Check(
      "config: the loader clamps import_reruns, expressions_share and the "
      "tighten factor",
      clamp_error.empty() && low_error.empty() && clamped.import_reruns == 20 &&
          clamped.minimizer.expressions_share == 1.0 &&
          clamped.crash_minimizer.expressions_share == 0.0 &&
          clamped.probe_timeout_tighten_factor == 0.0 &&
          low.import_reruns == low.min_stable_reruns,
      clamp_error + low_error);
}

// The "init" and "accept" blocks, stages.minimize.reducers and
// crash_minimizer.reducers: the values go to their fields, and the loader
// reports the mistakes.
void CheckStrategyBlocks(Checker* checker) {
  auto load = [](const std::string& text, FuzzConfig* config) -> std::string {
    JsonValue json;
    std::string error;
    if (!JsonValue::Parse(text, &json, &error)) {
      return "test bug, invalid JSON: " + error;
    }
    if (!FuzzConfig::LoadJsonForTest(json, config, &error)) {
      return error;
    }
    return FuzzConfig::ValidateReducers(config->minimizer_reducers,
                                        "stages.minimize.reducers", &error) &&
                   FuzzConfig::ValidateReducers(
                       config->crash_minimizer_reducers,
                       "crash_minimizer.reducers", &error)
               ? ""
               : error;
  };
  {
    FuzzConfig config;
    std::string error = load(
        R"({"init": {"strategy": "import", "params": {"paths": ["a.js", "dir"], "root": "/r", "keep": "all"}},
                "accept": {"strategy": "new-edges"},
                "stages": {"minimize": {"reducers": ["truncate", "statements"]}}})",
        &config);
    bool ok = error.empty() &&
              config.imports == std::vector<std::string>{"a.js", "dir"} &&
              config.import_root == "/r" && config.import_all &&
              config.minimizer_reducers ==
                  std::vector<std::string>{"truncate", "statements"} &&
              config.crash_minimizer_reducers.empty();
    checker->Check("init, accept, reducers: the blocks set their fields", ok,
                   error);
    error =
        load(R"({"init": {"strategy": "generate", "params": {"programs": 5}}})",
             &config);
    ok =
        error.empty() && config.imports.empty() && config.initial_programs == 5;
    checker->Check(
        "init: generate clears the imports and sets the program count", ok,
        error);
  }
  {
    // The crash minimizer has its own reducer order. The dump gives the
    // default order when the configuration gives none.
    FuzzConfig config;
    std::string error = load(
        R"({"crash_minimizer": {"max_rounds": 7, "reducers": ["unwrap", "dataflow"]}})",
        &config);
    JsonValue dumped;
    std::string parse_error;
    FuzzConfig defaults;
    const bool dumped_ok =
        load("{}", &defaults).empty() &&
        JsonValue::Parse(defaults.ToJson(), &dumped, &parse_error);
    const JsonValue& order = dumped.Get("crash_minimizer").Get("reducers");
    std::vector<std::string> dumped_order;
    if (order.is_array()) {
      for (const JsonValue& name : order.as_array()) {
        dumped_order.push_back(name.as_string());
      }
    }
    const bool ok = error.empty() && config.crash_minimizer.max_rounds == 7 &&
                    config.crash_minimizer_reducers ==
                        std::vector<std::string>{"unwrap", "dataflow"} &&
                    config.minimizer_reducers.empty() && dumped_ok &&
                    dumped_order == std::vector<std::string>(
                                        Minimizer::CrashOrder().begin(),
                                        Minimizer::CrashOrder().end());
    checker->Check(
        "crash_minimizer: the block sets the reducers, the dump gives the "
        "default order",
        ok, error.empty() ? parse_error : error);
    // The dump of the order that is not the default loads back to the same
    // order. An empty list gives the default order, as for
    // stages.minimize.reducers.
    FuzzConfig again;
    std::string again_error = load(config.ToJson(), &again);
    FuzzConfig empty;
    if (again_error.empty()) {
      again_error = load(R"({"crash_minimizer": {"reducers": []}})", &empty);
    }
    checker->Check(
        "crash_minimizer: the dumped reducers load back, an empty list gives "
        "the default order",
        again_error.empty() && again.crash_minimizer.max_rounds == 7 &&
            again.crash_minimizer_reducers ==
                std::vector<std::string>{"unwrap", "dataflow"} &&
            empty.crash_minimizer_reducers.empty(),
        again_error);
  }
  {
    // The keys of the flag search and the time limits of the crash
    // minimizers. FuzzConfig::Load changes values out of range to the
    // nearest valid values. Values that are not the default load back from
    // the dump.
    FuzzConfig low, high, again;
    std::string error = LoadFullConfig(
        R"({"crash_flag_runs": 3, "crash_flag_processes": 2, "crash_flag_budget_sec": -3})",
        &low);
    if (error.empty()) {
      error = LoadFullConfig(
          R"({"crash_flag_runs": 0, "crash_flag_processes": -5, "crash_flag_budget_sec": 20, "crash_amplified_budget_sec": -1, "crash_bundle_min_budget_sec": -1})",
          &high);
    }
    if (error.empty()) {
      error = load(high.ToJson(), &again);
    }
    checker->Check(
        "crash flags: the keys of the flag search",
        error.empty() && low.crash_flag_runs == 3 &&
            low.crash_flag_processes == 3 && low.crash_flag_budget_sec == 0 &&
            high.crash_flag_runs == 1 && high.crash_flag_processes == 0 &&
            again.crash_flag_runs == 1 && again.crash_flag_processes == 0 &&
            again.crash_flag_budget_sec == 20 &&
            high.crash_amplified_budget_sec == 0 &&
            again.crash_amplified_budget_sec == 0 &&
            low.crash_amplified_budget_sec == 300 &&
            high.crash_bundle_min_budget_sec == 0 &&
            again.crash_bundle_min_budget_sec == 0,
        StringPrintf("%s runs %d/%d, processes %d/%d, budget %g/%g, "
                     "amplified budget %g/%g/%g, bundle budget %g",
                     error.c_str(), low.crash_flag_runs, high.crash_flag_runs,
                     low.crash_flag_processes, high.crash_flag_processes,
                     low.crash_flag_budget_sec, again.crash_flag_budget_sec,
                     low.crash_amplified_budget_sec,
                     high.crash_amplified_budget_sec,
                     again.crash_amplified_budget_sec,
                     again.crash_bundle_min_budget_sec));
  }
  struct Case {
    const char* name;
    const char* json;
    const char* expect;
  };
  const Case cases[] = {
      {"init: unknown strategy is an error",
       R"({"init": {"strategy": "resume"}})", "unknown init strategy"},
      {"init: unknown parameter is an error",
       R"({"init": {"strategy": "generate", "params": {"paths": []}}})",
       "unknown parameter \"paths\""},
      {"init: bad keep value is an error",
       R"({"init": {"strategy": "import", "params": {"keep": "some"}}})",
       "keep must be"},
      {"accept: unknown criterion is an error",
       R"({"accept": {"strategy": "rarity"}})", "unknown accept strategy"},
      {"minimize: unknown reducer is an error",
       R"({"stages": {"minimize": {"reducers": ["statements", "shrink"]}}})",
       "unknown reducer \"shrink\" in stages.minimize.reducers"},
      {"crash_minimizer: unknown reducer is an error",
       R"({"crash_minimizer": {"reducers": ["dataflow", "shrink"]}})",
       "unknown reducer \"shrink\" in crash_minimizer.reducers"},
      {"crash_minimizer: reducers that are not a list are an error",
       R"({"crash_minimizer": {"reducers": "dataflow"}})",
       "crash_minimizer.reducers must be a list of reducer names"},
  };
  for (const Case& c : cases) {
    FuzzConfig config;
    std::string error = load(c.json, &config);
    checker->Check(c.name,
                   !error.empty() && error.find(c.expect) != std::string::npos,
                   error);
  }
}

// --- FAST node kinds --------------------------------------------------------

// Samples per generator for the node-kind check. Fixed seeds: the result
// only changes when a generator changes.
constexpr int kKindSamples = 40;

// The node kinds that no generator produces, with the reason. A kind that
// a generator starts to produce must leave this list (the check says so).
struct UngeneratedKind {
  const char* kind;
  const char* reason;
};
const UngeneratedKind kUngeneratedKinds[] = {
    {"Program", "the root of every tree, never inside generated code"},
    {"EmptyStatement", "`;` has no behaviour to test"},
    {"DebuggerStatement",
     "`debugger;` does nothing without an attached debugger, and d8 runs "
     "without one"},
    {"ImportExpression",
     "import() loads module files, and a fuzzed program is one script without "
     "modules"},
};

// Which node kinds appear in the code of any generator.
std::vector<bool> GeneratedKinds() {
  std::unique_ptr<GeneratorSet> generators = GeneratorSet::CreateDefault();
  const Environment& env = Environment::Default();
  std::shared_ptr<Program> program = ParseOrDie(kProgram, "selftest");
  std::vector<bool> seen(fast::kNumNodeKinds, false);
  for (Generator* g : generators->candidates()) {
    for (int i = 0; i < kKindSamples; i++) {
      std::vector<fast::Statement*> out;
      // The statements live in the tree's arena: keep the tree.
      std::unique_ptr<fast::Tree> tree =
          GenerateSample(g, i, *program, *generators, env, &out);
      if (tree == nullptr) {
        continue;
      }
      for (fast::Statement* statement : out) {
        fast::Walk(statement, [&](fast::Node* node) {
          seen[static_cast<size_t>(node->kind)] = true;
          return true;
        });
      }
    }
  }
  return seen;
}

// True if the code of `root` reads or writes the variable `name`. A
// property name is not a use of a variable.
bool UsesVariable(fast::Node* root, std::string_view name) {
  bool found = false;
  fast::Walk(root, [&](fast::Node* node) {
    if (auto* member = node->As<fast::MemberExpression>();
        member != nullptr && !member->computed) {
      found = found || UsesVariable(member->object, name);
      return false;
    }
    if (auto* property = node->As<fast::Property>();
        property != nullptr && !property->computed) {
      found = found || UsesVariable(property->value, name);
      return false;
    }
    if (auto* field = node->As<fast::PropertyDefinition>();
        field != nullptr && !field->computed) {
      found = found || UsesVariable(field->value, name);
      return false;
    }
    if (auto* id = node->As<fast::Identifier>()) {
      found = found || id->name == name;
    }
    return true;
  });
  return found;
}

// True if an if statement or a for statement in `root` contains another if
// statement or for statement.
bool HasNestedBlock(fast::Node* root) {
  auto is_block = [](fast::Node* node) {
    return node->As<fast::IfStatement>() != nullptr ||
           node->As<fast::ForStatement>() != nullptr;
  };
  bool found = false;
  fast::Walk(root, [&](fast::Node* node) {
    if (!is_block(node)) {
      return true;
    }
    fast::ForEachChild(node, [&](fast::Node* child) {
      fast::Walk(child, [&](fast::Node* inner) {
        found = found || is_block(inner);
        return !found;
      });
    });
    return false;
  });
  return found;
}

// Runs ObjectFlow `samples` times and parses each program. A block of the
// program must not contain a block. At the top level, the program is the
// output of ObjectFlow. In a function, the program is `function g(u) {...}`.
// The generator then sees u, a variable of unknown type, and in half of the
// samples `arguments` with the type object. The initializer of a new object
// must not use u. No code can use `arguments`: in the function of
// ObjectFlow, it is a different object.
bool CheckObjectFlowSamples(const GeneratorSet& generators, bool in_function,
                            int samples, std::string* error) {
  Generator* object_flow = generators.Find("ObjectFlow");
  if (object_flow == nullptr) {
    *error = "no generator ObjectFlow";
    return false;
  }
  const Environment& env = Environment::Default();
  Random rng(kSeed);
  for (int i = 0; i < samples; i++) {
    auto tree = std::make_unique<fast::Tree>();
    tree->set_root(tree->New<fast::Program>());
    Builder builder(tree.get(), &rng);
    GenContext ctx(builder, generators, env);
    if (in_function) {
      ctx.syntax.in_function = true;
      ctx.syntax.has_arguments = true;
      ctx.Declare("u", JsType::kUnknown);
      if (i % 2 == 0) {
        ctx.Declare("arguments", JsType::kObject);
      }
    }
    std::vector<fast::Statement*> out;
    ctx.out = &out;
    if (!object_flow->Applicable(ctx) || !object_flow->Generate(ctx, &out)) {
      *error = StringPrintf("sample %d: ObjectFlow made no code", i);
      return false;
    }
    for (fast::Statement* statement : out) {
      auto* declaration = statement->As<fast::VariableDeclaration>();
      if (in_function && declaration != nullptr &&
          declaration->declaration_kind == fast::VariableKind::kConst &&
          UsesVariable(declaration, "u")) {
        *error = StringPrintf("sample %d: a new object uses u: %s", i,
                              fast::Print(statement).c_str());
        return false;
      }
      if (UsesVariable(statement, "arguments")) {
        *error = StringPrintf("sample %d: the code uses arguments: %s", i,
                              fast::Print(statement).c_str());
        return false;
      }
      if (HasNestedBlock(statement)) {
        *error = StringPrintf("sample %d: a block contains a block: %s", i,
                              fast::Print(statement).c_str());
        return false;
      }
      tree->root()->body.push_back(statement);
    }
    std::string source = fast::Print(tree->root());
    if (in_function) {
      source = "function g(u) {\n" + source + "}\n";
    }
    fast::ParseError parse_error;
    if (fast::Parser::ParseScript(source, fast::ParseOptions(), &parse_error) ==
        nullptr) {
      *error = StringPrintf("sample %d does not parse: %s", i,
                            parse_error.message.c_str());
      return false;
    }
  }
  return true;
}

void CheckEdgeCases(Checker* checker) {
  // Lexer & Parser edge cases: invalid UTF-8, invalid numeric literals,
  // `for (await using of ...)` disambiguation, and deep expression chains.
  {
    fast::ParseOptions opts;
    fast::ParseError err;
    bool rejected_overlong =
        fast::Parser::ParseScript("let \xc0\xaf = 1;", opts, &err) == nullptr &&
        fast::Parser::ParseScript("let \xf4\x90\x80\x80 = 1;", opts, &err) ==
            nullptr;
    checker->Check(
        "lexer: overlong and out-of-range UTF-8 sequences are rejected",
        rejected_overlong, err.message);

    const char* bad_numbers[] = {
        "let x = 0_1;",  "let x = 07_7;", "let x = 0x1g;", "let x = 0b12;",
        "let x = 0o78;", "let x = 1n2;",  "let x = 08a;"};
    bool all_bad_rejected = true;
    for (const char* src : bad_numbers) {
      if (fast::Parser::ParseScript(src, opts, &err) != nullptr) {
        all_bad_rejected = false;
        break;
      }
    }
    checker->Check(
        "lexer: invalid numeric separators and trailing identifier chars are "
        "rejected",
        all_bad_rejected, err.message);

    bool for_await_using_ok =
        fast::Parser::ParseScript(
            "async function f(x) { for await (using of x) {} }", opts, &err) !=
            nullptr &&
        fast::Parser::ParseScript(
            "async function f(x) { for (await using y of x) {} }", opts,
            &err) != nullptr &&
        fast::Parser::ParseScript(
            "async function f(x) { for (await using of of x) {} }", opts,
            &err) != nullptr &&
        fast::Parser::ParseScript("for (using \\u006ff of []) {}", opts,
                                  &err) != nullptr;
    checker->Check("parser: for-await with 'using' identifier vs declaration",
                   for_await_using_ok, err.message);

    std::string deep = "let x = 0";
    for (int i = 0; i < 1300; i++) {
      deep += " + 1";
    }
    deep += ";";
    bool deep_rejected =
        fast::Parser::ParseScript(deep, opts, &err) == nullptr &&
        err.message.find("nesting") != std::string::npos;
    checker->Check("parser: excessive binary expression nesting is rejected",
                   deep_rejected, err.message);
  }
  // JSON and Random edge cases.
  {
    JsonValue val;
    std::string err;
    bool surrogate_not_swallowed =
        JsonValue::Parse("\"\\uD800\\u0041\"", &val, &err) &&
        val.as_string().size() == 4 && val.as_string().back() == 'A';
    JsonValue pos_huge, neg_huge;
    bool clamp_ok = JsonValue::Parse("1e100", &pos_huge, &err) &&
                    pos_huge.as_int() == std::numeric_limits<int64_t>::max() &&
                    JsonValue::Parse("-1e100", &neg_huge, &err) &&
                    neg_huge.as_int() == std::numeric_limits<int64_t>::min();
    checker->Check(
        "json: non-surrogate \\u after high surrogate and out-of-range "
        "as_int()",
        surrogate_not_swallowed && clamp_ok, err);

    Random rng(42);
    int64_t r = rng.Range(INT64_MIN, INT64_MAX);
    (void)r;
    std::vector<double> weights = {1.0, 0.0};
    bool weighted_ok = true;
    for (int i = 0; i < 32; i++) {
      if (rng.Weighted(weights) != 0) {
        weighted_ok = false;
      }
    }
    checker->Check(
        "random: full int64 Range and trailing zero weight in Weighted",
        weighted_ok);
  }
  // CodeGen recursion cap under adversarial tunable settings.
  {
    TunableRegistry& reg = TunableRegistry::Instance();
    std::string err;
    bool set_ok = reg.Set("codegen.argument.reuse_chance", 0.0, &err) &&
                  reg.Set("codegen.value.variable_chance", 0.0, &err) &&
                  reg.Set("codegen.argument.optional_stop_chance", 0.0, &err) &&
                  reg.Set("codegen.expression.max_depth", 16.0, &err) &&
                  reg.Set("codegen.argument.max_depth", 16.0, &err) &&
                  reg.Set("codegen.max_expr_depth", 10.0, &err);
    std::unique_ptr<GeneratorSet> generators = GeneratorSet::CreateDefault();
    const Environment& env = Environment::Default();
    Random rng(12345);
    bool depth_ok = set_ok;
    for (int i = 0; i < 300 && depth_ok; i++) {
      auto tree = std::make_unique<fast::Tree>();
      tree->set_root(tree->New<fast::Program>());
      Builder builder(tree.get(), &rng);
      GenContext ctx(builder, *generators, env);
      CodeGen cg(ctx);
      fast::Expression* e1 = cg.Expression(0);
      fast::Expression* e2 = cg.BuiltinCall();
      fast::Expression* e3 = cg.Construct();
      if (ctx.expr_depth != 0 || e1 == nullptr || e2 == nullptr ||
          e3 == nullptr) {
        depth_ok = false;
        break;
      }
      tree->root()->body.push_back(cg.Stmt(e1));
      tree->root()->body.push_back(cg.Stmt(e2));
      tree->root()->body.push_back(cg.Stmt(e3));
      fast::ParseError parse_err;
      if (fast::Parser::ParseScript(fast::Print(tree->root()),
                                    fast::ParseOptions(),
                                    &parse_err) == nullptr) {
        err = parse_err.message;
        depth_ok = false;
      }
    }
    reg.Reset();
    checker->Check("codegen: expression recursion depth stays bounded",
                   depth_ok, err);
  }
  // ObjectFlow with the weight 0 for each step that is not a block. The
  // blocks then contain loads, not more blocks.
  {
    TunableRegistry& reg = TunableRegistry::Instance();
    std::string err;
    bool ok = true;
    for (const char* step :
         {"store", "load", "update", "rebind", "accessor", "escape"}) {
      ok = ok && reg.Set(StringPrintf("generators.ObjectFlow.%s_weight", step),
                         0.0, &err);
    }
    std::unique_ptr<GeneratorSet> generators = GeneratorSet::CreateDefault();
    ok = ok &&
         CheckObjectFlowSamples(*generators, /*in_function=*/false, 200, &err);
    reg.Reset();
    checker->Check("ObjectFlow: steps of weight 0 do not make nested blocks",
                   ok, err);
  }
  // ObjectFlow in a function with visible variables that are not objects of
  // ObjectFlow.
  {
    TunableRegistry& reg = TunableRegistry::Instance();
    std::string err;
    bool ok = reg.Set("generators.ObjectFlow.max_outer_objects", 8.0, &err) &&
              reg.Set("generators.ObjectFlow.visible_object_chance", 0.5, &err);
    std::unique_ptr<GeneratorSet> generators = GeneratorSet::CreateDefault();
    ok = ok &&
         CheckObjectFlowSamples(*generators, /*in_function=*/true, 200, &err);
    reg.Reset();
    checker->Check(
        "ObjectFlow: new objects do not use visible variables, and no code "
        "uses arguments",
        ok, err);
  }
}

// Checks that `OsrLoop` always sets its trigger iteration strictly below the
// loop bound so the OSR native call can fire.
void CheckOsrLoopTrigger(Checker* checker) {
  std::unique_ptr<GeneratorSet> generators = GeneratorSet::CreateDefault();
  Generator* osr_loop = generators->Find("OsrLoop");
  const Environment& env = Environment::Default();
  auto run_samples = [&](int samples, bool require_positive,
                         std::string* error) -> bool {
    if (osr_loop == nullptr) {
      *error = "no generator OsrLoop";
      return false;
    }
    Random rng(kSeed + 101);
    for (int i = 0; i < samples; i++) {
      auto tree = std::make_unique<fast::Tree>();
      tree->set_root(tree->New<fast::Program>());
      Builder builder(tree.get(), &rng);
      GenContext ctx(builder, *generators, env);
      std::vector<fast::Statement*> out;
      ctx.out = &out;
      if (!osr_loop->Generate(ctx, &out) || out.empty()) {
        *error = StringPrintf("sample %d: OsrLoop made no code", i);
        return false;
      }
      fast::ForStatement* loop = nullptr;
      for (fast::Statement* s : out) {
        if (auto* fl = s->As<fast::ForStatement>()) {
          loop = fl;
          break;
        }
        if (auto* fn = s->As<fast::FunctionDeclaration>();
            fn != nullptr && fn->body != nullptr && !fn->body->body.empty()) {
          loop = fn->body->body.front()->As<fast::ForStatement>();
          break;
        }
      }
      if (loop == nullptr || loop->test == nullptr || loop->body == nullptr) {
        *error = StringPrintf("sample %d: missing OSR for loop", i);
        return false;
      }
      auto* test = loop->test->As<fast::BinaryExpression>();
      auto* bound_lit =
          test != nullptr ? test->right->As<fast::NumericLiteral>() : nullptr;
      auto* body = loop->body->As<fast::BlockStatement>();
      auto* guard = body != nullptr && !body->body.empty()
                        ? body->body.front()->As<fast::IfStatement>()
                        : nullptr;
      auto* eq = guard != nullptr ? guard->test->As<fast::BinaryExpression>()
                                  : nullptr;
      auto* k_lit =
          eq != nullptr ? eq->right->As<fast::NumericLiteral>() : nullptr;
      if (bound_lit == nullptr || k_lit == nullptr) {
        *error = StringPrintf("sample %d: unexpected OSR loop shape", i);
        return false;
      }
      const double bound = bound_lit->value;
      const double k = k_lit->value;
      const double min_k = require_positive ? 1.0 : 0.0;
      if (!(k >= min_k && k < bound)) {
        *error = StringPrintf("sample %d: trigger %g not in [%g, %g)", i, k,
                              min_k, bound);
        return false;
      }
    }
    return true;
  };
  TunableRegistry::Instance().Reset();
  std::string err;
  bool ok = run_samples(200, /*require_positive=*/true, &err);
  checker->Check("OsrLoop: trigger iteration is always below loop bound", ok,
                 err);
  std::string clamp_err;
  bool clamp_ok =
      TunableRegistry::Instance().Set(
          "generators.OsrLoop.min_trigger_iteration", 20.0, &clamp_err) &&
      run_samples(40, /*require_positive=*/false, &clamp_err);
  TunableRegistry::Instance().Reset();
  checker->Check(
      "OsrLoop: trigger iteration clamps below bound when min exceeds bound",
      clamp_ok, clamp_err);
}

// Checks `Builder::SetFocusSubtree`, `PickFocusTarget`, `ApplyFocusTierUp`,
// and the `stages.mutate.focus_chain` setting across presets.
void CheckFocusChain(Checker* checker) {
  std::unique_ptr<MutatorSet> mutators = MutatorSet::CreateDefault();
  std::unique_ptr<GeneratorSet> generators = GeneratorSet::CreateDefault();
  const Environment& env = Environment::Default();

  const char* kTwoFunctions = R"(let g1 = 1 + 2;
let g2 = {p: 1, q: 2};
let g3 = [10, 20];
function other(a, b) {
  let u = a + b;
  return u * 2;
}
function target(x, y) {
  let z = x + y;
  return z + 1;
}
let r0 = other(g1, 3);
let r1 = target(g1, 4);
)";
  ProgramPtr program = ParseOrDie(kTwoFunctions, "selftest");
  auto find_fn = [](fast::Program* root,
                    std::string_view name) -> fast::FunctionDeclaration* {
    for (fast::Statement* s : root->body) {
      if (auto* fn = s->As<fast::FunctionDeclaration>();
          fn != nullptr && fn->name != nullptr && fn->name->name == name) {
        return fn;
      }
    }
    return nullptr;
  };
  fast::FunctionDeclaration* orig_target =
      find_fn(program->tree().root(), "target");
  const uint32_t target_body_id =
      orig_target != nullptr && orig_target->body != nullptr
          ? orig_target->body->id
          : 0;
  std::vector<std::string> orig_outside;
  for (fast::Statement* s : program->tree().root()->body) {
    if (s != orig_target) {
      orig_outside.push_back(fast::Print(s));
    }
  }
  const std::string orig_target_src =
      orig_target != nullptr ? fast::Print(orig_target) : "";

  // Focus concentrates edits of `CodeGen`, `Operation`, `Input`, `Expression`,
  // and `Combine` inside `target`'s body.
  ProgramPtr partner =
      ParseOrDie("let p0 = 7;\nlet p1 = p0 + 1;\n", "selftest");
  bool focus_ok = orig_target != nullptr && orig_target->body != nullptr;
  std::string focus_err;
  for (const char* name :
       {"CodeGen", "Operation", "Input", "Expression", "Combine"}) {
    Mutator* m = mutators->Find(name);
    if (m == nullptr) {
      focus_ok = false;
      focus_err = std::string("missing mutator ") + name;
      break;
    }
    ProgramPtr use_partner = m->needs_partner() ? partner : nullptr;
    int mutated = 0;
    for (int i = 0; i < 25 && focus_ok; i++) {
      Random rng(kSeed + 500 + static_cast<uint64_t>(i));
      std::string detail;
      std::unique_ptr<fast::Tree> tree =
          ApplyMutation(*m, *program, use_partner, rng, env, *generators,
                        /*rounds=*/1, &detail, nullptr, target_body_id);
      if (tree == nullptr) {
        continue;
      }
      mutated++;
      fast::FunctionDeclaration* new_target = find_fn(tree->root(), "target");
      if (new_target == nullptr || fast::Print(new_target) == orig_target_src) {
        focus_ok = false;
        focus_err = StringPrintf("%s did not change target", name);
        break;
      }
      std::vector<std::string> new_outside;
      for (fast::Statement* s : tree->root()->body) {
        if (s != new_target) {
          new_outside.push_back(fast::Print(s));
        }
      }
      if (new_outside != orig_outside) {
        focus_ok = false;
        focus_err = StringPrintf("%s edited code outside target", name);
        break;
      }
    }
    if (focus_ok && mutated == 0) {
      focus_ok = false;
      focus_err = StringPrintf("%s produced 0 mutations", name);
    }
  }
  checker->Check(
      "focus: SetFocusSubtree keeps CodeGen/Operation/Input/Expression/Combine "
      "inside target body",
      focus_ok, focus_err);

  // A detached focus root yields empty queries, and `SlotOf` still searches the
  // full tree while focused.
  {
    std::unique_ptr<fast::Tree> tree = program->CopyTree();
    std::unique_ptr<fast::Tree> other_tree = program->CopyTree();
    Random rng(kSeed + 800);
    Builder builder(tree.get(), &rng);
    fast::FunctionDeclaration* target_fn = find_fn(tree->root(), "target");
    fast::FunctionDeclaration* other_fn = find_fn(tree->root(), "other");
    builder.SetFocusSubtree(target_fn->body);
    const bool slot_of_outside = builder.SlotOf(other_fn).valid();
    builder.SetFocusSubtree(other_tree->root());
    const bool detached_empty =
        builder.InsertionSlots().empty() && builder.StatementSlots().empty() &&
        builder.MemberSlots().empty() && builder.ValueExpressions().empty() &&
        builder.Collect([](fast::Node*) { return true; }).empty() &&
        !builder.RandomInsertionSlot().valid();
    checker->Check(
        "focus: detached focus root returns empty queries and SlotOf stays "
        "full-tree",
        slot_of_outside && detached_empty);
  }

  // `focus_root == nullptr` produces bit-identical output to default runs.
  bool identical_ok = true;
  for (const char* name : {"CodeGen", "Operation", "Input", "Expression"}) {
    Mutator* m = mutators->Find(name);
    if (m == nullptr) {
      identical_ok = false;
      break;
    }
    for (int i = 0; i < 10 && identical_ok; i++) {
      const uint64_t seed = kSeed + 900 + static_cast<uint64_t>(i);
      Random rng_a(seed);
      Random rng_b(seed);
      std::string detail_a, detail_b;
      std::unique_ptr<fast::Tree> tree_a =
          ApplyMutation(*m, *program, nullptr, rng_a, env, *generators,
                        /*rounds=*/1, &detail_a);
      std::unique_ptr<fast::Tree> tree_b = program->CopyTree();
      Builder builder_b(tree_b.get(), &rng_b);
      builder_b.SetFocusSubtree(nullptr);
      MutationContext ctx_b;
      ctx_b.env = &env;
      ctx_b.generators = generators.get();
      bool applied_b = m->Mutate(builder_b, ctx_b);
      if ((tree_a != nullptr) != applied_b) {
        identical_ok = false;
      } else if (tree_a != nullptr &&
                 fast::Print(tree_a->root()) != fast::Print(tree_b->root())) {
        identical_ok = false;
      }
    }
  }
  checker->Check("focus: nullptr focus root is bit-identical to unfocused",
                 identical_ok);

  // Empty focused body falls back cleanly to unfiltered candidates.
  {
    ProgramPtr empty_fn_prog = ParseOrDie(
        "let a = 1 + 2;\nfunction empty() {}\nfunction h(x) { return x; }\n"
        "h(a);\n",
        "selftest");
    fast::FunctionDeclaration* empty_fn =
        find_fn(empty_fn_prog->tree().root(), "empty");
    const uint32_t empty_body_id =
        empty_fn != nullptr && empty_fn->body != nullptr ? empty_fn->body->id
                                                         : 0;
    Mutator* op_mut = mutators->Find("Operation");
    Mutator* call_mut = mutators->Find("Call");
    Random rng(kSeed + 1200);
    std::string detail;
    const uint64_t op_inv_before =
        op_mut != nullptr ? op_mut->stats().invocations.load() : 0;
    std::unique_ptr<fast::Tree> op_tree =
        op_mut != nullptr
            ? ApplyMutation(*op_mut, *empty_fn_prog, nullptr, rng, env,
                            *generators, 1, &detail, nullptr, empty_body_id)
            : nullptr;
    const uint64_t op_inv_after =
        op_mut != nullptr ? op_mut->stats().invocations.load() : 0;
    std::unique_ptr<fast::Tree> call_tree =
        call_mut != nullptr
            ? ApplyMutation(*call_mut, *empty_fn_prog, nullptr, rng, env,
                            *generators, 1, &detail, nullptr, empty_body_id)
            : nullptr;
    checker->Check(
        "focus: ApplyMutation falls back without double-counting invocations",
        op_tree != nullptr && call_tree != nullptr &&
            op_inv_after == op_inv_before + 1);
  }

  // `PickFocusTarget` prefers hot functions, ignores concise arrows, and
  // `ApplyFocusTierUp` adds optimization calls for cold top-level declarations.
  {
    ProgramPtr hot_vs_cold = ParseOrDie(
        "function cold(a) { return a; }\n"
        "function hot(x) { return x + 1; }\n"
        "%PrepareFunctionForOptimization(hot);\n"
        "hot(1);\n"
        "%OptimizeFunctionOnNextCall(hot);\n"
        "hot(2);\n",
        "selftest");
    bool always_hot = true;
    Random rng(kSeed + 1300);
    for (int i = 0; i < 50; i++) {
      FocusTarget picked;
      if (!PickFocusTarget(hot_vs_cold->tree(), rng, &picked) ||
          picked.name != "hot" || !picked.is_hot || !picked.has_tier_up) {
        always_hot = false;
        break;
      }
    }
    ProgramPtr concise_only = ParseOrDie(
        "const arrow = x => x + 1;\n"
        "for (let i = 0; i < 5; i++) arrow(i);\n",
        "selftest");
    FocusTarget none_picked;
    const bool concise_ignored =
        !PickFocusTarget(concise_only->tree(), rng, &none_picked);

    ProgramPtr fn_expr_prog = ParseOrDie(
        "const g = function foo(x) { return x + 1; };\n"
        "const arrow_blk = (y) => { return y + 2; };\n"
        "%OptimizeFunctionOnNextCall(g);\n",
        "selftest");
    FocusTarget fn_expr_picked;
    const bool fn_expr_ok =
        PickFocusTarget(fn_expr_prog->tree(), rng, &fn_expr_picked) &&
        fn_expr_picked.name == "g" && fn_expr_picked.is_hot &&
        fn_expr_picked.has_tier_up && !fn_expr_picked.is_top_level_decl;

    ProgramPtr block_arrow_prog =
        ParseOrDie("const h = (y) => { return y + 2; };\n", "selftest");
    FocusTarget block_arrow_picked;
    const bool block_arrow_ok =
        PickFocusTarget(block_arrow_prog->tree(), rng, &block_arrow_picked) &&
        block_arrow_picked.name == "h" && !block_arrow_picked.has_tier_up;

    ProgramPtr method_prog = ParseOrDie(
        "class C { work(x) { return x + 1; } }\n"
        "let inst = new C();\n"
        "for (let i = 0; i < 5; i++) inst.work(i);\n",
        "selftest");
    FocusTarget method_picked;
    const bool method_ok =
        PickFocusTarget(method_prog->tree(), rng, &method_picked) &&
        method_picked.name == "work" && method_picked.is_hot &&
        method_picked.has_tier_up && !method_picked.is_top_level_decl;

    ProgramPtr cold_only =
        ParseOrDie("function cold(x) { return x + 1; }\n", "selftest");
    FocusTarget cold_picked;
    std::string tier_detail;
    bool cold_ok = PickFocusTarget(cold_only->tree(), rng, &cold_picked) &&
                   cold_picked.name == "cold" && !cold_picked.has_tier_up &&
                   cold_picked.is_top_level_decl;
    std::unique_ptr<fast::Tree> tiered =
        cold_ok ? ApplyFocusTierUp(*cold_only, cold_picked, rng, env,
                                   *generators, &tier_detail)
                : nullptr;
    std::string tiered_src =
        tiered != nullptr ? fast::Print(tiered->root()) : "";
    fast::ParseError parse_err;
    bool tier_ok = tiered != nullptr &&
                   tiered->FindNode(cold_picked.body_id) != nullptr &&
                   tiered_src.find("%PrepareFunctionForOptimization(cold)") !=
                       std::string::npos &&
                   (tiered_src.find("%OptimizeFunctionOnNextCall(cold)") !=
                        std::string::npos ||
                    tiered_src.find("%OptimizeMaglevOnNextCall(cold)") !=
                        std::string::npos) &&
                   fast::Parser::ParseScript(tiered_src, fast::ParseOptions(),
                                             &parse_err) != nullptr;
    checker->Check(
        "focus: PickFocusTarget handles all function kinds and tiers up cold "
        "ones",
        always_hot && concise_ignored && fn_expr_ok && block_arrow_ok &&
            method_ok && tier_ok,
        parse_err.message.empty() ? tier_detail : parse_err.message);
  }

  // `stages.mutate.focus_chain` config validation, dump round-trip, and preset
  // values.
  {
    FuzzConfig defaults;
    FuzzConfig cfg;
    std::string err =
        LoadFullConfig(R"({"stages": {"mutate": {"focus_chain": 0.3}}})", &cfg);
    JsonValue dumped;
    std::string parse_err;
    bool dump_ok =
        err.empty() && defaults.focus_chain == 0.0 && cfg.focus_chain == 0.3 &&
        JsonValue::Parse(cfg.ToJson(FuzzConfig::Detail::kFull), &dumped,
                         &parse_err) &&
        dumped.Get("stages").Get("mutate").GetNumber("focus_chain", -1.0) ==
            0.3;
    FuzzConfig bad_low, bad_high, bad_type;
    bool reject_ok =
        !LoadFullConfig(R"({"stages": {"mutate": {"focus_chain": -0.1}}})",
                        &bad_low)
             .empty() &&
        !LoadFullConfig(R"({"stages": {"mutate": {"focus_chain": 1.5}}})",
                        &bad_high)
             .empty() &&
        !LoadFullConfig(R"({"stages": {"mutate": {"focus_chain": "bad"}}})",
                        &bad_type)
             .empty();
    checker->Check(
        "focus: stages.mutate.focus_chain loads, dumps, and validates range",
        dump_ok && reject_ok, err + parse_err);

    auto preset_focus = [](const std::string& name, double* value,
                           std::string* error) {
      std::string arg = "--preset=" + name;
      const char* argv[] = {"redline", "fuzz", arg.c_str()};
      Flags flags = Flags::Parse(3, argv, 2);
      FuzzConfig loaded;
      bool ok = FuzzConfig::Load(flags, &loaded, error, /*for_run=*/false);
      TunableRegistry::Instance().Reset();
      if (ok) {
        *value = loaded.focus_chain;
      }
      return ok;
    };
    double maglev_fc = -1.0;
    double turbolev_fc = -1.0;
    double default_fc = -1.0;
    double fuzzilli_fc = -1.0;
    std::string preset_err;
    bool presets_ok = preset_focus("maglev", &maglev_fc, &preset_err) &&
                      preset_focus("turbolev", &turbolev_fc, &preset_err) &&
                      preset_focus("default", &default_fc, &preset_err) &&
                      preset_focus("fuzzilli", &fuzzilli_fc, &preset_err) &&
                      maglev_fc == 0.3 && turbolev_fc == 0.3 &&
                      default_fc == 0.0 && fuzzilli_fc == 0.0;
    checker->Check(
        "focus: maglev and turbolev presets set focus_chain to 0.3, default "
        "and fuzzilli keep 0.0",
        presets_ok,
        preset_err.empty()
            ? StringPrintf("maglev=%g turbolev=%g default=%g fuzzilli=%g",
                           maglev_fc, turbolev_fc, default_fc, fuzzilli_fc)
            : preset_err);
  }
}

}  // namespace

void RunSelectionChecks(Checker* checker, const std::string& testdata_dir,
                        bool write_golden) {
  // Defaults everywhere before the traces are compared.
  TunableRegistry::Instance().Reset();
  CheckConfigErrors(checker);
  CheckStageConfig(checker);
  CheckStrategyBlocks(checker);
  const std::string dir = testdata_dir + "/selection";
  std::string error;
  if (write_golden && !MakeDirectories(dir, &error)) {
    checker->Check("selection: create golden directory", false, error);
    return;
  }
  std::unique_ptr<MutatorSet> mutators = MutatorSet::CreateDefault();
  std::unique_ptr<GeneratorSet> generators = GeneratorSet::CreateDefault();
  const Environment& env = Environment::Default();

  CompareOrWrite(checker, "selection: mutator picks match the golden trace",
                 dir + "/mutators.trace", TraceMutatorPicks(*mutators),
                 write_golden);
  CompareOrWrite(checker, "selection: generator picks match the golden trace",
                 dir + "/generators.trace",
                 TraceGeneratorPicks(*generators, env), write_golden);
  CompareOrWrite(checker,
                 "selection: CodeGen insertions match the golden trace",
                 dir + "/codegen.trace",
                 TraceCodeGen(*mutators, *generators, env), write_golden);
  CompareOrWrite(
      checker, "components: every generator's output matches the golden trace",
      dir + "/generator_outputs.trace", TraceGeneratorOutputs(*generators, env),
      write_golden);
  CompareOrWrite(
      checker, "components: every mutator's output matches the golden trace",
      dir + "/mutator_outputs.trace",
      TraceMutatorOutputs(*mutators, *generators, env), write_golden);
  for (const std::string& name :
       {"uniform", "recency", "tournament", "productive"}) {
    std::string check = "selection: scheduler '";
    check += name;
    check += "' picks match the golden trace";
    std::string path = dir;
    path += "/scheduler_";
    path += name;
    path += ".trace";
    CompareOrWrite(checker, check, path, TraceScheduler(name), write_golden);
  }
  CheckTunables(checker);
  CheckConfigDump(checker);
  CheckD8ArgLayers(checker);
  CheckCommitCostConfig(checker);
  CheckAdaptiveWindow(checker);
  CheckSwarm(checker);
  CheckAmplifyConfig(checker);
  CheckWrapInTry(checker);
  CheckOsrLoopTrigger(checker);
  CheckFocusChain(checker);
  CheckEdgeCases(checker);
}

void RunNodeKindChecks(Checker* checker) {
  TunableRegistry::Instance().Reset();
  const std::vector<bool> seen = GeneratedKinds();
  std::vector<std::string> missing, stale;
  for (int i = 0; i < fast::kNumNodeKinds; i++) {
    const char* name = fast::NodeKindName(static_cast<fast::NodeKind>(i));
    bool listed = std::any_of(
        std::begin(kUngeneratedKinds), std::end(kUngeneratedKinds),
        [&](const UngeneratedKind& k) { return std::string(k.kind) == name; });
    if (!seen[static_cast<size_t>(i)] && !listed) {
      missing.emplace_back(name);
    }
    if (seen[static_cast<size_t>(i)] && listed) {
      stale.emplace_back(name);
    }
  }
  auto join = [](const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
      out += (out.empty() ? "" : ", ") + name;
    }
    return out;
  };
  checker->Check("node kinds: every FAST node kind comes out of a generator",
                 missing.empty(),
                 missing.empty() ? StringPrintf("%d kinds", fast::kNumNodeKinds)
                                 : "no generator produces " + join(missing) +
                                       " (add a generator, or list the kind in "
                                       "kUngeneratedKinds with a reason)");
  checker->Check("node kinds: the list of ungenerated kinds is up to date",
                 stale.empty(),
                 stale.empty() ? ""
                               : "generators now produce " + join(stale) +
                                     ": remove them from kUngeneratedKinds");
}

void RunPresetChecks(Checker* checker) {
  const std::string dir = RedlineDataDir("presets");
  checker->Check("presets: locate experimental/redline/presets", !dir.empty(),
                 dir);
  if (dir.empty()) {
    return;
  }
  std::vector<std::string> files = ListFiles(dir, ".json");
  checker->Check("presets: the directory has presets", !files.empty(), dir);
  for (const std::string& path : files) {
    // The same path as `redline fuzz --preset=FILE`: the preset, then
    // the validation that every configuration goes through.
    std::string arg = "--preset=" + path;
    const char* argv[] = {"redline", "fuzz", arg.c_str()};
    Flags flags = Flags::Parse(3, argv, 2);
    FuzzConfig config;
    std::string error;
    bool ok = FuzzConfig::Load(flags, &config, &error, /*for_run=*/false);
    checker->Check(("presets: " + BaseName(path) + " loads").c_str(), ok,
                   error);
    // The flag --d8-args keeps the d8_extra_args of the preset. A d8_args
    // key in --config removes them.
    std::string text;
    JsonValue json;
    if (!ok || !ReadFileToString(path, &text, &error) ||
        !JsonValue::Parse(text, &json, &error) ||
        !json.Get("d8_extra_args").is_array() ||
        json.Get("d8_extra_args").as_array().empty()) {
      continue;
    }
    std::string preset_flags;
    for (const JsonValue& flag : json.Get("d8_extra_args").as_array()) {
      if (flag.is_string()) {
        preset_flags += " " + flag.as_string();
      }
    }
    std::string got =
        LoadedD8Args({arg, "--d8-args=--base-a", "--d8-extra-args=--extra-b"});
    checker->Check(
        ("presets: --d8-args keeps the d8_extra_args of " + BaseName(path))
            .c_str(),
        got == "--base-a" + preset_flags + " --extra-b", got);
    got = LoadedD8Args({arg, kD8ArgsConfig, "--d8-args=--base-a",
                        "--d8-extra-args=--extra-b"});
    checker->Check(("presets: a d8_args key removes the d8_extra_args of " +
                    BaseName(path))
                       .c_str(),
                   got == "--base-a --extra-a --extra-b", got);
  }
  TunableRegistry::Instance().Reset();
}

}  // namespace redline
