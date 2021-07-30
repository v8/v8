// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bigint/bigint-internal.h"
#include "src/bigint/vector-arithmetic.h"

namespace v8 {
namespace bigint {

// The classic algorithm: for every part, multiply the accumulator with
// the appropriate multiplier, and add the part. O(n²) overall.
void ProcessorImpl::FromStringClassic(RWDigits Z,
                                      FromStringAccumulator* accumulator) {
  Z[0] = (*accumulator->parts_)[0];
  RWDigits already_set(Z, 0, 1);
  for (int i = 1; i < Z.len(); i++) Z[i] = 0;
  for (int i = 1; i < accumulator->parts_size(); i++) {
    MultiplySingle(Z, already_set, (*accumulator->multipliers_)[i]);
    if (should_terminate()) return;
    Add(Z, (*accumulator->parts_)[i]);
    already_set.set_len(already_set.len() + 1);
  }
}

void ProcessorImpl::FromString(RWDigits Z, FromStringAccumulator* accumulator) {
  if (!accumulator->parts_) {
    if (Z.len() > 0) Z[0] = accumulator->part_;
    for (int i = 1; i < Z.len(); i++) Z[i] = 0;
  } else {
    FromStringClassic(Z, accumulator);
  }
}

Status Processor::FromString(RWDigits Z, FromStringAccumulator* accumulator) {
  ProcessorImpl* impl = static_cast<ProcessorImpl*>(this);
  impl->FromString(Z, accumulator);
  return impl->get_and_clear_status();
}

}  // namespace bigint
}  // namespace v8
