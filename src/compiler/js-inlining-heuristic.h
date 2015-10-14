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
  enum Mode { kGeneralInlining, kRestrictedInlining, kStressInlining };
  JSInliningHeuristic(Editor* editor, Mode mode, Zone* local_zone,
                      CompilationInfo* info, JSGraph* jsgraph)
      : AdvancedReducer(editor),
        mode_(mode),
        local_zone_(local_zone),
        jsgraph_(jsgraph),
        inliner_(editor, local_zone, info, jsgraph),
        candidates_(local_zone) {}

  Reduction Reduce(Node* node) final;

  // Processes the list of candidates gathered while the reducer was running,
  // and inlines call sites that the heuristic determines to be important.
  void ProcessCandidates();

 private:
  struct Candidate {
    Handle<JSFunction> function;  // The call target being inlined.
    Node* node;                   // The call site at which to inline.
    int calls;                    // Number of times the call site was hit.
  };

  static bool Compare(const Candidate& left, const Candidate& right);
  void PrintCandidates();

  Mode const mode_;
  Zone* local_zone_;
  JSGraph* jsgraph_;
  JSInliner inliner_;
  ZoneVector<Candidate> candidates_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_INLINING_HEURISTIC_H_
