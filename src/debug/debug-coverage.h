// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_DEBUG_COVERAGE_H_
#define V8_DEBUG_DEBUG_COVERAGE_H_

#include <vector>

#include "src/allocation.h"
#include "src/debug/debug-interface.h"
#include "src/objects.h"

namespace v8 {
namespace internal {

// Forward declaration.
class Isolate;

class Coverage : public AllStatic {
 public:
  struct Range {
    Range(int s, int e, uint32_t c) : start(s), end(e), count(c) {}
    int start;
    int end;
    uint32_t count;
    std::vector<uint16_t> name;
    std::vector<Range> inner;
  };

  struct ScriptData {
    // Initialize top-level function in case it has been garbage-collected.
    ScriptData(Handle<Script> s, int source_length)
        : script(s), toplevel(0, source_length, 1) {}
    Handle<Script> script;
    Range toplevel;
  };

  V8_EXPORT_PRIVATE static std::vector<ScriptData> Collect(Isolate* isolate);

  // Enable precise code coverage. This disables optimization and makes sure
  // invocation count is not affected by GC.
  V8_EXPORT_PRIVATE static void EnablePrecise(Isolate* isolate);
  V8_EXPORT_PRIVATE static void DisablePrecise(Isolate* isolate);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DEBUG_DEBUG_COVERAGE_H_
