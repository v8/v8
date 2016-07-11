// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PROFILER_TICK_SAMPLE_H_
#define V8_PROFILER_TICK_SAMPLE_H_

#include "include/v8-profiler.h"
#include "src/base/platform/time.h"
#include "src/globals.h"

namespace v8 {
namespace internal {

class Isolate;

struct TickSample : public v8::TickSample {
  TickSample() : v8::TickSample() {}
  void Init(Isolate* isolate, const v8::RegisterState& state,
            RecordCEntryFrame record_c_entry_frame, bool update_stats);
  base::TimeTicks timestamp;
};

#if defined(USE_SIMULATOR)
class SimulatorHelper {
 public:
  // Returns true if register values were successfully retrieved
  // from the simulator, otherwise returns false.
  static bool FillRegisters(Isolate* isolate, v8::RegisterState* state);
};
#endif  // USE_SIMULATOR

}  // namespace internal
}  // namespace v8

#endif  // V8_PROFILER_TICK_SAMPLE_H_
