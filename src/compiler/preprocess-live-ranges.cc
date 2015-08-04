// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/preprocess-live-ranges.h"
#include "src/compiler/register-allocator.h"

namespace v8 {
namespace internal {
namespace compiler {


#define TRACE(...)                             \
  do {                                         \
    if (FLAG_trace_alloc) PrintF(__VA_ARGS__); \
  } while (false)


void PreprocessLiveRanges::PreprocessRanges() {
  SplitRangesAroundDeferredBlocks();
}


void PreprocessLiveRanges::SplitRangesAroundDeferredBlocks() {}


}  // namespace compiler
}  // namespace internal
}  // namespace v8
