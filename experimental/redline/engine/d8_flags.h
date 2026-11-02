// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXPERIMENTAL_REDLINE_ENGINE_D8_FLAGS_H_
#define V8_EXPERIMENTAL_REDLINE_ENGINE_D8_FLAGS_H_

#include <string>
#include <vector>

#include "experimental/redline/core/random.h"

namespace redline {

// A random set of d8 flags for one target process, like the random part of
// the V8 profile of Fuzzilli. The flags change how V8 compiles and checks
// code: Maglev features, inlining limits, assertions and feedback
// allocation. Each flag has a probability, which is a tunable (flags.*).
// A session with randomize_d8_flags draws a new set for each new target
// process. The flags come before the other d8 flags, so that a flag of the
// configuration wins.
std::vector<std::string> RandomD8Flags(Random& rng);

}  // namespace redline

#endif  // V8_EXPERIMENTAL_REDLINE_ENGINE_D8_FLAGS_H_
