// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "experimental/redline/engine/d8_flags.h"

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

}  // namespace redline
