// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_INLINING_HEURISTIC_H_
#define V8_COMPILER_JS_INLINING_HEURISTIC_H_

#include "src/compiler/js-inlining.h"

namespace v8 {
namespace internal {
namespace compiler {

class JSInliningHeuristic final : public AdvancedReducer {
 public:
  enum Mode { kRestrictedInlining, kGeneralInlining };
  JSInliningHeuristic(Editor* editor, Mode mode, Zone* local_zone,
                      CompilationInfo* info, JSGraph* jsgraph)
      : AdvancedReducer(editor),
        mode_(mode),
        inliner_(editor, local_zone, info, jsgraph) {}

  Reduction Reduce(Node* node) final;

 private:
  Mode const mode_;
  JSInliner inliner_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_INLINING_HEURISTIC_H_
