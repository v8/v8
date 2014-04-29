// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "v8.h"

#include "counters.h"
#include "isolate.h"
#include "platform.h"

namespace v8 {
namespace internal {

StatsTable::StatsTable()
    : lookup_function_(NULL),
      create_histogram_function_(NULL),
      add_histogram_sample_function_(NULL) {}


int* StatsCounter::FindLocationInStatsTable() const {
  return isolate_->stats_table()->FindLocation(name_);
}


void Histogram::AddSample(int sample) {
  if (Enabled()) {
    isolate()->stats_table()->AddHistogramSample(histogram_, sample);
  }
}

void* Histogram::CreateHistogram() const {
  return isolate()->stats_table()->
      CreateHistogram(name_, min_, max_, num_buckets_);
}


// Start the timer.
void HistogramTimer::Start() {
  if (Enabled()) {
    timer_.Start();
  }
  isolate()->event_logger()(name(), Logger::START);
}


// Stop the timer and record the results.
void HistogramTimer::Stop() {
  if (Enabled()) {
    // Compute the delta between start and stop, in milliseconds.
    AddSample(static_cast<int>(timer_.Elapsed().InMilliseconds()));
    timer_.Stop();
  }
  isolate()->event_logger()(name(), Logger::END);
}

} }  // namespace v8::internal
