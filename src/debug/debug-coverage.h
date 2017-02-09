// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_DEBUG_COVERAGE_H_
#define V8_DEBUG_DEBUG_COVERAGE_H_

#include <vector>

#include "src/allocation.h"
#include "src/base/macros.h"

namespace v8 {
namespace internal {

// Forward declaration.
class Isolate;

class Coverage : public AllStatic {
 public:
  struct RangeEntry {
    int end_position;
    uint32_t count;
  };

  struct ScriptData {
    ScriptData(int s, std::vector<RangeEntry> e)
        : script_id(s), entries(std::move(e)) {}
    int script_id;
    std::vector<RangeEntry> entries;
  };

  static std::vector<ScriptData> Collect(Isolate* isolate);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DEBUG_DEBUG_COVERAGE_H_
