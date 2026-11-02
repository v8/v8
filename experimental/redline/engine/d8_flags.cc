// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "experimental/redline/engine/d8_flags.h"

#include <algorithm>

#include "experimental/redline/core/tunable.h"

namespace redline {

namespace {

// --single-threaded stops the background compiler and GC tasks, so it can hide
// bugs in concurrent code. Thus the default probability is 0.
REDLINE_TUNABLE(kFlagsSingleThreaded, "flags.single_threaded", 0, 0, 1,
                "probability of --single-threaded: synchronous tier-up "
                "compilation without background threads");
REDLINE_TUNABLE(kFlagsNoLazyFeedbackAllocation,
                "flags.no_lazy_feedback_allocation", 0.8, 0, 1,
                "probability of --no-lazy-feedback-allocation: a function "
                "gets its feedback vector at its first call");
REDLINE_TUNABLE(kFlagsNoBaselineBatchCompilation,
                "flags.no_baseline_batch_compilation", 0.8, 0, 1,
                "probability of --no-baseline-batch-compilation");
REDLINE_TUNABLE(kFlagsMaglevFuture, "flags.maglev_future", 0.25, 0, 1,
                "probability of --maglev-future");
REDLINE_TUNABLE(kFlagsMaglevObjectTracking, "flags.maglev_object_tracking", 0.2,
                0, 1, "probability of --maglev-object-tracking");
REDLINE_TUNABLE(kFlagsMaglevNonEagerInlining, "flags.maglev_non_eager_inlining",
                0.25, 0, 1, "probability of --maglev-non-eager-inlining");
REDLINE_TUNABLE(kFlagsMaglevEagerInlinedSizeZero,
                "flags.maglev_eager_inlined_size_zero", 0.35, 0, 1,
                "probability of --max-maglev-eager-inlined-bytecode-size=0, "
                "when --maglev-non-eager-inlining is on");
REDLINE_TUNABLE(kFlagsMaglevSmallInlinedSizeZero,
                "flags.maglev_small_inlined_size_zero", 0.25, 0, 1,
                "probability of --max-maglev-inlined-bytecode-size-small=0, "
                "when --maglev-non-eager-inlining is on");
REDLINE_TUNABLE(kFlagsMaglevDisableBuiltinReducers,
                "flags.maglev_disable_builtin_reducers", 0.15, 0, 1,
                "probability of --maglev-disable-builtin-reducers");
REDLINE_TUNABLE(kFlagsNoMaglevUntaggedPhis, "flags.no_maglev_untagged_phis",
                0.1, 0, 1, "probability of --no-maglev-untagged-phis");
REDLINE_TUNABLE(kFlagsNoMaglevLoopPeeling, "flags.no_maglev_loop_peeling", 0.1,
                0, 1, "probability of --no-maglev-loop-peeling");
REDLINE_TUNABLE(kFlagsMaglevAssert, "flags.maglev_assert", 0.2, 0, 1,
                "probability of --maglev-assert");
REDLINE_TUNABLE(kFlagsMaglevAssertTypes, "flags.maglev_assert_types", 0.1, 0, 1,
                "probability of --maglev-assert-types");
REDLINE_TUNABLE(kFlagsOptimizeToMaglev, "flags.optimize_to_maglev", 0.2, 0, 1,
                "probability of --optimize-on-next-call-optimizes-to-maglev");
REDLINE_TUNABLE(kFlagsStressIc, "flags.stress_ic", 0.1, 0, 1,
                "probability of --stress-ic");
REDLINE_TUNABLE(kFlagsVerifyHeap, "flags.verify_heap", 0.1, 0, 1,
                "probability of --verify-heap");
REDLINE_TUNABLE(kFlagsTargetGuidedChance, "flags.target_guided_chance", 0.75, 0,
                1,
                "change-based fuzzing: probability that a new target process "
                "picks flags that reached target edges");
REDLINE_TUNABLE(kFlagsTargetMutateChance, "flags.target_mutate_chance", 0.4, 0,
                1,
                "probability that target-guided flags are combined with a "
                "fresh random flag draw");
REDLINE_TUNABLE(kFlagsTargetKeepChance, "flags.target_keep_chance", 0.85, 0, 1,
                "probability that CombineD8Flags keeps each flag of the base "
                "set");
REDLINE_TUNABLE(kFlagsTargetTournament, "flags.target_tournament", 4, 1, 32,
                "tournament size when picking target-hitting flags");
REDLINE_TUNABLE(kFlagsTargetProbeExecs, "flags.target_probe_execs", 64, 0,
                10000,
                "change-based fuzzing: executions without a target hit "
                "before a child respawns");

}  // namespace

std::vector<std::string> RandomD8Flags(Random& rng) {
  std::vector<std::string> flags;
  auto maybe = [&](const Tunable& probability, const char* flag) {
    if (rng.Chance(probability())) {
      flags.emplace_back(flag);
      return true;
    }
    return false;
  };
  maybe(kFlagsSingleThreaded, "--single-threaded");
  maybe(kFlagsNoLazyFeedbackAllocation, "--no-lazy-feedback-allocation");
  maybe(kFlagsNoBaselineBatchCompilation, "--no-baseline-batch-compilation");
  maybe(kFlagsMaglevFuture, "--maglev-future");
  maybe(kFlagsMaglevObjectTracking, "--maglev-object-tracking");
  if (maybe(kFlagsMaglevNonEagerInlining, "--maglev-non-eager-inlining")) {
    // Without eager inlining, the calls stay for the later inliner.
    maybe(kFlagsMaglevEagerInlinedSizeZero,
          "--max-maglev-eager-inlined-bytecode-size=0");
    maybe(kFlagsMaglevSmallInlinedSizeZero,
          "--max-maglev-inlined-bytecode-size-small=0");
  }
  maybe(kFlagsMaglevDisableBuiltinReducers,
        "--maglev-disable-builtin-reducers");
  maybe(kFlagsNoMaglevUntaggedPhis, "--no-maglev-untagged-phis");
  maybe(kFlagsNoMaglevLoopPeeling, "--no-maglev-loop-peeling");
  maybe(kFlagsMaglevAssert, "--maglev-assert");
  maybe(kFlagsMaglevAssertTypes, "--maglev-assert-types");
  maybe(kFlagsOptimizeToMaglev, "--optimize-on-next-call-optimizes-to-maglev");
  maybe(kFlagsStressIc, "--stress-ic");
  maybe(kFlagsVerifyHeap, "--verify-heap");
  return flags;
}

std::vector<std::string> CombineD8Flags(const std::vector<std::string>& base,
                                        const std::vector<std::string>& extra,
                                        Random& rng) {
  std::vector<std::string> combined;
  for (const std::string& flag : base) {
    if (rng.Chance(kFlagsTargetKeepChance())) {
      combined.push_back(flag);
    }
  }
  for (const std::string& flag : extra) {
    if (std::find(combined.begin(), combined.end(), flag) == combined.end()) {
      combined.push_back(flag);
    }
  }
  const bool has_non_eager =
      std::find(combined.begin(), combined.end(),
                "--maglev-non-eager-inlining") != combined.end();
  if (!has_non_eager) {
    combined.erase(
        std::remove_if(
            combined.begin(), combined.end(),
            [](const std::string& flag) {
              return flag == "--max-maglev-eager-inlined-bytecode-size=0" ||
                     flag == "--max-maglev-inlined-bytecode-size-small=0";
            }),
        combined.end());
  }
  return combined;
}

double TargetGuidedChance() { return kFlagsTargetGuidedChance(); }
double TargetMutateChance() { return kFlagsTargetMutateChance(); }
int TargetTournament() { return kFlagsTargetTournament.AsInt(); }
int TargetProbeExecs() { return kFlagsTargetProbeExecs.AsInt(); }

}  // namespace redline
