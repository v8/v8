// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_TIERING_MANAGER_H_
#define V8_EXECUTION_TIERING_MANAGER_H_

#include "src/common/assert-scope.h"
#include "src/handles/handles.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

class BytecodeArray;
class Isolate;
class UnoptimizedFrame;
class JavaScriptFrame;
class JSFunction;
enum class CodeKind;
enum class OptimizationReason : uint8_t;

class TieringManager {
 public:
  explicit TieringManager(Isolate* isolate) : isolate_(isolate) {}

  void OnInterruptTickFromBytecode();

  void NotifyICChanged() { any_ic_changed_ = true; }

  void AttemptOnStackReplacement(UnoptimizedFrame* frame,
                                 int nesting_levels = 1);

 private:
  // Helper function called from OnInterruptTick*
  void OnInterruptTick(JavaScriptFrame* frame);

  // Make the decision whether to optimize the given function, and mark it for
  // optimization if the decision was 'yes'.
  void MaybeOptimizeFrame(JSFunction function, JavaScriptFrame* frame,
                          CodeKind code_kind);

  // Potentially attempts OSR from and returns whether no other
  // optimization attempts should be made.
  bool MaybeOSR(JSFunction function, UnoptimizedFrame* frame);
  OptimizationReason ShouldOptimize(JSFunction function,
                                    BytecodeArray bytecode_array,
                                    JavaScriptFrame* frame);
  void Optimize(JSFunction function, OptimizationReason reason,
                CodeKind code_kind);
  void Baseline(JSFunction function, OptimizationReason reason);

  class V8_NODISCARD OnInterruptTickScope final {
   public:
    explicit OnInterruptTickScope(TieringManager* profiler);
    ~OnInterruptTickScope();

   private:
    HandleScope handle_scope_;
    TieringManager* const profiler_;
    DisallowGarbageCollection no_gc;
  };

  Isolate* const isolate_;
  bool any_ic_changed_ = false;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_TIERING_MANAGER_H_
